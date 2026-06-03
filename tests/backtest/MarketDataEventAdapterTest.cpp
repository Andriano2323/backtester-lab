#include "TestSupport.hpp"

#include "backtest/MarketDataEventAdapter.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace md::test
{
namespace
{

MarketDataEvent makeEvent(
    Action action,
    InstrumentId instrument_id,
    OrderId order_id,
    Side side,
    Price price,
    Quantity size,
    RawTimestampNs timestamp = xeur_base_timestamp + 100)
{
    MarketDataEvent event;
    event.timestamp = timestamp;
    event.ts_recv = timestamp;
    event.ts_event = timestamp;
    event.instrument_id = instrument_id;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = action;
    return event;
}

MarketDataEvent addAsk(OrderId order_id, Price price, Quantity size, RawTimestampNs timestamp = xeur_base_timestamp + 100)
{
    return makeEvent(Action::Add, 42, order_id, Side::Ask, price, size, timestamp);
}

MarketDataEvent addBid(OrderId order_id, Price price, Quantity size, RawTimestampNs timestamp = xeur_base_timestamp + 100)
{
    return makeEvent(Action::Add, 42, order_id, Side::Bid, price, size, timestamp);
}

void requireLevel(
    const std::optional<lob::BookLevel>& level,
    Price price,
    Quantity size,
    const std::string& case_name)
{
    require(level.has_value(), case_name + ": missing level");
    require(level->price == price, case_name + ": unexpected price");
    require(level->size == size, case_name + ": unexpected size");
}

template <typename Message>
Message requireMessage(
    const backtest::MarketDataEventAdapterResult& result,
    std::size_t index,
    const std::string& case_name)
{
    require(index < result.messages.size(), case_name + ": missing message");
    require(std::holds_alternative<Message>(result.messages[index]), case_name + ": unexpected message type");
    return std::get<Message>(result.messages[index]);
}

void discard(const backtest::MarketDataEventAdapterResult&) {}

} // namespace

void testMarketDataEventAdapterAddAskUpdatesBestAsk()
{
    backtest::MarketDataEventAdapter adapter;

    const auto result = adapter.process(addAsk(1, 101, 10));

    require(result.accepted, "add ask accepted");
    require(result.applied_to_lob, "add ask applies to lob");
    requireLevel(adapter.store().bestAsk(42), 101, 10, "add ask best ask");
    const auto update = requireMessage<BookUpdate>(result, 0, "add ask update");
    require(update.instrument_id == 42, "add ask update instrument");
    require(update.side == Side::Ask, "add ask update side");
    require(update.price == 101, "add ask update price");
    require(update.size == 10, "add ask update size");
}

void testMarketDataEventAdapterAddBidUpdatesBestBid()
{
    backtest::MarketDataEventAdapter adapter;

    const auto result = adapter.process(addBid(1, 100, 8));

    require(result.accepted, "add bid accepted");
    require(result.applied_to_lob, "add bid applies to lob");
    requireLevel(adapter.store().bestBid(42), 100, 8, "add bid best bid");
    const auto update = requireMessage<BookUpdate>(result, 0, "add bid update");
    require(update.instrument_id == 42, "add bid update instrument");
    require(update.side == Side::Bid, "add bid update side");
    require(update.price == 100, "add bid update price");
    require(update.size == 8, "add bid update size");
}

void testMarketDataEventAdapterModifyChangesPriceAndSize()
{
    backtest::MarketDataEventAdapter adapter;
    discard(adapter.process(addBid(1, 100, 10)));

    const auto result = adapter.process(makeEvent(Action::Modify, 42, 1, Side::Bid, 101, 7));

    require(result.accepted, "modify accepted");
    require(result.applied_to_lob, "modify applies to lob");
    requireLevel(adapter.store().bestBid(42), 101, 7, "modify best bid");
    const auto snapshot = adapter.store().snapshot(42, 5);
    require(snapshot.bids.size() == 1, "modify removes old bid level");
    require(snapshot.bids[0].price == 101, "modify snapshot price");
    require(snapshot.bids[0].size == 7, "modify snapshot size");
}

void testMarketDataEventAdapterCancelReducesAndRemovesOrder()
{
    backtest::MarketDataEventAdapter adapter;
    discard(adapter.process(addAsk(1, 101, 10)));

    discard(adapter.process(makeEvent(Action::Cancel, 42, 1, Side::Ask, 101, 4)));
    requireLevel(adapter.store().bestAsk(42), 101, 6, "partial cancel best ask");

    discard(adapter.process(makeEvent(Action::Cancel, 42, 1, Side::Ask, 101, 0)));
    require(!adapter.store().bestAsk(42).has_value(), "full cancel removes ask");
}

void testMarketDataEventAdapterClearClearsOneInstrument()
{
    backtest::MarketDataEventAdapter adapter;
    discard(adapter.process(addAsk(1, 101, 10)));
    discard(adapter.process(makeEvent(Action::Add, 7, 2, Side::Bid, 99, 5)));

    const auto result = adapter.process(makeEvent(Action::Clear, 42, 0, Side::None, undefined_price, 0));

    require(result.accepted, "clear accepted");
    require(result.applied_to_lob, "clear applies to lob");
    require(!adapter.store().bestAsk(42).has_value(), "clear removes instrument ask");
    requireLevel(adapter.store().bestBid(7), 99, 5, "clear keeps other instrument");
}

void testMarketDataEventAdapterTradePublishesTradeWithoutMutatingLob()
{
    backtest::MarketDataEventAdapter adapter(backtest::MarketDataEventAdapterConfig{
        .publish_book_updates = false,
    });
    discard(adapter.process(addAsk(1, 101, 10)));

    const auto result = adapter.process(makeEvent(Action::Trade, 42, 0, Side::Bid, 102, 3));

    require(result.accepted, "trade accepted");
    require(!result.applied_to_lob, "trade does not apply to lob");
    require(result.published_trade, "trade published");
    requireLevel(adapter.store().bestAsk(42), 101, 10, "trade leaves ask");
    const auto trade = requireMessage<Trade>(result, 0, "trade message");
    require(trade.instrument_id == 42, "trade instrument");
    require(trade.price == 102, "trade price");
    require(trade.size == 3, "trade size");
    require(trade.aggressor_side == Side::Bid, "trade side");
}

void testMarketDataEventAdapterNonePublishesNothing()
{
    backtest::MarketDataEventAdapter adapter(backtest::MarketDataEventAdapterConfig{
        .snapshot_interval_events = 1,
    });

    const auto result = adapter.process(makeEvent(Action::None, 42, 0, Side::None, undefined_price, 0));

    require(result.accepted, "none accepted");
    require(!result.applied_to_lob, "none does not apply to lob");
    require(result.messages.empty(), "none publishes nothing");
}

void testMarketDataEventAdapterPublishesSnapshotAfterInterval()
{
    backtest::MarketDataEventAdapter adapter(backtest::MarketDataEventAdapterConfig{
        .publish_book_updates = false,
        .publish_trades = false,
        .snapshot_depth = 2,
        .snapshot_interval_events = 3,
    });

    require(adapter.process(addAsk(1, 105, 10)).messages.empty(), "first event no snapshot");
    require(adapter.process(addAsk(2, 106, 7)).messages.empty(), "second event no snapshot");
    const auto result = adapter.process(addBid(3, 100, 5));

    require(result.published_snapshot, "third event publishes snapshot");
    const auto snapshot = requireMessage<BookSnapshot>(result, 0, "interval snapshot");
    require(snapshot.instrument_id == 42, "snapshot instrument");
    require(snapshot.bids.size() == 1, "snapshot bid count");
    require(snapshot.bids[0].price == 100, "snapshot bid price");
    require(snapshot.bids[0].size == 5, "snapshot bid size");
    require(snapshot.asks.size() == 2, "snapshot ask depth");
    require(snapshot.asks[0].price == 105, "snapshot best ask");
    require(snapshot.asks[1].price == 106, "snapshot second ask");
}

void testMarketDataEventAdapterFiltersInstruments()
{
    backtest::MarketDataEventAdapter adapter(backtest::MarketDataEventAdapterConfig{
        .instruments = {42},
    });

    const auto ignored = adapter.process(makeEvent(Action::Add, 7, 1, Side::Ask, 101, 10));
    const auto accepted = adapter.process(addAsk(2, 102, 4));

    require(!ignored.accepted, "filtered instrument ignored");
    require(ignored.messages.empty(), "filtered instrument no messages");
    require(accepted.accepted, "configured instrument accepted");
    require(!adapter.store().bestAsk(7).has_value(), "filtered instrument not in store");
    requireLevel(adapter.store().bestAsk(42), 102, 4, "configured instrument in store");
}

void testMarketDataEventAdapterEqualTimestampsUseSourceTieBreaker()
{
    MarketDataEvent add = addBid(1, 100, 5, xeur_base_timestamp + 100);
    add.source_file_id = 0;
    add.source_sequence = 1;

    MarketDataEvent modify = makeEvent(Action::Modify, 42, 1, Side::Bid, 101, 7, xeur_base_timestamp + 100);
    modify.source_file_id = 1;
    modify.source_sequence = 1;

    std::vector<MarketDataEvent> events{modify, add};
    std::sort(events.begin(), events.end(), eventComesBefore);

    backtest::MarketDataEventAdapter adapter;
    for (const auto& event : events)
    {
        discard(adapter.process(event));
    }

    require(events[0].action == Action::Add, "equal timestamp tie-breaker first event");
    require(events[1].action == Action::Modify, "equal timestamp tie-breaker second event");
    requireLevel(adapter.store().bestBid(42), 101, 7, "equal timestamp deterministic final state");
}

} // namespace md::test
