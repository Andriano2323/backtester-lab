# Python Strategy API

This document describes the M3 / Group 4 Python Strategy API. The API lets strategy authors write Python strategies against normalized market data callbacks, send orders through a strategy context, inspect pandas result objects, and use progress/risk helpers during synthetic backtests.

The implementation lives in:

```text
python/backtester/
bindings/python/backtester_module.cpp
examples/python/mean_reversion_strategy.py
notebooks/mean_reversion_example.ipynb
```

## Purpose

Group 4 provides the Python user-facing layer on top of the C++ backtester protocols:

- Group 2 market data messages are exposed as Python dataclasses and optional C++ bindings.
- Group 1 order gateway messages and runtime components are wrapped for Python callbacks.
- `BacktestRunner` currently supports deterministic synthetic Python feeds and a simple JSONL fixture loader.
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
| `BacktestRunner` | Replays Python market data events or JSONL fixtures into a strategy. |
| `BacktestResult` | Stores result records and exposes pandas outputs. |
| `ProgressMetrics` | Progress snapshot sent to `on_progress` and optional callbacks. |
| `RiskLimits` | Minimal Python risk-limit configuration enforced before order submission. |

Top-level convenience API:

```python
import backtester as backtest

result = backtest.run(strategy, data_path=None, date_range=None, events=events)
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

## Result Object

`BacktestResult` stores raw records and exposes pandas objects:

```python
result.pnl_series      # pandas.Series indexed by timestamp_ns
result.fills_df        # pandas.DataFrame
result.order_log_df    # pandas.DataFrame
result.metrics_df      # pandas.DataFrame
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

## Current Limitations

- The JSONL Python feed loader is a simple fixture loader.
- Full Databento L3 -> LOB -> Python strategy integration will be connected later.
- Python is not intended for hot-path matching logic.
- The current Python runner uses a simple synthetic server behavior for accepts and optional fills.
- No live trading, network FIX protocol, persistent order journal, or production execution gateway is implemented.
