"""Python orchestration for the C++ integrated backtest engine."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Any

from ._cpp import require_cpp
from .adapters import from_cpp_message, to_cpp_market_data_event
from .context import StrategyContext
from .integrated_feed_loader import load_integrated_events
from .gateway import CppOrderGatewayFacade
from .portfolio import Portfolio
from .result import BacktestResult
from .risk import RiskLimits
from .strategy import Strategy
from .types import (
    BookSnapshot,
    BookUpdate,
    MarketDataEvent,
    OrderAck,
    OrderAckType,
    OrderFill,
    OrderReject,
    OrderStatus,
    PriceLevel,
    Side,
    Trade,
)


class IntegratedBacktestError(RuntimeError):
    """Raised when the Python integrated runner cannot continue safely."""


@dataclass(frozen=True)
class _VisibleLevel:
    price: int
    size: int


@dataclass
class _MarketView:
    best_bids: dict[int, _VisibleLevel] = field(default_factory=dict)
    best_asks: dict[int, _VisibleLevel] = field(default_factory=dict)
    consumed_bids: dict[int, dict[int, int]] = field(default_factory=dict)
    consumed_asks: dict[int, dict[int, int]] = field(default_factory=dict)

    def update(self, message: BookUpdate | BookSnapshot | Trade) -> None:
        instrument_id = int(message.instrument_id)
        if isinstance(message, BookSnapshot):
            self._set_snapshot_side(self.best_bids, instrument_id, message.bids)
            self._set_snapshot_side(self.best_asks, instrument_id, message.asks)
        elif isinstance(message, BookUpdate):
            target = (
                self.best_bids
                if _python_side(message.side) is Side.BID
                else self.best_asks
            )
            self._set_update_side(target, instrument_id, message.price, message.size)

    def visible_best(self, instrument_id: int) -> tuple[int | None, int | None]:
        bid = self.visible_level(instrument_id, Side.BID)
        ask = self.visible_level(instrument_id, Side.ASK)
        return (
            bid.price if bid is not None else None,
            ask.price if ask is not None else None,
        )

    def visible_level(self, instrument_id: int, side: Side) -> _VisibleLevel | None:
        historical = (
            self.best_bids.get(instrument_id)
            if side is Side.BID
            else self.best_asks.get(instrument_id)
        )
        if historical is None:
            return None
        consumed_by_price = (
            self.consumed_bids.get(instrument_id, {})
            if side is Side.BID
            else self.consumed_asks.get(instrument_id, {})
        )
        visible_size = historical.size - consumed_by_price.get(historical.price, 0)
        if visible_size <= 0:
            return None
        return _VisibleLevel(price=historical.price, size=visible_size)

    def consume_historical_liquidity(
        self, instrument_id: int, side: Side, price: int, size: int
    ) -> None:
        if size <= 0:
            return
        consumed = self.consumed_bids if side is Side.BID else self.consumed_asks
        consumed_by_price = consumed.setdefault(instrument_id, {})
        consumed_by_price[price] = consumed_by_price.get(price, 0) + int(size)

    def crossing_fill(
        self, instrument_id: int, side: Side, limit_price: int, size: int
    ) -> tuple[int, int, str] | None:
        if side is Side.BID:
            ask = self.visible_level(instrument_id, Side.ASK)
            if ask is None:
                return None
            if limit_price >= ask.price:
                return ask.price, min(size, ask.size), "crossed_best_ask"
        elif side is Side.ASK:
            bid = self.visible_level(instrument_id, Side.BID)
            if bid is None:
                return None
            if limit_price <= bid.price:
                return bid.price, min(size, bid.size), "crossed_best_bid"
        return None

    @staticmethod
    def _set_snapshot_side(
        levels_by_instrument: dict[int, _VisibleLevel],
        instrument_id: int,
        levels: list[PriceLevel],
    ) -> None:
        if not levels:
            levels_by_instrument.pop(instrument_id, None)
            return
        level = levels[0]
        levels_by_instrument[instrument_id] = _VisibleLevel(
            price=int(level.price),
            size=int(level.size),
        )

    @staticmethod
    def _set_update_side(
        levels_by_instrument: dict[int, _VisibleLevel],
        instrument_id: int,
        price: int,
        size: int,
    ) -> None:
        if size == 0:
            existing = levels_by_instrument.get(instrument_id)
            if existing is not None and existing.price == price:
                levels_by_instrument.pop(instrument_id, None)
            return
        levels_by_instrument[instrument_id] = _VisibleLevel(
            price=int(price),
            size=int(size),
        )


@dataclass
class _SubmittedOrder:
    order_id: int
    instrument_id: int
    side: Side
    price: int
    size: int
    timestamp_ns: int
    activation_time_ns: int


class _IntegratedOrderGatewayFacade:
    def __init__(
        self,
        cpp_facade: CppOrderGatewayFacade,
        result: BacktestResult,
        market_view: _MarketView,
        trading_engine_id: int,
        strategy_name: str | None,
        execution_model: str,
        latency_ns: int,
    ) -> None:
        self._cpp_facade = cpp_facade
        self._result = result
        self._market_view = market_view
        self._trading_engine_id = trading_engine_id
        self._strategy_name = strategy_name
        self._execution_model = execution_model
        self._latency_ns = latency_ns
        self._submitted_orders: dict[int, _SubmittedOrder] = {}
        self._fill_trace_context: dict[int, deque[dict[str, Any]]] = {}

    def send_order(
        self, instrument_id: int, side: Side, price: int, size: int, timestamp_ns: int
    ) -> int:
        side = _python_side(side)
        order_id = self._cpp_facade.send_order(
            instrument_id, side, price, size, timestamp_ns
        )
        activation_time_ns = self._activation_time(timestamp_ns)
        self._submitted_orders[order_id] = _SubmittedOrder(
            order_id=order_id,
            instrument_id=instrument_id,
            side=side,
            price=price,
            size=size,
            timestamp_ns=timestamp_ns,
            activation_time_ns=activation_time_ns,
        )
        best_bid_before, best_ask_before = self._market_view.visible_best(instrument_id)
        self._result.add_order_log(
            timestamp_ns=timestamp_ns,
            trading_engine_id=self._trading_engine_id,
            strategy_name=self._strategy_name,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            status=OrderStatus.NEW.value,
            event_type="new_order",
        )
        self._result.add_trace(
            timestamp_ns=timestamp_ns,
            stage="order_request",
            event_type="new_order",
            trading_engine_id=self._trading_engine_id,
            strategy_name=self._strategy_name,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            activation_time_ns=activation_time_ns,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
            reason=self._execution_model,
        )
        return order_id

    def cancel_order(
        self, order_id: int, instrument_id: int, timestamp_ns: int
    ) -> None:
        self._cpp_facade.cancel_order(order_id, instrument_id, timestamp_ns)

    def modify_order(
        self,
        order_id: int,
        instrument_id: int,
        side: Side,
        price: int,
        size: int,
        timestamp_ns: int,
    ) -> None:
        self._cpp_facade.modify_order(
            order_id, instrument_id, side, price, size, timestamp_ns
        )

    def drain_events(self) -> int:
        return self._cpp_facade.drain_events()

    def on_ack(self, callback) -> None:
        self._cpp_facade.on_ack(callback)

    def on_fill(self, callback) -> None:
        self._cpp_facade.on_fill(callback)

    def on_reject(self, callback) -> None:
        self._cpp_facade.on_reject(callback)

    def submitted_order(self, order_id: int) -> _SubmittedOrder | None:
        return self._submitted_orders.get(order_id)

    def add_fill_trace_context(
        self,
        order_id: int,
        best_bid_before: int | None,
        best_ask_before: int | None,
        fill_reason: str,
    ) -> None:
        best_bid_after, best_ask_after = self._market_view.visible_best(
            self._submitted_orders[order_id].instrument_id
        )
        queue = self._fill_trace_context.setdefault(order_id, deque())
        queue.append(
            {
                "best_bid_before": best_bid_before,
                "best_ask_before": best_ask_before,
                "best_bid_after": best_bid_after,
                "best_ask_after": best_ask_after,
                "reason": fill_reason,
                "activation_time_ns": self._submitted_orders[
                    order_id
                ].activation_time_ns,
            }
        )

    @property
    def strategy_name(self) -> str | None:
        return self._strategy_name

    def consume_historical_liquidity(
        self, instrument_id: int, side: Side, price: int, size: int
    ) -> None:
        self._market_view.consume_historical_liquidity(instrument_id, side, price, size)

    def pop_fill_trace_context(self, order_id: int) -> dict[str, Any]:
        queue = self._fill_trace_context.get(order_id)
        if not queue:
            return {}
        context = queue.popleft()
        if not queue:
            self._fill_trace_context.pop(order_id, None)
        return context

    def activation_time(self, order_id: int, fallback_timestamp_ns: int) -> int:
        submitted = self._submitted_orders.get(order_id)
        return (
            submitted.activation_time_ns
            if submitted is not None
            else self._activation_time(fallback_timestamp_ns)
        )

    def _activation_time(self, timestamp_ns: int) -> int:
        if self._execution_model == "latency":
            return int(timestamp_ns) + self._latency_ns
        return int(timestamp_ns)


@dataclass
class _EngineRuntime:
    strategy_name: str
    strategy: Strategy
    trading_engine_id: int
    ctx: StrategyContext
    gateway: _IntegratedOrderGatewayFacade
    accepted_new_orders: deque[OrderAck] = field(default_factory=deque)


@dataclass
class IntegratedBacktestRunner:
    """Run Python strategies from C++ integrated market-data callbacks."""

    config: dict[str, Any] = field(default_factory=dict)
    trading_engine_id: int = 1
    risk_limits: RiskLimits | None = None

    def run(
        self,
        strategy: Strategy,
        data_path: Any | None = None,
        date_range: Any | None = None,
        events: list[Any] | None = None,
        progress_callback: Any | None = None,
        progress_interval_seconds: float | None = 30.0,
        progress_interval_events: int | None = None,
        risk_limits: RiskLimits | dict[str, Any] | None = None,
        context: StrategyContext | None = None,
        explain: bool | None = None,
    ) -> BacktestResult:
        del progress_callback, progress_interval_seconds, progress_interval_events
        if not isinstance(strategy, Strategy):
            raise TypeError("strategy must be an instance of backtester.Strategy")
        if data_path is not None and events is None:
            events = load_integrated_events(
                data_path,
                input_mode=self._input_mode(),
                input_format=self._input_format(),
                date_range=date_range,
            )

        replay_events = list(events or [])
        cpp = require_cpp()
        result = BacktestResult(trace_enabled=self._trace_enabled(explain))
        market_view = _MarketView()
        cpp_config = self._cpp_config(cpp, replay_events)
        engine = cpp.IntegratedBacktestEngine(cpp_config)
        execution_model = self._execution_model()
        latency_ns = self._latency_ns()

        channel = cpp.OrderChannel(request_batch_size=256, event_batch_size=256)
        client = cpp.OrderGatewayClient(
            trading_engine_id=self.trading_engine_id,
            channel=channel,
        )
        server = cpp.OrderGatewayServer()
        server.register_engine(self.trading_engine_id, channel)
        gateway = _IntegratedOrderGatewayFacade(
            CppOrderGatewayFacade(client),
            result,
            market_view,
            self.trading_engine_id,
            None,
            execution_model,
            latency_ns,
        )

        ctx = context or StrategyContext(metadata={"config": self.config})
        ctx.gateway = gateway
        ctx.result = result
        if ctx.portfolio is None:
            ctx.portfolio = Portfolio()
        if ctx.risk_limits is None:
            ctx.risk_limits = self._risk_limits(risk_limits)
        ctx.metadata.setdefault("config", self.config)
        ctx.metadata.setdefault("trading_engine_id", self.trading_engine_id)

        accepted_new_orders: deque[OrderAck] = deque()
        gateway.on_ack(
            lambda ack: self._handle_ack(strategy, ack, ctx, accepted_new_orders)
        )
        gateway.on_fill(lambda fill: self._handle_fill(strategy, fill, ctx, gateway))
        gateway.on_reject(lambda reject: self._handle_reject(strategy, reject, ctx))

        for instrument_id in cpp_config.instruments:
            engine.subscribe(
                instrument_id,
                lambda message: self._dispatch_market_message(
                    strategy, from_cpp_message(message), ctx, market_view
                ),
            )

        try:
            self._invoke("on_start", None, lambda: strategy.on_start(ctx))
            for event in replay_events:
                cpp_event = self._to_cpp_event(cpp, event)
                event_result = engine.process_event(cpp_event)
                self._drain_order_flow(
                    server,
                    gateway,
                    accepted_new_orders,
                    current_timestamp_ns=int(cpp_event.timestamp),
                )
                if event_result.processed:
                    result.add_pnl_point(
                        int(cpp_event.timestamp),
                        ctx.current_pnl(),
                        trading_engine_id=self.trading_engine_id,
                    )
                if event_result.skipped_by_max_events:
                    break
            engine.finish()
            self._drain_order_flow(
                server,
                gateway,
                accepted_new_orders,
                current_timestamp_ns=None,
            )
            self._record_engine_metrics(result, engine)
            result.add_metric("integrated_execution_model", execution_model)
            result.add_metric("integrated_latency_ns", latency_ns)
            for metric in ctx.metrics:
                result.add_metric(
                    metric["name"],
                    metric["value"],
                    timestamp_ns=metric.get("timestamp_ns"),
                )
            self._invoke("on_finish", None, lambda: strategy.on_finish(result, ctx))
        except IntegratedBacktestError:
            raise
        except Exception as exc:
            raise IntegratedBacktestError(f"integrated run failed: {exc}") from exc

        return result

    def run_many(
        self,
        strategies: dict[str, Strategy] | list[Strategy] | tuple[Strategy, ...],
        data_path: Any | None = None,
        date_range: Any | None = None,
        events: list[Any] | None = None,
        progress_callback: Any | None = None,
        progress_interval_seconds: float | None = 30.0,
        progress_interval_events: int | None = None,
        risk_limits: RiskLimits | dict[str, Any] | None = None,
        contexts: dict[str, StrategyContext] | None = None,
        explain: bool | None = None,
    ) -> BacktestResult:
        del progress_callback, progress_interval_seconds, progress_interval_events
        strategy_specs = self._strategy_specs(strategies)
        if data_path is not None and events is None:
            events = load_integrated_events(
                data_path,
                input_mode=self._input_mode(),
                input_format=self._input_format(),
                date_range=date_range,
            )

        replay_events = list(events or [])
        cpp = require_cpp()
        result = BacktestResult(trace_enabled=self._trace_enabled(explain))
        cpp_config = self._cpp_config(cpp, replay_events)
        engine = cpp.IntegratedBacktestEngine(cpp_config)
        execution_model = self._execution_model()
        latency_ns = self._latency_ns()
        server = cpp.OrderGatewayServer()
        contexts = contexts or {}

        runtimes: list[_EngineRuntime] = []
        for strategy_name, strategy, trading_engine_id in strategy_specs:
            channel = cpp.OrderChannel(request_batch_size=256, event_batch_size=256)
            client = cpp.OrderGatewayClient(
                trading_engine_id=trading_engine_id,
                channel=channel,
            )
            server.register_engine(trading_engine_id, channel)
            gateway = _IntegratedOrderGatewayFacade(
                CppOrderGatewayFacade(client),
                result,
                _MarketView(),
                trading_engine_id,
                strategy_name,
                execution_model,
                latency_ns,
            )
            ctx = contexts.get(strategy_name) or StrategyContext(
                metadata={"config": self.config}
            )
            ctx.gateway = gateway
            ctx.result = result
            if ctx.portfolio is None:
                ctx.portfolio = Portfolio()
            if ctx.risk_limits is None:
                ctx.risk_limits = self._risk_limits(risk_limits)
            ctx.metadata.setdefault("config", self.config)
            ctx.metadata["trading_engine_id"] = trading_engine_id
            ctx.metadata["strategy_name"] = strategy_name

            runtime = _EngineRuntime(
                strategy_name=strategy_name,
                strategy=strategy,
                trading_engine_id=trading_engine_id,
                ctx=ctx,
                gateway=gateway,
            )
            gateway.on_ack(
                lambda ack, runtime=runtime: self._handle_ack(
                    runtime.strategy,
                    ack,
                    runtime.ctx,
                    runtime.accepted_new_orders,
                )
            )
            gateway.on_fill(
                lambda fill, runtime=runtime: self._handle_fill(
                    runtime.strategy,
                    fill,
                    runtime.ctx,
                    runtime.gateway,
                )
            )
            gateway.on_reject(
                lambda reject, runtime=runtime: self._handle_reject(
                    runtime.strategy,
                    reject,
                    runtime.ctx,
                )
            )
            runtimes.append(runtime)

        for instrument_id in cpp_config.instruments:
            engine.subscribe(
                instrument_id,
                lambda message, runtimes=runtimes: self._dispatch_market_message_many(
                    from_cpp_message(message), runtimes
                ),
            )

        try:
            for runtime in runtimes:
                self._invoke(
                    f"on_start[{runtime.strategy_name}]",
                    None,
                    lambda runtime=runtime: runtime.strategy.on_start(runtime.ctx),
                )
            for event in replay_events:
                cpp_event = self._to_cpp_event(cpp, event)
                event_result = engine.process_event(cpp_event)
                self._drain_order_flow_many(
                    server,
                    runtimes,
                    current_timestamp_ns=int(cpp_event.timestamp),
                )
                if event_result.processed:
                    timestamp_ns = int(cpp_event.timestamp)
                    for runtime in runtimes:
                        result.add_pnl_point(
                            timestamp_ns,
                            runtime.ctx.current_pnl(),
                            trading_engine_id=runtime.trading_engine_id,
                            strategy_name=runtime.strategy_name,
                        )
                if event_result.skipped_by_max_events:
                    break
            engine.finish()
            self._drain_order_flow_many(
                server,
                runtimes,
                current_timestamp_ns=None,
            )
            self._record_engine_metrics(result, engine)
            result.add_metric("integrated_execution_model", execution_model)
            result.add_metric("integrated_latency_ns", latency_ns)
            result.add_metric("integrated_engine_count", len(runtimes))
            result.add_metric("integrated_strategy_count", len(runtimes))
            for runtime in runtimes:
                for metric in runtime.ctx.metrics:
                    result.add_metric(
                        metric["name"],
                        metric["value"],
                        timestamp_ns=metric.get("timestamp_ns"),
                        trading_engine_id=runtime.trading_engine_id,
                        strategy_name=runtime.strategy_name,
                    )
            for runtime in runtimes:
                self._invoke(
                    f"on_finish[{runtime.strategy_name}]",
                    None,
                    lambda runtime=runtime: runtime.strategy.on_finish(
                        result, runtime.ctx
                    ),
                )
        except IntegratedBacktestError:
            raise
        except Exception as exc:
            raise IntegratedBacktestError(f"integrated run_many failed: {exc}") from exc

        return result

    def _strategy_specs(
        self, strategies: dict[str, Strategy] | list[Strategy] | tuple[Strategy, ...]
    ) -> list[tuple[str, Strategy, int]]:
        if isinstance(strategies, dict):
            named_strategies = [
                (str(name), strategy) for name, strategy in strategies.items()
            ]
        else:
            named_strategies = []
            used_names: set[str] = set()
            for index, strategy in enumerate(strategies, start=1):
                base_name = str(
                    getattr(strategy, "name", None) or strategy.__class__.__name__
                )
                strategy_name = base_name
                if strategy_name in used_names:
                    strategy_name = f"{base_name}_{index}"
                used_names.add(strategy_name)
                named_strategies.append((strategy_name, strategy))
        if not named_strategies:
            raise ValueError("run_many requires at least one strategy")
        names = [name for name, _ in named_strategies]
        if len(set(names)) != len(names):
            raise ValueError("strategy names must be unique")
        for _, strategy in named_strategies:
            if not isinstance(strategy, Strategy):
                raise TypeError(
                    "all strategies must be instances of backtester.Strategy"
                )
        engine_ids = self._strategy_engine_ids(names)
        return [
            (name, strategy, engine_ids[name]) for name, strategy in named_strategies
        ]

    def _strategy_engine_ids(self, strategy_names: list[str]) -> dict[str, int]:
        configured = self.config.get(
            "strategy_engine_ids",
            self.config.get("strategy_engines", self.config.get("engine_ids")),
        )
        if configured is None:
            return {
                strategy_name: int(self.trading_engine_id) + index
                for index, strategy_name in enumerate(strategy_names)
            }
        if not isinstance(configured, dict):
            raise ValueError("strategy engine mapping must be a dict")
        missing = [name for name in strategy_names if name not in configured]
        if missing:
            raise ValueError(
                "strategy engine mapping is missing: " + ", ".join(missing)
            )
        engine_ids = {
            strategy_name: int(configured[strategy_name])
            for strategy_name in strategy_names
        }
        if len(set(engine_ids.values())) != len(engine_ids):
            raise ValueError("strategy engine ids must be unique")
        return engine_ids

    def _input_mode(self) -> str | None:
        configured = self.config.get("input_mode", self.config.get("ingestion_mode"))
        return None if configured is None else str(configured).lower()

    def _trace_enabled(self, explain: bool | None = None) -> bool:
        if explain is not None:
            return bool(explain)
        configured = self.config.get("explain", self.config.get("trace_enabled", True))
        return bool(configured)

    def _input_format(self) -> str | None:
        configured = self.config.get("input_format")
        return None if configured is None else str(configured).lower()

    def _execution_model(self) -> str:
        configured = self.config.get("execution_model")
        if configured is None:
            return "latency" if self._latency_ns() > 0 else "immediate"
        model = str(configured).lower()
        if model not in {"immediate", "latency"}:
            raise ValueError("execution_model must be 'immediate' or 'latency'")
        return model

    def _latency_ns(self) -> int:
        latency_ns = int(self.config.get("latency_ns", 0))
        if latency_ns < 0:
            raise ValueError("latency_ns must be non-negative")
        return latency_ns

    def _cpp_config(self, cpp: Any, events: list[Any]):
        config = cpp.IntegratedBacktestConfig()
        configured_instruments = self.config.get("instruments")
        if configured_instruments is None:
            configured_instruments = sorted(
                {
                    int(getattr(event, "instrument_id"))
                    for event in events
                    if int(getattr(event, "instrument_id", 0)) != 0
                }
            )
        config.instruments = list(configured_instruments)
        config.publish_book_updates = bool(
            self.config.get("publish_book_updates", False)
        )
        config.publish_trades = bool(self.config.get("publish_trades", True))
        config.snapshot_depth = int(self.config.get("snapshot_depth", 5))
        config.snapshot_interval_events = int(
            self.config.get("snapshot_interval_events", 0)
        )
        config.max_events = int(self.config.get("max_events", 0))
        config.subscriber_queue_batch_size = int(
            self.config.get("subscriber_queue_batch_size", 256)
        )
        return config

    def _market_callback_interval(self) -> int:
        configured = self.config.get(
            "market_callback_interval_events",
            self.config.get("callback_interval_events", 1),
        )
        interval = int(configured)
        if interval < 0:
            raise ValueError("market_callback_interval_events must be non-negative")
        return interval

    @staticmethod
    def _record_engine_metrics(result: BacktestResult, engine: Any) -> None:
        stats = engine.stats()
        result.add_metric("integrated_input_event_count", int(stats.input_event_count))
        result.add_metric(
            "integrated_accepted_event_count", int(stats.accepted_event_count)
        )
        result.add_metric(
            "integrated_skipped_by_filter_count",
            int(stats.skipped_by_filter_count),
        )
        result.add_metric(
            "integrated_skipped_by_max_events_count",
            int(stats.skipped_by_max_events_count),
        )
        result.add_metric(
            "integrated_chronological_violations",
            int(stats.chronological_violations),
        )
        result.add_metric("integrated_lob_digest", engine.stable_state_digest())

    def _risk_limits(
        self, override: RiskLimits | dict[str, Any] | None = None
    ) -> RiskLimits | None:
        configured = (
            override if override is not None else self.config.get("risk_limits")
        )
        if isinstance(configured, RiskLimits):
            return configured
        if isinstance(configured, dict):
            return RiskLimits(**configured)
        return self.risk_limits

    @staticmethod
    def _to_cpp_event(cpp: Any, event: Any):
        if isinstance(event, MarketDataEvent):
            return to_cpp_market_data_event(event)
        if isinstance(event, cpp.MarketDataEvent):
            return event
        raise TypeError(
            "mode='integrated' expects backtester.types.MarketDataEvent "
            f"or _backtester_cpp.MarketDataEvent, got {type(event)!r}"
        )

    def _dispatch_market_message(
        self,
        strategy: Strategy,
        message: BookUpdate | BookSnapshot | Trade,
        ctx: StrategyContext,
        market_view: _MarketView,
    ) -> None:
        best_bid_before, best_ask_before = market_view.visible_best(
            message.instrument_id
        )
        market_view.update(message)
        best_bid_after, best_ask_after = market_view.visible_best(message.instrument_id)
        if ctx.portfolio is not None:
            ctx.portfolio.update_market_data(message)
        ctx.result.add_trace(
            timestamp_ns=message.timestamp_ns,
            stage="market_publish",
            event_type=_message_event_type(message),
            trading_engine_id=int(ctx.metadata.get("trading_engine_id", 0)) or None,
            strategy_name=ctx.metadata.get("strategy_name"),
            instrument_id=message.instrument_id,
            side=_message_side(message),
            price=_message_price(message),
            size=_message_size(message),
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_after,
            best_ask_after=best_ask_after,
        )
        if not self._should_deliver_market_callback(ctx):
            return

        if isinstance(message, BookUpdate):
            self._invoke(
                "on_book_update",
                message.timestamp_ns,
                lambda: strategy.on_book_update(message, ctx),
            )
        elif isinstance(message, BookSnapshot):
            self._invoke(
                "on_book_snapshot",
                message.timestamp_ns,
                lambda: strategy.on_book_snapshot(message, ctx),
            )
        elif isinstance(message, Trade):
            self._invoke(
                "on_trade",
                message.timestamp_ns,
                lambda: strategy.on_trade(message, ctx),
            )

    def _dispatch_market_message_many(
        self,
        message: BookUpdate | BookSnapshot | Trade,
        runtimes: list[_EngineRuntime],
    ) -> None:
        for runtime in runtimes:
            market_view = runtime.gateway._market_view
            best_bid_before, best_ask_before = market_view.visible_best(
                message.instrument_id
            )
            market_view.update(message)
            best_bid_after, best_ask_after = market_view.visible_best(
                message.instrument_id
            )
            if runtime.ctx.portfolio is not None:
                runtime.ctx.portfolio.update_market_data(message)
            runtime.ctx.result.add_trace(
                timestamp_ns=message.timestamp_ns,
                stage="market_publish",
                event_type=_message_event_type(message),
                trading_engine_id=runtime.trading_engine_id,
                strategy_name=runtime.strategy_name,
                instrument_id=message.instrument_id,
                side=_message_side(message),
                price=_message_price(message),
                size=_message_size(message),
                best_bid_before=best_bid_before,
                best_ask_before=best_ask_before,
                best_bid_after=best_bid_after,
                best_ask_after=best_ask_after,
            )

        for runtime in runtimes:
            if not self._should_deliver_market_callback(runtime.ctx):
                continue
            if isinstance(message, BookUpdate):
                self._invoke(
                    f"on_book_update[{runtime.strategy_name}]",
                    message.timestamp_ns,
                    lambda runtime=runtime: runtime.strategy.on_book_update(
                        message, runtime.ctx
                    ),
                )
            elif isinstance(message, BookSnapshot):
                self._invoke(
                    f"on_book_snapshot[{runtime.strategy_name}]",
                    message.timestamp_ns,
                    lambda runtime=runtime: runtime.strategy.on_book_snapshot(
                        message, runtime.ctx
                    ),
                )
            elif isinstance(message, Trade):
                self._invoke(
                    f"on_trade[{runtime.strategy_name}]",
                    message.timestamp_ns,
                    lambda runtime=runtime: runtime.strategy.on_trade(
                        message, runtime.ctx
                    ),
                )

    def _should_deliver_market_callback(self, ctx: StrategyContext) -> bool:
        interval = self._market_callback_interval()
        if interval == 0:
            return False
        counter = int(ctx.metadata.get("_integrated_market_callback_counter", 0)) + 1
        ctx.metadata["_integrated_market_callback_counter"] = counter
        return counter % interval == 0

    def _drain_order_flow(
        self,
        server: Any,
        gateway: _IntegratedOrderGatewayFacade,
        accepted_new_orders: deque[OrderAck],
        current_timestamp_ns: int | None,
    ) -> None:
        for _ in range(1000):
            request_count = int(server.drain_requests())
            server.flush_events()
            event_count = gateway.drain_events()
            fill_count = self._emit_crossing_fills(
                server,
                gateway,
                accepted_new_orders,
                current_timestamp_ns,
            )
            if request_count == 0 and event_count == 0 and fill_count == 0:
                return
        raise IntegratedBacktestError("integrated order flow did not quiesce")

    def _drain_order_flow_many(
        self,
        server: Any,
        runtimes: list[_EngineRuntime],
        current_timestamp_ns: int | None,
    ) -> None:
        for _ in range(1000):
            request_count = int(server.drain_requests())
            server.flush_events()
            event_count = sum(runtime.gateway.drain_events() for runtime in runtimes)
            fill_count = sum(
                self._emit_crossing_fills(
                    server,
                    runtime.gateway,
                    runtime.accepted_new_orders,
                    current_timestamp_ns,
                )
                for runtime in runtimes
            )
            if request_count == 0 and event_count == 0 and fill_count == 0:
                return
        raise IntegratedBacktestError(
            "integrated multi-engine order flow did not quiesce"
        )

    def _emit_crossing_fills(
        self,
        server: Any,
        gateway: _IntegratedOrderGatewayFacade,
        accepted_new_orders: deque[OrderAck],
        current_timestamp_ns: int | None,
    ) -> int:
        if current_timestamp_ns is None:
            return 0

        emitted = 0
        remaining_orders: deque[OrderAck] = deque()
        while accepted_new_orders:
            ack = accepted_new_orders.popleft()
            side = _python_side(ack.side)
            activation_time_ns = gateway.activation_time(ack.order_id, ack.timestamp_ns)
            best_bid_before, best_ask_before = gateway._market_view.visible_best(
                ack.instrument_id
            )
            if current_timestamp_ns < activation_time_ns:
                self._record_not_active_trace(
                    gateway,
                    ack,
                    current_timestamp_ns,
                    activation_time_ns,
                    best_bid_before,
                    best_ask_before,
                )
                remaining_orders.append(ack)
                continue

            crossing = gateway._market_view.crossing_fill(
                ack.instrument_id, side, ack.price, ack.size
            )
            if crossing is None:
                self._record_no_fill_trace(
                    gateway,
                    ack,
                    current_timestamp_ns,
                    best_bid_before,
                    best_ask_before,
                    _no_fill_reason(side, best_bid_before, best_ask_before),
                    activation_time_ns,
                )
                remaining_orders.append(ack)
                continue
            fill_price, fill_size, reason = crossing
            if fill_size <= 0:
                self._record_no_fill_trace(
                    gateway,
                    ack,
                    current_timestamp_ns,
                    best_bid_before,
                    best_ask_before,
                    "no_visible_liquidity",
                    activation_time_ns,
                )
                remaining_orders.append(ack)
                continue
            if server.emit_fill(
                ack.trading_engine_id,
                ack.order_id,
                fill_price,
                fill_size,
                current_timestamp_ns,
            ):
                gateway.consume_historical_liquidity(
                    ack.instrument_id,
                    _historical_liquidity_side(side),
                    fill_price,
                    fill_size,
                )
                gateway.add_fill_trace_context(
                    ack.order_id,
                    best_bid_before,
                    best_ask_before,
                    reason,
                )
                emitted += 1

        accepted_new_orders.extend(remaining_orders)
        if emitted:
            server.flush_events()
            gateway.drain_events()
        return emitted

    def _handle_ack(
        self,
        strategy: Strategy,
        ack: OrderAck,
        ctx: StrategyContext,
        accepted_new_orders: deque[OrderAck],
    ) -> None:
        gateway = ctx.gateway
        activation_time_ns = (
            gateway.activation_time(ack.order_id, ack.timestamp_ns)
            if isinstance(gateway, _IntegratedOrderGatewayFacade)
            else ack.timestamp_ns
        )
        ctx.result.add_order_log(
            timestamp_ns=ack.timestamp_ns,
            trading_engine_id=ack.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=ack.order_id,
            instrument_id=ack.instrument_id,
            side=ack.side.value,
            price=ack.price,
            size=ack.size,
            status=ack.status.value,
            event_type="ack",
        )
        ctx.result.add_trace(
            timestamp_ns=ack.timestamp_ns,
            stage="order_ack",
            event_type="ack",
            trading_engine_id=ack.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=ack.order_id,
            activation_time_ns=activation_time_ns,
            instrument_id=ack.instrument_id,
            side=ack.side.value,
            price=ack.price,
            size=ack.size,
            reason=ack.ack_type.value,
        )
        if ack.ack_type is OrderAckType.NEW_ACCEPTED:
            accepted_new_orders.append(ack)
        self._invoke("on_ack", ack.timestamp_ns, lambda: strategy.on_ack(ack, ctx))

    def _handle_fill(
        self,
        strategy: Strategy,
        fill: OrderFill,
        ctx: StrategyContext,
        gateway: _IntegratedOrderGatewayFacade,
    ) -> None:
        trace_context = gateway.pop_fill_trace_context(fill.order_id)
        ctx.result.add_fill(
            timestamp_ns=fill.timestamp_ns,
            trading_engine_id=fill.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=fill.order_id,
            instrument_id=fill.instrument_id,
            side=fill.side.value,
            fill_price=fill.fill_price,
            fill_size=fill.fill_size,
            remaining_size=fill.remaining_size,
        )
        ctx.result.add_order_log(
            timestamp_ns=fill.timestamp_ns,
            trading_engine_id=fill.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=fill.order_id,
            instrument_id=fill.instrument_id,
            side=fill.side.value,
            price=fill.price,
            size=fill.size,
            status=fill.status.value,
            event_type="fill",
        )
        ctx.result.add_trace(
            timestamp_ns=fill.timestamp_ns,
            stage="order_fill",
            event_type="fill",
            trading_engine_id=fill.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=fill.order_id,
            activation_time_ns=trace_context.get("activation_time_ns"),
            instrument_id=fill.instrument_id,
            side=fill.side.value,
            price=fill.price,
            size=fill.size,
            best_bid_before=trace_context.get("best_bid_before"),
            best_ask_before=trace_context.get("best_ask_before"),
            best_bid_after=trace_context.get("best_bid_after"),
            best_ask_after=trace_context.get("best_ask_after"),
            fill_price=fill.fill_price,
            fill_size=fill.fill_size,
            remaining_size=fill.remaining_size,
            reason=trace_context.get("reason"),
        )
        if ctx.portfolio is not None:
            ctx.portfolio.on_fill(fill)
        self._record_position_update(fill, ctx)
        self._invoke("on_fill", fill.timestamp_ns, lambda: strategy.on_fill(fill, ctx))

    def _handle_reject(
        self, strategy: Strategy, reject: OrderReject, ctx: StrategyContext
    ) -> None:
        ctx.result.add_order_log(
            timestamp_ns=reject.timestamp_ns,
            trading_engine_id=reject.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=reject.order_id,
            instrument_id=reject.instrument_id,
            side=reject.side.value,
            price=reject.price,
            size=reject.size,
            status=reject.status.value,
            event_type="reject",
            reason=reject.reason.value,
            text=reject.text,
        )
        ctx.result.add_trace(
            timestamp_ns=reject.timestamp_ns,
            stage="order_reject",
            event_type="reject",
            trading_engine_id=reject.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=reject.order_id,
            instrument_id=reject.instrument_id,
            side=reject.side.value,
            price=reject.price,
            size=reject.size,
            reason=reject.reason.value,
            text=f"gateway_validation: {reject.text}",
        )
        self._invoke(
            "on_reject", reject.timestamp_ns, lambda: strategy.on_reject(reject, ctx)
        )

    @staticmethod
    def _record_position_update(fill: OrderFill, ctx: StrategyContext) -> None:
        if ctx.result is None:
            return
        realized_pnl = int(getattr(ctx.portfolio, "realized_pnl", 0))
        unrealized_pnl = (
            int(ctx.portfolio.unrealized_pnl(fill.instrument_id))
            if ctx.portfolio is not None
            else 0
        )
        current_pnl = ctx.current_pnl()
        position = ctx.current_position(fill.instrument_id)
        ctx.result.add_position(
            timestamp_ns=fill.timestamp_ns,
            trading_engine_id=fill.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            instrument_id=fill.instrument_id,
            position=position,
            realized_pnl=realized_pnl,
            unrealized_pnl=unrealized_pnl,
            pnl=current_pnl,
        )
        ctx.result.add_trace(
            timestamp_ns=fill.timestamp_ns,
            stage="portfolio_update",
            event_type="position_update",
            trading_engine_id=fill.trading_engine_id,
            strategy_name=ctx.metadata.get("strategy_name"),
            order_id=fill.order_id,
            instrument_id=fill.instrument_id,
            side=fill.side.value,
            price=fill.price,
            size=fill.size,
            fill_price=fill.fill_price,
            fill_size=fill.fill_size,
            remaining_size=fill.remaining_size,
            reason="fill",
            text=f"position={position}; pnl={current_pnl}",
        )

    @staticmethod
    def _record_no_fill_trace(
        gateway: _IntegratedOrderGatewayFacade,
        ack: OrderAck,
        current_timestamp_ns: int,
        best_bid_before: int | None,
        best_ask_before: int | None,
        reason: str,
        activation_time_ns: int,
    ) -> None:
        gateway._result.add_trace(
            timestamp_ns=current_timestamp_ns,
            stage="fill_simulation",
            event_type="resting_order",
            trading_engine_id=ack.trading_engine_id,
            strategy_name=gateway.strategy_name,
            order_id=ack.order_id,
            activation_time_ns=activation_time_ns,
            instrument_id=ack.instrument_id,
            side=ack.side.value,
            price=ack.price,
            size=ack.size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
            remaining_size=ack.size,
            reason=reason,
        )

    @staticmethod
    def _record_not_active_trace(
        gateway: _IntegratedOrderGatewayFacade,
        ack: OrderAck,
        current_timestamp_ns: int,
        activation_time_ns: int,
        best_bid_before: int | None,
        best_ask_before: int | None,
    ) -> None:
        gateway._result.add_trace(
            timestamp_ns=current_timestamp_ns,
            stage="fill_simulation",
            event_type="not_active",
            trading_engine_id=ack.trading_engine_id,
            strategy_name=gateway.strategy_name,
            order_id=ack.order_id,
            activation_time_ns=activation_time_ns,
            instrument_id=ack.instrument_id,
            side=ack.side.value,
            price=ack.price,
            size=ack.size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
            remaining_size=ack.size,
            reason="activation_pending",
            text=(
                f"current_timestamp_ns={current_timestamp_ns}; "
                f"activation_time_ns={activation_time_ns}"
            ),
        )

    @staticmethod
    def _invoke(callback_name: str, timestamp_ns: int | None, callback) -> None:
        try:
            callback()
        except IntegratedBacktestError:
            raise
        except Exception as exc:
            suffix = (
                f" at timestamp_ns={timestamp_ns}" if timestamp_ns is not None else ""
            )
            raise IntegratedBacktestError(
                f"integrated strategy callback {callback_name} failed{suffix}: {exc}"
            ) from exc


def _message_event_type(message: BookUpdate | BookSnapshot | Trade) -> str:
    if isinstance(message, BookUpdate):
        return "book_update"
    if isinstance(message, BookSnapshot):
        return "book_snapshot"
    return "trade"


def _message_side(message: BookUpdate | BookSnapshot | Trade) -> str | None:
    if isinstance(message, BookUpdate):
        return _python_side(message.side).value
    if isinstance(message, Trade):
        return _python_side(message.aggressor_side).value
    return None


def _message_price(message: BookUpdate | BookSnapshot | Trade) -> int | None:
    if isinstance(message, (BookUpdate, Trade)):
        return message.price
    return None


def _message_size(message: BookUpdate | BookSnapshot | Trade) -> int | None:
    if isinstance(message, (BookUpdate, Trade)):
        return message.size
    return None


def _python_side(side: Any) -> Side:
    if isinstance(side, Side):
        return side
    return Side(side)


def _no_fill_reason(side: Side, best_bid: int | None, best_ask: int | None) -> str:
    if side is Side.BID and best_ask is None:
        return "no_visible_liquidity"
    if side is Side.ASK and best_bid is None:
        return "no_visible_liquidity"
    return "resting_no_fill"


def _historical_liquidity_side(order_side: Side) -> Side:
    if order_side is Side.BID:
        return Side.ASK
    if order_side is Side.ASK:
        return Side.BID
    return Side.NONE


__all__ = ["IntegratedBacktestError", "IntegratedBacktestRunner"]
