# Python Strategy API

This document describes the M3 / Group 4 Python Strategy API. The API lets strategy authors write Python strategies against normalized market data callbacks, send orders through a strategy context, inspect pandas result objects, and use progress/risk helpers during synthetic backtests.

The implementation lives in:

```text
python/backtester/
bindings/python/backtester_module.cpp
examples/python/mean_reversion_strategy.py
examples/python/integrated_explain_strategy.py
notebooks/mean_reversion_example.ipynb
notebooks/integrated_explainability_walkthrough.ipynb
```

## Purpose

Group 4 provides the Python user-facing layer on top of the C++ backtester protocols:

- Group 2 market data messages are exposed as Python dataclasses and optional C++ bindings.
- Group 1 order gateway messages and runtime components are wrapped for Python callbacks.
- `BacktestRunner` supports deterministic synthetic Python feeds, a simple JSONL fixture loader,
  and a tiny integrated mode that drives strategy market callbacks from the C++ L3 event loop.
- Strategy authors interact with `StrategyContext`, not raw pybind11 objects.

The Python API is intended for strategy logic, research, testing, examples, and reporting. Hot-path matching, LOB mutation, and transport primitives remain C++ responsibilities.

## Installation / Dev Setup

Create and activate a virtual environment:

```bash
python3.12 -m venv .venv
source .venv/bin/activate
```

Install Python development dependencies:

```bash
pip install pytest pandas numpy matplotlib pybind11
```

Configure CMake with Python bindings enabled:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBACKTESTER_BUILD_PYTHON=ON \
  -DPython3_EXECUTABLE="$(which python)"

cmake --build build -j
```

Optional Arrow/Feather integrated input build:

```bash
pip install pyarrow

cmake -S . -B build-arrow -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_ARROW=ON \
  -DBACKTESTER_BUILD_PYTHON=ON \
  -DPython3_EXECUTABLE="$(which python)"

cmake --build build-arrow -j
PYTHONPATH=python:build-arrow/python pytest -q tests/python
```

Run Python tests with both pure Python package code and the compiled extension visible:

```bash
PYTHONPATH=python:build/python pytest -q tests/python
```

## Package Overview

Core user-facing objects:

| Object | Purpose |
| --- | --- |
| `Strategy` | Base class for strategy callbacks. |
| `StrategyContext` | Runtime services exposed to strategies, including order submission, position/PnL, and metrics. |
| `BacktestRunner` | Replays Python market data events, JSONL fixtures, or integrated C++ L3 events into a strategy. |
| `BacktestResult` | Stores result records and exposes pandas outputs. |
| `ProgressMetrics` | Progress snapshot sent to `on_progress` and optional callbacks. |
| `RiskLimits` | Minimal Python risk-limit configuration enforced before order submission. |

Top-level convenience API:

```python
import backtester as backtest

result = backtest.run(strategy, data_path=None, date_range=None, events=events)
```

Integrated explainability runs can make tracing explicit:

```python
result = backtest.run(
    strategy,
    data_path="examples/data/integrated_explain.ndjson",
    mode="integrated",
    explain=True,
    config={"instruments": [10], "snapshot_interval_events": 1},
)
```

## Strategy Callback Lifecycle

Subclass `Strategy` and override whichever callbacks you need:

```python
from backtester import Strategy


class MyStrategy(Strategy):
    def on_start(self, ctx):
        pass

    def on_book_update(self, update, ctx):
        pass

    def on_book_snapshot(self, snapshot, ctx):
        pass

    def on_trade(self, trade, ctx):
        pass

    def on_ack(self, ack, ctx):
        pass

    def on_fill(self, fill, ctx):
        pass

    def on_reject(self, reject, ctx):
        pass

    def on_progress(self, metrics, ctx):
        pass

    def on_finish(self, result, ctx):
        pass
```

Runner order:

```text
on_start(ctx)
market-data callbacks in timestamp order
order event callbacks after each market event
on_progress(metrics, ctx) at configured intervals and at final progress
on_finish(result, ctx)
```

## Example Strategy

```python
from collections import deque

import backtester as backtest
from backtester import Strategy
from backtester.types import PRICE_SCALE, Side


