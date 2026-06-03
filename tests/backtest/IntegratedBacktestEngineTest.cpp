#include "TestSupport.hpp"

#include "backtest/IntegratedBacktestEngine.hpp"

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

MarketDataEvent addAsk(
    InstrumentId instrument_id,
    OrderId order_id,
    Price price,
    Quantity size,
    RawTimestampNs timestamp = xeur_base_timestamp + 100)
{
    return makeEvent(Action::Add, instrument_id, order_id, Side::Ask, price, size, timestamp);
}

MarketDataEvent addBid(
    InstrumentId instrument_id,
    OrderId order_id,
    Price price,
    Quantity size,
    RawTimestampNs timestamp = xeur_base_timestamp + 100)
{
    return makeEvent(Action::Add, instrument_id, order_id, Side::Bid, price, size, timestamp);
}

template <typename Message>
Message requireMessage(const MarketDataMessage& message, const std::string& case_name)
{
    require(std::holds_alternative<Message>(message), case_name + ": unexpected message type");
    return std::get<Message>(message);
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

} // namespace

void testIntegratedBacktestEngineSubscriberReceivesOnlyOwnInstrument()
{
    backtest::IntegratedBacktestEngine engine;
    std::vector<MarketDataMessage> received;
    engine.subscribe(42, [&received](const MarketDataMessage& message)
                     { received.push_back(message); });

    (void)engine.processEvent(addAsk(42, 1, 101, 10));
    (void)engine.processEvent(addAsk(7, 2, 201, 5));

    require(received.size() == 1, "single subscriber receives one instrument only");
    const auto update = requireMessage<BookUpdate>(received[0], "single subscriber update");
    require(update.instrument_id == 42, "single subscriber update instrument");
    requireLevel(engine.store().bestAsk(42), 101, 10, "single subscriber store instrument 42");
    requireLevel(engine.store().bestAsk(7), 201, 5, "single subscriber store instrument 7");
}

void testIntegratedBacktestEngineMultipleSubscribersReceiveSameMessage()
{
    backtest::IntegratedBacktestEngine engine;
    std::vector<MarketDataMessage> first;
    std::vector<MarketDataMessage> second;
    engine.subscribe(42, [&first](const MarketDataMessage& message)
                     { first.push_back(message); });
    engine.subscribe(42, [&second](const MarketDataMessage& message)
                     { second.push_back(message); });

    const auto result = engine.processEvent(addBid(42, 1, 100, 4));

    require(result.delivered_message_count == 2, "multiple subscribers delivery count");
    require(first.size() == 1, "first subscriber count");
    require(second.size() == 1, "second subscriber count");
    const auto first_update = requireMessage<BookUpdate>(first[0], "first subscriber update");
    const auto second_update = requireMessage<BookUpdate>(second[0], "second subscriber update");
    require(first_update.instrument_id == second_update.instrument_id, "subscriber instrument equality");
    require(first_update.seq_no == second_update.seq_no, "subscriber seq equality");
    require(first_update.price == second_update.price, "subscriber price equality");
    require(first_update.size == second_update.size, "subscriber size equality");
}

void testIntegratedBacktestEngineSeqNoIsPerInstrument()
{
    backtest::IntegratedBacktestEngine engine;
    std::vector<MarketDataMessage> instrument_42;
    std::vector<MarketDataMessage> instrument_7;
    engine.subscribe(42, [&instrument_42](const MarketDataMessage& message)
                     { instrument_42.push_back(message); });
    engine.subscribe(7, [&instrument_7](const MarketDataMessage& message)
                     { instrument_7.push_back(message); });

    (void)engine.processEvent(addBid(42, 1, 100, 4, xeur_base_timestamp + 100));
    (void)engine.processEvent(addBid(7, 2, 200, 5, xeur_base_timestamp + 200));
    (void)engine.processEvent(addAsk(42, 3, 101, 6, xeur_base_timestamp + 300));

    require(instrument_42.size() == 2, "instrument 42 message count");
    require(instrument_7.size() == 1, "instrument 7 message count");
    require(seqNo(instrument_42[0]) == 1, "instrument 42 first seq");
    require(seqNo(instrument_42[1]) == 2, "instrument 42 second seq");
    require(seqNo(instrument_7[0]) == 1, "instrument 7 first seq");
}

void testIntegratedBacktestEnginePublishesTradeCallbacks()
{
    backtest::IntegratedBacktestEngine engine(backtest::IntegratedBacktestConfig{
        .publish_book_updates = false,
        .publish_trades = true,
    });
    std::vector<MarketDataMessage> received;
    engine.subscribe(42, [&received](const MarketDataMessage& message)
                     { received.push_back(message); });

    const auto result = engine.processEvent(makeEvent(Action::Trade, 42, 0, Side::Bid, 106, 3));

    require(result.processed, "trade callback processed");
    require(result.published_message_count == 1, "trade callback publish count");
    require(result.delivered_message_count == 1, "trade callback delivery count");
    require(received.size() == 1, "trade callback received count");
    const auto trade = requireMessage<Trade>(received[0], "trade callback message");
    require(trade.instrument_id == 42, "trade callback instrument");
    require(trade.seq_no == 1, "trade callback seq");
    require(trade.price == 106, "trade callback price");
    require(trade.size == 3, "trade callback size");
}

