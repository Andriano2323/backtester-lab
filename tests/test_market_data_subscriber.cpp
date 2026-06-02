#include "messaging/MarketDataSubscriber.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
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

md::BookUpdate makeUpdate(md::SeqNo seq_no)
{
    return md::BookUpdate{
        .instrument_id = md::InstrumentId{42},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(seq_no)},
        .seq_no = seq_no,
        .side = md::Side::Bid,
        .price = md::Price{100'000'000'000LL + static_cast<md::Price>(seq_no)},
        .size = md::Quantity{10 + seq_no},
    };
}

md::BookSnapshot makeSnapshot(md::SeqNo seq_no)
{
    return md::BookSnapshot{
        .instrument_id = md::InstrumentId{43},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'001'000LL + static_cast<md::TimestampNs>(seq_no)},
        .seq_no = seq_no,
        .bids = {
            {.level_index = 0, .price = md::Price{99'000'000'000LL}, .size = md::Quantity{5}},
        },
        .asks = {
            {.level_index = 0, .price = md::Price{101'000'000'000LL}, .size = md::Quantity{6}},
        },
    };
}

md::Trade makeTrade(md::SeqNo seq_no)
{
    return md::Trade{
        .instrument_id = md::InstrumentId{44},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'002'000LL + static_cast<md::TimestampNs>(seq_no)},
        .seq_no = seq_no,
        .price = md::Price{100'500'000'000LL},
        .size = md::Quantity{7},
        .aggressor_side = md::Side::Ask,
    };
}

void testTryPopReceivesPushedFlushedBookUpdate()
{
    md::MarketDataSubscriber subscriber([](const md::MarketDataMessage&) {}, 2);

    subscriber.push(makeUpdate(md::SeqNo{1}));
    subscriber.flush();

    const std::optional<md::MarketDataMessage> message = subscriber.tryPop();
    require(message.has_value(), "subscriber tryPop receives flushed message");
    require(md::messageType(*message) == md::MarketDataMessageType::BookUpdate, "subscriber tryPop receives book update");
    require(md::instrumentId(*message) == 42, "subscriber tryPop book update instrument");
    require(md::seqNo(*message) == 1, "subscriber tryPop book update seq_no");
}

void testDrainAvailableInvokesCallbackOncePerAvailableMessage()
{
    std::vector<md::SeqNo> seen_seq_nos;
    md::MarketDataSubscriber subscriber(
        [&seen_seq_nos](const md::MarketDataMessage& message)
        {
            seen_seq_nos.push_back(md::seqNo(message));
        },
        4);

    subscriber.push(makeUpdate(md::SeqNo{1}));
    subscriber.push(makeUpdate(md::SeqNo{2}));
    subscriber.push(makeUpdate(md::SeqNo{3}));
    subscriber.flush();

    require(subscriber.drainAvailable() == 3, "drainAvailable returns drained message count");
    require(seen_seq_nos.size() == 3, "callback called once per drained message");
    require(seen_seq_nos[0] == 1 && seen_seq_nos[1] == 2 && seen_seq_nos[2] == 3, "drainAvailable callback order");
}

void testDrainAvailableReturnsZeroWithoutFlushedMessages()
{
    std::size_t callback_count = 0;
    md::MarketDataSubscriber subscriber(
        [&callback_count](const md::MarketDataMessage&)
        {
            ++callback_count;
        },
        8);

    subscriber.push(makeUpdate(md::SeqNo{1}));

    require(subscriber.drainAvailable() == 0, "drainAvailable returns zero without flushed messages");
    require(callback_count == 0, "callback is not called for unflushed messages");
}

void testFifoOrderIsPreservedForHundredMessages()
{
    std::vector<md::SeqNo> seen_seq_nos;
    md::MarketDataSubscriber subscriber(
        [&seen_seq_nos](const md::MarketDataMessage& message)
        {
            seen_seq_nos.push_back(md::seqNo(message));
        },
        7);

    for (md::SeqNo seq_no = 1; seq_no <= 100; ++seq_no)
    {
        subscriber.push(makeUpdate(seq_no));
    }
    subscriber.flush();

    require(subscriber.drainAvailable() == 100, "drainAvailable drains 100 messages");
    require(seen_seq_nos.size() == 100, "callback receives 100 messages");
    for (md::SeqNo seq_no = 1; seq_no <= 100; ++seq_no)
    {
        require(seen_seq_nos[seq_no - 1] == seq_no, "subscriber preserves FIFO order");
    }
}

void testBlockingPopWorksWithProducerThread()
{
    md::MarketDataSubscriber subscriber([](const md::MarketDataMessage&) {}, 4);

    std::thread producer(
        [&subscriber]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            subscriber.push(makeUpdate(md::SeqNo{55}));
            subscriber.flush();
        });

    const md::MarketDataMessage message = subscriber.pop();
    producer.join();

    require(md::messageType(message) == md::MarketDataMessageType::BookUpdate, "blocking pop receives book update");
    require(md::seqNo(message) == 55, "blocking pop receives producer message");
}

void testSubscriberHandlesAllMessageTypes()
{
    std::vector<md::MarketDataMessageType> seen_types;
    md::MarketDataSubscriber subscriber(
        [&seen_types](const md::MarketDataMessage& message)
        {
            seen_types.push_back(md::messageType(message));
        },
        3);

    subscriber.push(makeUpdate(md::SeqNo{1}));
    subscriber.push(makeSnapshot(md::SeqNo{2}));
    subscriber.push(makeTrade(md::SeqNo{3}));
    subscriber.flush();

    require(subscriber.drainAvailable() == 3, "subscriber drains all message types");
    require(seen_types.size() == 3, "callback receives all message types");
    require(seen_types[0] == md::MarketDataMessageType::BookUpdate, "subscriber handles book update");
    require(seen_types[1] == md::MarketDataMessageType::BookSnapshot, "subscriber handles book snapshot");
    require(seen_types[2] == md::MarketDataMessageType::Trade, "subscriber handles trade");
}

} // namespace

int main()
{
    try
    {
        testTryPopReceivesPushedFlushedBookUpdate();
        testDrainAvailableInvokesCallbackOncePerAvailableMessage();
        testDrainAvailableReturnsZeroWithoutFlushedMessages();
        testFifoOrderIsPreservedForHundredMessages();
        testBlockingPopWorksWithProducerThread();
        testSubscriberHandlesAllMessageTypes();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
