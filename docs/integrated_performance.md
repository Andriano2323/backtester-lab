# Integrated Performance And Regression Safety

Integrated mode has an explicit benchmark so research features do not hide
replay regressions.

## Benchmark Command

```bash
PYTHONPATH=python:build/python \
python benchmarks/integrated_benchmark.py \
  --events 100000 \
  --snapshot-interval 100 \
  --callback-interval 1 \
  --orders-every 0
```

Smoke-sized run:

```bash
PYTHONPATH=python:build/python \
python benchmarks/integrated_benchmark.py --events 1000 --snapshot-interval 10
```

## Modes

The benchmark reports one CSV row per mode:

- `ingestion_only`: canonical `MarketDataEvent` to C++ event conversion;
- `lob_only`: C++ integrated engine mutates `HistoricalLobStore` without
  subscribers;
- `integrated_no_strategy`: C++ integrated engine runs with no Python strategy
  callbacks;
- `integrated_strategy`: Python `BacktestRunner(mode="integrated")` with a
  counting strategy.

The output columns are:

```text
mode
event_count
wall_clock_seconds
events_per_second
callbacks
callbacks_per_second
market_callbacks
orders
orders_per_second
fills
fills_per_second
trace_rows
```

Absolute numbers depend on hardware, Python version, build type, and loaded
extension. CI tests assert invariants, not fixed throughput thresholds.

## Release Baseline

Measured locally on 2026-06-03 with:

```bash
.venv/bin/python benchmarks/integrated_benchmark.py \
  --events 100000 \
  --snapshot-interval 10 \
  --callback-interval 10 \
  --orders-every 100
```

```text
mode,event_count,wall_clock_seconds,events_per_second,callbacks,callbacks_per_second,market_callbacks,orders,orders_per_second,fills,fills_per_second,trace_rows
ingestion_only,100000,0.799549388,125070.447806,0,0.000000,0,0,0.000000,0,0.000000,0
lob_only,100000,1.144810596,87350.693948,0,0.000000,0,0,0.000000,0,0.000000,0
integrated_no_strategy,100000,1.156489755,86468.556741,0,0.000000,0,0,0.000000,0,0.000000,0
integrated_strategy,100000,2.444739083,40904.160569,1022,418.040521,1000,10,4.090416,10,4.090416,0
```

## Throttling

Use these integrated config knobs to control Python callback pressure:

```python
config = {
    "publish_book_updates": False,
    "publish_trades": False,
    "snapshot_interval_events": 100,
    "market_callback_interval_events": 10,
}
```

`snapshot_interval_events` reduces how often snapshots are published. It is the
preferred throttle because it avoids producing unnecessary subscriber messages.

`market_callback_interval_events` is a Python-side market callback throttle. It
still lets the runner update internal market views and result state, but the
strategy sees only every Nth market message. `0` disables market callbacks.

Use `explain=False` for long performance runs so `trace_df` rows do not
accumulate:

```python
result = backtest.run(strategy, events=events, mode="integrated", explain=False)
```

## Regression Safety

The Python smoke tests cover:

- benchmark CLI completion on a tiny dataset;
- positive ingestion-only throughput;
- no Python callbacks in `integrated_no_strategy`;
- lower market callback counts with snapshot throttling;
- lower market callback counts with callback throttling;
- bounded Python allocations for a no-strategy replay;
- zero trace rows on long integrated runs when tracing is disabled.
