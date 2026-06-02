#pragma once

#include <cstdint>
#include <limits>

namespace md
{

// Core identifiers.
using InstrumentId = std::uint64_t;
using OrderId = std::uint64_t;
using TradingEngineId = std::uint32_t;

// Time.
using TimestampNs = std::int64_t;
using RawTimestampNs = std::uint64_t;

// Market quantities.
using Price = std::int64_t;
using Quantity = std::uint64_t;

// Sequencing / feed metadata.
using SeqNo = std::uint64_t;
using SourceFileId = std::uint32_t;
using SourceSequence = std::uint64_t;

// Constants.
inline constexpr Price price_scale = 1'000'000'000LL;
inline constexpr Price undefined_price = std::numeric_limits<Price>::max();

inline constexpr RawTimestampNs raw_undefined_timestamp =
    std::numeric_limits<RawTimestampNs>::max();

inline constexpr TimestampNs undefined_timestamp =
    std::numeric_limits<TimestampNs>::min();

inline constexpr TradingEngineId invalid_trading_engine_id =
    std::numeric_limits<TradingEngineId>::max();

enum class Side : char
{
    Ask = 'A',
    Bid = 'B',
    None = 'N'
};

enum class Action : char
{
    Add = 'A',
    Modify = 'M',
    Cancel = 'C',
    Clear = 'R',
    Trade = 'T',
    Fill = 'F',
    None = 'N'
};

enum class OrderStatus : std::uint8_t
{
    New,
    Accepted,
    ModifyRequested,
    CancelRequested,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

[[nodiscard]] constexpr bool isDefinedPrice(Price price) noexcept
{
    return price != undefined_price;
}

[[nodiscard]] constexpr bool isDefinedTimestamp(TimestampNs timestamp) noexcept
{
    return timestamp != undefined_timestamp;
}

[[nodiscard]] constexpr bool isValidTradingEngineId(TradingEngineId id) noexcept
{
    return id != invalid_trading_engine_id;
}

} // namespace md
