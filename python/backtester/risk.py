"""Minimal Python risk limits for StrategyContext order submission."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from .portfolio import Portfolio
from .types import Side


class RiskRejectReason(str, Enum):
    MAX_ORDER_SIZE = "MaxOrderSize"
    MAX_POSITION_PER_INSTRUMENT = "MaxPositionPerInstrument"
    SHORT_NOT_ALLOWED = "ShortNotAllowed"


@dataclass(frozen=True)
class RiskLimits:
    max_position_per_instrument: int | None = None
    max_order_size: int | None = None
    allow_short: bool = True


class RiskLimitExceeded(RuntimeError):
    """Raised when StrategyContext rejects an order before it reaches the gateway."""

    def __init__(self, reason: RiskRejectReason, text: str) -> None:
        super().__init__(text)
        self.reason = reason
        self.text = text


def check_order(
    limits: RiskLimits | None,
    portfolio: Portfolio | None,
    instrument_id: int,
    side: Side,
    size: int,
) -> RiskRejectReason | None:
    if limits is None:
        return None

    size = int(size)
    if limits.max_order_size is not None and size > limits.max_order_size:
        return RiskRejectReason.MAX_ORDER_SIZE

    current_position = portfolio.position(instrument_id) if portfolio is not None else 0
    projected_position = current_position + _signed_quantity(side, size)

    if not limits.allow_short and projected_position < 0:
        return RiskRejectReason.SHORT_NOT_ALLOWED
    if (
        limits.max_position_per_instrument is not None
        and abs(projected_position) > limits.max_position_per_instrument
    ):
        return RiskRejectReason.MAX_POSITION_PER_INSTRUMENT

    return None


def risk_reject_text(reason: RiskRejectReason) -> str:
    if reason is RiskRejectReason.MAX_ORDER_SIZE:
        return "order size exceeds max_order_size"
    if reason is RiskRejectReason.MAX_POSITION_PER_INSTRUMENT:
        return "projected position exceeds max_position_per_instrument"
    if reason is RiskRejectReason.SHORT_NOT_ALLOWED:
        return "order would create a short position"
    return "risk limit exceeded"


def _signed_quantity(side: Side, size: int) -> int:
    if side is Side.BID:
        return size
    if side is Side.ASK:
        return -size
    return 0


__all__ = [
    "RiskLimitExceeded",
    "RiskLimits",
    "RiskRejectReason",
    "check_order",
    "risk_reject_text",
]
