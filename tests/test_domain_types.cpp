#include "domain/MarketDataEvent.hpp"
#include "domain/Types.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

int main()
{
    static_assert(std::is_same_v<md::InstrumentId, std::uint64_t>);
    static_assert(std::is_same_v<md::OrderId, std::uint64_t>);
    static_assert(std::is_same_v<md::TradingEngineId, std::uint32_t>);

    static_assert(std::is_same_v<md::TimestampNs, std::int64_t>);
    static_assert(std::is_signed_v<md::TimestampNs>);

    static_assert(std::is_same_v<md::RawTimestampNs, std::uint64_t>);
    static_assert(std::is_unsigned_v<md::RawTimestampNs>);

    static_assert(std::is_same_v<md::Price, std::int64_t>);
    static_assert(std::is_same_v<md::Quantity, std::uint64_t>);
    static_assert(std::is_same_v<md::SeqNo, std::uint64_t>);

    static_assert(md::price_scale == 1'000'000'000LL);
    static_assert(md::undefined_price == std::numeric_limits<md::Price>::max());
    static_assert(md::raw_undefined_timestamp == std::numeric_limits<md::RawTimestampNs>::max());
    static_assert(md::undefined_timestamp == std::numeric_limits<md::TimestampNs>::min());

    static_assert(static_cast<char>(md::Side::Bid) == 'B');
    static_assert(static_cast<char>(md::Side::Ask) == 'A');
    static_assert(static_cast<char>(md::Side::None) == 'N');

    static_assert(static_cast<char>(md::Action::Add) == 'A');
    static_assert(static_cast<char>(md::Action::Modify) == 'M');
    static_assert(static_cast<char>(md::Action::Cancel) == 'C');
    static_assert(static_cast<char>(md::Action::Clear) == 'R');
    static_assert(static_cast<char>(md::Action::Trade) == 'T');
    static_assert(static_cast<char>(md::Action::Fill) == 'F');
    static_assert(static_cast<char>(md::Action::None) == 'N');

    static_assert(md::isDefinedPrice(md::Price{0}));
    static_assert(!md::isDefinedPrice(md::undefined_price));

    static_assert(md::isDefinedTimestamp(md::TimestampNs{0}));
    static_assert(!md::isDefinedTimestamp(md::undefined_timestamp));

    static_assert(md::isValidTradingEngineId(md::TradingEngineId{0}));
    static_assert(!md::isValidTradingEngineId(md::invalid_trading_engine_id));

    md::MarketDataEvent event{};
    event.instrument_id = md::InstrumentId{42};
    event.order_id = md::OrderId{1001};
    event.timestamp = md::RawTimestampNs{1'743'500'000'000'000'000ULL};
    event.price = md::Price{1'234'567'890};
    event.size = md::Quantity{10};
    event.side = md::Side::Bid;
    event.action = md::Action::Add;

    if (event.instrument_id != 42)
    {
        return 1;
    }
    if (event.order_id != 1001)
    {
        return 1;
    }
    if (event.price != 1'234'567'890)
    {
        return 1;
    }
    if (event.size != 10)
    {
        return 1;
    }
    if (md::formatPrice(event.price) != std::string{"1.234567890"})
    {
        return 1;
    }
    if (md::formatPrice(md::undefined_price) != std::string{"UNDEF"})
    {
        return 1;
    }

    return 0;
}
