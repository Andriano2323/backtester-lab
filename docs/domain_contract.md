# Domain Contract

This document defines the shared domain types and conventions used across the C++ backtesting engine.

The goal is to make the order gateway, market data feed, limit order book simulation, strategy API, and Python bindings use the same identifiers, units, sentinel values, and enum semantics.

## Type Ownership

The canonical C++ definitions live in:

```text
src/domain/Types.hpp
```

All new modules should use these aliases instead of raw integer types in public APIs.

## Identifiers

| Name | C++ type | Meaning |
| --- | ---: | --- |
| `InstrumentId` | `std::uint64_t` | Numeric instrument identifier from market data. Matches the `instrument_id` field used by the ingestion layer. |
| `OrderId` | `std::uint64_t` | Order identifier. Used for both historical market-data orders and synthetic strategy orders, disambiguated by context and `TradingEngineId`. |
| `TradingEngineId` | `std::uint32_t` | Identifier of a trading engine / strategy instance. Multiple trading engines may run simultaneously. |
| `SeqNo` | `std::uint64_t` | Monotonic sequence number for market data messages, used for gap detection. |

## Time

| Name | C++ type | Meaning |
| --- | ---: | --- |
| `TimestampNs` | `std::int64_t` | Application-level timestamp: nanoseconds since Unix epoch. |
| `RawTimestampNs` | `std::uint64_t` | Raw Databento timestamp representation. Used where `UINT64_MAX` can appear as an undefined timestamp sentinel. |

Rules:

- Public simulation/order/feed APIs should use `TimestampNs`.
- Raw ingestion code may use `RawTimestampNs` while parsing Databento fields.
- Undefined raw Databento timestamp is `raw_undefined_timestamp`.
- Undefined application-level timestamp is `undefined_timestamp`.

## Price

| Name | C++ type | Meaning |
| --- | ---: | --- |
| `Price` | `std::int64_t` | Fixed-precision signed integer price. One unit equals `1e-9`. |
| `Quantity` | `std::uint64_t` | Non-negative order or trade size. |

Rules:

- Price scale is `price_scale = 1'000'000'000`.
- Decimal price = `Price / price_scale`.
- Undefined price is `undefined_price = INT64_MAX`.
- Floating point should not be used in core matching logic.

## Side

`Side` is represented as:

| Value | Meaning |
| --- | --- |
| `Side::Bid` | Buy / bid side |
| `Side::Ask` | Sell / ask side |
| `Side::None` | No side specified |

The enum values intentionally match the Databento one-character representation:

```text
B = bid / buy
A = ask / sell
N = none
```

## Market Data Action

`Action` is represented as:

| Value | Meaning |
| --- | --- |
| `Action::Add` | Add order |
| `Action::Modify` | Modify order |
| `Action::Cancel` | Cancel order |
| `Action::Clear` | Clear book |
| `Action::Trade` | Aggressing trade |
| `Action::Fill` | Resting order fill |
| `Action::None` | No action |

## Order Status

`OrderStatus` is used by the simulated order gateway:

| Value | Meaning |
| --- | --- |
| `New` | Order was created locally |
| `Accepted` | Backtest engine accepted the order |
| `ModifyRequested` | Order modification was requested locally |
| `CancelRequested` | Order cancellation was requested locally |
| `PartiallyFilled` | Order was partially filled |
| `Filled` | Order was fully filled |
| `Cancelled` | Order was cancelled |
| `Rejected` | Order was rejected |

## Compatibility Rule

The domain contract should not change replay behavior by itself. After adding or changing shared types, all existing ingestion tests and CLI modes must still pass.
