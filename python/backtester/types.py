"""Shared Python type aliases for the Strategy API."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import TypeAlias

InstrumentId: TypeAlias = int
OrderId: TypeAlias = int
TradingEngineId: TypeAlias = int
TimestampNs: TypeAlias = int
Price: TypeAlias = int
Quantity: TypeAlias = int

PRICE_SCALE = 1_000_000_000
UNDEFINED_PRICE = 9_223_372_036_854_775_807


class Side(str, Enum):
    """Order side values shared with the C++ domain contract."""

    ASK = "A"
    BID = "B"
    NONE = "N"


class OrderStatus(str, Enum):
    """Order lifecycle states exposed to Python strategies."""

    NEW = "New"
    ACCEPTED = "Accepted"
    MODIFY_REQUESTED = "ModifyRequested"
    CANCEL_REQUESTED = "CancelRequested"
    PARTIALLY_FILLED = "PartiallyFilled"
    FILLED = "Filled"
    CANCELLED = "Cancelled"
    REJECTED = "Rejected"


class OrderRejectReason(str, Enum):
    """Server-side order reject reasons."""

    DUPLICATE_ORDER_ID = "DuplicateOrderId"
    UNKNOWN_ORDER_ID = "UnknownOrderId"
    INVALID_INSTRUMENT = "InvalidInstrument"
    INVALID_SIDE = "InvalidSide"
    INVALID_PRICE = "InvalidPrice"
    INVALID_QUANTITY = "InvalidQuantity"
    ALREADY_TERMINAL = "AlreadyTerminal"
    INTERNAL_ERROR = "InternalError"


class OrderAckType(str, Enum):
    """Order acknowledgement transition type."""

    NEW_ACCEPTED = "NewAccepted"
    MODIFY_ACCEPTED = "ModifyAccepted"
    CANCEL_ACCEPTED = "CancelAccepted"


@dataclass(frozen=True)
class PriceLevel:
    level_index: int
    price: Price
    size: Quantity


@dataclass(frozen=True)
class BookUpdate:
    instrument_id: InstrumentId
    timestamp_ns: TimestampNs
    seq_no: int
    side: Side
    price: Price
    size: Quantity


@dataclass(frozen=True)
class BookSnapshot:
    instrument_id: InstrumentId
    timestamp_ns: TimestampNs
    seq_no: int
    bids: list[PriceLevel] = field(default_factory=list)
    asks: list[PriceLevel] = field(default_factory=list)


@dataclass(frozen=True)
class Trade:
    instrument_id: InstrumentId
    timestamp_ns: TimestampNs
    seq_no: int
    price: Price
    size: Quantity
    aggressor_side: Side


@dataclass(frozen=True)
class OrderAck:
    trading_engine_id: TradingEngineId
    order_id: OrderId
    instrument_id: InstrumentId
    side: Side
    price: Price
    size: Quantity
    timestamp_ns: TimestampNs
    status: OrderStatus
    ack_type: OrderAckType


@dataclass(frozen=True)
class OrderFill:
    trading_engine_id: TradingEngineId
    order_id: OrderId
    instrument_id: InstrumentId
    side: Side
    price: Price
    size: Quantity
    timestamp_ns: TimestampNs
    status: OrderStatus
    fill_price: Price
    fill_size: Quantity
    remaining_size: Quantity


@dataclass(frozen=True)
class OrderReject:
    trading_engine_id: TradingEngineId
    order_id: OrderId
    instrument_id: InstrumentId
    side: Side
    price: Price
    size: Quantity
    timestamp_ns: TimestampNs
    status: OrderStatus
    reason: OrderRejectReason
    text: str


def price_to_float(price: Price) -> float:
    """Convert fixed-precision integer price to decimal float."""

    return price / PRICE_SCALE


def float_to_price(value: float) -> Price:
    """Convert decimal float price to fixed-precision integer price."""

    return int(round(value * PRICE_SCALE))


def is_defined_price(price: Price) -> bool:
    """Return whether a price is not the shared undefined-price sentinel."""

    return price != UNDEFINED_PRICE
