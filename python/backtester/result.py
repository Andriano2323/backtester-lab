"""Backtest result containers for the pure Python Strategy API."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


FILL_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
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

METRIC_COLUMNS = [
    "timestamp_ns",
    "name",
    "value",
]


@dataclass(frozen=True)
class FillRecord:
    timestamp_ns: int
    trading_engine_id: int
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
    pnl: int


@dataclass(frozen=True)
class MetricRecord:
    timestamp_ns: int | None
    name: str
    value: Any


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
    def order_statistics(self) -> OrderStatistics:
        return self._order_statistics_for(self.order_log, self.fills)

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
        cancelled = 0
        filled = 0
        rejected = 0

        for record in order_log:
            event_type = record.event_type.lower()
            status = record.status.lower()
            if event_type in {"new_order", "neworder", "sent"}:
                sent += 1
            if event_type in {"cancel_order", "cancelorder"} or status == "cancelled":
                cancelled += 1
            if event_type == "fill" or status == "filled":
                filled += 1
            if event_type == "reject" or status == "rejected":
                rejected += 1

        if fills:
            filled = max(filled, len(fills))

        return OrderStatistics(
            sent=sent, cancelled=cancelled, filled=filled, rejected=rejected
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
    ) -> FillRecord:
        record = FillRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
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
    ) -> OrderLogRecord:
        record = OrderLogRecord(
            timestamp_ns=timestamp_ns,
            trading_engine_id=trading_engine_id,
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

    def add_pnl_point(self, timestamp_ns: int, pnl: int) -> PnlPoint:
        record = PnlPoint(timestamp_ns=timestamp_ns, pnl=pnl)
        self.pnl.append(record)
        return record

    def add_metric(
        self, name: str, value: Any, timestamp_ns: int | None = None
    ) -> MetricRecord:
        record = MetricRecord(timestamp_ns=timestamp_ns, name=name, value=value)
        self.metrics.append(record)
        return record

    @staticmethod
    def _frame(records: list[Any], columns: list[str]):
        import pandas as pd

        if not records:
            return pd.DataFrame(columns=columns)

        return pd.DataFrame([asdict(record) for record in records], columns=columns)
