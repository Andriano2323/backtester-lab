# Order & Trade Protocol

This document describes the M2 / Group 1 order and trade protocol used by the C++ backtesting engine.

The protocol is an in-process contract between trading engines and the backtest engine. Trading engines submit order requests. The backtest engine validates requests, returns acknowledgements or rejects, and can emit fill notifications. Real matching is intentionally outside M2; fills are exposed through a server API that will later be called by `SimulatedLOB` / Group 3.

The implementation lives in:

```text
src/gateway/OrderMessage.hpp
src/gateway/OrderChannel.hpp
src/gateway/OrderGatewayClient.hpp
src/gateway/OrderGatewayServer.hpp
```

## Purpose

Group 1 owns the order path:

- Trading engine to backtest engine: `NewOrder`, `ModifyOrder`, `CancelOrder`.
- Backtest engine to trading engine: `OrderAck`, `OrderReject`, `OrderFill`.

M2 defines the protocol, SPSC transport, validation, state store, callbacks, and latency benchmark. It does not implement real LOB matching or execution simulation.

## Architecture

Threading model:

- The trading engine thread owns `OrderGatewayClient`.
- The backtest engine thread owns `OrderGatewayServer`.
- Each trading engine session has one `OrderChannel`.
- `OrderChannel` has two SPSC queues:
  - trading engine -> backtest engine: `NonBlockingQueue<OrderRequest>`
  - backtest engine -> trading engine: `NonBlockingQueue<OrderEvent>`

Each queue direction keeps single-producer / single-consumer semantics. Request queues are produced by one trading-engine client and consumed by the backtest server. Event queues are produced by the backtest server and consumed by one trading-engine client.

Queues are batched. Requests and events may not be visible to the consumer until the producer calls `flush()` / `flushRequests()` / `flushEvents()` or the queue reaches its configured batch size.

## Message Schema

All protocol messages use shared domain types from `src/domain/Types.hpp`.

Required common fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `trading_engine_id` | `TradingEngineId` | Trading engine / strategy instance. |
| `order_id` | `OrderId` | Order identifier scoped by `TradingEngineId`. |
| `instrument_id` | `InstrumentId` | Instrument being traded. |
| `side` | `Side` | `Bid`, `Ask`, or `None` where the message has no active side. |
| `price` | `Price` | Fixed-precision price. `undefined_price` is invalid for active orders. |
| `size` | `Quantity` | Order quantity. Zero is invalid for active new/modify requests. |
| `timestamp_ns` | `TimestampNs` | Application-level nanosecond timestamp. |
| `status` | `OrderStatus` | Local/requested/final order status. |

Shared field container:

```cpp
struct OrderFields {
    TradingEngineId trading_engine_id;
    OrderId order_id;
    InstrumentId instrument_id;
    Side side;
    Price price;
    Quantity size;
    TimestampNs timestamp_ns;
    OrderStatus status;
};
```

Request variant:

```cpp
using OrderRequest = std::variant<NewOrder, CancelOrder, ModifyOrder>;
```

Event variant:

```cpp
using OrderEvent = std::variant<OrderAck, OrderFill, OrderReject>;
```

### NewOrder

Submitted by the trading engine to request a new order:

```cpp
struct NewOrder {
    OrderFields fields;
};
```

Valid new orders are stored by the server as `Accepted` and receive `OrderAck{NewAccepted}`.

### CancelOrder

Submitted by the trading engine to cancel an existing order:

```cpp
struct CancelOrder {
    OrderFields fields;
};
```

The client-created request uses `OrderStatus::CancelRequested`. Valid cancellation sets the stored order to `Cancelled` and receives `OrderAck{CancelAccepted}`.

### ModifyOrder

Submitted by the trading engine to modify an existing order:

```cpp
struct ModifyOrder {
    OrderFields fields;
};
```

The client-created request uses `OrderStatus::ModifyRequested`. Valid modification updates price, side, original size, remaining size, and timestamp, then receives `OrderAck{ModifyAccepted}`.

### OrderAck

Acknowledgement emitted by the backtest engine:

```cpp
enum class OrderAckType {
    NewAccepted,
    ModifyAccepted,
    CancelAccepted
};

struct OrderAck {
    OrderFields fields;
    OrderAckType ack_type;
};
```

Ack `status` is the final server status for that transition:

- `NewAccepted` -> `Accepted`
- `ModifyAccepted` -> `Accepted`
- `CancelAccepted` -> `Cancelled`

### OrderFill

Fill notification emitted by the backtest engine:

```cpp
struct OrderFill {
    OrderFields fields;
    Price fill_price;
    Quantity fill_size;
    Quantity remaining_size;
};
```

M2 exposes `OrderGatewayServer::emitFill()` so the future matching component can report fills. Partial fills set `status = PartiallyFilled`; final fills set `status = Filled`.

### OrderReject

Reject emitted by the backtest engine:

```cpp
enum class OrderRejectReason {
    DuplicateOrderId,
    UnknownOrderId,
    InvalidInstrument,
    InvalidSide,
    InvalidPrice,
    InvalidQuantity,
    AlreadyTerminal,
    InternalError
};

struct OrderReject {
    OrderFields fields;
    OrderRejectReason reason;
    std::string text;
};
```

Reject events preserve `order_id`, `instrument_id`, and `timestamp_ns` from the request and set `status = Rejected`.

## Order Lifecycle

Supported M2 lifecycle paths:

