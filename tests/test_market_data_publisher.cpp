#include "messaging/MarketDataPublisher.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

md::BookUpdate makeUpdate(md::InstrumentId instrument_id, md::Price price = md::Price{100'000'000'000LL})
{
    return md::BookUpdate{
        .instrument_id = instrument_id,
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL},
        .seq_no = md::SeqNo{999},
        .side = md::Side::Bid,
        .price = price,
        .size = md::Quantity{10},
    };
}

md::BookSnapshot makeSnapshot(md::InstrumentId instrument_id)
{
    return md::BookSnapshot{
        .instrument_id = instrument_id,
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'001'000LL},
        .seq_no = md::SeqNo{999},
        .bids = {
            {.level_index = 0, .price = md::Price{99'000'000'000LL}, .size = md::Quantity{5}},
        },
        .asks = {
            {.level_index = 0, .price = md::Price{101'000'000'000LL}, .size = md::Quantity{6}},
        },
    };
}

md::Trade makeTrade(md::InstrumentId instrument_id)
{
    return md::Trade{
        .instrument_id = instrument_id,
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'002'000LL},
        .seq_no = md::SeqNo{999},
        .price = md::Price{100'500'000'000LL},
        .size = md::Quantity{7},
        .aggressor_side = md::Side::Ask,
    };
}

void testSubscribeReturnsUniqueIdsAndCountsSubscribers()
{
    md::MarketDataPublisher publisher;

    const auto first = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {});
    const auto second = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {});
    const auto third = publisher.subscribe(md::InstrumentId{20}, [](const md::MarketDataMessage&) {});

    require(first.subscriber_id != second.subscriber_id, "first and second subscriber ids are unique");
    require(first.subscriber_id != third.subscriber_id, "first and third subscriber ids are unique");
    require(second.subscriber_id != third.subscriber_id, "second and third subscriber ids are unique");
    require(publisher.subscriberCount() == 3, "subscriberCount increments");
    require(publisher.subscriberCount(md::InstrumentId{10}) == 2, "subscriberCount filters instrument 10");
    require(publisher.subscriberCount(md::InstrumentId{20}) == 1, "subscriberCount filters instrument 20");
    require(publisher.subscriberCount(md::InstrumentId{30}) == 0, "subscriberCount returns zero for missing instrument");
}

void testPublishUpdateDeliversToMatchingSubscriber()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);

    const md::SeqNo seq_no = publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    publisher.flush();

    const auto message = subscription.subscriber->tryPop();
    require(seq_no == 1, "publishUpdate returns assigned seq_no");
    require(message.has_value(), "publishUpdate delivers to matching subscriber");
    require(md::messageType(*message) == md::MarketDataMessageType::BookUpdate, "publishUpdate delivers book update");
    require(md::instrumentId(*message) == 10, "publishUpdate delivered instrument");
    require(md::seqNo(*message) == 1, "publishUpdate assigns seq_no in payload");
}

void testPublishUpdateDoesNotDeliverToOtherInstrument()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{20}, [](const md::MarketDataMessage&) {}, 8);

    publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    publisher.flush();

    require(!subscription.subscriber->tryPop().has_value(), "subscriber for another instrument receives nothing");
}

