#include "messaging/MarketDataMessage.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testMessageFieldTypesUseDomainAliases()
{
    static_assert(std::is_same_v<decltype(md::PriceLevel{}.level_index), std::uint32_t>);
    static_assert(std::is_same_v<decltype(md::PriceLevel{}.price), md::Price>);
    static_assert(std::is_same_v<decltype(md::PriceLevel{}.size), md::Quantity>);

    static_assert(std::is_same_v<decltype(md::BookUpdate{}.instrument_id), md::InstrumentId>);
    static_assert(std::is_same_v<decltype(md::BookUpdate{}.timestamp_ns), md::TimestampNs>);
    static_assert(std::is_same_v<decltype(md::BookUpdate{}.seq_no), md::SeqNo>);
    static_assert(std::is_same_v<decltype(md::BookUpdate{}.side), md::Side>);
    static_assert(std::is_same_v<decltype(md::BookUpdate{}.price), md::Price>);
    static_assert(std::is_same_v<decltype(md::BookUpdate{}.size), md::Quantity>);

    static_assert(std::is_same_v<decltype(md::BookSnapshot{}.instrument_id), md::InstrumentId>);
    static_assert(std::is_same_v<decltype(md::BookSnapshot{}.timestamp_ns), md::TimestampNs>);
    static_assert(std::is_same_v<decltype(md::BookSnapshot{}.seq_no), md::SeqNo>);

    static_assert(std::is_same_v<decltype(md::Trade{}.instrument_id), md::InstrumentId>);
    static_assert(std::is_same_v<decltype(md::Trade{}.timestamp_ns), md::TimestampNs>);
    static_assert(std::is_same_v<decltype(md::Trade{}.seq_no), md::SeqNo>);
    static_assert(std::is_same_v<decltype(md::Trade{}.price), md::Price>);
    static_assert(std::is_same_v<decltype(md::Trade{}.size), md::Quantity>);
    static_assert(std::is_same_v<decltype(md::Trade{}.aggressor_side), md::Side>);
}

