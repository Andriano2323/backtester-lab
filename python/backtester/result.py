"""Backtest result containers for the pure Python Strategy API."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


FILL_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "instrument_id",
    "side",
    "fill_price",
    "fill_size",
    "remaining_size",
]

ORDER_LOG_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "instrument_id",
    "side",
    "price",
    "size",
    "status",
    "event_type",
    "reason",
    "text",
]

PNL_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "pnl",
]

METRIC_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "name",
    "value",
]

POSITION_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "instrument_id",
    "position",
    "realized_pnl",
    "unrealized_pnl",
    "pnl",
]

TRACE_COLUMNS = [
    "timestamp_ns",
    "sequence",
    "stage",
    "explain_stage",
    "event_type",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "synthetic_order_id",
    "activation_time_ns",
    "instrument_id",
    "side",
    "price",
    "size",
    "best_bid_before",
    "best_ask_before",
    "best_bid_after",
    "best_ask_after",
    "fill_price",
    "fill_size",
    "remaining_size",
    "reason",
    "text",
]


@dataclass(frozen=True)
class FillRecord:
    timestamp_ns: int
    trading_engine_id: int
    strategy_name: str | None
    order_id: int
    instrument_id: int
    side: str
    fill_price: int
    fill_size: int
    remaining_size: int


@dataclass(frozen=True)
class OrderLogRecord:
    timestamp_ns: int
    trading_engine_id: int
    strategy_name: str | None
    order_id: int
    instrument_id: int
    side: str
    price: int
    size: int
    status: str
    event_type: str
    reason: str | None = None
    text: str | None = None


@dataclass(frozen=True)
class PnlPoint:
    timestamp_ns: int
    trading_engine_id: int | None
    strategy_name: str | None
    pnl: int


@dataclass(frozen=True)
class MetricRecord:
    timestamp_ns: int | None
    trading_engine_id: int | None
    strategy_name: str | None
    name: str
    value: Any


@dataclass(frozen=True)
class PositionRecord:
    timestamp_ns: int
    trading_engine_id: int
    strategy_name: str | None
    instrument_id: int
    position: int
    realized_pnl: int
    unrealized_pnl: int
    pnl: int


@dataclass(frozen=True)
class TraceRecord:
    timestamp_ns: int | None
    sequence: int
    stage: str
    explain_stage: str
    event_type: str
    trading_engine_id: int | None = None
    strategy_name: str | None = None
    order_id: int | None = None
    synthetic_order_id: int | None = None
    activation_time_ns: int | None = None
    instrument_id: int | None = None
    side: str | None = None
    price: int | None = None
    size: int | None = None
    best_bid_before: int | None = None
    best_ask_before: int | None = None
    best_bid_after: int | None = None
    best_ask_after: int | None = None
    fill_price: int | None = None
    fill_size: int | None = None
    remaining_size: int | None = None
    reason: str | None = None
    text: str | None = None


@dataclass(frozen=True)
class OrderStatistics:
    sent: int = 0
    cancelled: int = 0
    filled: int = 0
    rejected: int = 0


@dataclass
class BacktestResult:
    """Result object returned by BacktestRunner.

    The object stores raw Python records and creates pandas objects on demand.
    Importing this module does not require compiled C++ bindings.
    """

    fills: list[FillRecord] = field(default_factory=list)
    order_log: list[OrderLogRecord] = field(default_factory=list)
    pnl: list[PnlPoint] = field(default_factory=list)
    metrics: list[MetricRecord] = field(default_factory=list)
    positions: list[PositionRecord] = field(default_factory=list)
    trace: list[TraceRecord] = field(default_factory=list)
    trace_enabled: bool = True

    @property
    def pnl_series(self):
        """Return PnL as a pandas Series indexed by timestamp_ns."""

        import pandas as pd

        if not self.pnl:
            return pd.Series(
                [], index=pd.Index([], name="timestamp_ns"), name="pnl", dtype="int64"
            )

        return pd.Series(
            [point.pnl for point in self.pnl],
            index=pd.Index(
                [point.timestamp_ns for point in self.pnl], name="timestamp_ns"
            ),
            name="pnl",
        )

    @property
    def fills_df(self):
        """Return fills as a pandas DataFrame with stable columns."""

        return self._frame(self.fills, FILL_COLUMNS)

    @property
    def order_log_df(self):
        """Return order events as a pandas DataFrame with stable columns."""

        return self._frame(self.order_log, ORDER_LOG_COLUMNS)

    @property
    def metrics_df(self):
        """Return custom metrics as a pandas DataFrame with stable columns."""

        return self._frame(self.metrics, METRIC_COLUMNS)

    @property
    def pnl_df(self):
        """Return PnL points as a pandas DataFrame with grouping columns."""

        return self._frame(self.pnl, PNL_COLUMNS)

    @property
    def positions_df(self):
        """Return position snapshots as a pandas DataFrame with stable columns."""

        return self._frame(self.positions, POSITION_COLUMNS)

    @property
    def trace_df(self):
        """Return explainability trace rows as a pandas DataFrame."""

        return self._frame(self.trace, TRACE_COLUMNS)

    @property
    def order_statistics(self) -> OrderStatistics:
        return self._order_statistics_for(self.order_log, self.fills)

    def for_engine(self, trading_engine_id: int) -> "BacktestResult":
        """Return a result view filtered to one trading engine id."""

        return self.filter(trading_engine_id=trading_engine_id)

    def for_strategy(self, strategy_name: str) -> "BacktestResult":
        """Return a result view filtered to one strategy name."""

        return self.filter(strategy_name=strategy_name)

    def by_engine(self) -> dict[int, "BacktestResult"]:
        """Group result records by trading engine id."""

        engine_ids: set[int] = set()
        for collection in (
            self.fills,
            self.order_log,
            self.pnl,
            self.metrics,
            self.positions,
            self.trace,
        ):
            for record in collection:
                engine_id = getattr(record, "trading_engine_id", None)
                if engine_id is not None:
                    engine_ids.add(int(engine_id))
        return {
            engine_id: self.for_engine(engine_id) for engine_id in sorted(engine_ids)
        }

    def by_strategy(self) -> dict[str, "BacktestResult"]:
        """Group result records by strategy name."""

        strategy_names: set[str] = set()
        for collection in (
            self.fills,
            self.order_log,
            self.pnl,
            self.metrics,
            self.positions,
            self.trace,
        ):
            for record in collection:
                strategy_name = getattr(record, "strategy_name", None)
                if strategy_name is not None:
                    strategy_names.add(str(strategy_name))
        return {
            strategy_name: self.for_strategy(strategy_name)
            for strategy_name in sorted(strategy_names)
        }

    def filter(
        self,
        *,
        trading_engine_id: int | None = None,
        strategy_name: str | None = None,
    ) -> "BacktestResult":
        """Return a result containing records matching the supplied grouping keys."""

        def keep(record: Any) -> bool:
            if trading_engine_id is not None:
                record_engine_id = getattr(record, "trading_engine_id", None)
                if record_engine_id != trading_engine_id:
                    return False
            if strategy_name is not None:
                if getattr(record, "strategy_name", None) != strategy_name:
                    return False
            return True

        return BacktestResult(
            fills=[record for record in self.fills if keep(record)],
            order_log=[record for record in self.order_log if keep(record)],
            pnl=[record for record in self.pnl if keep(record)],
            metrics=[record for record in self.metrics if keep(record)],
            positions=[record for record in self.positions if keep(record)],
            trace=[record for record in self.trace if keep(record)],
            trace_enabled=self.trace_enabled,
        )

    @property
    def per_instrument_order_statistics(self) -> dict[int, OrderStatistics]:
        instrument_ids = {record.instrument_id for record in self.order_log}
        instrument_ids.update(fill.instrument_id for fill in self.fills)
        return {
            instrument_id: self._order_statistics_for(
                [
                    record
                    for record in self.order_log
                    if record.instrument_id == instrument_id
                ],
                [fill for fill in self.fills if fill.instrument_id == instrument_id],
            )
            for instrument_id in sorted(instrument_ids)
        }

    @staticmethod
    def _order_statistics_for(
        order_log: list[OrderLogRecord], fills: list[FillRecord]
    ) -> OrderStatistics:
        sent = 0
        cancelled_order_ids: set[int] = set()
        filled = 0
        rejected = 0

        for record in order_log:
            event_type = record.event_type.lower()
            status = record.status.lower()
            if event_type in {"new_order", "neworder", "sent"}:
                sent += 1
            if status == "cancelled":
                cancelled_order_ids.add(record.order_id)
            if event_type == "fill" or status == "filled":
                filled += 1
            if event_type == "reject" or status == "rejected":
                rejected += 1

        if fills:
            filled = max(filled, len(fills))

        return OrderStatistics(
            sent=sent,
            cancelled=len(cancelled_order_ids),
            filled=filled,
            rejected=rejected,
        )

    def add_fill(
        self,
        timestamp_ns: int,
        trading_engine_id: int,
        order_id: int,
        instrument_id: int,
        side: str,
        fill_price: int,
        fill_size: int,
        remaining_size: int,
        strategy_name: str | None = None,
    ) -> FillRecord:
        record = FillRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side,
            fill_price=fill_price,
            fill_size=fill_size,
            remaining_size=remaining_size,
        )
        self.fills.append(record)
        return record

    def add_order_log(
        self,
        timestamp_ns: int,
        trading_engine_id: int,
        order_id: int,
        instrument_id: int,
        side: str,
        price: int,
        size: int,
        status: str,
        event_type: str,
        reason: str | None = None,
        text: str | None = None,
        strategy_name: str | None = None,
    ) -> OrderLogRecord:
        record = OrderLogRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            order_id=order_id,
            instrument_id=instrument_id,
            side=side,
            price=price,
            size=size,
            status=status,
            event_type=event_type,
            reason=reason,
            text=text,
        )
        self.order_log.append(record)
        return record

    def add_pnl_point(
        self,
        timestamp_ns: int,
        pnl: int,
        trading_engine_id: int | None = None,
        strategy_name: str | None = None,
    ) -> PnlPoint:
        record = PnlPoint(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            pnl=pnl,
        )
        self.pnl.append(record)
        return record

    def add_metric(
        self,
        name: str,
        value: Any,
        timestamp_ns: int | None = None,
        trading_engine_id: int | None = None,
        strategy_name: str | None = None,
    ) -> MetricRecord:
        record = MetricRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            name=name,
            value=value,
        )
        self.metrics.append(record)
        return record

    def add_position(
        self,
        timestamp_ns: int,
        trading_engine_id: int,
        instrument_id: int,
        position: int,
        realized_pnl: int,
        unrealized_pnl: int,
        pnl: int,
        strategy_name: str | None = None,
    ) -> PositionRecord:
        record = PositionRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            instrument_id=instrument_id,
            position=position,
            realized_pnl=realized_pnl,
            unrealized_pnl=unrealized_pnl,
            pnl=pnl,
        )
        self.positions.append(record)
        return record

    def add_trace(
        self,
        timestamp_ns: int | None,
        stage: str,
        event_type: str,
        *,
        sequence: int | None = None,
        explain_stage: str | None = None,
        trading_engine_id: int | None = None,
        strategy_name: str | None = None,
        order_id: int | None = None,
        synthetic_order_id: int | None = None,
        activation_time_ns: int | None = None,
        instrument_id: int | None = None,
        side: str | None = None,
        price: int | None = None,
        size: int | None = None,
        best_bid_before: int | None = None,
        best_ask_before: int | None = None,
        best_bid_after: int | None = None,
        best_ask_after: int | None = None,
        fill_price: int | None = None,
        fill_size: int | None = None,
        remaining_size: int | None = None,
        reason: str | None = None,
        text: str | None = None,
    ) -> TraceRecord:
        record = TraceRecord(
            timestamp_ns=timestamp_ns,
            sequence=sequence if sequence is not None else len(self.trace) + 1,
            stage=stage,
            explain_stage=explain_stage
            if explain_stage is not None
            else _default_explain_stage(stage, event_type),
            event_type=event_type,
            trading_engine_id=trading_engine_id,
            strategy_name=strategy_name,
            order_id=order_id,
            synthetic_order_id=synthetic_order_id,
            activation_time_ns=activation_time_ns,
            instrument_id=instrument_id,
            side=side,
            price=price,
            size=size,
            best_bid_before=best_bid_before,
            best_ask_before=best_ask_before,
            best_bid_after=best_bid_after,
            best_ask_after=best_ask_after,
            fill_price=fill_price,
            fill_size=fill_size,
            remaining_size=remaining_size,
            reason=reason,
            text=text,
        )
        if self.trace_enabled:
            self.trace.append(record)
        return record

    @staticmethod
    def _frame(records: list[Any], columns: list[str]):
        import pandas as pd

        if not records:
            return pd.DataFrame(columns=columns)

        return pd.DataFrame([asdict(record) for record in records], columns=columns)


def _default_explain_stage(stage: str, event_type: str) -> str:
    if stage in {"market_event", "lob_update", "market_publish"}:
        return "market"
    if stage in {"order_request", "strategy_callback"}:
        return "strategy_order" if stage == "order_request" else "strategy"
    if stage == "order_ack":
        return "ack"
    if stage == "order_reject":
        return "reject"
    if stage in {"fill_simulation", "order_fill"}:
        return "fill"
    if stage == "portfolio_update":
        return "portfolio_update"
    if event_type in {"ack"}:
        return "ack"
    if event_type in {"reject", "risk_reject"}:
        return "reject"
    if event_type in {"fill", "resting_order", "not_active"}:
        return "fill"
    return stage
