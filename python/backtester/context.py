"""Strategy execution context."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Mapping

from .portfolio import Portfolio
from .result import BacktestResult
from .risk import RiskLimitExceeded, RiskLimits, check_order, risk_reject_text
from .types import InstrumentId, OrderId, OrderStatus, Price, Quantity, Side, TimestampNs


@dataclass
class StrategyContext:
    """Runtime services exposed to Strategy implementations."""

    gateway: Any | None = None
    state: Any | None = None
    portfolio: Portfolio | None = None
    risk_limits: RiskLimits | None = None
    result: BacktestResult | None = None
    metadata: dict[str, Any] = field(default_factory=dict)
    positions: dict[InstrumentId, int] = field(default_factory=dict)
    pnl: int = 0
    metrics: list[dict[str, Any]] = field(default_factory=list)

    def send_order(
        self,
        instrument_id: InstrumentId,
        side: Side,
        price: Price,
        size: Quantity,
        timestamp_ns: TimestampNs,
    ) -> OrderId:
        if self.gateway is None:
            raise RuntimeError("StrategyContext has no gateway")
        side = self._python_side(side)
        reject_reason = check_order(self.risk_limits, self.portfolio, instrument_id, side, size)
        if reject_reason is not None:
            text = risk_reject_text(reject_reason)
            self._record_risk_reject(instrument_id, side, price, size, timestamp_ns, reject_reason.value, text)
            raise RiskLimitExceeded(reject_reason, text)
        return self.gateway.send_order(instrument_id, side, price, size, timestamp_ns)

    def cancel_order(self, order_id: OrderId, instrument_id: InstrumentId, timestamp_ns: TimestampNs) -> None:
        if self.gateway is None:
            raise RuntimeError("StrategyContext has no gateway")
        self.gateway.cancel_order(order_id, instrument_id, timestamp_ns)

    def modify_order(
        self,
        order_id: OrderId,
        instrument_id: InstrumentId,
        side: Side,
        price: Price,
        size: Quantity,
        timestamp_ns: TimestampNs,
    ) -> None:
        if self.gateway is None:
            raise RuntimeError("StrategyContext has no gateway")
        self.gateway.modify_order(order_id, instrument_id, side, price, size, timestamp_ns)

    def current_position(self, instrument_id: InstrumentId) -> int:
        if self.portfolio is not None:
            return self.portfolio.current_position(instrument_id)

        if self.state is not None:
            if hasattr(self.state, "current_position"):
                return int(self.state.current_position(instrument_id))
            positions = self._lookup_state_value("positions")
            if isinstance(positions, Mapping):
                return int(positions.get(instrument_id, 0))

        return int(self.positions.get(instrument_id, 0))

    def current_pnl(self) -> int:
        if self.portfolio is not None:
            return self.portfolio.current_pnl()

        if self.state is not None:
            if hasattr(self.state, "current_pnl"):
                return int(self.state.current_pnl())
            pnl = self._lookup_state_value("pnl")
            if pnl is not None:
                return int(pnl)

        return int(self.pnl)

    def record_metric(self, name: str, value: Any, timestamp_ns: TimestampNs | None = None) -> None:
        self.metrics.append(
            {
                "name": name,
                "value": value,
                "timestamp_ns": timestamp_ns,
            }
        )

    def _lookup_state_value(self, name: str) -> Any:
        if isinstance(self.state, Mapping):
            return self.state.get(name)
        return getattr(self.state, name, None)

    def _record_risk_reject(
        self,
        instrument_id: InstrumentId,
        side: Side,
        price: Price,
        size: Quantity,
        timestamp_ns: TimestampNs,
        reason: str,
        text: str,
    ) -> None:
        if self.result is None:
            return
        self.result.add_order_log(
            timestamp_ns=timestamp_ns,
            trading_engine_id=int(self.metadata.get("trading_engine_id", 0)),
            order_id=0,
            instrument_id=instrument_id,
            side=side.value,
            price=price,
            size=size,
            status=OrderStatus.REJECTED.value,
            event_type="risk_reject",
            reason=reason,
            text=text,
        )

    @staticmethod
    def _python_side(side: Any) -> Side:
        if isinstance(side, Side):
            return side
        return Side(side)
