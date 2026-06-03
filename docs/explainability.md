# Explainability UX

This guide describes the research-facing path for understanding an integrated
backtest run.

## Mental Model

Integrated mode is a deterministic event loop:

```text
historical L3 event
  -> shared HistoricalLobStore
  -> post-event market-data snapshot/update/trade
  -> Python strategy callback
  -> OrderGatewayServer validation
  -> fill simulation against the strategy-visible book
  -> portfolio/result/trace rows
```

The historical LOB is the canonical market state. Strategy-visible state may be
private: after a strategy consumes historical liquidity, that consumed liquidity
is hidden only from that strategy's engine view. Other engines still see the
shared historical book.

## Example Dataset

The deterministic explainability fixture is:

```text
examples/data/integrated_explain.ndjson
```

It contains two L3 events for instrument `10`:

- timestamp `100`: add ask `102000000000 x 4`;
- timestamp `200`: add bid `101000000000 x 3`.

The example strategy buys `2` at the first visible ask. This creates a compact
trace with market, strategy order, ack, fill, and portfolio update rows.

Run it with:

```bash
PYTHONPATH=python:build/python python examples/python/integrated_explain_strategy.py
```

The notebook walkthrough is:

```text
notebooks/integrated_explainability_walkthrough.ipynb
```

The notebook delegates the executable logic to the example script, so CI can
smoke-test the same code path without executing a notebook kernel.

## Python API

Tracing is enabled by default for compatibility with the current research API.
Use `explain=True` to make the intent explicit:

```python
result = backtest.run(
    strategy,
    data_path="examples/data/integrated_explain.ndjson",
    mode="integrated",
    explain=True,
    config={
        "instruments": [10],
        "snapshot_interval_events": 1,
        "snapshot_depth": 1,
    },
)
```

Use `explain=False` to keep fills, order logs, positions, PnL, and metrics while
skipping trace accumulation.

## Event Lifecycle

For each accepted historical event:

1. Ingestion filters choose whether the event is replayed.
2. The event mutates `HistoricalLobStore` if its action is book-mutating.
3. The integrated engine publishes configured market-data messages.
4. The strategy receives callbacks from the post-event book state.
5. Any strategy orders are flushed to the gateway.
6. Gateway events and fill simulation are drained until quiescent.
7. Result rows and trace rows are appended deterministically.

Equal timestamps remain deterministic through ingestion ordering and trace
`sequence`.

## Order Lifecycle

Accepted new orders produce:

```text
order_request -> order_ack -> fill_simulation/order_fill -> portfolio_update
```

Rejected orders produce:

```text
order_request -> order_reject
```

The result tables preserve raw order details:

- `order_log_df`: new order, ack, reject, cancel, modify, fill events;
- `fills_df`: executed fills;
- `positions_df`: portfolio snapshots after fills;
- `trace_df`: explainability rows.

## Fill Model

`immediate` execution lets an accepted order fill against the current post-event
book. `latency` execution delays fill eligibility until:

```text
activation_time_ns = order_timestamp_ns + latency_ns
```

The fill model is fill-at-touch:

- buy limit `>= best ask` fills at best ask;
- sell limit `<= best bid` fills at best bid;
- non-crossing orders remain resting for later market states;
- private consumed liquidity is tracked per engine.

## Trace Columns

`trace_df` keeps raw runtime stages and a compact UX stage:

- `stage`: exact internal source such as `market_publish`, `order_request`,
  `order_ack`, `order_fill`, or `portfolio_update`;
- `explain_stage`: stable mental-model category such as `market`,
  `strategy_order`, `ack`, `reject`, `fill`, or `portfolio_update`;
- `reason`: why the row happened, for example `immediate`, `NewAccepted`,
  `crossed_best_ask`, or `activation_pending`.

For compact regression comparisons, use
`examples/python/integrated_explain_strategy.py::trace_summary`.