void testBookUpdateCarriesFields()
{
    const md::BookUpdate update{
        .instrument_id = md::InstrumentId{42},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'001LL},
        .seq_no = md::SeqNo{7},
        .side = md::Side::Bid,
        .price = md::Price{101'250'000'000LL},
        .size = md::Quantity{11},
    };

    require(update.instrument_id == 42, "book update instrument_id");
    require(update.timestamp_ns == 1'700'000'000'000'000'001LL, "book update timestamp_ns");
    require(update.seq_no == 7, "book update seq_no");
    require(update.side == md::Side::Bid, "book update side");
    require(update.price == 101'250'000'000LL, "book update price");
    require(update.size == 11, "book update size");
}

void testBookSnapshotCarriesLevels()
{
    const md::BookSnapshot snapshot{
        .instrument_id = md::InstrumentId{99},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'002LL},
        .seq_no = md::SeqNo{8},
        .bids = {
            {.level_index = 0, .price = md::Price{100'000'000'000LL}, .size = md::Quantity{5}},
            {.level_index = 1, .price = md::Price{99'500'000'000LL}, .size = md::Quantity{6}},
        },
        .asks = {
            {.level_index = 0, .price = md::Price{100'500'000'000LL}, .size = md::Quantity{7}},
        },
    };

    require(snapshot.bids.size() == 2, "snapshot bid count");
    require(snapshot.asks.size() == 1, "snapshot ask count");

    require(snapshot.bids[0].level_index == 0, "snapshot first bid level");
    require(snapshot.bids[0].price == 100'000'000'000LL, "snapshot first bid price");
    require(snapshot.bids[0].size == 5, "snapshot first bid size");

    require(snapshot.bids[1].level_index == 1, "snapshot second bid level");
    require(snapshot.bids[1].price == 99'500'000'000LL, "snapshot second bid price");
    require(snapshot.bids[1].size == 6, "snapshot second bid size");

    require(snapshot.asks[0].level_index == 0, "snapshot first ask level");
    require(snapshot.asks[0].price == 100'500'000'000LL, "snapshot first ask price");
    require(snapshot.asks[0].size == 7, "snapshot first ask size");
}

void testTradeCarriesFields()
{
    const md::Trade trade{
        .instrument_id = md::InstrumentId{123},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'003LL},
        .seq_no = md::SeqNo{9},
        .price = md::Price{102'000'000'000LL},
        .size = md::Quantity{12},
        .aggressor_side = md::Side::Ask,
    };

    require(trade.aggressor_side == md::Side::Ask, "trade aggressor_side");
    require(trade.price == 102'000'000'000LL, "trade price");
    require(trade.size == 12, "trade size");
}

void testVariantReportsMessageType()
{
    md::MarketDataMessage update = md::BookUpdate{};
    md::MarketDataMessage snapshot = md::BookSnapshot{};
    md::MarketDataMessage trade = md::Trade{};

    require(md::messageType(update) == md::MarketDataMessageType::BookUpdate, "book update type");
    require(md::messageType(snapshot) == md::MarketDataMessageType::BookSnapshot, "book snapshot type");
    require(md::messageType(trade) == md::MarketDataMessageType::Trade, "trade type");
}

void testCommonAccessorsWorkForAllMessageTypes()
{
    const md::MarketDataMessage update = md::BookUpdate{
        .instrument_id = md::InstrumentId{1},
        .timestamp_ns = md::TimestampNs{10},
        .seq_no = md::SeqNo{100},
    };
    const md::MarketDataMessage snapshot = md::BookSnapshot{
        .instrument_id = md::InstrumentId{2},
        .timestamp_ns = md::TimestampNs{20},
        .seq_no = md::SeqNo{200},
    };
    const md::MarketDataMessage trade = md::Trade{
        .instrument_id = md::InstrumentId{3},
        .timestamp_ns = md::TimestampNs{30},
        .seq_no = md::SeqNo{300},
    };

    require(md::instrumentId(update) == 1, "book update instrumentId accessor");
    require(md::timestampNs(update) == 10, "book update timestampNs accessor");
    require(md::seqNo(update) == 100, "book update seqNo accessor");

    require(md::instrumentId(snapshot) == 2, "book snapshot instrumentId accessor");
    require(md::timestampNs(snapshot) == 20, "book snapshot timestampNs accessor");
    require(md::seqNo(snapshot) == 200, "book snapshot seqNo accessor");

    require(md::instrumentId(trade) == 3, "trade instrumentId accessor");
    require(md::timestampNs(trade) == 30, "trade timestampNs accessor");
    require(md::seqNo(trade) == 300, "trade seqNo accessor");
}

void testSetSeqNoWorksForAllMessageTypes()
{
    md::MarketDataMessage update = md::BookUpdate{.seq_no = md::SeqNo{1}};
    md::MarketDataMessage snapshot = md::BookSnapshot{.seq_no = md::SeqNo{2}};
    md::MarketDataMessage trade = md::Trade{.seq_no = md::SeqNo{3}};

    md::setSeqNo(update, md::SeqNo{10});
    md::setSeqNo(snapshot, md::SeqNo{20});
    md::setSeqNo(trade, md::SeqNo{30});

    require(md::seqNo(update) == 10, "book update setSeqNo");
    require(md::seqNo(snapshot) == 20, "book snapshot setSeqNo");
    require(md::seqNo(trade) == 30, "trade setSeqNo");
}

} // namespace

int main()
{
    try
    {
        testMessageFieldTypesUseDomainAliases();
        testBookUpdateCarriesFields();
        testBookSnapshotCarriesLevels();
        testTradeCarriesFields();
        testVariantReportsMessageType();
        testCommonAccessorsWorkForAllMessageTypes();
        testSetSeqNoWorksForAllMessageTypes();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
