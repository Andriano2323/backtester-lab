"""Adapters between optional C++ bindings and pure Python dataclasses."""

from __future__ import annotations

from typing import Any

from ._cpp import require_cpp
from .types import (
    BookSnapshot,
    BookUpdate,
    OrderAck,
    OrderAckType,
    OrderFill,
    OrderReject,
    OrderRejectReason,
    OrderStatus,
    PriceLevel,
    Side,
    Trade,
)


def _enum_name(value: Any) -> str:
    name = getattr(value, "name", None)
    if isinstance(name, str):
        return name
    text = str(value)
    return text.rsplit(".", 1)[-1]


def _side_from_cpp(value: Any) -> Side:
    name = _enum_name(value).upper()
    if name in {"ASK", "A"}:
        return Side.ASK
    if name in {"BID", "B"}:
        return Side.BID
    if name in {"NONE", "N"}:
        return Side.NONE
    raise ValueError(f"Unknown C++ Side value: {value!r}")


def _status_from_cpp(value: Any) -> OrderStatus:
    return OrderStatus(_enum_name(value))


def _ack_type_from_cpp(value: Any) -> OrderAckType:
    return OrderAckType(_enum_name(value))


def _reject_reason_from_cpp(value: Any) -> OrderRejectReason:
    return OrderRejectReason(_enum_name(value))


def _cpp_side(side: Side):
    cpp = require_cpp()
    if side is Side.ASK:
        return cpp.Side.Ask
    if side is Side.BID:
        return cpp.Side.Bid
    return getattr(cpp.Side, "None")


def _cpp_order_status(status: OrderStatus):
    cpp = require_cpp()
    return getattr(cpp.OrderStatus, status.value)


def _cpp_ack_type(ack_type: OrderAckType):
    cpp = require_cpp()
    return getattr(cpp.OrderAckType, ack_type.value)


def _cpp_reject_reason(reason: OrderRejectReason):
    cpp = require_cpp()
    return getattr(cpp.OrderRejectReason, reason.value)


def from_cpp_price_level(level: Any) -> PriceLevel:
    return PriceLevel(
        level_index=int(level.level_index),
        price=int(level.price),
        size=int(level.size),
    )


def to_cpp_price_level(level: PriceLevel):
    cpp = require_cpp()
    return cpp.PriceLevel(
        level_index=level.level_index, price=level.price, size=level.size
    )


def from_cpp_book_update(update: Any) -> BookUpdate:
    return BookUpdate(
        instrument_id=int(update.instrument_id),
        timestamp_ns=int(update.timestamp_ns),
        seq_no=int(update.seq_no),
        side=_side_from_cpp(update.side),
        price=int(update.price),
        size=int(update.size),
    )


def to_cpp_book_update(update: BookUpdate):
    cpp = require_cpp()
    return cpp.BookUpdate(
        instrument_id=update.instrument_id,
        timestamp_ns=update.timestamp_ns,
        seq_no=update.seq_no,
        side=_cpp_side(update.side),
        price=update.price,
        size=update.size,
    )


def from_cpp_book_snapshot(snapshot: Any) -> BookSnapshot:
    return BookSnapshot(
        instrument_id=int(snapshot.instrument_id),
        timestamp_ns=int(snapshot.timestamp_ns),
        seq_no=int(snapshot.seq_no),
        bids=[from_cpp_price_level(level) for level in snapshot.bids],
        asks=[from_cpp_price_level(level) for level in snapshot.asks],
    )


def to_cpp_book_snapshot(snapshot: BookSnapshot):
    cpp = require_cpp()
    return cpp.BookSnapshot(
        instrument_id=snapshot.instrument_id,
        timestamp_ns=snapshot.timestamp_ns,
        seq_no=snapshot.seq_no,
        bids=[to_cpp_price_level(level) for level in snapshot.bids],
        asks=[to_cpp_price_level(level) for level in snapshot.asks],
    )


def from_cpp_trade(trade: Any) -> Trade:
    return Trade(
        instrument_id=int(trade.instrument_id),
        timestamp_ns=int(trade.timestamp_ns),
        seq_no=int(trade.seq_no),
        price=int(trade.price),
        size=int(trade.size),
        aggressor_side=_side_from_cpp(trade.aggressor_side),
    )


def to_cpp_trade(trade: Trade):
    cpp = require_cpp()
    return cpp.Trade(
        instrument_id=trade.instrument_id,
        timestamp_ns=trade.timestamp_ns,
        seq_no=trade.seq_no,
        price=trade.price,
        size=trade.size,
        aggressor_side=_cpp_side(trade.aggressor_side),
    )


