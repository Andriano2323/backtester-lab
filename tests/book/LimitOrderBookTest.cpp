#include "TestSupport.hpp"

#include "book/LimitOrderBook.hpp"

namespace md::test
{

namespace
{

std::int64_t P(std::int64_t integer_price)
{
    return integer_price * 1'000'000'000LL;
}

MarketDataEvent add(
    std::uint64_t order_id,
    Side side,
    std::int64_t price,
    std::uint64_t size)
{
    MarketDataEvent event;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = Action::Add;
    event.instrument_id = 42;
    return event;
}

MarketDataEvent cancel(std::uint64_t order_id, std::uint64_t size)
{
    MarketDataEvent event;
    event.order_id = order_id;
    event.size = size;
    event.action = Action::Cancel;
    event.instrument_id = 42;
    return event;
}

MarketDataEvent modify(
    std::uint64_t order_id,
    Side side,
    std::int64_t price,
    std::uint64_t size)
{
    MarketDataEvent event;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = Action::Modify;
    event.instrument_id = 42;
    return event;
}

MarketDataEvent clearEvent()
{
    MarketDataEvent event;
    event.action = Action::Clear;
    event.instrument_id = 42;
    return event;
}

MarketDataEvent trade(std::int64_t price, std::uint64_t size)
{
    MarketDataEvent event;
    event.price = price;
    event.size = size;
    event.action = Action::Trade;
    event.instrument_id = 42;
    return event;
}

MarketDataEvent fill(std::uint64_t order_id, std::uint64_t size)
{
    MarketDataEvent event;
    event.order_id = order_id;
    event.size = size;
    event.action = Action::Fill;
    event.instrument_id = 42;
    return event;
}

void attachDiagnosticMetadata(
    MarketDataEvent& event,
    std::uint64_t timestamp,
    std::uint32_t source_file_id,
    std::uint64_t source_sequence,
    std::size_t line_number)
{
    event.timestamp = timestamp;
    event.source_file_id = source_file_id;
    event.source_sequence = source_sequence;
    event.line_number = line_number;
}

} // namespace

void testLimitOrderBookStartsEmpty()
{
    LimitOrderBook book{42};

    require(!book.bestBid().has_value(), "new book has no best bid");
    require(!book.bestAsk().has_value(), "new book has no best ask");
    require(book.volumeAt(Side::Bid, P(100)) == 0, "new book has no bid volume");
    require(book.volumeAt(Side::Ask, P(100)) == 0, "new book has no ask volume");
    require(book.restingOrderCount() == 0, "new book has no resting orders");
    require(book.skippedUnknownOrderCount() == 0, "new book has no skipped unknown orders");
    require(book.unknownModifyRecoveredAsAddCount() == 0, "new book has no recovered unknown modifies");
    require(book.unknownModifySkippedCount() == 0, "new book has no skipped unknown modifies");
    require(book.unknownCancelSkippedCount() == 0, "new book has no skipped unknown cancels");
    require(book.unknownOrderDiagnostics().empty(), "new book has no unknown order diagnostics");
    require(book.tradeCount() == 0, "new book has no trades");
    require(book.fillCount() == 0, "new book has no fills");
}

void testLimitOrderBookReportsInstrumentId()
{
    LimitOrderBook book{42};

    require(book.instrumentId() == 42, "book reports instrument id");
}

void testLobAddSingleBid()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));

    require(book.bestBid() == P(100), "single bid becomes best bid");
    require(!book.bestAsk().has_value(), "single bid leaves ask side empty");
    require(book.volumeAt(Side::Bid, P(100)) == 10, "single bid volume");
    require(book.restingOrderCount() == 1, "single bid resting count");
}

void testLobAddSingleAsk()
{
    LimitOrderBook book{42};

    book.apply(add(2, Side::Ask, P(105), 7));

    require(!book.bestBid().has_value(), "single ask leaves bid side empty");
    require(book.bestAsk() == P(105), "single ask becomes best ask");
    require(book.volumeAt(Side::Ask, P(105)) == 7, "single ask volume");
    require(book.restingOrderCount() == 1, "single ask resting count");
}

void testLobAddAggregatesSameBidPrice()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(2, Side::Bid, P(100), 15));

    require(book.bestBid() == P(100), "aggregated bid price remains best bid");
    require(book.volumeAt(Side::Bid, P(100)) == 25, "bid volume aggregates");
    require(book.restingOrderCount() == 2, "two bid orders rest");
}

