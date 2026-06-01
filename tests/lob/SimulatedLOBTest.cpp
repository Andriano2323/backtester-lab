#include "TestSupport.hpp"

#include "lob/EngineView.hpp"
#include "lob/HistoricalLOB.hpp"
#include "lob/SimulatedLOB.hpp"

#include <optional>
#include <string>

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

} // namespace

void testSimulatedLobMergesHistoricalAndOwnOverlayOnly()
{
    md::lob::HistoricalLOB historical_book;
    historical_book.apply(addHistorical(1, Side::Bid, 99, 10));
    historical_book.apply(addHistorical(2, Side::Ask, 101, 10));

    md::lob::EngineView engine1{1};
    md::lob::EngineView engine2{2};
    engine1.addSyntheticOrder(42, Side::Bid, 100, 5, 1000);

    const md::lob::SimulatedLOB simulated1{historical_book, engine1};
    const md::lob::SimulatedLOB simulated2{historical_book, engine2};

    requireLevel(simulated1.bestBid(42), 100, 5, "engine 1 simulated best bid");
    requireLevel(simulated1.bestAsk(42), 101, 10, "engine 1 simulated best ask");
    requireLevel(simulated2.bestBid(42), 99, 10, "engine 2 simulated best bid");
    requireLevel(simulated2.bestAsk(42), 101, 10, "engine 2 simulated best ask");
    requireLevel(historical_book.bestBid(), 99, 10, "historical best bid unchanged");
}

} // namespace md::test