def from_cpp_order_ack(ack: Any) -> OrderAck:
    return OrderAck(
        trading_engine_id=int(ack.trading_engine_id),
        order_id=int(ack.order_id),
        instrument_id=int(ack.instrument_id),
        side=_side_from_cpp(ack.side),
        price=int(ack.price),
        size=int(ack.size),
        timestamp_ns=int(ack.timestamp_ns),
        status=_status_from_cpp(ack.status),
        ack_type=_ack_type_from_cpp(ack.ack_type),
    )


def to_cpp_order_ack(ack: OrderAck):
    cpp = require_cpp()
    return cpp.OrderAck(
        trading_engine_id=ack.trading_engine_id,
        order_id=ack.order_id,
        instrument_id=ack.instrument_id,
        side=_cpp_side(ack.side),
        price=ack.price,
        size=ack.size,
        timestamp_ns=ack.timestamp_ns,
        status=_cpp_order_status(ack.status),
        ack_type=_cpp_ack_type(ack.ack_type),
    )


def from_cpp_order_fill(fill: Any) -> OrderFill:
    return OrderFill(
        trading_engine_id=int(fill.trading_engine_id),
        order_id=int(fill.order_id),
        instrument_id=int(fill.instrument_id),
        side=_side_from_cpp(fill.side),
        price=int(fill.price),
        size=int(fill.size),
        timestamp_ns=int(fill.timestamp_ns),
        status=_status_from_cpp(fill.status),
        fill_price=int(fill.fill_price),
        fill_size=int(fill.fill_size),
        remaining_size=int(fill.remaining_size),
    )


def to_cpp_order_fill(fill: OrderFill):
    cpp = require_cpp()
    return cpp.OrderFill(
        trading_engine_id=fill.trading_engine_id,
        order_id=fill.order_id,
        instrument_id=fill.instrument_id,
        side=_cpp_side(fill.side),
        price=fill.price,
        size=fill.size,
        timestamp_ns=fill.timestamp_ns,
        status=_cpp_order_status(fill.status),
        fill_price=fill.fill_price,
        fill_size=fill.fill_size,
        remaining_size=fill.remaining_size,
    )


def from_cpp_order_reject(reject: Any) -> OrderReject:
    return OrderReject(
        trading_engine_id=int(reject.trading_engine_id),
        order_id=int(reject.order_id),
        instrument_id=int(reject.instrument_id),
        side=_side_from_cpp(reject.side),
        price=int(reject.price),
        size=int(reject.size),
        timestamp_ns=int(reject.timestamp_ns),
        status=_status_from_cpp(reject.status),
        reason=_reject_reason_from_cpp(reject.reason),
        text=str(reject.text),
    )


def to_cpp_order_reject(reject: OrderReject):
    cpp = require_cpp()
    return cpp.OrderReject(
        trading_engine_id=reject.trading_engine_id,
        order_id=reject.order_id,
        instrument_id=reject.instrument_id,
        side=_cpp_side(reject.side),
        price=reject.price,
        size=reject.size,
        timestamp_ns=reject.timestamp_ns,
        status=_cpp_order_status(reject.status),
        reason=_cpp_reject_reason(reject.reason),
        text=reject.text,
    )


def from_cpp_message(message: Any):
    cpp = require_cpp()
    if isinstance(message, cpp.BookUpdate):
        return from_cpp_book_update(message)
    if isinstance(message, cpp.BookSnapshot):
        return from_cpp_book_snapshot(message)
    if isinstance(message, cpp.Trade):
        return from_cpp_trade(message)
    if isinstance(message, cpp.OrderAck):
        return from_cpp_order_ack(message)
    if isinstance(message, cpp.OrderFill):
        return from_cpp_order_fill(message)
    if isinstance(message, cpp.OrderReject):
        return from_cpp_order_reject(message)
    raise TypeError(f"Unsupported C++ message type: {type(message)!r}")


def to_cpp_message(message: Any):
    if isinstance(message, BookUpdate):
        return to_cpp_book_update(message)
    if isinstance(message, BookSnapshot):
        return to_cpp_book_snapshot(message)
    if isinstance(message, Trade):
        return to_cpp_trade(message)
    if isinstance(message, OrderAck):
        return to_cpp_order_ack(message)
    if isinstance(message, OrderFill):
        return to_cpp_order_fill(message)
    if isinstance(message, OrderReject):
        return to_cpp_order_reject(message)
    raise TypeError(f"Unsupported Python message type: {type(message)!r}")


python_to_cpp_side = _cpp_side


__all__ = [
    "from_cpp_book_snapshot",
    "from_cpp_book_update",
    "from_cpp_message",
    "from_cpp_order_ack",
    "from_cpp_order_fill",
    "from_cpp_order_reject",
    "from_cpp_price_level",
    "from_cpp_trade",
    "python_to_cpp_side",
    "to_cpp_book_snapshot",
    "to_cpp_book_update",
    "to_cpp_message",
    "to_cpp_order_ack",
    "to_cpp_order_fill",
    "to_cpp_order_reject",
    "to_cpp_price_level",
    "to_cpp_trade",
]