void testTwoSubscribersReceiveCopiesOfSameUpdate()
{
    md::MarketDataPublisher publisher;
    const auto first = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);
    const auto second = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);

    publisher.publishUpdate(makeUpdate(md::InstrumentId{10}, md::Price{101'000'000'000LL}));
    publisher.flush();

    const auto first_message = first.subscriber->tryPop();
    const auto second_message = second.subscriber->tryPop();
    require(first_message.has_value(), "first subscriber receives update");
    require(second_message.has_value(), "second subscriber receives update");
    require(md::seqNo(*first_message) == 1, "first subscriber receives assigned seq_no");
    require(md::seqNo(*second_message) == 1, "second subscriber receives assigned seq_no");
    require(std::get<md::BookUpdate>(*first_message).price == md::Price{101'000'000'000LL}, "first subscriber receives update copy");
    require(std::get<md::BookUpdate>(*second_message).price == md::Price{101'000'000'000LL}, "second subscriber receives update copy");
}

void testFifoOrderIsPreservedAcrossManyPublishes()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 17);

    constexpr std::size_t message_count = 100;
    for (std::size_t i = 0; i < message_count; ++i)
    {
        publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    }
    publisher.flush();

    for (md::SeqNo expected_seq_no = 1; expected_seq_no <= message_count; ++expected_seq_no)
    {
        const auto message = subscription.subscriber->tryPop();
        require(message.has_value(), "FIFO message is available");
        require(md::seqNo(*message) == expected_seq_no, "publisher preserves FIFO seq_no order");
    }
    require(!subscription.subscriber->tryPop().has_value(), "subscriber queue drains after FIFO test");
}

void testSeqNoIsAssignedByPublisherPerInstrument()
{
    md::MarketDataPublisher publisher;
    const auto first_instrument = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);
    const auto second_instrument = publisher.subscribe(md::InstrumentId{20}, [](const md::MarketDataMessage&) {}, 8);

    const md::SeqNo first_seq = publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    const md::SeqNo second_seq = publisher.publishUpdate(makeUpdate(md::InstrumentId{20}));
    const md::SeqNo third_seq = publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    publisher.flush();

    require(first_seq == 1, "instrument 10 first seq_no");
    require(second_seq == 1, "instrument 20 first seq_no");
    require(third_seq == 2, "instrument 10 second seq_no");

    const auto first_message = first_instrument.subscriber->tryPop();
    const auto third_message = first_instrument.subscriber->tryPop();
    const auto second_message = second_instrument.subscriber->tryPop();
    require(first_message.has_value() && md::seqNo(*first_message) == 1, "instrument 10 subscriber receives seq 1");
    require(third_message.has_value() && md::seqNo(*third_message) == 2, "instrument 10 subscriber receives seq 2");
    require(second_message.has_value() && md::seqNo(*second_message) == 1, "instrument 20 subscriber receives seq 1");
}

void testPublishSnapshotDeliversBookSnapshot()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);

    const md::SeqNo seq_no = publisher.publishSnapshot(makeSnapshot(md::InstrumentId{10}));
    publisher.flush();

    const auto message = subscription.subscriber->tryPop();
    require(seq_no == 1, "publishSnapshot returns assigned seq_no");
    require(message.has_value(), "publishSnapshot delivers message");
    require(md::messageType(*message) == md::MarketDataMessageType::BookSnapshot, "publishSnapshot delivers snapshot");
    const auto& snapshot = std::get<md::BookSnapshot>(*message);
    require(snapshot.seq_no == 1, "snapshot payload seq_no");
    require(snapshot.bids.size() == 1 && snapshot.bids[0].price == md::Price{99'000'000'000LL}, "snapshot bid levels delivered");
    require(snapshot.asks.size() == 1 && snapshot.asks[0].price == md::Price{101'000'000'000LL}, "snapshot ask levels delivered");
}

void testPublishTradeDeliversTrade()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);

    const md::SeqNo seq_no = publisher.publishTrade(makeTrade(md::InstrumentId{10}));
    publisher.flush();

    const auto message = subscription.subscriber->tryPop();
    require(seq_no == 1, "publishTrade returns assigned seq_no");
    require(message.has_value(), "publishTrade delivers message");
    require(md::messageType(*message) == md::MarketDataMessageType::Trade, "publishTrade delivers trade");
    const auto& trade = std::get<md::Trade>(*message);
    require(trade.seq_no == 1, "trade payload seq_no");
    require(trade.price == md::Price{100'500'000'000LL}, "trade price delivered");
    require(trade.size == md::Quantity{7}, "trade size delivered");
    require(trade.aggressor_side == md::Side::Ask, "trade aggressor side delivered");
}

void testFlushMakesManuallyBatchedMessagesVisible()
{
    md::MarketDataPublisher publisher;
    const auto subscription = publisher.subscribe(md::InstrumentId{10}, [](const md::MarketDataMessage&) {}, 8);

    publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    require(!subscription.subscriber->tryPop().has_value(), "message is hidden before publisher flush");

    publisher.flush();
    require(subscription.subscriber->tryPop().has_value(), "publisher flush makes message visible");
}

void testConcurrentSubscriberSmoke()
{
    md::MarketDataPublisher publisher;

    constexpr std::size_t subscriber_count = 4;
    constexpr md::SeqNo message_count = 50;

    std::vector<std::vector<md::SeqNo>> received(subscriber_count);
    std::vector<md::MarketDataSubscription> subscriptions;
    subscriptions.reserve(subscriber_count);

    for (std::size_t i = 0; i < subscriber_count; ++i)
    {
        subscriptions.push_back(publisher.subscribe(
            md::InstrumentId{10},
            [&received, i](const md::MarketDataMessage& message)
            {
                received[i].push_back(md::seqNo(message));
            },
            256));
    }

    std::vector<std::thread> threads;
    threads.reserve(subscriber_count);
    for (std::size_t i = 0; i < subscriber_count; ++i)
    {
        const auto subscriber = subscriptions[i].subscriber;
        threads.emplace_back(
            [subscriber, &received, i]
            {
                while (received[i].size() < message_count)
                {
                    if (subscriber->drainAvailable() == 0)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            });
    }

    for (md::SeqNo seq_no = 1; seq_no <= message_count; ++seq_no)
    {
        (void)seq_no;
        publisher.publishUpdate(makeUpdate(md::InstrumentId{10}));
    }
    publisher.flush();

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    for (std::size_t subscriber_index = 0; subscriber_index < subscriber_count; ++subscriber_index)
    {
        require(received[subscriber_index].size() == message_count, "concurrent subscriber receives all messages");
        for (md::SeqNo seq_no = 1; seq_no <= message_count; ++seq_no)
        {
            require(received[subscriber_index][seq_no - 1] == seq_no, "concurrent subscriber preserves FIFO order");
        }
    }
}

} // namespace

int main()
{
    try
    {
        testSubscribeReturnsUniqueIdsAndCountsSubscribers();
        testPublishUpdateDeliversToMatchingSubscriber();
        testPublishUpdateDoesNotDeliverToOtherInstrument();
        testTwoSubscribersReceiveCopiesOfSameUpdate();
        testFifoOrderIsPreservedAcrossManyPublishes();
        testSeqNoIsAssignedByPublisherPerInstrument();
        testPublishSnapshotDeliversBookSnapshot();
        testPublishTradeDeliversTrade();
        testFlushMakesManuallyBatchedMessagesVisible();
        testConcurrentSubscriberSmoke();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
