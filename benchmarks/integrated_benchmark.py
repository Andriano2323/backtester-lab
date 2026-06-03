"""Benchmark integrated replay overhead across progressively richer modes."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
from time import perf_counter
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
for path in (REPO_ROOT / "build" / "python", REPO_ROOT / "python"):
    if path.exists() and str(path) not in sys.path:
        sys.path.insert(0, str(path))

from backtester import BacktestRunner, Strategy  # noqa: E402
from backtester._cpp import require_cpp  # noqa: E402
from backtester.adapters import to_cpp_market_data_event  # noqa: E402
from backtester.types import Action, MarketDataEvent, Side  # noqa: E402


PRICE = 102_000_000_000
INSTRUMENT_ID = 10
ALL_MODES = (
    "ingestion_only",
    "lob_only",
    "integrated_no_strategy",
    "integrated_strategy",
)


@dataclass(frozen=True)
class BenchmarkRow:
    mode: str
    event_count: int
    wall_clock_seconds: float
    events_per_second: float
    callbacks: int = 0
    callbacks_per_second: float = 0.0
    market_callbacks: int = 0
    orders: int = 0
    orders_per_second: float = 0.0
    fills: int = 0
    fills_per_second: float = 0.0
    trace_rows: int = 0

    @classmethod
    def from_counts(
        cls,
        *,
        mode: str,
        event_count: int,
        wall_clock_seconds: float,
        callbacks: int = 0,
        market_callbacks: int = 0,
        orders: int = 0,
        fills: int = 0,
        trace_rows: int = 0,
    ) -> "BenchmarkRow":
        return cls(
            mode=mode,
            event_count=event_count,
            wall_clock_seconds=wall_clock_seconds,
            events_per_second=_rate(event_count, wall_clock_seconds),
            callbacks=callbacks,
            callbacks_per_second=_rate(callbacks, wall_clock_seconds),
            market_callbacks=market_callbacks,
            orders=orders,
            orders_per_second=_rate(orders, wall_clock_seconds),
            fills=fills,
            fills_per_second=_rate(fills, wall_clock_seconds),
            trace_rows=trace_rows,
        )

    def csv_row(self) -> str:
        return (
            f"{self.mode},{self.event_count},{self.wall_clock_seconds:.9f},"
            f"{self.events_per_second:.6f},{self.callbacks},"
            f"{self.callbacks_per_second:.6f},{self.market_callbacks},"
            f"{self.orders},{self.orders_per_second:.6f},{self.fills},"
            f"{self.fills_per_second:.6f},{self.trace_rows}"
        )


class BenchmarkStrategy(Strategy):
    def __init__(self, orders_every: int = 0) -> None:
        self.orders_every = orders_every
        self.callbacks = 0
        self.market_callbacks = 0

    def on_start(self, ctx) -> None:
        self.callbacks += 1

    def on_book_snapshot(self, snapshot, ctx) -> None:
        self.callbacks += 1
        self.market_callbacks += 1
        if self.orders_every <= 0 or not snapshot.asks:
            return
        if self.market_callbacks % self.orders_every != 0:
            return
        ctx.send_order(
            snapshot.instrument_id,
            Side.BID,
            snapshot.asks[0].price,
            1,
            snapshot.timestamp_ns,
        )

    def on_book_update(self, update, ctx) -> None:
        self.callbacks += 1
        self.market_callbacks += 1

    def on_trade(self, trade, ctx) -> None:
        self.callbacks += 1
        self.market_callbacks += 1

    def on_ack(self, ack, ctx) -> None:
        self.callbacks += 1

    def on_fill(self, fill, ctx) -> None:
        self.callbacks += 1

    def on_reject(self, reject, ctx) -> None:
        self.callbacks += 1

    def on_finish(self, result, ctx) -> None:
        self.callbacks += 1


def make_events(event_count: int) -> list[MarketDataEvent]:
    return [
        MarketDataEvent(
            timestamp=index,
            ts_recv=index,
            ts_event=index,
            order_id=1_000_000 + index,
            side=Side.ASK,
            price=PRICE + (index % 5),
            size=10,
            action=Action.ADD,
            instrument_id=INSTRUMENT_ID,
            source_file_id=1,
            source_sequence=index,
        )
        for index in range(1, event_count + 1)
    ]


def run_ingestion_only(events: list[MarketDataEvent]) -> BenchmarkRow:
    started = perf_counter()
    checksum = 0
    for event in events:
        cpp_event = to_cpp_market_data_event(event)
        checksum ^= int(cpp_event.timestamp)
    elapsed = perf_counter() - started
    if checksum < 0:
        raise AssertionError("unreachable checksum guard")
    return BenchmarkRow.from_counts(
        mode="ingestion_only",
        event_count=len(events),
        wall_clock_seconds=elapsed,
    )


def run_lob_only(events: list[MarketDataEvent]) -> BenchmarkRow:
    started = perf_counter()
    engine = _cpp_engine(events, snapshot_interval_events=0)
    for event in events:
        engine.process_event(to_cpp_market_data_event(event))
    engine.finish()
    elapsed = perf_counter() - started
    return BenchmarkRow.from_counts(
        mode="lob_only",
        event_count=len(events),
        wall_clock_seconds=elapsed,
    )


def run_integrated_no_strategy(
    events: list[MarketDataEvent],
    *,
    snapshot_interval_events: int,
) -> BenchmarkRow:
    started = perf_counter()
    engine = _cpp_engine(events, snapshot_interval_events=snapshot_interval_events)
    for event in events:
        engine.process_event(to_cpp_market_data_event(event))
    engine.finish()
    elapsed = perf_counter() - started
    return BenchmarkRow.from_counts(
        mode="integrated_no_strategy",
        event_count=len(events),
        wall_clock_seconds=elapsed,
    )


def run_integrated_strategy(
    events: list[MarketDataEvent],
    *,
    snapshot_interval_events: int,
    market_callback_interval_events: int,
    orders_every: int,
    explain: bool,
) -> BenchmarkRow:
    strategy = BenchmarkStrategy(orders_every=orders_every)
    started = perf_counter()
    result = BacktestRunner(
        config={
            "instruments": [INSTRUMENT_ID],
            "publish_book_updates": False,
            "publish_trades": False,
            "snapshot_depth": 1,
            "snapshot_interval_events": snapshot_interval_events,
            "market_callback_interval_events": market_callback_interval_events,
        }
    ).run(
        strategy,
        events=events,
        mode="integrated",
        explain=explain,
        progress_interval_seconds=None,
        progress_interval_events=None,
    )
    elapsed = perf_counter() - started
    orders = sum(1 for record in result.order_log if record.event_type == "new_order")
    fills = len(result.fills)
    return BenchmarkRow.from_counts(
        mode="integrated_strategy",
        event_count=len(events),
        wall_clock_seconds=elapsed,
        callbacks=strategy.callbacks,
        market_callbacks=strategy.market_callbacks,
        orders=orders,
        fills=fills,
        trace_rows=len(result.trace),
    )


def run_benchmark(
    *,
    event_count: int,
    snapshot_interval_events: int,
    market_callback_interval_events: int,
    orders_every: int,
    modes: Iterable[str] = ALL_MODES,
    explain: bool = False,
) -> list[BenchmarkRow]:
    if event_count < 0:
        raise ValueError("event_count must be non-negative")
    if snapshot_interval_events < 0:
        raise ValueError("snapshot_interval_events must be non-negative")
    if orders_every < 0:
        raise ValueError("orders_every must be non-negative")
    if market_callback_interval_events < 0:
        raise ValueError("market_callback_interval_events must be non-negative")

    events = make_events(event_count)
    rows: list[BenchmarkRow] = []
    for mode in modes:
        if mode == "ingestion_only":
            rows.append(run_ingestion_only(events))
        elif mode == "lob_only":
            rows.append(run_lob_only(events))
        elif mode == "integrated_no_strategy":
            rows.append(
                run_integrated_no_strategy(
                    events,
                    snapshot_interval_events=snapshot_interval_events,
                )
            )
        elif mode == "integrated_strategy":
            rows.append(
                run_integrated_strategy(
                    events,
                    snapshot_interval_events=snapshot_interval_events,
                    market_callback_interval_events=market_callback_interval_events,
                    orders_every=orders_every,
                    explain=explain,
                )
            )
        else:
            raise ValueError(f"unknown benchmark mode: {mode}")
    return rows


def csv_header() -> str:
    return (
        "mode,event_count,wall_clock_seconds,events_per_second,callbacks,"
        "callbacks_per_second,market_callbacks,orders,orders_per_second,"
        "fills,fills_per_second,trace_rows"
    )


def _cpp_engine(events: list[MarketDataEvent], *, snapshot_interval_events: int):
    cpp = require_cpp()
    config = cpp.IntegratedBacktestConfig()
    config.instruments = sorted({event.instrument_id for event in events}) or [
        INSTRUMENT_ID
    ]
    config.publish_book_updates = False
    config.publish_trades = False
    config.snapshot_depth = 1
    config.snapshot_interval_events = snapshot_interval_events
    return cpp.IntegratedBacktestEngine(config)


def _rate(count: int, seconds: float) -> float:
    return float(count) / seconds if seconds > 0.0 else 0.0


def _parse_modes(text: str) -> list[str]:
    modes = [part.strip() for part in text.split(",") if part.strip()]
    if not modes:
        raise argparse.ArgumentTypeError("at least one mode is required")
    unknown = [mode for mode in modes if mode not in ALL_MODES]
    if unknown:
        raise argparse.ArgumentTypeError("unknown mode(s): " + ", ".join(unknown))
    return modes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure integrated replay overhead and callback throttling."
    )
    parser.add_argument("--events", type=int, default=100_000)
    parser.add_argument("--snapshot-interval", type=int, default=1)
    parser.add_argument("--callback-interval", type=int, default=1)
    parser.add_argument(
        "--orders-every",
        type=int,
        default=0,
        help="Submit one order every N market callbacks; 0 disables orders.",
    )
    parser.add_argument(
        "--modes",
        type=_parse_modes,
        default=list(ALL_MODES),
        help="Comma-separated modes: " + ",".join(ALL_MODES),
    )
    parser.add_argument(
        "--explain",
        action="store_true",
        help="Enable trace accumulation for integrated_strategy.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    rows = run_benchmark(
        event_count=args.events,
        snapshot_interval_events=args.snapshot_interval,
        market_callback_interval_events=args.callback_interval,
        orders_every=args.orders_every,
        modes=args.modes,
        explain=args.explain,
    )
    print(csv_header())
    for row in rows:
        print(row.csv_row())


if __name__ == "__main__":
    main()
