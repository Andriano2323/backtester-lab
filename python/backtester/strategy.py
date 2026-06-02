"""Base class for Python trading strategies."""

from __future__ import annotations

from .context import StrategyContext
from .progress import ProgressMetrics
from .result import BacktestResult
from .types import BookSnapshot, BookUpdate, OrderAck, OrderFill, OrderReject, Trade


class Strategy:
    """Base strategy with optional lifecycle hooks."""

    def on_book_update(self, update: BookUpdate, ctx: StrategyContext) -> None:
        """Called for each incremental book update."""

    def on_book_snapshot(self, snapshot: BookSnapshot, ctx: StrategyContext) -> None:
        """Called for each book snapshot."""

    def on_trade(self, trade: Trade, ctx: StrategyContext) -> None:
        """Called for each market trade."""

    def on_fill(self, fill: OrderFill, ctx: StrategyContext) -> None:
        """Called when an order fill is received."""

    def on_reject(self, reject: OrderReject, ctx: StrategyContext) -> None:
        """Called when an order reject is received."""

    def on_ack(self, ack: OrderAck, ctx: StrategyContext) -> None:
        """Called when an order acknowledgement is received."""

    def on_progress(self, metrics: ProgressMetrics, ctx: StrategyContext) -> None:
        """Called when runner progress is updated."""

    def on_start(self, ctx: StrategyContext) -> None:
        """Called before the runner starts delivering events."""

    def on_finish(self, result: BacktestResult, ctx: StrategyContext) -> None:
        """Called after the runner stops."""
