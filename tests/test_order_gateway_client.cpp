#include "gateway/OrderGatewayClient.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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

md::OrderFields makeFields(md::OrderId order_id, md::OrderStatus status)
{
    return md::OrderFields{
        .trading_engine_id = md::TradingEngineId{7},
        .order_id = order_id,
        .instrument_id = md::InstrumentId{42},
        .side = md::Side::Bid,
        .price = md::Price{101'250'000'000LL},
        .size = md::Quantity{10},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(order_id)},
        .status = status,
    };
}

md::OrderAck makeAck(md::OrderId order_id)
{
    return md::OrderAck{
        .fields = makeFields(order_id, md::OrderStatus::Accepted),
        .ack_type = md::OrderAckType::NewAccepted,
    };
}

md::OrderFill makeFill(md::OrderId order_id)
{
    return md::OrderFill{
        .fields = makeFields(order_id, md::OrderStatus::PartiallyFilled),
        .fill_price = md::Price{101'300'000'000LL},
        .fill_size = md::Quantity{4},
        .remaining_size = md::Quantity{6},
    };
}

md::OrderReject makeReject(md::OrderId order_id)
{
    return md::OrderReject{
        .fields = makeFields(order_id, md::OrderStatus::Rejected),
        .reason = md::OrderRejectReason::InvalidPrice,
        .text = "invalid price",
    };
}

std::shared_ptr<md::OrderChannel> makeChannel()
{
    return std::make_shared<md::OrderChannel>(8, 8);
}

void testSendOrderProducesNewOrderRequest()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    const md::OrderId order_id = client.sendOrder(
        md::InstrumentId{42},
        md::Side::Bid,
        md::Price{101'250'000'000LL},
        md::Quantity{10},
        md::TimestampNs{1'700'000'000'000'000'001LL});
    client.flush();

    const std::optional<md::OrderRequest> request = channel->tryPopRequest();
    require(request.has_value(), "sendOrder request is visible after flush");
    require(md::messageType(*request) == md::OrderMessageType::NewOrder, "sendOrder request type");
    require(md::tradingEngineId(*request) == 7, "sendOrder trading engine id");
    require(md::orderId(*request) == order_id, "sendOrder generated order id");
    require(md::instrumentId(*request) == 42, "sendOrder instrument id");
    require(md::fields(*request).side == md::Side::Bid, "sendOrder side");
    require(md::fields(*request).price == 101'250'000'000LL, "sendOrder price");
    require(md::fields(*request).size == 10, "sendOrder size");
    require(md::timestampNs(*request) == 1'700'000'000'000'000'001LL, "sendOrder timestamp");
    require(md::status(*request) == md::OrderStatus::New, "sendOrder status");
}

void testSendOrderAutoGeneratesDistinctOrderIds()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    const md::OrderId first = client.sendOrder(md::InstrumentId{42}, md::Side::Bid, md::Price{100}, md::Quantity{1}, md::TimestampNs{1});
    const md::OrderId second = client.sendOrder(md::InstrumentId{42}, md::Side::Ask, md::Price{101}, md::Quantity{2}, md::TimestampNs{2});
    client.flush();

    require(first != second, "auto-generated order ids are distinct");
    std::optional<md::OrderRequest> request = channel->tryPopRequest();
    require(request.has_value(), "first generated request is visible");
    require(md::orderId(*request) == first, "first generated order id in channel");
    request = channel->tryPopRequest();
    require(request.has_value(), "second generated request is visible");
    require(md::orderId(*request) == second, "second generated order id in channel");
}

void testSendExplicitNewOrderPreservesOrderId()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    client.sendOrder(md::NewOrder{
        .fields = {
            .trading_engine_id = md::TradingEngineId{99},
            .order_id = md::OrderId{777},
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::Ask,
            .price = md::Price{102'000'000'000LL},
            .size = md::Quantity{3},
            .timestamp_ns = md::TimestampNs{123},
            .status = md::OrderStatus::New,
        },
    });
    client.flush();

    const std::optional<md::OrderRequest> request = channel->tryPopRequest();
    require(request.has_value(), "explicit NewOrder is visible");
    require(md::orderId(*request) == 777, "explicit NewOrder preserves order id");
    require(md::tradingEngineId(*request) == 7, "explicit NewOrder uses client trading engine id");
}

void testCancelOrderProducesCancelRequestedRequest()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    client.cancelOrder(md::OrderId{44}, md::InstrumentId{42}, md::TimestampNs{456});
    client.flush();

    const std::optional<md::OrderRequest> request = channel->tryPopRequest();
    require(request.has_value(), "cancel request is visible");
    require(md::messageType(*request) == md::OrderMessageType::CancelOrder, "cancel request type");
    require(md::orderId(*request) == 44, "cancel request order id");
    require(md::instrumentId(*request) == 42, "cancel request instrument id");
    require(md::timestampNs(*request) == 456, "cancel request timestamp");
    require(md::status(*request) == md::OrderStatus::CancelRequested, "cancel request status");
}

