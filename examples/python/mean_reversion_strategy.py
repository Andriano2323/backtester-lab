"""Small deterministic mean-reversion strategy example."""

from __future__ import annotations

from collections import deque
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

import backtester as backtest  # noqa: E402
from backtester import BacktestResult, Strategy  # noqa: E402
from backtester.types import BookUpdate, PRICE_SCALE, Side, Trade  # noqa: E402


class MeanReversionStrategy(Strategy):
    """Buy below a rolling average and sell above it."""

    def __init__(
        self, window: int = 3, threshold: int = PRICE_SCALE // 2, order_size: int = 1
    ) -> None:
        self.window = window
        self.threshold = threshold
        self.order_size = order_size
        self.prices: deque[int] = deque(maxlen=window)
        self.order_ids: list[int] = []

    def on_book_update(self, update, ctx) -> None:
        self._on_price(
            update.instrument_id, update.price, update.size, update.timestamp_ns, ctx
        )

    def on_trade(self, trade, ctx) -> None:
        self._on_price(
            trade.instrument_id, trade.price, trade.size, trade.timestamp_ns, ctx
        )

    def _on_price(
        self,
        instrument_id: int,
        price: int,
        available_size: int,
        timestamp_ns: int,
        ctx,
    ) -> None:
        if len(self.prices) < self.window:
            self.prices.append(price)
            return

        rolling_average = sum(self.prices) // len(self.prices)
        self.prices.append(price)
        size = min(self.order_size, available_size)
        if size <= 0:
            return

        if price <= rolling_average - self.threshold:
            self.order_ids.append(
                ctx.send_order(instrument_id, Side.BID, price, size, timestamp_ns)
            )
        elif (
            price >= rolling_average + self.threshold
            and ctx.current_position(instrument_id) > 0
        ):
            self.order_ids.append(
                ctx.send_order(instrument_id, Side.ASK, price, size, timestamp_ns)
            )


def example_events() -> list[BookUpdate | Trade]:
    instrument_id = 10
    return [
        Trade(instrument_id, 1, 1, 100 * PRICE_SCALE, 1, Side.ASK),
        Trade(instrument_id, 2, 2, 101 * PRICE_SCALE, 1, Side.ASK),
        Trade(instrument_id, 3, 3, 102 * PRICE_SCALE, 1, Side.ASK),
        BookUpdate(instrument_id, 4, 4, Side.BID, 99 * PRICE_SCALE, 1),
        Trade(instrument_id, 5, 5, 98 * PRICE_SCALE, 1, Side.ASK),
        Trade(instrument_id, 6, 6, 103 * PRICE_SCALE, 1, Side.BID),
        BookUpdate(instrument_id, 7, 7, Side.ASK, 104 * PRICE_SCALE, 1),
    ]


def run_example() -> BacktestResult:
    strategy = MeanReversionStrategy()
    return backtest.run(
        strategy,
        events=example_events(),
        fill_at_touch=True,
        progress_interval_events=100,
    )


def main() -> None:
    result = run_example()
    print("pnl_series")
    print(result.pnl_series)
    print("\nfills_df")
    print(result.fills_df)
    print("\norder_log_df")
    print(result.order_log_df)


if __name__ == "__main__":
    main()
