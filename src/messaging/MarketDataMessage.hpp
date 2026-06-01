#pragma once

#include "domain/Types.hpp"

#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

namespace md
{

enum class MarketDataMessageType
{
    BookUpdate,
    BookSnapshot,
    Trade
};

struct PriceLevel
{
    std::uint32_t level_index{};
    Price price{};
    Quantity size{};
};

struct BookUpdate
{
    InstrumentId instrument_id{};
    TimestampNs timestamp_ns{};
    SeqNo seq_no{};
    Side side{Side::None};
    Price price{};
    Quantity size{};
};

struct BookSnapshot
{
    InstrumentId instrument_id{};
    TimestampNs timestamp_ns{};
    SeqNo seq_no{};
    std::vector<PriceLevel> bids{};
    std::vector<PriceLevel> asks{};
};

struct Trade
{
    InstrumentId instrument_id{};
    TimestampNs timestamp_ns{};
    SeqNo seq_no{};
    Price price{};
    Quantity size{};
    Side aggressor_side{Side::None};
};

using MarketDataMessage = std::variant<BookUpdate, BookSnapshot, Trade>;

[[nodiscard]] inline MarketDataMessageType messageType(const MarketDataMessage& message)
{
    return std::visit(
        [](const auto& payload) -> MarketDataMessageType
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, BookUpdate>)
            {
                return MarketDataMessageType::BookUpdate;
            }
            else if constexpr (std::is_same_v<Payload, BookSnapshot>)
            {
                return MarketDataMessageType::BookSnapshot;
            }
            else
            {
                return MarketDataMessageType::Trade;
            }
        },
        message);
}

[[nodiscard]] inline InstrumentId instrumentId(const MarketDataMessage& message)
{
    return std::visit([](const auto& payload) { return payload.instrument_id; }, message);
}

[[nodiscard]] inline TimestampNs timestampNs(const MarketDataMessage& message)
{
    return std::visit([](const auto& payload) { return payload.timestamp_ns; }, message);
}

[[nodiscard]] inline SeqNo seqNo(const MarketDataMessage& message)
{
    return std::visit([](const auto& payload) { return payload.seq_no; }, message);
}

inline void setSeqNo(MarketDataMessage& message, SeqNo seq_no)
{
    std::visit(
        [seq_no](auto& payload)
        {
            payload.seq_no = seq_no;
        },
        message);
}

} // namespace md
