#include "TestSupport.hpp"

#include "lob/HistoricalLOB.hpp"

#include <optional>
#include <string>
#include <vector>

namespace md::test
{
namespace
{

MarketDataEvent event(
    Action action,
    std::uint64_t order_id,
    Side side,
    md::lob::Price price,
    md::lob::Quantity quantity)
{
    MarketDataEvent market_event;
    market_event.order_id = order_id;
    market_event.side = side;
    market_event.price = price;
    market_event.size = quantity;
    market_event.action = action;
    market_event.instrument_id = 42;
    return market_event;
}

MarketDataEvent add(
    std::uint64_t order_id,
    Side side,
    md::lob::Price price,
    md::lob::Quantity quantity)
{
    return event(Action::Add, order_id, side, price, quantity);
}

MarketDataEvent modify(
    std::uint64_t order_id,
    Side side,
    md::lob::Price price,
    md::lob::Quantity quantity)
{
    return event(Action::Modify, order_id, side, price, quantity);
}

MarketDataEvent cancel(std::uint64_t order_id, md::lob::Quantity quantity)
{
    return event(Action::Cancel, order_id, Side::None, 0, quantity);
}

MarketDataEvent clear()
{
    return event(Action::Clear, 0, Side::None, 0, 0);
}

void requireLevel(
    const std::optional<md::lob::BookLevel>& level,
    md::lob::Price price,
    md::lob::Quantity quantity,
    const std::string& message)
{
    require(level.has_value(), message + ": missing level");
    require(level->price == price, message + ": unexpected price");
    require(level->size == quantity, message + ": unexpected size");
}

md::lob::Quantity quantityAt(
    const std::vector<md::lob::BookLevel>& levels,
    md::lob::Price price)
{
    for (const auto& level : levels)
    {
        if (level.price == price)
        {
            return level.size;
        }
    }

    return 0;
}

} // namespace

void testHistoricalLobAddModifyCancelClear()
{
    md::lob::HistoricalLOB book;

    book.apply(add(1, Side::Bid, 100, 10));
    requireLevel(book.bestBid(), 100, 10, "add bid creates best bid");
    require(!book.bestAsk().has_value(), "add bid leaves ask empty");

    book.apply(add(2, Side::Ask, 101, 7));
    requireLevel(book.bestBid(), 100, 10, "add ask preserves best bid");
    requireLevel(book.bestAsk(), 101, 7, "add ask creates best ask");

    book.apply(add(3, Side::Bid, 100, 5));
    require(quantityAt(book.bids(10), 100) == 15, "same-price bids aggregate");

    book.apply(modify(1, Side::Bid, 99, 4));
    require(quantityAt(book.bids(10), 100) == 5, "modify removes old bid volume");
    require(quantityAt(book.bids(10), 99) == 4, "modify adds new bid volume");
    requireLevel(book.bestBid(), 100, 5, "modify keeps highest bid as best bid");

    book.apply(cancel(3, 2));
    require(quantityAt(book.bids(10), 100) == 3, "partial cancel reduces bid volume");

    book.apply(cancel(3, 3));
    require(quantityAt(book.bids(10), 100) == 0, "full cancel removes bid level");
    require(book.restingOrderCount() == 2, "full cancel removes order");

    book.apply(clear());
    require(!book.bestBid().has_value(), "clear removes all bids");
    require(!book.bestAsk().has_value(), "clear removes all asks");
    require(book.restingOrderCount() == 0, "clear removes all resting orders");
    require(book.bidLevelCount() == 0, "clear removes bid levels");
    require(book.askLevelCount() == 0, "clear removes ask levels");
}

void testHistoricalLobTopNSnapshot()
{
    md::lob::HistoricalLOB book;

    book.apply(add(1, Side::Bid, 100, 10));
    book.apply(add(2, Side::Bid, 99, 20));
    book.apply(add(3, Side::Bid, 98, 30));
    book.apply(add(4, Side::Ask, 101, 5));
    book.apply(add(5, Side::Ask, 102, 6));
    book.apply(add(6, Side::Ask, 103, 7));

    const auto snapshot = book.snapshot(2);

    require(snapshot.instrument_id == 42, "snapshot instrument id");
    require(snapshot.bids.size() == 2, "snapshot bid depth");
    require(snapshot.asks.size() == 2, "snapshot ask depth");
    require(snapshot.bids[0].price == 100, "snapshot first bid price");
    require(snapshot.bids[0].size == 10, "snapshot first bid size");
    require(snapshot.bids[1].price == 99, "snapshot second bid price");
    require(snapshot.bids[1].size == 20, "snapshot second bid size");
    require(snapshot.asks[0].price == 101, "snapshot first ask price");
    require(snapshot.asks[0].size == 5, "snapshot first ask size");
    require(snapshot.asks[1].price == 102, "snapshot second ask price");
    require(snapshot.asks[1].size == 6, "snapshot second ask size");
}

} // namespace md::test
