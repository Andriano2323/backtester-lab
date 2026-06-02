#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
#include "gateway/OrderGatewayClient.hpp"
#include "gateway/OrderGatewayServer.hpp"
#include "gateway/OrderMessage.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<md::OrderChannel> makeChannel()
{
    return std::make_shared<md::OrderChannel>(8, 8);
}

md::NewOrder makeNewOrder(
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101},
    md::Quantity size = md::Quantity{10})
{
    return md::NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::Bid,
            .price = md::Price{101'250'000'000LL},
            .size = size,
            .timestamp_ns = md::TimestampNs{1'000 + static_cast<md::TimestampNs>(order_id)},
            .status = md::OrderStatus::New,
        },
    };
}

md::CancelOrder makeCancelOrder(
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101})
{
    return md::CancelOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::None,
            .price = md::Price{},
            .size = md::Quantity{},
            .timestamp_ns = md::TimestampNs{2'000 + static_cast<md::TimestampNs>(order_id)},
            .status = md::OrderStatus::CancelRequested,
        },
    };
}

md::OrderAck requireAck(const md::OrderEvent& event, const std::string& case_name)
{
    require(md::messageType(event) == md::OrderMessageType::OrderAck, case_name + ": event type is OrderAck");
    return std::get<md::OrderAck>(event);
}

md::OrderFill requireFill(const md::OrderEvent& event, const std::string& case_name)
{
    require(md::messageType(event) == md::OrderMessageType::OrderFill, case_name + ": event type is OrderFill");
    return std::get<md::OrderFill>(event);
}

md::OrderEvent popEvent(const std::shared_ptr<md::OrderChannel>& channel, const std::string& case_name)
{
    std::optional<md::OrderEvent> event = channel->tryPopEvent();
    require(event.has_value(), case_name + ": event is visible");
    return *event;
}

void submitRequest(
    md::OrderGatewayServer& server,
    const std::shared_ptr<md::OrderChannel>& channel,
    md::OrderRequest request,
    const std::string& case_name)
{
    channel->pushRequest(std::move(request));
    channel->flushRequests();
    require(server.drainRequests() == 1, case_name + ": server drains one request");
    server.flushEvents();
}

void acceptNewOrder(
    md::OrderGatewayServer& server,
    const std::shared_ptr<md::OrderChannel>& channel,
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101},
    md::Quantity size = md::Quantity{10})
{
    submitRequest(server, channel, makeNewOrder(trading_engine_id, order_id, size), "new order setup");
    const md::OrderAck ack = requireAck(popEvent(channel, "new order setup"), "new order setup");
    require(ack.ack_type == md::OrderAckType::NewAccepted, "new order setup ack type");
    require(!channel->tryPopEvent().has_value(), "new order setup consumes only ack");
}

void cancelOrder(
    md::OrderGatewayServer& server,
    const std::shared_ptr<md::OrderChannel>& channel,
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101})
{
    submitRequest(server, channel, makeCancelOrder(trading_engine_id, order_id), "cancel order setup");
    const md::OrderAck ack = requireAck(popEvent(channel, "cancel order setup"), "cancel order setup");
    require(ack.ack_type == md::OrderAckType::CancelAccepted, "cancel order setup ack type");
    require(!channel->tryPopEvent().has_value(), "cancel order setup consumes only ack");
}

md::OrderFill emitAndPopFill(
    md::OrderGatewayServer& server,
    const std::shared_ptr<md::OrderChannel>& channel,
    md::TradingEngineId trading_engine_id,
    md::OrderId order_id,
    md::Quantity fill_size,
    const std::string& case_name)
{
    require(
        server.emitFill(
            trading_engine_id,
            order_id,
            md::Price{101'300'000'000LL},
            fill_size,
            md::TimestampNs{9'000 + static_cast<md::TimestampNs>(order_id)}),
        case_name + ": emitFill returns true");
    require(!channel->tryPopEvent().has_value(), case_name + ": fill is hidden before flushEvents");
    server.flushEvents();
    return requireFill(popEvent(channel, case_name), case_name);
}

void testEmitFillForUnknownEngineReturnsFalse()
{
    md::OrderGatewayServer server;

    require(
        !server.emitFill(md::TradingEngineId{999}, md::OrderId{1}, md::Price{100}, md::Quantity{1}, md::TimestampNs{1}),
        "unknown engine fill returns false");
}

void testEmitFillForUnknownOrderReturnsFalse()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);

    require(
        !server.emitFill(md::TradingEngineId{1}, md::OrderId{999}, md::Price{100}, md::Quantity{1}, md::TimestampNs{1}),
        "unknown order fill returns false");
    server.flushEvents();
    require(!channel->tryPopEvent().has_value(), "unknown order emits no event");
}

void testEmitFillWithSizeZeroReturnsFalse()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    require(
        !server.emitFill(md::TradingEngineId{1}, md::OrderId{101}, md::Price{100}, md::Quantity{0}, md::TimestampNs{1}),
        "zero fill returns false");
    server.flushEvents();
    require(!channel->tryPopEvent().has_value(), "zero fill emits no event");
}

void testPartialFillEmitsOrderFillToCorrectEngine()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    const md::OrderFill fill = emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{4}, "partial fill");

    require(md::tradingEngineId(fill) == 1, "partial fill trading engine id");
    require(md::orderId(fill) == 101, "partial fill order id");
    require(fill.fill_size == 4, "partial fill size");
    require(fill.remaining_size == 6, "partial fill event remaining size");
    require(!channel->tryPopEvent().has_value(), "partial fill emits only one event");
}

