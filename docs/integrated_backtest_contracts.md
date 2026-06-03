# Integrated Backtest Contracts

This document fixes the first integration contract for the end-to-end backtest
runtime. It does not replace the existing domain, order, market-data, LOB, or
Python Strategy API documents. It defines how those existing components will be
composed by the integrated runner.

The MVP goal is a deterministic single-engine loop:

```text
MarketDataEvent
  -> HistoricalLobStore
  -> MarketDataPublisher
  -> Strategy callback
  -> OrderGatewayClient
  -> OrderGatewayServer
  -> FillSimulator + EngineView
  -> OrderAck / OrderReject / OrderFill
  -> portfolio / result / trace
```

## Scope

The integrated runtime is intentionally conservative:

- one Python strategy through `run()`, or multiple named strategies through
  `run_many()`;
- one `TradingEngineId` per strategy;
- one `OrderGatewayClient` per strategy;
- one shared `OrderGatewayServer`;
- one private engine view per strategy;
- one shared `HistoricalLobStore`;
- single-thread deterministic execution;
- explicit `immediate` and `latency` execution models.

Existing standalone protocols remain valid. `OrderGatewayServer::drainRequests()`
and `OrderGatewayServer::emitFill()` keep their current semantics for existing
tests, benchmarks, and users.

## Market Event Lifecycle

Input events must already be in the global ingestion order:

```text
(timestamp, source_file_id, source_sequence)
```

For every accepted historical event:

1. Apply configured filters: instrument filter, timestamp/date range, and
   `max_events`.
2. Record a trace row for the raw `MarketDataEvent`.
3. Apply the event to `HistoricalLobStore`.
4. Build zero or more market-data messages from the post-event state.
5. Publish messages through `MarketDataPublisher`.
6. Flush the publisher.
7. Drain subscriber queues synchronously, invoking strategy callbacks.
8. Flush strategy order clients.
9. Drain gateway request transitions.
10. Run order execution bridge logic for accepted transitions.
11. Flush gateway events.
12. Drain client event queues, invoking strategy order callbacks.
13. Update portfolio, PnL, metrics, and trace.

The strategy always sees the post-event book state. In MVP immediate mode, an
order sent from that callback can fill against the same post-event visible book.

### MarketDataEvent To MarketDataMessage Rules

`MarketDataEvent` remains the canonical ingestion event. Market-data messages
remain the existing variant:

```cpp
using MarketDataMessage = std::variant<BookUpdate, BookSnapshot, Trade>;
```

MVP conversion rules:

- `Action::Add`, `Action::Modify`, `Action::Cancel`, and `Action::Clear`
  mutate `HistoricalLobStore`.
- If `publish_book_updates` is enabled, a mutating event may publish a
  `BookUpdate` with the event instrument, timestamp, side, price, and size.
- If snapshots are enabled by interval or explicit config, publish a
  `BookSnapshot` created from `HistoricalLobStore::snapshot(instrument_id,
  depth)`.
- A clear for one instrument may publish an empty snapshot for that instrument.
- A global clear with `instrument_id == 0` clears the store and does not publish
  a per-instrument message unless an explicit affected-instrument policy is
  added later.
- `Action::Trade` and `Action::Fill` do not mutate `HistoricalLobStore` under
  the current reconstruction semantics. If `publish_trades` is enabled, they
  publish `Trade` with event instrument, timestamp, price, size, and side as the
  current best available aggressor-side field.
- `Action::None` publishes nothing.

`MarketDataPublisher` assigns `seq_no`; adapters must not preserve source
sequence as feed sequence.

## Order Lifecycle

The integrated runtime uses the existing order protocol:

```cpp
using OrderRequest = std::variant<NewOrder, CancelOrder, ModifyOrder>;
using OrderEvent = std::variant<OrderAck, OrderFill, OrderReject>;
```

Strategy code sends orders through `StrategyContext`, which delegates to
`OrderGatewayClient`. The client writes `OrderRequest` into its `OrderChannel`.
The integrated engine drains those requests after all market-data callbacks for
the current historical event have returned.

### Gateway Transition Handoff

The integrated engine needs accepted gateway requests in order to run
`FillSimulator`. The gateway must expose processed request transitions from the
same validation path that currently emits `OrderAck` and `OrderReject`.

