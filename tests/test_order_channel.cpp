#include "gateway/OrderChannel.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

md::OrderFields makeFields(md::OrderId order_id, md::OrderStatus status = md::OrderStatus::New)
{
    return md::OrderFields{
        .trading_engine_id = md::TradingEngineId{7},
        .order_id = order_id,
        .instrument_id = md::InstrumentId{42},
        .side = md::Side::Bid,
        .price = md::Price{101'250'000'000LL + static_cast<md::Price>(order_id)},
        .size = md::Quantity{10},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(order_id)},
        .status = status,
    };
}

md::OrderRequest makeNewOrder(md::OrderId order_id)
{
    return md::NewOrder{.fields = makeFields(order_id, md::OrderStatus::New)};
}

md::OrderEvent makeAck(md::OrderId order_id)
{
    return md::OrderAck{
        .fields = makeFields(order_id, md::OrderStatus::Accepted),
        .ack_type = md::OrderAckType::NewAccepted,
    };
}

void testEmptyTryPopReturnsNullopt()
{
    md::OrderChannel channel;

    require(!channel.tryPopRequest().has_value(), "empty request queue returns nullopt");
    require(!channel.tryPopEvent().has_value(), "empty event queue returns nullopt");
}

void testPushRequestFlushMakesNewOrderVisible()
{
    md::OrderChannel channel(4, 4);

    channel.pushRequest(makeNewOrder(md::OrderId{1}));
    channel.flushRequests();

    const std::optional<md::OrderRequest> request = channel.tryPopRequest();
    require(request.has_value(), "flushed request is visible");
    require(md::messageType(*request) == md::OrderMessageType::NewOrder, "flushed request type");
    require(md::orderId(*request) == 1, "flushed request order id");
}

void testPushEventFlushMakesOrderAckVisible()
{
    md::OrderChannel channel(4, 4);

    channel.pushEvent(makeAck(md::OrderId{1}));
    channel.flushEvents();

    const std::optional<md::OrderEvent> event = channel.tryPopEvent();
    require(event.has_value(), "flushed event is visible");
    require(md::messageType(*event) == md::OrderMessageType::OrderAck, "flushed event type");
    require(md::orderId(*event) == 1, "flushed event order id");
}

void testRequestFifoOrderForThousandMessages()
{
    md::OrderChannel channel(17, 8);

    for (md::OrderId order_id = 1; order_id <= 1000; ++order_id)
    {
        channel.pushRequest(makeNewOrder(order_id));
    }
    channel.flushRequests();

    for (md::OrderId expected_order_id = 1; expected_order_id <= 1000; ++expected_order_id)
    {
        const std::optional<md::OrderRequest> request = channel.tryPopRequest();
        require(request.has_value(), "request FIFO message available");
        require(md::orderId(*request) == expected_order_id, "request FIFO order");
    }
    require(!channel.tryPopRequest().has_value(), "request queue drains");
}

void testEventFifoOrderForThousandMessages()
{
    md::OrderChannel channel(8, 19);

    for (md::OrderId order_id = 1; order_id <= 1000; ++order_id)
    {
        channel.pushEvent(makeAck(order_id));
    }
    channel.flushEvents();

    for (md::OrderId expected_order_id = 1; expected_order_id <= 1000; ++expected_order_id)
    {
        const std::optional<md::OrderEvent> event = channel.tryPopEvent();
        require(event.has_value(), "event FIFO message available");
        require(md::orderId(*event) == expected_order_id, "event FIFO order");
    }
    require(!channel.tryPopEvent().has_value(), "event queue drains");
}

void testDirectionsAreIndependent()
{
    md::OrderChannel channel(4, 4);

    channel.pushRequest(makeNewOrder(md::OrderId{10}));
    channel.flushRequests();

    require(channel.tryPopRequest().has_value(), "request direction has message");
    require(!channel.tryPopEvent().has_value(), "event direction remains empty");

    channel.pushEvent(makeAck(md::OrderId{20}));
    channel.flushEvents();

    require(channel.tryPopEvent().has_value(), "event direction has message");
    require(!channel.tryPopRequest().has_value(), "request direction remains drained");
}

void testBlockingPopRequestWorksWithProducerThread()
{
    md::OrderChannel channel(4, 4);

    std::thread producer(
        [&channel]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            channel.pushRequest(makeNewOrder(md::OrderId{55}));
            channel.flushRequests();
        });

    const md::OrderRequest request = channel.popRequest();
    producer.join();

    require(md::messageType(request) == md::OrderMessageType::NewOrder, "blocking request type");
    require(md::orderId(request) == 55, "blocking request order id");
}

void testBlockingPopEventWorksWithProducerThread()
{
    md::OrderChannel channel(4, 4);

    std::thread producer(
        [&channel]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            channel.pushEvent(makeAck(md::OrderId{66}));
            channel.flushEvents();
        });

    const md::OrderEvent event = channel.popEvent();
    producer.join();

    require(md::messageType(event) == md::OrderMessageType::OrderAck, "blocking event type");
    require(md::orderId(event) == 66, "blocking event order id");
}

void testBatchVisibilityRequiresFlushOrThreshold()
{
    md::OrderChannel channel(2, 2);

    channel.pushRequest(makeNewOrder(md::OrderId{1}));
    require(!channel.tryPopRequest().has_value(), "single unflushed request is hidden");
    channel.pushRequest(makeNewOrder(md::OrderId{2}));
    std::optional<md::OrderRequest> request = channel.tryPopRequest();
    require(request.has_value(), "request batch threshold exposes first request");
    require(md::orderId(*request) == 1, "request batch threshold first request order id");
    request = channel.tryPopRequest();
    require(request.has_value(), "request batch threshold exposes second request");
    require(md::orderId(*request) == 2, "request batch threshold second request order id");

    channel.pushEvent(makeAck(md::OrderId{10}));
    require(!channel.tryPopEvent().has_value(), "single unflushed event is hidden");
    channel.pushEvent(makeAck(md::OrderId{11}));
    std::optional<md::OrderEvent> event = channel.tryPopEvent();
    require(event.has_value(), "event batch threshold exposes first event");
    require(md::orderId(*event) == 10, "event batch threshold first event order id");
    event = channel.tryPopEvent();
    require(event.has_value(), "event batch threshold exposes second event");
    require(md::orderId(*event) == 11, "event batch threshold second event order id");

    channel.pushRequest(makeNewOrder(md::OrderId{3}));
    channel.pushEvent(makeAck(md::OrderId{12}));
    require(!channel.tryPopRequest().has_value(), "second partial request batch is hidden");
    require(!channel.tryPopEvent().has_value(), "second partial event batch is hidden");

    channel.flushRequests();
    channel.flushEvents();

    request = channel.tryPopRequest();
    event = channel.tryPopEvent();

    require(request.has_value(), "flush exposes partial request batch");
    require(event.has_value(), "flush exposes partial event batch");
    require(md::orderId(*request) == 3, "flush partial request order id");
    require(md::orderId(*event) == 12, "flush partial event order id");
}

} // namespace

int main()
{
    try
    {
        testEmptyTryPopReturnsNullopt();
        testPushRequestFlushMakesNewOrderVisible();
        testPushEventFlushMakesOrderAckVisible();
        testRequestFifoOrderForThousandMessages();
        testEventFifoOrderForThousandMessages();
        testDirectionsAreIndependent();
        testBlockingPopRequestWorksWithProducerThread();
        testBlockingPopEventWorksWithProducerThread();
        testBatchVisibilityRequiresFlushOrThreshold();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
