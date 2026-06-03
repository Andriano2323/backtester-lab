# Changelog

## v0.1.0-integrated-mvp - 2026-06-03

Integrated backtest MVP release candidate.

### Added

- C++ integrated replay loop over historical L3 market events.
- Market data adapter from `MarketDataEvent` to `HistoricalLobStore` and publisher messages.
- Order execution bridge between `OrderGatewayServer` and `FillSimulator`.
- Python `mode="integrated"` runner, `run_many`, multi-engine private views, and result filtering helpers.
- Explainability trace, deterministic example dataset, example strategy, notebook, and docs.
- JSONL and optional Feather integrated input paths.
- Integrated performance benchmark with callback and snapshot throttling checks.

### Verified

- `cmake --build build -j`
- `ctest --test-dir build --output-on-failure`: 21/21 passed.
- `PYTHONPATH=python:build/python .venv/bin/python -m pytest -q tests/python`: 192 passed, 2 skipped.
- Example strategy and benchmark smoke run successfully.

### Known limitations

- This is a research/backtesting MVP, not a live-trading or production execution system.
- Matching is intentionally simplified; exchange-grade queue priority and full matching semantics are out of scope.
- The latency model is deterministic and basic.
- Risk, slippage, fees, and portfolio accounting are intentionally minimal.
- Throughput baselines are hardware- and build-dependent and should be re-measured on target datasets.