class MeanReversionStrategy(Strategy):
    def __init__(self, window=3, threshold=PRICE_SCALE // 2):
        self.prices = deque(maxlen=window)
        self.threshold = threshold

    def on_trade(self, trade, ctx):
        if len(self.prices) < self.prices.maxlen:
            self.prices.append(trade.price)
            return

        average = sum(self.prices) // len(self.prices)
        self.prices.append(trade.price)

        if trade.price <= average - self.threshold:
            ctx.send_order(trade.instrument_id, Side.BID, trade.price, 1, trade.timestamp_ns)
        elif trade.price >= average + self.threshold and ctx.current_position(trade.instrument_id) > 0:
            ctx.send_order(trade.instrument_id, Side.ASK, trade.price, 1, trade.timestamp_ns)
```

The working script version is in:

```bash
python examples/python/mean_reversion_strategy.py
```

Notebook deliverable:

```text
notebooks/mean_reversion_example.ipynb
```

Integrated explainability example:

```bash
PYTHONPATH=python:build/python python examples/python/integrated_explain_strategy.py
```

Notebook walkthrough:

```text
notebooks/integrated_explainability_walkthrough.ipynb
```

## Running A Backtest

Use in-memory Python events:

```python
import backtester as backtest
from backtester.types import BookUpdate, Side

events = [
    BookUpdate(
        instrument_id=10,
        timestamp_ns=1,
        seq_no=1,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )
]

result = backtest.run(strategy, events=events)
```

Use the JSONL fixture loader through `data_path` and `date_range`:

```python
result = backtest.run(
    strategy,
    data_path="tests/fixtures/python_feed/sample_feed.jsonl",
    date_range=(100, 300),  # start inclusive, end exclusive, timestamp_ns
)
```

Directory input is also supported. The loader reads all `.jsonl` files in the directory and sorts events by `timestamp_ns`.

Supported JSONL records:

```json
{"type":"book_update","instrument_id":10,"timestamp_ns":100,"seq_no":1,"side":"BID","price":101250000000,"size":5}
{"type":"book_snapshot","instrument_id":10,"timestamp_ns":200,"seq_no":2,"bids":[{"level_index":0,"price":101000000000,"size":3}],"asks":[{"level_index":0,"price":102000000000,"size":4}]}
{"type":"trade","instrument_id":10,"timestamp_ns":300,"seq_no":3,"price":101500000000,"size":2,"aggressor_side":"ASK"}
```

Use integrated mode for a tiny C++ L3 vertical slice:

```python
import backtester as backtest
from backtester import Action, MarketDataEvent, Strategy
from backtester.types import Side


class BuyAtAsk(Strategy):
    def on_book_snapshot(self, snapshot, ctx):
        ctx.send_order(snapshot.instrument_id, Side.BID, snapshot.asks[0].price, 1, snapshot.timestamp_ns)


events = [
    MarketDataEvent(
        timestamp=1,
        ts_recv=1,
        ts_event=1,
        order_id=9001,
        side=Side.ASK,
        price=102_000_000_000,
        size=4,
        action=Action.ADD,
        instrument_id=10,
    )
]

result = backtest.run(
    BuyAtAsk(),
    events=events,
    mode="integrated",
    config={
        "instruments": [10],
        "publish_book_updates": False,
        "snapshot_interval_events": 1,
    },
)
```

Integrated mode accepts explicit `MarketDataEvent` objects and Databento-style
JSONL/NDJSON through `data_path`:

```python
result = backtest.run(
    strategy,
    data_path="tests/fixtures/integrated_l3/tiny_real.ndjson",
    date_range=(None, None),
    mode="integrated",
    config={
        "input_mode": "standard",  # standard for one file; flat/hierarchy for folders
        "instruments": [10],
        "publish_book_updates": False,
        "snapshot_interval_events": 1,
    },
)
```

Feather input uses the same canonical `MarketDataEvent` path when the extension
is built with `ENABLE_ARROW=ON`:

```python
result = backtest.run(
    strategy,
    data_path="data_feather/XEUR-20260409-HTT6HHLT6R",
    mode="integrated",
    config={
        "input_mode": "flat",
        "input_format": "feather",
        "instruments": [12345],
        "snapshot_interval_events": 1000,
    },
)
```

For folder input, `input_mode="flat"` and `input_mode="hierarchy"` both preserve
the global `(timestamp, source_file_id, source_sequence)` ordering contract for
the Python integrated runner. With no market-data publish options enabled,
integrated mode mutates the C++ LOB and records metrics without calling Python
for every L3 event.

Integrated execution defaults to the optimistic `immediate` model, where an
accepted order can fill against the current post-event book. To test activation
delay, configure `latency_ns` or set `execution_model="latency"` explicitly:

```python
result = backtest.run(
    strategy,
    data_path="tests/fixtures/integrated_l3/tiny_real.ndjson",
    mode="integrated",
    config={
        "execution_model": "latency",
        "latency_ns": 50_000,
        "instruments": [10],
        "snapshot_interval_events": 1,
    },
)
```

In latency mode, accepted orders become fill-eligible only at
`order_timestamp_ns + latency_ns`. `result.trace_df` includes
`activation_time_ns` and an `activation_pending` row when an order is not active
yet.

Explainability tracing is enabled by default and can be requested explicitly with
`explain=True`. Use `explain=False` to skip trace accumulation while preserving
fills, order logs, positions, PnL, and metrics. The research-facing trace column
is `explain_stage`, which groups raw runtime stages into:

```text
market
strategy_order
ack
reject
fill
portfolio_update
```

For long runs, use snapshot and callback throttles to control Python callback
pressure:

```python
config = {
    "publish_book_updates": False,
    "publish_trades": False,
    "snapshot_interval_events": 100,
    "market_callback_interval_events": 10,
}
```

`snapshot_interval_events` reduces published snapshots. `market_callback_interval_events`
lets the Python strategy see only every Nth market message; `0` disables market
callbacks while the runner still replays the LOB.

Multi-strategy integrated runs use `run_many`. The historical LOB is shared, but
each strategy receives its own engine id, order gateway, portfolio, and private
consumed-liquidity view:

```python
result = backtest.run_many(
    {
        "alpha": AlphaStrategy(),
        "beta": BetaStrategy(),
    },
    data_path="tests/fixtures/integrated_l3/tiny_real.ndjson",
    mode="integrated",
    config={
        "strategy_engine_ids": {
            "alpha": 11,
            "beta": 22,
        },
        "instruments": [10],
        "snapshot_interval_events": 1,
    },
)
```

Order ids are scoped by `(trading_engine_id, order_id)`, so both strategies can
have `order_id == 1` without colliding. Result tables include `strategy_name`
where strategy-level grouping is meaningful:

```python
alpha_result = result.for_strategy("alpha")
engine_22_result = result.for_engine(22)
strategy_results = result.by_strategy()
engine_results = result.by_engine()
```

## Result Object

`BacktestResult` stores raw records and exposes pandas objects:

```python
result.pnl_series      # pandas.Series indexed by timestamp_ns
result.pnl_df          # pandas.DataFrame with engine/strategy grouping columns
result.fills_df        # pandas.DataFrame
result.order_log_df    # pandas.DataFrame
result.metrics_df      # pandas.DataFrame
result.positions_df    # pandas.DataFrame
result.trace_df        # pandas.DataFrame
```

Stable result columns are tested in `tests/python/test_result.py`.

Chart helpers:

```python
from backtester.charts import plot_cumulative_pnl, plot_fills_on_price

fig1 = plot_cumulative_pnl(result)
fig2 = plot_fills_on_price(result, price_series)
```

The helpers return `matplotlib.figure.Figure` and do not call `plt.show()`.

## Progress Callback

`BacktestRunner.run` and `backtest.run` support progress callbacks:

```python
def on_progress(metrics):
    print(metrics.progress_percent, metrics.last_timestamp_ns, metrics.current_pnl)


result = backtest.run(
    strategy,
    events=events,
    progress_callback=on_progress,
    progress_interval_seconds=30.0,
    progress_interval_events=None,
)
```

For deterministic tests, use `progress_interval_events`:

```python
result = backtest.run(
    strategy,
    events=events,
    progress_callback=on_progress,
    progress_interval_events=1000,
)
```

`ProgressMetrics` includes:

- `processed_events`
- `total_events`
- `progress_percent`
- `last_timestamp_ns`
- `current_pnl`
- `orders_sent`
- `orders_cancelled`
- `orders_filled`
- `orders_rejected`
- `per_instrument_order_stats`

The runner emits a final progress snapshot at the end of every run.

## Risk Limits

`RiskLimits` are enforced by `StrategyContext` before an order reaches the gateway:

```python
from backtester import RiskLimits

limits = RiskLimits(
    max_position_per_instrument=10,
    max_order_size=5,
    allow_short=False,
)

result = backtest.run(strategy, events=events, risk_limits=limits)
```

Supported checks:

- `max_order_size`
- `max_position_per_instrument`
- `allow_short=False`

Risk rejects:

- raise `RiskLimitExceeded`
- are recorded in `result.order_log_df` with `status = "Rejected"` and `event_type = "risk_reject"`

## Python Bindings And C++ Protocols

The pure Python package can be imported without compiled bindings. The optional `_backtester_cpp` extension exposes Group 1 and Group 2 protocol/runtime components:

- Group 2: `MarketDataPublisher`, subscriber handles, market data message types.
- Group 1: `OrderChannel`, `OrderGatewayClient`, `OrderGatewayServer`, order events.

Python-facing adapters convert C++ pybind objects into pure Python dataclasses before strategy callbacks. `CppOrderGatewayFacade` hides raw `_backtester_cpp.OrderGatewayClient` from `StrategyContext`.

## Benchmarks

Python callback overhead:

```bash
PYTHONPATH=python:build/python \
python benchmarks/python_strategy_callback_benchmark.py --events 100000
```

Output:

```text
events,wall_clock_seconds,callbacks_per_second
100000,0.162590966,615040.321491
```

Numbers depend on the machine and build.

Integrated replay overhead:

```bash
PYTHONPATH=python:build/python \
python benchmarks/integrated_benchmark.py \
  --events 100000 \
  --snapshot-interval 100 \
  --callback-interval 1 \
  --orders-every 0
```

The integrated benchmark reports `events_per_second`, `callbacks_per_second`,
`orders_per_second`, `fills_per_second`, and `trace_rows` for
`ingestion_only`, `lob_only`, `integrated_no_strategy`, and
`integrated_strategy`.

## Current Limitations

- The JSONL Python feed loader is a simple fixture loader.
- Integrated mode supports canonical L3 JSONL fixtures and optional Feather input
  when the extension is built with Arrow.
- Python is not intended for hot-path matching logic.
- The current Python runner uses a simple synthetic server behavior for accepts and optional fills.
- No live trading, network FIX protocol, persistent order journal, or production execution gateway is implemented.