MVP contract:

```cpp
enum class OrderGatewayTransitionType {
    NewAccepted,
    ModifyAccepted,
    CancelAccepted,
    Rejected
};

struct OrderGatewayTransition {
    OrderGatewayTransitionType type;
    OrderMessageType request_type;
    OrderFields fields;
    OrderSnapshot snapshot;
    OrderRejectReason reject_reason;
    std::string reject_text;
};
```

The exact API can be one of these, as long as it preserves current behavior:

```cpp
std::vector<OrderGatewayTransition> drainRequestTransitions();
```

or:

```cpp
std::size_t drainRequests(std::vector<OrderGatewayTransition>& out);
```

The integrated engine should prefer a transition-returning API. Existing callers
can keep using `drainRequests()` when they only need the count.

Required transition semantics:

- A transition is emitted only after gateway validation has completed.
- Accepted transitions are emitted after the server state has been updated and
  after the matching ack has been queued.
- Rejected transitions are emitted after the reject event has been queued.
- The bridge must not duplicate gateway validation.
- Event queue ordering for an accepted crossing order is `OrderAck` first, then
  `OrderFill`, because the ack is queued during gateway drain and fills are
  queued later by the bridge.

### NewOrder

For `NewAccepted`:

1. Record `new_order` and `ack` result rows.
2. Submit the accepted order to `FillSimulator`.
3. Emit one `OrderFill` through `OrderGatewayServer::emitFill()` for each
   simulated fill.
4. Store mapping for any remaining resting synthetic order.
5. Record trace rows for fill or rest reason.

If the order is fully filled, no resting mapping is kept.

### CancelOrder

For `CancelAccepted`:

1. Look up `(trading_engine_id, gateway_order_id)` in the execution mapping.
2. If a synthetic order exists, call `EngineView::cancelSyntheticOrder()`.
3. Remove the mapping.
4. Record `cancel_order`, `ack`, and trace rows.

If no mapping exists, the gateway ack is still valid. This can happen for a
previously fully filled order only if gateway allowed the cancel; normally a
terminal order should be rejected by gateway validation.

### ModifyOrder

For `ModifyAccepted`:

1. Look up and cancel the old synthetic order if one exists.
2. Submit the modified order to `FillSimulator`.
3. Emit fills through `OrderGatewayServer::emitFill()`.
4. Replace the mapping if remaining size rests.
5. Remove the mapping if the modified order fully fills.
6. Record `modify_order`, `ack`, and trace rows.

Rejected modify requests must not change `EngineView`.

### Rejected Requests

For `Rejected`:

1. Do not call `FillSimulator`.
2. Do not mutate `EngineView`.
3. Record the reject in `order_log_df` and `trace_df`.

Reject reasons remain the existing `OrderRejectReason` enum.

## Execution Policy

The integrated runner supports two explicit execution models.

The default model is:

```text
execution_model = immediate
```

Immediate means:

```text
historical event t is applied
strategy sees post-event book at t
strategy order is accepted/rejected at t
accepted order can fill against the same post-event book at t
```

This is intentionally optimistic and deterministic. It is the learning and
debugging mode.

The more realistic timing model is:

```text
execution_model = latency
activation_time_ns = order_timestamp_ns + latency_ns
```

Accepted orders are acknowledged at the submission timestamp, but they are not
eligible for fill simulation until the current market event timestamp is greater
than or equal to `activation_time_ns`. When `latency_ns > 0` and
`execution_model` is not explicitly configured, the Python integrated runner
uses `latency`; otherwise it uses `immediate`.

Trace rows for order requests, acknowledgements, fill simulation decisions, and
fills must include `activation_time_ns`. If an accepted order cannot be tested
against the current book because activation has not arrived yet, the trace
reason is `activation_pending`.

## FillSimulator Result Contract

`FillSimulator::submitLimitOrder()` currently returns only
`std::vector<SimulatedFill>`. The bridge needs the synthetic order id and
remaining quantity even when no fill occurs.

MVP result contract:

```cpp
struct SimulatedOrderResult {
    SyntheticOrderId synthetic_order_id;
    Quantity requested_size;
    Quantity filled_size;
    Quantity remaining_size;
    std::vector<SimulatedFill> fills;
};
```