void testModifyOrderProducesModifyRequestedRequest()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    client.modifyOrder(
        md::OrderId{45},
        md::InstrumentId{42},
        md::Side::Ask,
        md::Price{102'500'000'000LL},
        md::Quantity{12},
        md::TimestampNs{789});
    client.flush();

    const std::optional<md::OrderRequest> request = channel->tryPopRequest();
    require(request.has_value(), "modify request is visible");
    require(md::messageType(*request) == md::OrderMessageType::ModifyOrder, "modify request type");
    require(md::orderId(*request) == 45, "modify request order id");
    require(md::fields(*request).side == md::Side::Ask, "modify request side");
    require(md::fields(*request).price == 102'500'000'000LL, "modify request price");
    require(md::fields(*request).size == 12, "modify request size");
    require(md::timestampNs(*request) == 789, "modify request timestamp");
    require(md::status(*request) == md::OrderStatus::ModifyRequested, "modify request status");
}

void testFlushMakesQueuedRequestsVisible()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    client.sendOrder(md::InstrumentId{42}, md::Side::Bid, md::Price{100}, md::Quantity{1}, md::TimestampNs{1});

    require(!channel->tryPopRequest().has_value(), "request is hidden before client flush");
    client.flush();
    require(channel->tryPopRequest().has_value(), "request is visible after client flush");
}

void testDrainEventsInvokesCallbacksAndReturnsCount()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    std::size_t ack_count = 0;
    std::size_t fill_count = 0;
    std::size_t reject_count = 0;
    client.onAck(
        [&ack_count](const md::OrderAck& ack)
        {
            require(md::orderId(ack) == 1, "ack callback order id");
            ++ack_count;
        });
    client.onFill(
        [&fill_count](const md::OrderFill& fill)
        {
            require(fill.fill_size == 4, "fill callback fill size");
            ++fill_count;
        });
    client.onReject(
        [&reject_count](const md::OrderReject& reject)
        {
            require(reject.reason == md::OrderRejectReason::InvalidPrice, "reject callback reason");
            ++reject_count;
        });

    channel->pushEvent(makeAck(md::OrderId{1}));
    channel->pushEvent(makeFill(md::OrderId{2}));
    channel->pushEvent(makeReject(md::OrderId{3}));
    channel->flushEvents();

    require(client.drainEvents() == 3, "drainEvents returns consumed event count");
    require(ack_count == 1, "drainEvents invokes onAck once");
    require(fill_count == 1, "drainEvents invokes onFill once");
    require(reject_count == 1, "drainEvents invokes onReject once");
}

void testTryPopEventReturnsRawEventWithoutCallbacks()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    std::size_t ack_count = 0;
    client.onAck(
        [&ack_count](const md::OrderAck&)
        {
            ++ack_count;
        });

    channel->pushEvent(makeAck(md::OrderId{10}));
    channel->flushEvents();

    const std::optional<md::OrderEvent> event = client.tryPopEvent();
    require(event.has_value(), "tryPopEvent returns raw event");
    require(md::messageType(*event) == md::OrderMessageType::OrderAck, "tryPopEvent raw event type");
    require(ack_count == 0, "tryPopEvent does not invoke callbacks");
}

void testMultipleCallbacksReceiveEventsInFifoOrder()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    std::vector<std::string> seen;
    client.onAck(
        [&seen](const md::OrderAck& ack)
        {
            seen.push_back("ack:" + std::to_string(md::orderId(ack)));
        });
    client.onFill(
        [&seen](const md::OrderFill& fill)
        {
            seen.push_back("fill:" + std::to_string(md::orderId(fill)));
        });
    client.onReject(
        [&seen](const md::OrderReject& reject)
        {
            seen.push_back("reject:" + std::to_string(md::orderId(reject)));
        });

    channel->pushEvent(makeAck(md::OrderId{1}));
    channel->pushEvent(makeFill(md::OrderId{2}));
    channel->pushEvent(makeReject(md::OrderId{3}));
    channel->pushEvent(makeAck(md::OrderId{4}));
    channel->flushEvents();

    require(client.drainEvents() == 4, "drainEvents drains FIFO callback test events");
    require(seen.size() == 4, "callbacks saw four events");
    require(seen[0] == "ack:1", "first callback order");
    require(seen[1] == "fill:2", "second callback order");
    require(seen[2] == "reject:3", "third callback order");
    require(seen[3] == "ack:4", "fourth callback order");
}

void testDrainEventsWithoutCallbacksDoesNotThrow()
{
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{7}, channel);

    channel->pushEvent(makeAck(md::OrderId{1}));
    channel->pushEvent(makeFill(md::OrderId{2}));
    channel->pushEvent(makeReject(md::OrderId{3}));
    channel->flushEvents();

    require(client.drainEvents() == 3, "drainEvents consumes events even without callbacks");
}

} // namespace

int main()
{
    try
    {
        testSendOrderProducesNewOrderRequest();
        testSendOrderAutoGeneratesDistinctOrderIds();
        testSendExplicitNewOrderPreservesOrderId();
        testCancelOrderProducesCancelRequestedRequest();
        testModifyOrderProducesModifyRequestedRequest();
        testFlushMakesQueuedRequestsVisible();
        testDrainEventsInvokesCallbacksAndReturnsCount();
        testTryPopEventReturnsRawEventWithoutCallbacks();
        testMultipleCallbacksReceiveEventsInFifoOrder();
        testDrainEventsWithoutCallbacksDoesNotThrow();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
