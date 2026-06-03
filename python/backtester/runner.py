"""Synthetic in-memory runner for the Strategy API."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from time import perf_counter
from typing import Any

from .context import StrategyContext
from .feed_loader import load_events
from .portfolio import Portfolio
from .progress import ProgressMetrics
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
    OrderRejectReason,
    OrderStatus,
    Side,
    Trade,
    UNDEFINED_PRICE,
)


@dataclass
class _SyntheticOrder:
    order_id: int
    instrument_id: int
    side: Side
    price: int
    size: int
    remaining_size: int
    timestamp_ns: int
    status: OrderStatus = OrderStatus.ACCEPTED


class _SyntheticOrderGatewayFacade:
    """Tiny in-memory order gateway used by BacktestRunner synthetic mode."""

    def __init__(
        self,
        result: BacktestResult,
        trading_engine_id: int = 1,
        fill_at_touch: bool = False,
    ) -> None:
        self._result = result
        self._trading_engine_id = trading_engine_id
        self._fill_at_touch = fill_at_touch
        self._next_order_id = 1
        self._orders: dict[int, _SyntheticOrder] = {}
        self._events: deque[OrderAck | OrderFill | OrderReject] = deque()
        self._fill_trace_context: dict[int, deque[dict[str, Any]]] = {}
        self._best_bid_by_instrument: dict[int, int] = {}
        self._best_ask_by_instrument: dict[int, int] = {}
        self._on_ack = None
        self._on_fill = None
        self._on_reject = None

    def update_market_view(self, event: BookUpdate | BookSnapshot | Trade) -> None:
        instrument_id = int(event.instrument_id)
        if isinstance(event, BookSnapshot):
            if event.bids:
                self._best_bid_by_instrument[instrument_id] = int(event.bids[0].price)
            else:
                self._best_bid_by_instrument.pop(instrument_id, None)
            if event.asks:
                self._best_ask_by_instrument[instrument_id] = int(event.asks[0].price)
            else:
                self._best_ask_by_instrument.pop(instrument_id, None)
        elif isinstance(event, BookUpdate):
            side = _python_side(event.side)
            if side is Side.BID:
                self._set_visible_price(
                    self._best_bid_by_instrument, instrument_id, event.price, event.size
                )
            elif side is Side.ASK:
                self._set_visible_price(
                    self._best_ask_by_instrument, instrument_id, event.price, event.size
                )

    def visible_best(self, instrument_id: int) -> tuple[int | None, int | None]:
        return (
            self._best_bid_by_instrument.get(instrument_id),
            self._best_ask_by_instrument.get(instrument_id),
        )

    def send_order(
        self, instrument_id: int, side: Side, price: int, size: int, timestamp_ns: int
    ) -> int:
        order_id = self._next_order_id
        self._next_order_id += 1
        side = _python_side(side)
        best_bid_before, best_ask_before = self.visible_best(instrument_id)

        self._result.add_trace(
            timestamp_ns=timestamp_ns,
            stage="order_request",
            event_type="new_order",
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
        )

        self._result.add_order_log(
            timestamp_ns=timestamp_ns,
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            status=OrderStatus.NEW.value,
            event_type="new_order",
        )

        reason = self._validate_new_order(instrument_id, side, price, size)
        if reason is not None:
            self._events.append(
                OrderReject(
                    self._trading_engine_id,
                    order_id,
                    instrument_id,
                    side,
                    price,
                    size,
                    timestamp_ns,
                    OrderStatus.REJECTED,
                    reason,
                    _reject_text(reason),
                )
            )
            return order_id

        self._orders[order_id] = _SyntheticOrder(
            order_id=order_id,
            instrument_id=instrument_id,
            side=side,
            price=price,
            size=size,
            remaining_size=size,
            timestamp_ns=timestamp_ns,
        )
        self._events.append(
            OrderAck(
                self._trading_engine_id,
                order_id,
                instrument_id,
                side,
                price,
                size,
                timestamp_ns,
                OrderStatus.ACCEPTED,
                OrderAckType.NEW_ACCEPTED,
            )
        )
        if self._fill_at_touch:
            self.emit_fill(order_id, price, size, timestamp_ns)
        return order_id

    def cancel_order(
        self, order_id: int, instrument_id: int, timestamp_ns: int
    ) -> None:
        order = self._orders.get(order_id)
        side = order.side if order is not None else Side.NONE
        price = order.price if order is not None else 0
        size = order.remaining_size if order is not None else 0
        best_bid_before, best_ask_before = self.visible_best(instrument_id)

        self._result.add_trace(
            timestamp_ns=timestamp_ns,
            stage="order_request",
            event_type="cancel_order",
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
        )
        self._result.add_order_log(
            timestamp_ns=timestamp_ns,
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            status=OrderStatus.CANCEL_REQUESTED.value,
            event_type="cancel_order",
        )

        if order is None or order.status in {
            OrderStatus.CANCELLED,
            OrderStatus.FILLED,
            OrderStatus.REJECTED,
        }:
            self._events.append(
                OrderReject(
                    self._trading_engine_id,
                    order_id,
                    instrument_id,
                    Side.NONE,
                    0,
                    0,
                    timestamp_ns,
                    OrderStatus.REJECTED,
                    OrderRejectReason.UNKNOWN_ORDER_ID,
                    _reject_text(OrderRejectReason.UNKNOWN_ORDER_ID),
                )
            )
            return

        order.status = OrderStatus.CANCELLED
        self._events.append(
            OrderAck(
                self._trading_engine_id,
                order_id,
                order.instrument_id,
                order.side,
                order.price,
                order.size,
                timestamp_ns,
                OrderStatus.CANCELLED,
                OrderAckType.CANCEL_ACCEPTED,
            )
        )

    def modify_order(
        self,
        order_id: int,
        instrument_id: int,
        side: Side,
        price: int,
        size: int,
        timestamp_ns: int,
    ) -> None:
        order = self._orders.get(order_id)
        side = _python_side(side)
        best_bid_before, best_ask_before = self.visible_best(instrument_id)

        self._result.add_trace(
            timestamp_ns=timestamp_ns,
            stage="order_request",
            event_type="modify_order",
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_before,
            best_ask_after=best_ask_before,
        )
        self._result.add_order_log(
            timestamp_ns=timestamp_ns,
            trading_engine_id=self._trading_engine_id,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            status=OrderStatus.MODIFY_REQUESTED.value,
            event_type="modify_order",
        )

        if order is None or order.status in {
            OrderStatus.CANCELLED,
            OrderStatus.FILLED,
            OrderStatus.REJECTED,
        }:
            self._events.append(
                OrderReject(
                    self._trading_engine_id,
                    order_id,
                    instrument_id,
                    side,
                    price,
                    size,
                    timestamp_ns,
                    OrderStatus.REJECTED,
                    OrderRejectReason.UNKNOWN_ORDER_ID,
                    _reject_text(OrderRejectReason.UNKNOWN_ORDER_ID),
                )
            )
            return

        reason = self._validate_active_order(instrument_id, side, price, size)
        if reason is not None:
            self._events.append(
                OrderReject(
                    self._trading_engine_id,
                    order_id,
                    instrument_id,
                    side,
                    price,
                    size,
                    timestamp_ns,
                    OrderStatus.REJECTED,
                    reason,
                    _reject_text(reason),
                )
            )
            return

        order.instrument_id = instrument_id
        order.side = side
        order.price = price
        order.size = size
        order.remaining_size = size
        order.timestamp_ns = timestamp_ns
        order.status = OrderStatus.ACCEPTED
        self._events.append(
            OrderAck(
                self._trading_engine_id,
                order_id,
                instrument_id,
                side,
                price,
                size,
                timestamp_ns,
                OrderStatus.ACCEPTED,
                OrderAckType.MODIFY_ACCEPTED,
            )
        )

    def drain_events(self) -> int:
        drained = 0
        while self._events:
            event = self._events.popleft()
            self._record_order_event(event)
            if isinstance(event, OrderAck):
                if self._on_ack is not None:
                    self._on_ack(event)
            elif isinstance(event, OrderFill):
                if self._on_fill is not None:
                    self._on_fill(event)
            elif isinstance(event, OrderReject):
                if self._on_reject is not None:
                    self._on_reject(event)
            drained += 1
        return drained

    def on_ack(self, callback) -> None:
        self._on_ack = callback

    def on_fill(self, callback) -> None:
        self._on_fill = callback

    def on_reject(self, callback) -> None:
        self._on_reject = callback

    def emit_fill(
        self, order_id: int, fill_price: int, fill_size: int, timestamp_ns: int
    ) -> bool:
        order = self._orders.get(order_id)
        if (
            order is None
            or fill_size <= 0
            or order.status
            in {OrderStatus.CANCELLED, OrderStatus.FILLED, OrderStatus.REJECTED}
        ):
            return False

        best_bid_before, best_ask_before = self.visible_best(order.instrument_id)
        actual_fill_size = min(fill_size, order.remaining_size)
        order.remaining_size -= actual_fill_size
        order.status = (
            OrderStatus.FILLED
            if order.remaining_size == 0
            else OrderStatus.PARTIALLY_FILLED
        )
        best_bid_after, best_ask_after = self.visible_best(order.instrument_id)
        context_queue = self._fill_trace_context.setdefault(order_id, deque())
        context_queue.append(
            {
                "best_bid_before": best_bid_before,
                "best_ask_before": best_ask_before,
                "best_bid_after": best_bid_after,
                "best_ask_after": best_ask_after,
                "reason": self._fill_reason(order, best_bid_before, best_ask_before),
            }
        )
        self._events.append(
            OrderFill(
                self._trading_engine_id,
                order.order_id,
                order.instrument_id,
                order.side,
                order.price,
                order.size,
                timestamp_ns,
                order.status,
                fill_price,
                actual_fill_size,
                order.remaining_size,
            )
        )
        return True

    @staticmethod
    def _set_visible_price(
        prices: dict[int, int], instrument_id: int, price: int, size: int
    ) -> None:
        if size == 0:
            if prices.get(instrument_id) == price:
                prices.pop(instrument_id, None)
            return
        prices[instrument_id] = int(price)

    @staticmethod
    def _fill_reason(
        order: _SyntheticOrder, best_bid: int | None, best_ask: int | None
    ) -> str:
        if order.side is Side.BID and best_ask is not None and order.price >= best_ask:
            return "crossed_best_ask"
        if order.side is Side.ASK and best_bid is not None and order.price <= best_bid:
            return "crossed_best_bid"
        return "fill_at_touch"

    @staticmethod
    def _validate_new_order(
        instrument_id: int, side: Side, price: int, size: int
    ) -> OrderRejectReason | None:
        return _SyntheticOrderGatewayFacade._validate_active_order(
            instrument_id, side, price, size
        )

    @staticmethod
    def _validate_active_order(
        instrument_id: int, side: Side, price: int, size: int
    ) -> OrderRejectReason | None:
        if instrument_id == 0:
            return OrderRejectReason.INVALID_INSTRUMENT
        if side is Side.NONE:
            return OrderRejectReason.INVALID_SIDE
        if price == UNDEFINED_PRICE:
            return OrderRejectReason.INVALID_PRICE
        if size == 0:
            return OrderRejectReason.INVALID_QUANTITY
        return None

    def _record_order_event(self, event: OrderAck | OrderFill | OrderReject) -> None:
        if isinstance(event, OrderFill):
            trace_context = self._pop_fill_trace_context(event.order_id)
            self._result.add_fill(
                timestamp_ns=event.timestamp_ns,
                trading_engine_id=event.trading_engine_id,
                order_id=event.order_id,
                instrument_id=event.instrument_id,
                side=event.side.value,
                fill_price=event.fill_price,
                fill_size=event.fill_size,
                remaining_size=event.remaining_size,
            )
            event_type = "fill"
            reason = None
            text = None
            self._result.add_trace(
                timestamp_ns=event.timestamp_ns,
                stage="order_fill",
                event_type=event_type,
                trading_engine_id=event.trading_engine_id,
                order_id=event.order_id,
                instrument_id=event.instrument_id,
                side=event.side.value,
                price=event.price,
                size=event.size,
                best_bid_before=trace_context.get("best_bid_before"),
                best_ask_before=trace_context.get("best_ask_before"),
                best_bid_after=trace_context.get("best_bid_after"),
                best_ask_after=trace_context.get("best_ask_after"),
                fill_price=event.fill_price,
                fill_size=event.fill_size,
                remaining_size=event.remaining_size,
                reason=trace_context.get("reason"),
            )
        elif isinstance(event, OrderReject):
            event_type = "reject"
            reason = event.reason.value
            text = event.text
            best_bid_before, best_ask_before = self.visible_best(event.instrument_id)
            self._result.add_trace(
                timestamp_ns=event.timestamp_ns,
                stage="order_reject",
                event_type=event_type,
                trading_engine_id=event.trading_engine_id,
                order_id=event.order_id,
                instrument_id=event.instrument_id,
                side=event.side.value,
                price=event.price,
                size=event.size,
                best_bid_before=best_bid_before,
                best_ask_before=best_ask_before,
                best_bid_after=best_bid_before,
                best_ask_after=best_ask_before,
                reason=reason,
                text=f"gateway_validation: {text}",
            )
        else:
            event_type = "ack"
            reason = None
            text = None
            best_bid_before, best_ask_before = self.visible_best(event.instrument_id)
            self._result.add_trace(
                timestamp_ns=event.timestamp_ns,
                stage="order_ack",
                event_type=event_type,
                trading_engine_id=event.trading_engine_id,
                order_id=event.order_id,
                instrument_id=event.instrument_id,
                side=event.side.value,
                price=event.price,
                size=event.size,
                best_bid_before=best_bid_before,
                best_ask_before=best_ask_before,
                best_bid_after=best_bid_before,
                best_ask_after=best_ask_before,
                reason=event.ack_type.value,
            )

        self._result.add_order_log(
            timestamp_ns=event.timestamp_ns,
            trading_engine_id=event.trading_engine_id,
            order_id=event.order_id,
            instrument_id=event.instrument_id,
            side=event.side.value,
            price=event.price,
            size=event.size,
            status=event.status.value,
            event_type=event_type,
            reason=reason,
            text=text,
        )

    def _pop_fill_trace_context(self, order_id: int) -> dict[str, Any]:
        context_queue = self._fill_trace_context.get(order_id)
        if not context_queue:
            return {}
        context = context_queue.popleft()
        if not context_queue:
            self._fill_trace_context.pop(order_id, None)
        return context


@dataclass
class BacktestRunner:
    """Replay Python market-data events through a Strategy."""

    config: dict[str, Any] = field(default_factory=dict)
    trading_engine_id: int = 1
    fill_at_touch: bool = False
    risk_limits: RiskLimits | None = None

    def run(
        self,
        strategy: Strategy,
        data_path: Any | None = None,
        date_range: Any | None = None,
        events: list[BookUpdate | BookSnapshot | Trade | MarketDataEvent] | None = None,
        mode: str | None = None,
        progress_callback: Any | None = None,
        progress_interval_seconds: float | None = 30.0,
        progress_interval_events: int | None = None,
        risk_limits: RiskLimits | dict[str, Any] | None = None,
        context: StrategyContext | None = None,
        explain: bool | None = None,
    ) -> BacktestResult:
        if not isinstance(strategy, Strategy):
            raise TypeError("strategy must be an instance of backtester.Strategy")
        if progress_interval_events is not None and progress_interval_events <= 0:
            raise ValueError("progress_interval_events must be positive when provided")
        if isinstance(data_path, StrategyContext) and context is None:
            context = data_path
            data_path = None
        selected_mode = str(mode or self.config.get("mode", "synthetic")).lower()
        if selected_mode == "integrated":
            from .integrated_runner import IntegratedBacktestRunner

            return IntegratedBacktestRunner(
                config=self.config,
                trading_engine_id=self.trading_engine_id,
                risk_limits=self.risk_limits,
            ).run(
                strategy,
                data_path=data_path,
                date_range=date_range,
                events=events,
                progress_callback=progress_callback,
                progress_interval_seconds=progress_interval_seconds,
                progress_interval_events=progress_interval_events,
                risk_limits=risk_limits,
                context=context,
                explain=explain,
            )
        if selected_mode != "synthetic":
            raise ValueError("mode must be 'synthetic' or 'integrated'")
        if data_path is not None and events is None:
            events = load_events(data_path, date_range=date_range)

        result = BacktestResult(trace_enabled=self._trace_enabled(explain))
        replay_events = sorted(list(events or []), key=_timestamp_ns)
        gateway = _SyntheticOrderGatewayFacade(
            result,
            trading_engine_id=self.trading_engine_id,
            fill_at_touch=self._config_bool("fill_at_touch")
            or self._config_bool("auto_fill")
            or self.fill_at_touch,
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

        gateway.on_ack(lambda ack: strategy.on_ack(ack, ctx))
        gateway.on_fill(lambda fill: self._handle_fill(strategy, fill, ctx))
        gateway.on_reject(lambda reject: strategy.on_reject(reject, ctx))

        start = perf_counter()
        strategy.on_start(ctx)
        total_events = len(replay_events)
        last_timestamp_ns: int | None = None
        last_progress_time = start
        last_progress_processed: int | None = None

        def emit_progress(processed_events: int) -> None:
            nonlocal last_progress_time, last_progress_processed
            metrics = self._progress_metrics(
                result=result,
                ctx=ctx,
                processed_events=processed_events,
                total_events=total_events,
                last_timestamp_ns=last_timestamp_ns,
                start_time=start,
            )
            strategy.on_progress(metrics, ctx)
            if progress_callback is not None:
                progress_callback(metrics)
            last_progress_time = perf_counter()
            last_progress_processed = processed_events

        for processed_events, event in enumerate(replay_events, start=1):
            instrument_id = int(event.instrument_id)
            best_bid_before, best_ask_before = gateway.visible_best(instrument_id)
            gateway.update_market_view(event)
            best_bid_after, best_ask_after = gateway.visible_best(instrument_id)
            self._record_market_trace(
                result,
                event,
                best_bid_before,
                best_ask_before,
                best_bid_after,
                best_ask_after,
            )
            if ctx.portfolio is not None:
                ctx.portfolio.update_market_data(event)
            self._dispatch_market_event(strategy, event, ctx)
            gateway.drain_events()
            last_timestamp_ns = _timestamp_ns(event)
            result.add_pnl_point(last_timestamp_ns, ctx.current_pnl())
            if self._should_emit_progress(
                processed_events=processed_events,
                last_progress_processed=last_progress_processed,
                last_progress_time=last_progress_time,
                progress_interval_events=progress_interval_events,
                progress_interval_seconds=progress_interval_seconds,
            ):
                emit_progress(processed_events)

        gateway.drain_events()
        if last_progress_processed != total_events:
            emit_progress(total_events)
        for metric in ctx.metrics:
            result.add_metric(
                metric["name"], metric["value"], timestamp_ns=metric.get("timestamp_ns")
            )
        strategy.on_finish(result, ctx)
        return result

    def run_many(
        self,
        strategies: dict[str, Strategy] | list[Strategy] | tuple[Strategy, ...],
        data_path: Any | None = None,
        date_range: Any | None = None,
        events: list[BookUpdate | BookSnapshot | Trade | MarketDataEvent] | None = None,
        mode: str | None = None,
        progress_callback: Any | None = None,
        progress_interval_seconds: float | None = 30.0,
        progress_interval_events: int | None = None,
        risk_limits: RiskLimits | dict[str, Any] | None = None,
        contexts: dict[str, StrategyContext] | None = None,
        explain: bool | None = None,
    ) -> BacktestResult:
        selected_mode = str(mode or self.config.get("mode", "integrated")).lower()
        if selected_mode != "integrated":
            raise ValueError("run_many currently supports mode='integrated'")

        from .integrated_runner import IntegratedBacktestRunner

        return IntegratedBacktestRunner(
            config=self.config,
            trading_engine_id=self.trading_engine_id,
            risk_limits=self.risk_limits,
        ).run_many(
            strategies,
            data_path=data_path,
            date_range=date_range,
            events=events,
            progress_callback=progress_callback,
            progress_interval_seconds=progress_interval_seconds,
            progress_interval_events=progress_interval_events,
            risk_limits=risk_limits,
            contexts=contexts,
            explain=explain,
        )

    def _config_bool(self, name: str) -> bool:
        return bool(self.config.get(name, False))

    def _trace_enabled(self, explain: bool | None = None) -> bool:
        if explain is not None:
            return bool(explain)
        configured = self.config.get("explain", self.config.get("trace_enabled", True))
        return bool(configured)

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
    def _progress_metrics(
        result: BacktestResult,
        ctx: StrategyContext,
        processed_events: int,
        total_events: int,
        last_timestamp_ns: int | None,
        start_time: float,
    ) -> ProgressMetrics:
        order_statistics = result.order_statistics
        return ProgressMetrics(
            processed_events=processed_events,
            total_events=total_events,
            last_timestamp_ns=last_timestamp_ns,
            current_pnl=ctx.current_pnl(),
            orders_sent=order_statistics.sent,
            orders_cancelled=order_statistics.cancelled,
            orders_filled=order_statistics.filled,
            orders_rejected=order_statistics.rejected,
            per_instrument_order_stats=result.per_instrument_order_statistics,
            elapsed_seconds=perf_counter() - start_time,
        )

    @staticmethod
    def _handle_fill(strategy: Strategy, fill: OrderFill, ctx: StrategyContext) -> None:
        if ctx.portfolio is not None:
            ctx.portfolio.on_fill(fill)
        BacktestRunner._record_position_update(fill, ctx)
        strategy.on_fill(fill, ctx)

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
    def _record_market_trace(
        result: BacktestResult,
        event: BookUpdate | BookSnapshot | Trade,
        best_bid_before: int | None,
        best_ask_before: int | None,
        best_bid_after: int | None,
        best_ask_after: int | None,
    ) -> None:
        result.add_trace(
            timestamp_ns=event.timestamp_ns,
            stage="market_event",
            event_type=_market_event_type(event),
            instrument_id=event.instrument_id,
            side=_market_event_side(event),
            price=_market_event_price(event),
            size=_market_event_size(event),
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_after,
            best_ask_after=best_ask_after,
        )

    @staticmethod
    def _should_emit_progress(
        processed_events: int,
        last_progress_processed: int | None,
        last_progress_time: float,
        progress_interval_events: int | None,
        progress_interval_seconds: float | None,
    ) -> bool:
        if progress_interval_events is not None:
            previous = last_progress_processed or 0
            if processed_events - previous >= progress_interval_events:
                return True
        if progress_interval_seconds is not None:
            if progress_interval_seconds <= 0:
                return True
            if perf_counter() - last_progress_time >= progress_interval_seconds:
                return True
        return False

    @staticmethod
    def _dispatch_market_event(
        strategy: Strategy,
        event: BookUpdate | BookSnapshot | Trade,
        ctx: StrategyContext,
    ) -> None:
        if isinstance(event, BookUpdate):
            strategy.on_book_update(event, ctx)
        elif isinstance(event, BookSnapshot):
            strategy.on_book_snapshot(event, ctx)
        elif isinstance(event, Trade):
            strategy.on_trade(event, ctx)
        else:
            raise TypeError(f"Unsupported synthetic event type: {type(event)!r}")


def _timestamp_ns(event: BookUpdate | BookSnapshot | Trade) -> int:
    return int(getattr(event, "timestamp_ns"))


def _market_event_type(event: BookUpdate | BookSnapshot | Trade) -> str:
    if isinstance(event, BookUpdate):
        return "book_update"
    if isinstance(event, BookSnapshot):
        return "book_snapshot"
    if isinstance(event, Trade):
        return "trade"
    return type(event).__name__


def _market_event_side(event: BookUpdate | BookSnapshot | Trade) -> str | None:
    if isinstance(event, BookUpdate):
        return _python_side(event.side).value
    if isinstance(event, Trade):
        return _python_side(event.aggressor_side).value
    return None


def _market_event_price(event: BookUpdate | BookSnapshot | Trade) -> int | None:
    if isinstance(event, (BookUpdate, Trade)):
        return event.price
    return None


def _market_event_size(event: BookUpdate | BookSnapshot | Trade) -> int | None:
    if isinstance(event, (BookUpdate, Trade)):
        return event.size
    return None


def _python_side(side: Any) -> Side:
    if isinstance(side, Side):
        return side
    return Side(side)


def _reject_text(reason: OrderRejectReason) -> str:
    return reason.value


def run(
    strategy: Strategy,
    data_path: Any | None = None,
    date_range: Any | None = None,
    **kwargs: Any,
) -> BacktestResult:
    """Convenience API matching ``backtester.run(strategy, data_path, date_range)``."""

    runner_kwargs: dict[str, Any] = {}
    for name in ("config", "trading_engine_id", "fill_at_touch"):
        if name in kwargs:
            runner_kwargs[name] = kwargs.pop(name)

    runner = BacktestRunner(**runner_kwargs)
    return runner.run(strategy, data_path=data_path, date_range=date_range, **kwargs)


def run_many(
    strategies: dict[str, Strategy] | list[Strategy] | tuple[Strategy, ...],
    data_path: Any | None = None,
    date_range: Any | None = None,
    **kwargs: Any,
) -> BacktestResult:
    """Convenience API for integrated multi-strategy backtests."""

    runner_kwargs: dict[str, Any] = {}
    for name in ("config", "trading_engine_id", "fill_at_touch"):
        if name in kwargs:
            runner_kwargs[name] = kwargs.pop(name)

    runner = BacktestRunner(**runner_kwargs)
    return runner.run_many(
        strategies,
        data_path=data_path,
        date_range=date_range,
        **kwargs,
    )


__all__ = ["BacktestRunner", "run", "run_many"]