void testIntegratedBacktestEngineInstrumentFilterSkipsUnconfiguredEvents()
{
    backtest::IntegratedBacktestEngine engine(backtest::IntegratedBacktestConfig{
        .instruments = {42},
    });
    std::vector<MarketDataMessage> received_42;
    std::vector<MarketDataMessage> received_7;
    engine.subscribe(42, [&received_42](const MarketDataMessage& message)
                     { received_42.push_back(message); });
    engine.subscribe(7, [&received_7](const MarketDataMessage& message)
                     { received_7.push_back(message); });

    const auto skipped = engine.processEvent(addAsk(7, 1, 201, 5, xeur_base_timestamp + 100));
    const auto accepted = engine.processEvent(addAsk(42, 2, 101, 10, xeur_base_timestamp + 200));

    require(!skipped.processed, "instrument filter skipped event");
    require(skipped.skipped_by_filter, "instrument filter skipped flag");
    require(accepted.processed, "instrument filter accepted event");
    require(received_7.empty(), "instrument filter no delivery to skipped instrument");
    require(received_42.size() == 1, "instrument filter delivery to configured instrument");
    require(!engine.store().bestAsk(7).has_value(), "instrument filter skipped store");
    requireLevel(engine.store().bestAsk(42), 101, 10, "instrument filter accepted store");
    require(engine.stats().skipped_by_filter_count == 1, "instrument filter stats");
}

void testIntegratedBacktestEngineSnapshotAppearsOnConfiguredInterval()
{
    backtest::IntegratedBacktestEngine engine(backtest::IntegratedBacktestConfig{
        .publish_book_updates = false,
        .publish_trades = false,
        .snapshot_depth = 2,
        .snapshot_interval_events = 2,
    });
    std::vector<MarketDataMessage> received;
    engine.subscribe(42, [&received](const MarketDataMessage& message)
                     { received.push_back(message); });

    (void)engine.processEvent(addAsk(42, 1, 105, 10, xeur_base_timestamp + 100));
    (void)engine.processEvent(addBid(42, 2, 100, 4, xeur_base_timestamp + 200));
    (void)engine.processEvent(addAsk(42, 3, 106, 7, xeur_base_timestamp + 300));
    (void)engine.processEvent(addBid(42, 4, 99, 3, xeur_base_timestamp + 400));

    require(received.size() == 2, "snapshot interval count");
    const auto first_snapshot = requireMessage<BookSnapshot>(received[0], "first interval snapshot");
    const auto second_snapshot = requireMessage<BookSnapshot>(received[1], "second interval snapshot");
    require(first_snapshot.seq_no == 1, "first snapshot seq");
    require(second_snapshot.seq_no == 2, "second snapshot seq");
    require(first_snapshot.bids.size() == 1, "first snapshot bid count");
    require(first_snapshot.asks.size() == 1, "first snapshot ask count");
    require(second_snapshot.bids.size() == 2, "second snapshot bid depth");
    require(second_snapshot.asks.size() == 2, "second snapshot ask depth");
}

void testIntegratedBacktestEngineMaxEventsStopsReplay()
{
    backtest::IntegratedBacktestEngine engine(backtest::IntegratedBacktestConfig{
        .max_events = 2,
    });
    std::vector<MarketDataMessage> received;
    engine.subscribe(42, [&received](const MarketDataMessage& message)
                     { received.push_back(message); });

    const auto first = engine.processEvent(addAsk(42, 1, 101, 10, xeur_base_timestamp + 100));
    const auto second = engine.processEvent(addAsk(42, 2, 102, 7, xeur_base_timestamp + 200));
    const auto third = engine.processEvent(addAsk(42, 3, 103, 5, xeur_base_timestamp + 300));

    require(first.processed, "max events first processed");
    require(second.processed, "max events second processed");
    require(!third.processed, "max events third not processed");
    require(third.skipped_by_max_events, "max events third skipped");
    require(received.size() == 2, "max events delivered count");
    const auto snapshot = engine.store().snapshot(42, 5);
    require(snapshot.asks.size() == 2, "max events resting ask count");
    const bool has_third_price = std::any_of(
        snapshot.asks.begin(),
        snapshot.asks.end(),
        [](const lob::BookLevel& level)
        {
            return level.price == 103;
        });
    require(!has_third_price, "max events third event not applied");
    require(engine.stats().accepted_event_count == 2, "max events accepted count");
    require(engine.stats().skipped_by_max_events_count == 1, "max events skipped count");
}

void testIntegratedBacktestEngineFinalLobMatchesDirectStore()
{
    std::vector<MarketDataEvent> events{
        addBid(42, 1, 100, 10, xeur_base_timestamp + 100),
        addAsk(42, 2, 105, 6, xeur_base_timestamp + 200),
        makeEvent(Action::Modify, 42, 1, Side::Bid, 101, 8, xeur_base_timestamp + 300),
        makeEvent(Action::Cancel, 42, 2, Side::Ask, 105, 2, xeur_base_timestamp + 400),
        makeEvent(Action::Trade, 42, 0, Side::Bid, 106, 3, xeur_base_timestamp + 500),
    };

    backtest::IntegratedBacktestEngine engine(backtest::IntegratedBacktestConfig{
        .publish_book_updates = false,
        .publish_trades = false,
    });
    (void)engine.run(events);

    lob::HistoricalLobStore direct_store;
    for (const MarketDataEvent& event : events)
    {
        direct_store.apply(event);
    }

    require(engine.store().stableStateDigest() == direct_store.stableStateDigest(), "engine final lob digest");
}

void testIntegratedBacktestEngineChronologicalViolationsRemainZeroForSortedInput()
{
    backtest::IntegratedBacktestEngine engine;

    (void)engine.run({
        addBid(42, 1, 100, 10, xeur_base_timestamp + 100),
        addAsk(42, 2, 105, 6, xeur_base_timestamp + 200),
        addBid(7, 3, 200, 4, xeur_base_timestamp + 300),
    });

    require(engine.stats().chronological_violations == 0, "sorted input chronological violations");
}

} // namespace md::test
