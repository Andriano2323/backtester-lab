"""Benchmark Python Strategy callback overhead on synthetic BookUpdate events."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from time import perf_counter

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

import backtester as backtest  # noqa: E402
from backtester import Strategy  # noqa: E402
from backtester.types import BookUpdate, PRICE_SCALE, Side  # noqa: E402


class NoOpStrategy(Strategy):
    def __init__(self) -> None:
        self.callbacks = 0

    def on_book_update(self, update, ctx) -> None:
        self.callbacks += 1


def make_events(count: int) -> list[BookUpdate]:
    return [
        BookUpdate(
            instrument_id=10,
            timestamp_ns=index,
            seq_no=index,
            side=Side.BID,
            price=100 * PRICE_SCALE + index,
            size=1,
        )
        for index in range(1, count + 1)
    ]


def run_benchmark(event_count: int) -> tuple[int, float, float]:
    strategy = NoOpStrategy()
    events = make_events(event_count)

    started = perf_counter()
    backtest.run(
        strategy,
        events=events,
        progress_interval_seconds=None,
        progress_interval_events=None,
    )
    elapsed = perf_counter() - started

    callbacks_per_second = strategy.callbacks / elapsed if elapsed > 0 else 0.0
    return strategy.callbacks, elapsed, callbacks_per_second


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure Python Strategy callback overhead."
    )
    parser.add_argument(
        "--events",
        type=int,
        default=100_000,
        help="Number of synthetic BookUpdate events.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.events < 0:
        raise SystemExit("--events must be non-negative")

    callbacks, elapsed, callbacks_per_second = run_benchmark(args.events)
    print("events,wall_clock_seconds,callbacks_per_second")
    print(f"{callbacks},{elapsed:.9f},{callbacks_per_second:.6f}")


if __name__ == "__main__":
    main()