void testLobAddAggregatesSameAskPrice()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Ask, P(105), 10));
    book.apply(add(2, Side::Ask, P(105), 15));

    require(book.bestAsk() == P(105), "aggregated ask price remains best ask");
    require(book.volumeAt(Side::Ask, P(105)) == 25, "ask volume aggregates");
    require(book.restingOrderCount() == 2, "two ask orders rest");
}

void testLobBestBidIsHighestBidPrice()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(2, Side::Bid, P(101), 5));
    book.apply(add(3, Side::Bid, P(99), 20));

    require(book.bestBid() == P(101), "best bid is highest bid price");
}

void testLobBestAskIsLowestAskPrice()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Ask, P(105), 10));
    book.apply(add(2, Side::Ask, P(104), 5));
    book.apply(add(3, Side::Ask, P(106), 20));

    require(book.bestAsk() == P(104), "best ask is lowest ask price");
}

void testLobDuplicateAddReplacesOldOrder()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(1, Side::Bid, P(101), 8));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "duplicate add removes old price volume");
    require(book.volumeAt(Side::Bid, P(101)) == 8, "duplicate add inserts replacement volume");
    require(book.bestBid() == P(101), "replacement order becomes best bid");
    require(book.restingOrderCount() == 1, "duplicate add keeps one resting order");
}

void testLobCancelPartial()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(cancel(1, 4));

    require(book.volumeAt(Side::Bid, P(100)) == 6, "partial cancel subtracts bid volume");
    require(book.bestBid() == P(100), "partial cancel leaves bid level live");
    require(book.restingOrderCount() == 1, "partial cancel leaves order resting");
}

void testLobCancelFullWithSize()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(cancel(1, 10));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "full cancel removes bid volume");
    require(!book.bestBid().has_value(), "full cancel clears best bid");
    require(book.restingOrderCount() == 0, "full cancel removes order");
}

void testLobCancelFullWithZeroSize()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Ask, P(105), 7));
    book.apply(cancel(1, 0));

    require(book.volumeAt(Side::Ask, P(105)) == 0, "zero-size cancel removes ask volume");
    require(!book.bestAsk().has_value(), "zero-size cancel clears best ask");
    require(book.restingOrderCount() == 0, "zero-size cancel removes order");
}

void testLobCancelLargerThanRestingSizeIsCapped()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(cancel(1, 999));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "oversized cancel removes capped bid volume");
    require(!book.bestBid().has_value(), "oversized cancel clears best bid");
    require(book.restingOrderCount() == 0, "oversized cancel removes order without underflow");
}

void testLobCancelUnknownOrderIsNoop()
{
    LimitOrderBook book{42};

    auto event = cancel(999, 10);
    attachDiagnosticMetadata(event, 123, 7, 8, 9);
    book.apply(event);

    require(!book.bestBid().has_value(), "unknown cancel leaves bid side empty");
    require(!book.bestAsk().has_value(), "unknown cancel leaves ask side empty");
    require(book.restingOrderCount() == 0, "unknown cancel leaves no resting orders");
    require(book.skippedUnknownOrderCount() == 1, "unknown cancel increments skipped count");
    require(book.unknownCancelSkippedCount() == 1, "unknown cancel increments explicit cancel skipped count");
    require(book.unknownModifySkippedCount() == 0, "unknown cancel does not increment modify skipped count");
    require(book.unknownModifyRecoveredAsAddCount() == 0, "unknown cancel does not increment recovered modify count");

    const auto& diagnostics = book.unknownOrderDiagnostics();
    require(diagnostics.size() == 1, "unknown cancel records one diagnostic");
    require(diagnostics[0].operation == "cancel", "unknown cancel diagnostic operation");
    require(diagnostics[0].decision == "skipped", "unknown cancel diagnostic decision");
    require(diagnostics[0].timestamp == 123, "unknown cancel diagnostic timestamp");
    require(diagnostics[0].instrument_id == 42, "unknown cancel diagnostic instrument");
    require(diagnostics[0].order_id == 999, "unknown cancel diagnostic order id");
    require(diagnostics[0].side == Side::None, "unknown cancel diagnostic side");
    require(diagnostics[0].price == 0, "unknown cancel diagnostic price");
    require(diagnostics[0].size == 10, "unknown cancel diagnostic size");
    require(diagnostics[0].source_file_id == 7, "unknown cancel diagnostic source file");
    require(diagnostics[0].source_sequence == 8, "unknown cancel diagnostic source sequence");
    require(diagnostics[0].line_number == 9, "unknown cancel diagnostic line number");
}

void testLobCancelRemovesEmptyPriceLevel()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(2, Side::Bid, P(99), 5));
    book.apply(cancel(1, 10));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "full cancel removes empty price level");
    require(book.bestBid() == P(99), "best bid skips removed empty price level");
    require(book.restingOrderCount() == 1, "only lower bid remains resting");
}

