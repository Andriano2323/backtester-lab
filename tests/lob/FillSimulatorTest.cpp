#include "TestSupport.hpp"

#include "lob/FillSimulator.hpp"
#include "lob/HistoricalLOB.hpp"
#include "lob/SimulatedLOB.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace md::test
{
namespace
{

MarketDataEvent addHistorical(
    std::uint64_t order_id,
    Side side,
    md::lob::Price price,
    md::lob::Quantity size)
{
    MarketDataEvent event;
    event.instrument_id = 42;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = Action::Add;
    return event;
}

void requireLevel(
    const std::optional<md::lob::BookLevel>& level,
    md::lob::Price price,
    md::lob::Quantity size,
    const std::string& message)
{
    require(level.has_value(), message + ": missing level");
    require(level->price == price, message + ": unexpected price");
    require(level->size == size, message + ": unexpected size");
}

md::lob::FillSimulator::EngineViews makeEngineViews()
{
    md::lob::FillSimulator::EngineViews views;
    views.try_emplace(1, 1);
    views.try_emplace(2, 2);
    return views;
}

} // namespace

void testFillAtTouchConsumesOnlyPrivateLiquidity()
{
    md::lob::HistoricalLOB historical_book;
    historical_book.apply(addHistorical(1, Side::Ask, 101, 10));

    auto engine_views = makeEngineViews();
    md::lob::FillSimulator simulator{historical_book, engine_views};

    const auto fills = simulator.submitLimitOrder(md::lob::SimulatedOrderRequest{
        .engine_id = 1,
        .instrument_id = 42,
        .side = Side::Bid,
        .limit_price = 101,
        .size = 4,
        .timestamp_ns = 1000,
    });

    require(fills.size() == 1, "touching buy receives one fill");
    require(fills[0].engine_id == 1, "fill engine id");
    require(fills[0].instrument_id == 42, "fill instrument id");
    require(fills[0].order_id != 0, "fill order id");
    require(fills[0].side == Side::Bid, "fill side");
    require(fills[0].price == 101, "fill price");
    require(fills[0].size == 4, "fill size");
    require(fills[0].timestamp_ns == 1000, "fill timestamp");

    const md::lob::SimulatedLOB engine1_lob{historical_book, engine_views.at(1)};
    const md::lob::SimulatedLOB engine2_lob{historical_book, engine_views.at(2)};

    requireLevel(engine1_lob.bestAsk(42), 101, 6, "engine 1 sees consumed ask liquidity");
    requireLevel(engine2_lob.bestAsk(42), 101, 10, "engine 2 sees full historical ask liquidity");
    requireLevel(historical_book.bestAsk(), 101, 10, "historical ask remains unchanged");
}

void testNonCrossingLimitOrderRestsInEngineView()
{
    md::lob::HistoricalLOB historical_book;
    historical_book.apply(addHistorical(1, Side::Ask, 101, 10));

    auto engine_views = makeEngineViews();
    md::lob::FillSimulator simulator{historical_book, engine_views};

    const auto fills = simulator.submitLimitOrder(md::lob::SimulatedOrderRequest{
        .engine_id = 1,
        .instrument_id = 42,
        .side = Side::Bid,
        .limit_price = 100,
        .size = 4,
        .timestamp_ns = 1000,
    });

    require(fills.empty(), "non-crossing buy receives no fill");
    requireLevel(engine_views.at(1).syntheticBook(42).bestBid(), 100, 4, "non-crossing buy rests as synthetic bid");
    requireLevel(historical_book.bestAsk(), 101, 10, "historical ask remains unchanged after resting order");
}

} // namespace md::test