void testPartialFillReducesRemainingSize()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    (void)emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{4}, "partial fill state");

    const std::optional<md::OrderSnapshot> snapshot = server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "partial fill order still exists");
    require(snapshot->remaining_size == 6, "partial fill reduces remaining size");
}

void testPartialFillSetsStatusPartiallyFilled()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    const md::OrderFill fill = emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{4}, "partial fill status");

    const std::optional<md::OrderSnapshot> snapshot = server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "partial fill status order exists");
    require(snapshot->status == md::OrderStatus::PartiallyFilled, "partial fill snapshot status");
    require(md::status(fill) == md::OrderStatus::PartiallyFilled, "partial fill event status");
}

void testFullFillEmitsOrderFillWithStatusFilled()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    const md::OrderFill fill = emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{10}, "full fill");

    require(md::status(fill) == md::OrderStatus::Filled, "full fill event status");
    require(fill.remaining_size == 0, "full fill event remaining size");
}

void testFullFillSetsRemainingSizeToZero()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);

    (void)emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{10}, "full fill state");

    const std::optional<md::OrderSnapshot> snapshot = server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "full fill order exists");
    require(snapshot->remaining_size == 0, "full fill remaining size");
    require(snapshot->status == md::OrderStatus::Filled, "full fill snapshot status");
    require(server.openOrderCount() == 0, "full fill removes order from open count");
}

void testOverfillRequestCapsFillSizeToRemainingQuantity()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{5});

    const md::OrderFill fill = emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{99}, "overfill");

    require(fill.fill_size == 5, "overfill caps fill size");
    require(fill.remaining_size == 0, "overfill leaves no remaining size");
    require(md::status(fill) == md::OrderStatus::Filled, "overfill event status");
}

void testFillAfterFilledReturnsFalse()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);
    (void)emitAndPopFill(server, channel, md::TradingEngineId{1}, md::OrderId{101}, md::Quantity{10}, "fill before terminal check");

    require(
        !server.emitFill(md::TradingEngineId{1}, md::OrderId{101}, md::Price{100}, md::Quantity{1}, md::TimestampNs{10}),
        "fill after filled returns false");
    server.flushEvents();
    require(!channel->tryPopEvent().has_value(), "fill after filled emits no event");
}

void testFillAfterCancelledReturnsFalse()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    server.registerEngine(md::TradingEngineId{1}, channel);
    acceptNewOrder(server, channel);
    cancelOrder(server, channel);

    require(
        !server.emitFill(md::TradingEngineId{1}, md::OrderId{101}, md::Price{100}, md::Quantity{1}, md::TimestampNs{10}),
        "fill after cancelled returns false");
    server.flushEvents();
    require(!channel->tryPopEvent().has_value(), "fill after cancelled emits no event");
}

void testEngineOneFillIsNotVisibleToEngineTwo()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> first_channel = makeChannel();
    const std::shared_ptr<md::OrderChannel> second_channel = makeChannel();

    server.registerEngine(md::TradingEngineId{1}, first_channel);
    server.registerEngine(md::TradingEngineId{2}, second_channel);
    acceptNewOrder(server, first_channel, md::TradingEngineId{1}, md::OrderId{101});
    acceptNewOrder(server, second_channel, md::TradingEngineId{2}, md::OrderId{202});

    require(
        server.emitFill(md::TradingEngineId{1}, md::OrderId{101}, md::Price{100}, md::Quantity{4}, md::TimestampNs{10}),
        "engine 1 fill emits");
    server.flushEvents();

    require(first_channel->tryPopEvent().has_value(), "engine 1 fill is visible to engine 1");
    require(!second_channel->tryPopEvent().has_value(), "engine 1 fill is not visible to engine 2");
}

void testClientOnFillCallbackReceivesEmittedFillThroughDrainEvents()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{1}, channel);
    server.registerEngine(md::TradingEngineId{1}, channel);

    client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{303}));
    client.flush();
    require(server.drainRequests() == 1, "client callback setup request drained");
    server.flushEvents();
    require(client.tryPopEvent().has_value(), "client callback setup ack consumed");

    std::size_t fill_count = 0;
    client.onFill(
        [&fill_count](const md::OrderFill& fill)
        {
            require(md::orderId(fill) == 303, "client onFill order id");
            require(fill.fill_size == 4, "client onFill fill size");
            require(fill.remaining_size == 6, "client onFill remaining size");
            ++fill_count;
        });

    require(
        server.emitFill(md::TradingEngineId{1}, md::OrderId{303}, md::Price{101'300'000'000LL}, md::Quantity{4}, md::TimestampNs{10}),
        "client callback emitFill returns true");
    server.flushEvents();

    require(client.drainEvents() == 1, "client drainEvents consumes emitted fill");
    require(fill_count == 1, "client onFill callback count");
}

} // namespace

int main()
{
    try
    {
        testEmitFillForUnknownEngineReturnsFalse();
        testEmitFillForUnknownOrderReturnsFalse();
        testEmitFillWithSizeZeroReturnsFalse();
        testPartialFillEmitsOrderFillToCorrectEngine();
        testPartialFillReducesRemainingSize();
        testPartialFillSetsStatusPartiallyFilled();
        testFullFillEmitsOrderFillWithStatusFilled();
        testFullFillSetsRemainingSizeToZero();
        testOverfillRequestCapsFillSizeToRemainingQuantity();
        testFillAfterFilledReturnsFalse();
        testFillAfterCancelledReturnsFalse();
        testEngineOneFillIsNotVisibleToEngineTwo();
        testClientOnFillCallbackReceivesEmittedFillThroughDrainEvents();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