void testLobModifySizeSamePrice()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(modify(1, Side::Bid, P(100), 20));

    require(book.volumeAt(Side::Bid, P(100)) == 20, "modify updates same-price bid size");
    require(book.bestBid() == P(100), "same-price modify keeps best bid");
    require(book.restingOrderCount() == 1, "same-price modify keeps one resting order");
}

void testLobModifyPriceLevel()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(modify(1, Side::Bid, P(101), 8));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "modify removes old bid price volume");
    require(book.volumeAt(Side::Bid, P(101)) == 8, "modify adds new bid price volume");
    require(book.bestBid() == P(101), "modify price level updates best bid");
    require(book.restingOrderCount() == 1, "price modify keeps one resting order");
}

void testLobModifySideChange()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(modify(1, Side::Ask, P(105), 7));

    require(book.volumeAt(Side::Bid, P(100)) == 0, "modify side change removes bid volume");
    require(book.volumeAt(Side::Ask, P(105)) == 7, "modify side change adds ask volume");
    require(!book.bestBid().has_value(), "modify side change clears bid side");
    require(book.bestAsk() == P(105), "modify side change sets best ask");
    require(book.restingOrderCount() == 1, "side modify keeps one resting order");
}

void testLobModifyUnknownOrderWithFullStateBecomesAdd()
{
    LimitOrderBook book{42};

    auto event = modify(1, Side::Bid, P(100), 10);
    attachDiagnosticMetadata(event, 200, 2, 3, 4);
    book.apply(event);

    require(book.volumeAt(Side::Bid, P(100)) == 10, "unknown full-state modify adds bid volume");
    require(book.bestBid() == P(100), "unknown full-state modify sets best bid");
    require(book.restingOrderCount() == 1, "unknown full-state modify creates resting order");
    require(book.skippedUnknownOrderCount() == 0, "unknown full-state modify is not skipped");
    require(
        book.unknownModifyRecoveredAsAddCount() == 1,
        "unknown full-state modify increments recovered-as-add count");
    require(book.unknownModifySkippedCount() == 0, "unknown full-state modify does not increment skipped modify count");
    require(book.unknownCancelSkippedCount() == 0, "unknown full-state modify does not increment skipped cancel count");

    const auto& diagnostics = book.unknownOrderDiagnostics();
    require(diagnostics.size() == 1, "unknown full-state modify records one diagnostic");
    require(diagnostics[0].operation == "modify", "unknown full-state modify diagnostic operation");
    require(diagnostics[0].decision == "recovered_as_add", "unknown full-state modify diagnostic decision");
    require(diagnostics[0].timestamp == 200, "unknown full-state modify diagnostic timestamp");
    require(diagnostics[0].instrument_id == 42, "unknown full-state modify diagnostic instrument");
    require(diagnostics[0].order_id == 1, "unknown full-state modify diagnostic order id");
    require(diagnostics[0].side == Side::Bid, "unknown full-state modify diagnostic side");
    require(diagnostics[0].price == P(100), "unknown full-state modify diagnostic price");
    require(diagnostics[0].size == 10, "unknown full-state modify diagnostic size");
    require(diagnostics[0].source_file_id == 2, "unknown full-state modify diagnostic source file");
    require(diagnostics[0].source_sequence == 3, "unknown full-state modify diagnostic source sequence");
    require(diagnostics[0].line_number == 4, "unknown full-state modify diagnostic line number");
}

void testLobModifyUnknownOrderWithoutFullStateIsSkipped()
{
    LimitOrderBook book{42};

    MarketDataEvent event;
    event.order_id = 77;
    event.action = Action::Modify;
    event.instrument_id = 42;
    event.side = Side::None;
    event.price = P(100);
    event.size = 10;
    attachDiagnosticMetadata(event, 300, 4, 5, 6);
    book.apply(event);

    require(!book.bestBid().has_value(), "unknown partial modify leaves bid side empty");
    require(!book.bestAsk().has_value(), "unknown partial modify leaves ask side empty");
    require(book.restingOrderCount() == 0, "unknown partial modify creates no resting order");
    require(book.skippedUnknownOrderCount() == 1, "unknown partial modify increments skipped count");
    require(book.unknownModifySkippedCount() == 1, "unknown partial modify increments explicit skipped modify count");
    require(book.unknownModifyRecoveredAsAddCount() == 0, "unknown partial modify is not recovered");
    require(book.unknownCancelSkippedCount() == 0, "unknown partial modify does not increment cancel skipped count");

    const auto& diagnostics = book.unknownOrderDiagnostics();
    require(diagnostics.size() == 1, "unknown partial modify records one diagnostic");
    require(diagnostics[0].operation == "modify", "unknown partial modify diagnostic operation");
    require(diagnostics[0].decision == "skipped", "unknown partial modify diagnostic decision");
    require(diagnostics[0].timestamp == 300, "unknown partial modify diagnostic timestamp");
    require(diagnostics[0].instrument_id == 42, "unknown partial modify diagnostic instrument");
    require(diagnostics[0].order_id == 77, "unknown partial modify diagnostic order id");
    require(diagnostics[0].side == Side::None, "unknown partial modify diagnostic side");
    require(diagnostics[0].price == P(100), "unknown partial modify diagnostic price");
    require(diagnostics[0].size == 10, "unknown partial modify diagnostic size");
    require(diagnostics[0].source_file_id == 4, "unknown partial modify diagnostic source file");
    require(diagnostics[0].source_sequence == 5, "unknown partial modify diagnostic source sequence");
    require(diagnostics[0].line_number == 6, "unknown partial modify diagnostic line number");
}

