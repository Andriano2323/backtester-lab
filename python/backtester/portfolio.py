"""Minimal position and PnL tracker for Python strategies."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .types import BookSnapshot, BookUpdate, OrderFill, Side, Trade


@dataclass
class Portfolio:
    """Track positions, realized PnL, and mark-to-market PnL."""

    positions: dict[int, int] = field(default_factory=dict)
    realized_pnl: int = 0
    last_price: dict[int, int] = field(default_factory=dict)
    _average_price: dict[int, int] = field(default_factory=dict)

    def position(self, instrument_id: int) -> int:
        return int(self.positions.get(instrument_id, 0))

    def current_position(self, instrument_id: int) -> int:
        return self.position(instrument_id)

    def update_last_price(self, instrument_id: int, price: int) -> None:
        self.last_price[instrument_id] = int(price)

    def update_market_data(self, event: BookUpdate | BookSnapshot | Trade) -> None:
        if isinstance(event, BookUpdate):
            self.update_last_price(event.instrument_id, event.price)
        elif isinstance(event, Trade):
            self.update_last_price(event.instrument_id, event.price)
        elif isinstance(event, BookSnapshot):
            price = _snapshot_mark_price(event)
            if price is not None:
                self.update_last_price(event.instrument_id, price)

    def apply_fill(self, instrument_id: int, side: Side, price: int, size: int) -> None:
        size = int(size)
        if size <= 0:
            return

        side = _python_side(side)
        signed_size = size if side is Side.BID else -size
        old_position = self.position(instrument_id)
        old_average = int(self._average_price.get(instrument_id, 0))
        new_position = old_position + signed_size

        if old_position == 0:
            self._set_open_position(instrument_id, new_position, price)
            return

        if _same_direction(old_position, signed_size):
            old_abs = abs(old_position)
            new_abs = abs(new_position)
            average = ((old_average * old_abs) + (int(price) * size)) // new_abs
            self._set_open_position(instrument_id, new_position, average)
            return

        closed_size = min(abs(old_position), size)
        if old_position > 0:
            self.realized_pnl += (int(price) - old_average) * closed_size
        else:
            self.realized_pnl += (old_average - int(price)) * closed_size

        if new_position == 0:
            self.positions[instrument_id] = 0
            self._average_price.pop(instrument_id, None)
        elif _same_direction(old_position, new_position):
            self._set_open_position(instrument_id, new_position, old_average)
        else:
            self._set_open_position(instrument_id, new_position, price)

    def on_fill(self, fill: OrderFill) -> None:
        self.apply_fill(fill.instrument_id, fill.side, fill.fill_price, fill.fill_size)

    def unrealized_pnl(self, instrument_id: int | None = None) -> int:
        if instrument_id is not None:
            return self._unrealized_for_instrument(instrument_id)
        return sum(self._unrealized_for_instrument(instrument_id) for instrument_id in self.positions)

    def mark_to_market_pnl(self) -> int:
        return int(self.realized_pnl + self.unrealized_pnl())

    def current_pnl(self) -> int:
        return self.mark_to_market_pnl()

    def _unrealized_for_instrument(self, instrument_id: int) -> int:
        position = self.position(instrument_id)
        if position == 0 or instrument_id not in self.last_price or instrument_id not in self._average_price:
            return 0

        last = self.last_price[instrument_id]
        average = self._average_price[instrument_id]
        if position > 0:
            return (last - average) * position
        return (average - last) * abs(position)

    def _set_open_position(self, instrument_id: int, position: int, average_price: int) -> None:
        self.positions[instrument_id] = int(position)
        if position == 0:
            self._average_price.pop(instrument_id, None)
        else:
            self._average_price[instrument_id] = int(average_price)


def _same_direction(left: int, right: int) -> bool:
    return (left > 0 and right > 0) or (left < 0 and right < 0)


def _python_side(side: Any) -> Side:
    if isinstance(side, Side):
        return side
    return Side(side)


def _snapshot_mark_price(snapshot: BookSnapshot) -> int | None:
    best_bid = snapshot.bids[0].price if snapshot.bids else None
    best_ask = snapshot.asks[0].price if snapshot.asks else None
    if best_bid is not None and best_ask is not None:
        return (best_bid + best_ask) // 2
    return best_bid if best_bid is not None else best_ask


__all__ = ["Portfolio"]
