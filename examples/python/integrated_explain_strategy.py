"""Deterministic integrated-mode explainability example."""

from __future__ import annotations

from pathlib import Path
import math
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
for path in (REPO_ROOT / "build" / "python", REPO_ROOT / "python"):
    if path.exists() and str(path) not in sys.path:
        sys.path.insert(0, str(path))

import backtester as backtest  # noqa: E402
from backtester import BacktestResult, Strategy  # noqa: E402
from backtester.types import Side  # noqa: E402


DATA_PATH = REPO_ROOT / "examples" / "data" / "integrated_explain.ndjson"

TRACE_SUMMARY_COLUMNS = [
    "timestamp_ns",
    "explain_stage",
    "stage",
    "event_type",
    "trading_engine_id",
    "order_id",
    "instrument_id",
    "side",
    "price",
    "size",
    "fill_price",
    "fill_size",
    "remaining_size",
    "reason",
]


class IntegratedExplainStrategy(Strategy):
    """Buy the first visible ask so the trace contains a complete order lifecycle."""

    def __init__(self, order_size: int = 2) -> None:
        self.order_size = order_size
        self.sent_order_id: int | None = None
        self.acks = []
        self.fills = []
        self.rejects = []
        self.snapshots = []

    def on_book_snapshot(self, snapshot, ctx) -> None:
        self.snapshots.append(snapshot)
        if self.sent_order_id is not None or not snapshot.asks:
            return
        self.sent_order_id = ctx.send_order(
            snapshot.instrument_id,
            Side.BID,
            snapshot.asks[0].price,
            self.order_size,
            snapshot.timestamp_ns,
        )

    def on_ack(self, ack, ctx) -> None:
        self.acks.append(ack)

    def on_fill(self, fill, ctx) -> None:
        self.fills.append(fill)

    def on_reject(self, reject, ctx) -> None:
        self.rejects.append(reject)


def run_example(
    *,
    explain: bool = True,
    data_path: Path | str = DATA_PATH,
) -> BacktestResult:
    """Run the integrated example on the deterministic fixture."""

    return backtest.run(
        IntegratedExplainStrategy(),
        data_path=data_path,
        mode="integrated",
        explain=explain,
        config={
            "instruments": [10],
            "publish_book_updates": False,
            "publish_trades": False,
            "snapshot_depth": 1,
            "snapshot_interval_events": 1,
        },
    )


def trace_summary(result: BacktestResult) -> list[dict[str, Any]]:
    """Return a stable, compact trace view for docs, notebooks, and tests."""

    if result.trace_df.empty:
        return []
    rows: list[dict[str, Any]] = []
    for raw in result.trace_df[TRACE_SUMMARY_COLUMNS].to_dict("records"):
        rows.append({key: _stable_value(value) for key, value in raw.items()})
    return rows


def smoke_summary() -> dict[str, Any]:
    """Small CI-friendly summary used by the smoke test and command-line run."""

    result = run_example(explain=True)
    return {
        "fills": len(result.fills_df),
        "orders": len(result.order_log_df),
        "positions": len(result.positions_df),
        "trace_rows": len(result.trace_df),
        "trace_stages": sorted(set(result.trace_df["explain_stage"].tolist())),
    }


def _stable_value(value: Any) -> Any:
    if value is None:
        return None
    if isinstance(value, float) and math.isnan(value):
        return None
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if hasattr(value, "item"):
        return _stable_value(value.item())
    return value


def main() -> None:
    result = run_example(explain=True)
    print("fills_df")
    print(result.fills_df)
    print("\norder_log_df")
    print(result.order_log_df)
    print("\ntrace_summary")
    for row in trace_summary(result):
        print(row)


if __name__ == "__main__":
    main()