void testLobUnknownOrderDiagnosticsAreRateLimited()
{
    LimitOrderBook book{42};

    for (std::uint64_t order_id = 1; order_id <= 40; ++order_id)
    {
        auto event = cancel(order_id, 1);
        attachDiagnosticMetadata(event, order_id, 0, order_id, static_cast<std::size_t>(order_id));
        book.apply(event);
    }

    require(book.unknownCancelSkippedCount() == 40, "unknown cancel counter is not rate limited");
    require(book.unknownOrderDiagnostics().size() == 32, "unknown diagnostics samples are rate limited");
    require(book.unknownOrderDiagnostics().front().order_id == 1, "diagnostic samples keep first event");
    require(book.unknownOrderDiagnostics().back().order_id == 32, "diagnostic samples stop at sample limit");
}

void testLobBidAskViewsIterateWithoutCopy()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(2, Side::Bid, P(101), 5));
    book.apply(add(3, Side::Ask, P(105), 7));
    book.apply(add(4, Side::Ask, P(104), 3));

    const auto& bids = book.bidLevelsView();
    const auto& asks = book.askLevelsView();

    require(&bids == &book.bidLevelsView(), "bid view returns stable internal reference");
    require(&asks == &book.askLevelsView(), "ask view returns stable internal reference");
    require(bids.size() == 2, "bid view sees two levels");
    require(asks.size() == 2, "ask view sees two levels");

    auto bid_it = bids.begin();
    require(bid_it->first == P(101), "bid view iterates highest price first");
    require(bid_it->second == 5, "bid view first level volume");
    ++bid_it;
    require(bid_it->first == P(100), "bid view iterates next lower price");
    require(bid_it->second == 10, "bid view second level volume");

    auto ask_it = asks.begin();
    require(ask_it->first == P(104), "ask view iterates lowest price first");
    require(ask_it->second == 3, "ask view first level volume");
    ++ask_it;
    require(ask_it->first == P(105), "ask view iterates next higher price");
    require(ask_it->second == 7, "ask view second level volume");
}

void testLobClearEmptiesBook()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(add(2, Side::Ask, P(105), 7));
    book.apply(clearEvent());

    require(!book.bestBid().has_value(), "clear removes best bid");
    require(!book.bestAsk().has_value(), "clear removes best ask");
    require(book.volumeAt(Side::Bid, P(100)) == 0, "clear removes bid volume");
    require(book.volumeAt(Side::Ask, P(105)) == 0, "clear removes ask volume");
    require(book.restingOrderCount() == 0, "clear removes resting orders");
}

void testLobTradeIsExplicitNoop()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(trade(P(100), 5));

    require(book.volumeAt(Side::Bid, P(100)) == 10, "trade leaves bid volume unchanged");
    require(book.bestBid() == P(100), "trade leaves best bid unchanged");
    require(book.restingOrderCount() == 1, "trade leaves resting order count unchanged");
    require(book.tradeCount() == 1, "trade increments explicit noop count");
}

void testLobFillIsExplicitNoop()
{
    LimitOrderBook book{42};

    book.apply(add(1, Side::Bid, P(100), 10));
    book.apply(fill(1, 5));

    require(book.volumeAt(Side::Bid, P(100)) == 10, "fill leaves bid volume unchanged");
    require(book.bestBid() == P(100), "fill leaves best bid unchanged");
    require(book.restingOrderCount() == 1, "fill leaves resting order count unchanged");
    require(book.fillCount() == 1, "fill increments explicit noop count");
}

} // namespace md::test
