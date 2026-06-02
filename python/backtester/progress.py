"""Progress metrics reported by backtest runners."""

from __future__ import annotations

from dataclasses import dataclass, field

from .result import OrderStatistics


@dataclass(frozen=True)
class ProgressMetrics:
    """Backtest progress snapshot for callbacks and Strategy.on_progress."""

    processed_events: int = 0
    total_events: int | None = None
    progress_percent: float | None = None
    last_timestamp_ns: int | None = None
    current_pnl: int = 0
    orders_sent: int = 0
    orders_cancelled: int = 0
    orders_filled: int = 0
    orders_rejected: int = 0
    per_instrument_order_stats: dict[int, OrderStatistics] = field(default_factory=dict)
    elapsed_seconds: float = 0.0

    def __post_init__(self) -> None:
        if self.progress_percent is None and self.total_events is not None:
            object.__setattr__(
                self,
                "progress_percent",
                _progress_percent(self.processed_events, self.total_events),
            )

    @property
    def progress_fraction(self) -> float | None:
        """Return progress in [0, 1], or None when total work is unknown."""

        if self.progress_percent is None:
            return None
        return self.progress_percent / 100.0


def _progress_percent(processed_events: int, total_events: int) -> float:
    if total_events <= 0:
        return 100.0
    value = (processed_events / total_events) * 100.0
    return max(0.0, min(100.0, value))
