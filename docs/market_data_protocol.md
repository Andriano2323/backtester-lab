# Market Data Protocol

This document describes the M1 / Group 2 market data feed contract used by the C++ backtesting engine.

The feed is a lightweight in-process protocol between the backtest engine and one or more trading engines. Its job is to deliver normalized market data messages with deterministic per-instrument sequence numbers so subscribers can detect dropped, duplicated, or out-of-order feed events.

The implementation lives in:

```text
src/messaging/MarketDataMessage.hpp
src/messaging/MarketDataSubscriber.hpp
src/messaging/MarketDataPublisher.hpp
src/messaging/MarketDataGapDetector.hpp
```

## Purpose

Group 2 owns the market data delivery layer. The publisher belongs to the backtest engine. Trading engines subscribe to instruments and consume messages from their own queues.

M1 defines the protocol and delivery mechanics only. It does not yet connect the feed to the historical LOB reconstruction pipeline.

## Message Schema

All protocol messages use the shared domain types from `src/domain/Types.hpp`.

Required fields common to all market data messages:

| Field | Type | Meaning |
| --- | --- | --- |
| `instrument_id` | `InstrumentId` | Instrument the message belongs to. |
| `timestamp_ns` | `TimestampNs` | Application-level nanosecond timestamp. |
| `seq_no` | `SeqNo` | Per-instrument sequence number assigned by `MarketDataPublisher`. |

The message variant is:

```cpp
using MarketDataMessage = std::variant<BookUpdate, BookSnapshot, Trade>;
```

### BookUpdate

Incremental top-of-book or level update payload:

```cpp
struct BookUpdate {
    InstrumentId instrument_id;
    TimestampNs timestamp_ns;
    SeqNo seq_no;
    Side side;
    Price price;
    Quantity size;
};
```

### BookSnapshot

Multi-level book snapshot payload:

```cpp
struct PriceLevel {
    std::uint32_t level_index;
    Price price;
    Quantity size;
};

struct BookSnapshot {
    InstrumentId instrument_id;
    TimestampNs timestamp_ns;
    SeqNo seq_no;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};
```

### Trade

Trade payload:

```cpp
struct Trade {
    InstrumentId instrument_id;
    TimestampNs timestamp_ns;
    SeqNo seq_no;
    Price price;
    Quantity size;
    Side aggressor_side;
};
```

Helper functions:

```cpp
MarketDataMessageType messageType(const MarketDataMessage&);
InstrumentId instrumentId(const MarketDataMessage&);
TimestampNs timestampNs(const MarketDataMessage&);
SeqNo seqNo(const MarketDataMessage&);
void setSeqNo(MarketDataMessage&, SeqNo);
```

## Sequence Numbers

`MarketDataPublisher` assigns `seq_no` internally. Callers may pass messages with any input `seq_no`; the publisher overwrites it before routing.

Sequence numbers are tracked per instrument:

```text
publish instrument 10 -> seq_no 1
publish instrument 20 -> seq_no 1
publish instrument 10 -> seq_no 2
```

Publishing another instrument must not create artificial sequence gaps for subscribers of the first instrument.

## Publisher / Subscriber Model

Threading model:

- One backtest-engine publisher.
- Many trading-engine subscribers.
- Each subscriber has exactly one SPSC queue.
- Each subscriber queue is written by the publisher side and read by one trading-engine consumer side.
- Multiple subscribers to the same instrument each receive their own copy of every message.

Subscriber queues are batched. Messages may not be visible to `tryPop()` or `drainAvailable()` until the queue is flushed or the queue batch reaches its configured batch size.

## API Overview

Subscribe a trading engine callback to one instrument:

```cpp
MarketDataPublisher publisher;

MarketDataSubscription subscription = publisher.subscribe(
    instrument_id,
    [](const MarketDataMessage& message) {
        // trading-engine callback
    },
    queue_batch_size);
```

Publish market data:

```cpp
SeqNo update_seq = publisher.publishUpdate(update);
SeqNo snapshot_seq = publisher.publishSnapshot(snapshot);
SeqNo trade_seq = publisher.publishTrade(trade);
```

Make manually batched messages visible:

```cpp
publisher.flush();
```

Consumer-side options:

```cpp
std::optional<MarketDataMessage> message = subscription.subscriber->tryPop();
MarketDataMessage blocking_message = subscription.subscriber->pop();
std::size_t drained = subscription.subscriber->drainAvailable();
std::size_t pending = subscription.subscriber->pendingApprox();
```

`drainAvailable()` repeatedly calls `tryPop()` and invokes the subscriber callback once per available message.

## Gap Detection

`MarketDataGapDetector` tracks the last observed `seq_no` per `InstrumentId`.

Statuses:

| Status | Meaning |
| --- | --- |
| `Ok` | The observed sequence number is exactly the expected next value. |
| `Gap` | The observed sequence number is greater than the expected next value. |
| `DuplicateOrOutOfOrder` | The observed sequence number is less than or equal to the last seen value. |

Rules:

- First observed `seq_no` for an instrument is accepted if it is `1`.
- First observed `seq_no > 1` reports `Gap` with `expected_seq_no = 1`.
- `observed_seq_no == last + 1` reports `Ok`.
- `observed_seq_no > last + 1` reports `Gap`.
- `observed_seq_no <= last` reports `DuplicateOrOutOfOrder`.
- `reset(instrument_id)` clears one instrument.
- `reset()` clears all instruments.

Example:

```cpp
MarketDataGapDetector detector;
SequenceCheckResult result = detector.observe(message);
```

## Benchmark

The feed throughput benchmark is a separate executable:

```bash
./build/market_data_feed_benchmark
```

CLI flags:

```text
--subscribers N   Number of subscribers. Default: 4.
--messages M      Number of published BookUpdate messages. Default: 100000.
--batch-size N    Subscriber queue batch size. Default: 256.
```

Smoke command:

```bash
./build/market_data_feed_benchmark --subscribers 2 --messages 1000 --batch-size 64
```

Release examples:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j

./build-release/market_data_feed_benchmark --subscribers 1 --messages 1000000 --batch-size 256
./build-release/market_data_feed_benchmark --subscribers 2 --messages 1000000 --batch-size 256
./build-release/market_data_feed_benchmark --subscribers 4 --messages 1000000 --batch-size 256
./build-release/market_data_feed_benchmark --subscribers 8 --messages 1000000 --batch-size 256
```

Output:

```text
subscribers,messages,total_deliveries,wall_clock_seconds,publishes_per_second,deliveries_per_second
4,1000000,4000000,0.842317,1187190.12,4748760.48
```

The benchmark verifies that every subscriber receives all messages and that the gap detector reports no missing, duplicated, or out-of-order sequence numbers.

## Non-Goals In M1

- No Order Gateway.
- No Python bindings.
- No full LOB reconstruction in the market data feed.
- No unsubscribe API.
- `BookUpdate` and `BookSnapshot` producers are not connected to the LOB simulation yet.