```text
New -> Accepted -> PartiallyFilled -> Filled
New -> Accepted -> Cancelled
Rejected
```

`Filled`, `Cancelled`, and `Rejected` are terminal states. A terminal order cannot be cancelled, modified, or filled again.

## Client API

`OrderGatewayClient` is the trading-engine-side API:

```cpp
OrderGatewayClient client(trading_engine_id, channel);

OrderId order_id = client.sendOrder(instrument_id, side, price, size, timestamp_ns);
client.sendOrder(explicit_new_order);
client.cancelOrder(order_id, instrument_id, timestamp_ns);
client.modifyOrder(order_id, instrument_id, side, new_price, new_size, timestamp_ns);
client.flush();
```

Event consumption:

```cpp
client.onAck([](const OrderAck& ack) {});
client.onFill([](const OrderFill& fill) {});
client.onReject([](const OrderReject& reject) {});

std::size_t drained = client.drainEvents();
```

`sendOrder(...)` auto-generates `OrderId` values scoped to one client. `sendOrder(NewOrder)` preserves the explicit `order_id` but uses the client's `TradingEngineId`.

`drainEvents()` repeatedly consumes currently available events and dispatches callbacks. `tryPopEvent()` and `popEvent()` are also available for raw event consumption without invoking callbacks.

## Server API

`OrderGatewayServer` is the backtest-engine-side API:

```cpp
OrderGatewayServer server;
server.registerEngine(trading_engine_id, channel);

std::size_t processed = server.drainRequests();
server.flushEvents();
```

Session inspection:

```cpp
bool exists = server.hasEngine(trading_engine_id);
std::size_t engines = server.engineCount();
std::shared_ptr<OrderChannel> channel = server.channelFor(trading_engine_id);
```

Order state inspection:

```cpp
std::size_t open_orders = server.openOrderCount();
std::optional<OrderSnapshot> order = server.findOrder(trading_engine_id, order_id);
```

Fill notification API:

```cpp
bool emitted = server.emitFill(
    trading_engine_id,
    order_id,
    fill_price,
    fill_size,
    timestamp_ns);
server.flushEvents();
```

`emitFill()` only routes to the order's trading engine. It returns `false` and emits no event for unknown engines, unknown orders, terminal orders, or zero-size fills.

## Validation Rules

`OrderGatewayServer` tracks orders by `(TradingEngineId, OrderId)`.

`NewOrder` validation:

- Duplicate `(trading_engine_id, order_id)` -> `OrderRejectReason::DuplicateOrderId`.
- `instrument_id == 0` -> `InvalidInstrument`.
- `side == Side::None` -> `InvalidSide`.
- `price == undefined_price` -> `InvalidPrice`.
- `size == 0` -> `InvalidQuantity`.
- Valid new order -> store as `Accepted`, `remaining_size = size`, emit `NewAccepted`.

`CancelOrder` validation:

- Unknown order -> `UnknownOrderId`.
- Terminal order -> `AlreadyTerminal`.
- Valid cancel -> set status `Cancelled`, emit `CancelAccepted`.

`ModifyOrder` validation:

- Unknown order -> `UnknownOrderId`.
- Terminal order -> `AlreadyTerminal`.
- `side == Side::None` -> `InvalidSide`.
- `price == undefined_price` -> `InvalidPrice`.
- `size == 0` -> `InvalidQuantity`.
- Valid modify -> update side, price, original size, remaining size, timestamp, set status `Accepted`, emit `ModifyAccepted`.

Fill notification behavior:

- Unknown engine or order -> `false`, no event.
- Terminal order -> `false`, no event.
- `fill_size == 0` -> `false`, no event.
- `fill_size < remaining_size` -> decrement `remaining_size`, set `PartiallyFilled`, emit `OrderFill`.
- `fill_size >= remaining_size` -> cap fill to remaining quantity, set `remaining_size = 0`, set `Filled`, emit `OrderFill`.

## Latency Benchmark

The order gateway round-trip latency benchmark is a separate executable:

```bash
./build/order_gateway_latency_benchmark
```

CLI flags:

```text
--orders N       Number of NewOrder messages. Default: 100000.
--batch-size N   Request/event queue batch size. Default: 256.
--threaded 0|1   Use a server thread that continuously drains requests. Default: 1.
```

Smoke command:

```bash
./build/order_gateway_latency_benchmark --orders 1000 --batch-size 64 --threaded 1
```

Release examples:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j

./build-release/order_gateway_latency_benchmark --orders 100000 --batch-size 256 --threaded 0
./build-release/order_gateway_latency_benchmark --orders 100000 --batch-size 256 --threaded 1
./build-release/order_gateway_latency_benchmark --orders 1000000 --batch-size 256 --threaded 1
```

Output:

```text
orders,batch_size,threaded,total_acks,wall_clock_seconds,orders_per_second,avg_latency_ns,min_latency_ns,p50_latency_ns,p95_latency_ns,p99_latency_ns,max_latency_ns
100000,256,1,100000,0.132814,752931.42,1843,900,1600,3100,5400,19000
```

The benchmark verifies that all submitted orders receive acknowledgements, that no rejects are produced, and that no messages are lost.

## Non-Goals In M2

- No real LOB matching.
- No slippage model.
- No Python bindings.
- No persistent order journal.
- No network FIX protocol.
- No real exchange connectivity.

Real fills will later come from `SimulatedLOB` / Group 3. M2 only defines and tests the notification/routing path that future matching logic will call.
