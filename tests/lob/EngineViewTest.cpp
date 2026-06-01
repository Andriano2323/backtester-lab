#include "TestSupport.hpp"

#include "lob/EngineView.hpp"
#include "lob/HistoricalLOB.hpp"

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

void testEngineViewsArePrivate()
{
    md::lob::HistoricalLOB historical_book;
    historical_book.apply(addHistorical(1, Side::Bid, 99, 10));

    md::lob::EngineView engine1{1};
    md::lob::EngineView engine2{2};

    const auto synthetic_order_id = engine1.addSyntheticOrder(42, Side::Bid, 100, 5, 1000);

    require(engine1.engineId() == 1, "engine 1 id");
    require(engine2.engineId() == 2, "engine 2 id");
    require(synthetic_order_id != 0, "synthetic order id allocated");
    requireLevel(engine1.syntheticBook(42).bestBid(), 100, 5, "engine 1 synthetic best bid");
    require(!engine2.syntheticBook(42).bestBid().has_value(), "engine 2 synthetic bid side remains empty");
    requireLevel(historical_book.bestBid(), 99, 10, "historical book unchanged");
    require(historical_book.restingOrderCount() == 1, "historical resting order count unchanged");
}

} // namespace md::test