Required semantics:

- One submitted order reserves exactly one `SyntheticOrderId`.
- All fills for that order use the same `SyntheticOrderId`.
- If `remaining_size > 0`, the remaining quantity is resting in `EngineView`.
- If `remaining_size == 0`, no synthetic resting order remains.
- Invalid simulated requests return `synthetic_order_id == 0`, no fills, and no
  resting quantity.

## Gateway To Synthetic Mapping

The bridge owns this mapping:

```text
(TradingEngineId, OrderId) -> SyntheticOrderId
```

Recommended stored value:

```cpp
struct ExecutionOrderMapping {
    TradingEngineId trading_engine_id;
    OrderId gateway_order_id;
    lob::SyntheticOrderId synthetic_order_id;
    InstrumentId instrument_id;
    Side side;
    Price price;
    Quantity remaining_size;
    TimestampNs last_timestamp_ns;
};
```

Mapping rules:

- Create or replace mapping only when a simulated order has
  `remaining_size > 0`.
- Erase mapping after full fill.
- Erase mapping after accepted cancel.
- Replace mapping after accepted modify with resting remainder.
- Keep mapping scoped by `(TradingEngineId, OrderId)`, not by `OrderId` alone.
- Do not map rejected orders.

## Result Contract

The integrated runner must preserve the existing Python result surface:

```python
result.pnl_series
result.pnl_df
result.fills_df
result.order_log_df
result.metrics_df
```

Minimum integrated result additions:

```python
result.trace_df
result.positions_df
```

Optional later additions:

```python
result.events_df
result.book_df
```

Stable `fills_df` columns remain:

```text
timestamp_ns
trading_engine_id
strategy_name
order_id
instrument_id
side
fill_price
fill_size
remaining_size
```

Stable `order_log_df` columns remain:

```text
timestamp_ns
trading_engine_id
strategy_name
order_id
instrument_id
side
price
size
status
event_type
reason
text
```

Integrated order event types:

```text
new_order
cancel_order
modify_order
ack
reject
fill
risk_reject
```

Stable `positions_df` columns:

```text
timestamp_ns
trading_engine_id
strategy_name
instrument_id
position
realized_pnl
unrealized_pnl
pnl
```

## Trace Contract

`trace_df` is the explainability surface. It should be present when
`explain=True` or `trace_enabled=True`; it may be empty when tracing is disabled.

Minimum stable columns:

```text
timestamp_ns
sequence
stage
explain_stage
event_type
trading_engine_id
strategy_name
order_id
synthetic_order_id
activation_time_ns
instrument_id
side
price
size
best_bid_before
best_ask_before
best_bid_after
best_ask_after
fill_price
fill_size
remaining_size
reason
text
```

`sequence` is a monotonic runtime sequence assigned by the integrated engine.
It disambiguates multiple rows with the same timestamp.

`explain_stage` is the research-facing lifecycle category derived from the raw
runtime stage. Stable categories are:

```text
market
strategy_order
ack
reject
fill
portfolio_update
```

Minimum trace stages:

```text
market_event
lob_update
market_publish
strategy_callback
order_request
order_ack
order_reject
fill_simulation
order_fill
portfolio_update
```

Minimum fill reasons:

```text
crossed_best_ask
crossed_best_bid
resting_no_fill
no_visible_liquidity
invalid_simulated_request
```

Gateway reject reasons should use the existing `OrderRejectReason` names.

## Required Scenarios

The first integrated test fixtures should cover these scenarios.

### Accepted New Order

Given a valid strategy `NewOrder`, when gateway drains requests, then:

- one `OrderAck` with `OrderAckType::NewAccepted` is queued;
- one `OrderGatewayTransitionType::NewAccepted` transition is returned;
- no reject is emitted;
- no fill is required unless the order crosses visible liquidity.

### Rejected New Order

Given invalid instrument, side, price, quantity, or duplicate order id, then:

- one `OrderReject` is queued;
- one rejected transition is returned;
- `FillSimulator` is not called;
- `EngineView` does not change;
- `order_log_df` and `trace_df` contain the reject reason.

### Filled Buy

Given historical ask `101 x 5`, when strategy sends buy limit `101 x 2`, then:

- gateway emits `NewAccepted`;
- fill simulator consumes `2` from visible ask liquidity for that engine;
- gateway emits `OrderFill` at `101 x 2`;
- no mapping remains if the order is fully filled;
- trace reason is `crossed_best_ask`.

### Filled Sell

Given historical bid `100 x 5`, when strategy sends sell limit `100 x 2`, then:

- gateway emits `NewAccepted`;
- fill simulator consumes `2` from visible bid liquidity for that engine;
- gateway emits `OrderFill` at `100 x 2`;
- trace reason is `crossed_best_bid`.

### Resting Order

Given historical ask `101 x 5`, when strategy sends buy limit `100 x 2`, then:

- gateway emits `NewAccepted`;
- no fill is emitted;
- `EngineView` contains a synthetic bid `100 x 2`;
- mapping contains `(engine_id, gateway_order_id) -> synthetic_order_id`;
- trace reason is `resting_no_fill`.

### Partial Fill Then Rest

Given historical ask `101 x 2`, when strategy sends buy limit `101 x 5`, then:

- one fill at `101 x 2` is emitted;
- remaining `3` rests in `EngineView`;
- mapping is kept for remaining size `3`;
- trace includes both fill and resting rows.

### Cancel Resting Order

Given a mapped resting order, when strategy sends valid cancel, then:

- gateway emits `CancelAccepted`;
- bridge calls `EngineView::cancelSyntheticOrder()`;
- mapping is erased;
- synthetic book no longer contains that order.

### Modify Resting Order

Given a mapped resting order, when strategy sends valid modify, then:

- gateway emits `ModifyAccepted`;
- bridge cancels old synthetic order;
- bridge submits modified order to fill simulator;
- mapping is replaced, erased, or filled according to the modified price and
  size.

### Multi-Engine Isolation

Given Engine A consumes historical ask liquidity, Engine B should still see the
full historical ask until it consumes liquidity in its own `EngineView`.

Multi-engine runtime uses the same contracts:

- `run_many()` accepts a mapping of `strategy_name -> Strategy`;
- `strategy_engine_ids` maps `strategy_name -> TradingEngineId`;
- if no mapping is supplied, engine ids are assigned deterministically from the
  runner's base `trading_engine_id`;
- each strategy has its own `OrderGatewayClient`, `StrategyContext`,
  `Portfolio`, and private consumed-liquidity view;
- all strategies receive the same shared historical market-data feed;
- order ids are scoped by `(trading_engine_id, order_id)`;
- a fill in one engine consumes historical liquidity only for that engine's
  private view;
- `fills_df`, `order_log_df`, `positions_df`, `pnl_df`, and `trace_df` include
  `strategy_name` for filtering and grouping.

## Review Checklist

Before writing integration code, verify:

- every lifecycle step above maps to an existing component or a named new
  integration component;
- market-data publishing uses existing `BookUpdate`, `BookSnapshot`, and
  `Trade`;
- order requests and events use existing `NewOrder`, `CancelOrder`,
  `ModifyOrder`, `OrderAck`, `OrderReject`, and `OrderFill`;
- validation remains owned by `OrderGatewayServer`;
- fill decisions remain owned by `FillSimulator`;
- private consumed liquidity remains owned by `EngineView`;
- canonical historical state remains owned by `HistoricalLobStore`;
- multi-engine runs keep one shared historical LOB and private strategy views;
- sequence numbers for feed messages are assigned by `MarketDataPublisher`;
- gateway order ids are never confused with LOB historical order ids;
- gateway order ids are never confused with `SyntheticOrderId`;
- same-timestamp ordering is deterministic via engine `sequence` in trace and
  ingestion source ordering for input events;
- no order can produce a fill before an ack;
- in latency mode, no order can produce a fill before `activation_time_ns`;
- rejected orders never mutate `EngineView`;
- trace rows explain every ack, reject, fill, and resting decision.

## Non-Goals For MVP

- No network protocol.
- No real exchange matching.
- No full exchange/network latency simulation beyond configured activation
  delay.
- No slippage model beyond fill-at-touch.
- No multi-threaded strategy runtime.
- No persistent result database.
- No attempt to send every full L3 event into Python by default.
