#include "gateway/OrderGatewayClient.hpp"
#include "gateway/OrderGatewayServer.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

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

md::NewOrder makeNewOrder(md::TradingEngineId trading_engine_id, md::OrderId order_id)
{
    return md::NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::Bid,
            .price = md::Price{101'250'000'000LL},
            .size = md::Quantity{10},
            .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(order_id)},
            .status = md::OrderStatus::New,
        },
    };
}

md::OrderAck requireAck(const md::OrderEvent& event, const std::string& case_name)
{
    require(md::messageType(event) == md::OrderMessageType::OrderAck, case_name + ": event type");
    return std::get<md::OrderAck>(event);
}

void testRegisterEngineStoresOneEngine()
{
    md::OrderGatewayServer server;
    const auto channel = makeChannel();

    server.registerEngine(md::TradingEngineId{1}, channel);

    require(server.hasEngine(md::TradingEngineId{1}), "server has registered engine");
    require(server.engineCount() == 1, "server engine count is one");
    require(server.channelFor(md::TradingEngineId{1}) == channel, "server returns registered channel");
}

void testRegisterEngineStoresMultipleEngines()
{
    md::OrderGatewayServer server;
    const auto first = makeChannel();
    const auto second = makeChannel();

    server.registerEngine(md::TradingEngineId{1}, first);
    server.registerEngine(md::TradingEngineId{2}, second);

    require(server.hasEngine(md::TradingEngineId{1}), "server has engine 1");
    require(server.hasEngine(md::TradingEngineId{2}), "server has engine 2");
    require(!server.hasEngine(md::TradingEngineId{3}), "server does not have engine 3");
    require(server.engineCount() == 2, "server engine count is two");
    require(server.channelFor(md::TradingEngineId{1}) == first, "server returns engine 1 channel");
    require(server.channelFor(md::TradingEngineId{2}) == second, "server returns engine 2 channel");
    require(server.channelFor(md::TradingEngineId{3}) == nullptr, "server returns null for unknown engine");
}

void testDuplicateRegistrationReplacesChannel()
{
    md::OrderGatewayServer server;
    const auto first = makeChannel();
    const auto replacement = makeChannel();

    server.registerEngine(md::TradingEngineId{1}, first);
    server.registerEngine(md::TradingEngineId{1}, replacement);

    require(server.engineCount() == 1, "duplicate registration does not increase engine count");
    require(server.channelFor(md::TradingEngineId{1}) == replacement, "duplicate registration replaces channel");
}

void testNewOrderAckRoutesOnlyToOriginatingEngine()
{
    md::OrderGatewayServer server;
    const auto first_channel = makeChannel();
    const auto second_channel = makeChannel();
    md::OrderGatewayClient first_client(md::TradingEngineId{1}, first_channel);
    md::OrderGatewayClient second_client(md::TradingEngineId{2}, second_channel);

    server.registerEngine(md::TradingEngineId{1}, first_channel);
    server.registerEngine(md::TradingEngineId{2}, second_channel);

    first_client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{101}));
    first_client.flush();

    require(server.drainRequests() == 1, "server drains one request");
    server.flushEvents();

    const std::optional<md::OrderEvent> first_event = first_client.tryPopEvent();
    require(first_event.has_value(), "engine 1 ack event is visible");
    const md::OrderAck first_ack = requireAck(*first_event, "engine 1 ack");
    require(md::tradingEngineId(first_ack) == 1, "engine 1 ack trading engine id");
    require(md::orderId(first_ack) == 101, "engine 1 ack order id");
    require(first_ack.ack_type == md::OrderAckType::NewAccepted, "engine 1 new accepted ack");
    require(!second_client.tryPopEvent().has_value(), "engine 2 receives no engine 1 ack");
}

void testNewOrderAckRoutesOnlyToSecondEngine()
{
    md::OrderGatewayServer server;
    const auto first_channel = makeChannel();
    const auto second_channel = makeChannel();
    md::OrderGatewayClient first_client(md::TradingEngineId{1}, first_channel);
    md::OrderGatewayClient second_client(md::TradingEngineId{2}, second_channel);

    server.registerEngine(md::TradingEngineId{1}, first_channel);
    server.registerEngine(md::TradingEngineId{2}, second_channel);

    second_client.sendOrder(makeNewOrder(md::TradingEngineId{2}, md::OrderId{202}));
    second_client.flush();

    require(server.drainRequests() == 1, "server drains one engine 2 request");
    server.flushEvents();

    require(!first_client.tryPopEvent().has_value(), "engine 1 receives no engine 2 ack");
    const std::optional<md::OrderEvent> second_event = second_client.tryPopEvent();
    require(second_event.has_value(), "engine 2 ack event is visible");
    const md::OrderAck second_ack = requireAck(*second_event, "engine 2 ack");
    require(md::tradingEngineId(second_ack) == 2, "engine 2 ack trading engine id");
    require(md::orderId(second_ack) == 202, "engine 2 ack order id");
    require(second_ack.ack_type == md::OrderAckType::NewAccepted, "engine 2 new accepted ack");
}

void testModifyAndCancelProduceAcceptedAcks()
{
    md::OrderGatewayServer server;
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{1}, channel);

    server.registerEngine(md::TradingEngineId{1}, channel);

    client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{301}));
    client.flush();
    require(server.drainRequests() == 1, "server drains setup new order");
    server.flushEvents();
    require(client.tryPopEvent().has_value(), "setup new order ack is consumed");

    client.modifyOrder(
        md::OrderId{301},
        md::InstrumentId{42},
        md::Side::Ask,
        md::Price{102'000'000'000LL},
        md::Quantity{12},
        md::TimestampNs{123});
    client.cancelOrder(md::OrderId{301}, md::InstrumentId{42}, md::TimestampNs{124});
    client.flush();

    require(server.drainRequests() == 2, "server drains modify and cancel");
    server.flushEvents();

    const std::optional<md::OrderEvent> modify_event = client.tryPopEvent();
    const std::optional<md::OrderEvent> cancel_event = client.tryPopEvent();
    require(modify_event.has_value(), "modify ack event is visible");
    require(cancel_event.has_value(), "cancel ack event is visible");
    const md::OrderAck modify_ack = requireAck(*modify_event, "modify ack");
    const md::OrderAck cancel_ack = requireAck(*cancel_event, "cancel ack");

    require(modify_ack.ack_type == md::OrderAckType::ModifyAccepted, "modify accepted ack type");
    require(md::orderId(modify_ack) == 301, "modify accepted order id");
    require(md::status(modify_ack) == md::OrderStatus::Accepted, "modify ack status");

    require(cancel_ack.ack_type == md::OrderAckType::CancelAccepted, "cancel accepted ack type");
    require(md::orderId(cancel_ack) == 301, "cancel accepted order id");
    require(md::status(cancel_ack) == md::OrderStatus::Cancelled, "cancel ack status");
}

void testDrainRequestsReturnsProcessedRequestCount()
{
    md::OrderGatewayServer server;
    const auto first_channel = makeChannel();
    const auto second_channel = makeChannel();
    md::OrderGatewayClient first_client(md::TradingEngineId{1}, first_channel);
    md::OrderGatewayClient second_client(md::TradingEngineId{2}, second_channel);

    server.registerEngine(md::TradingEngineId{1}, first_channel);
    server.registerEngine(md::TradingEngineId{2}, second_channel);

    first_client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{1}));
    first_client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{2}));
    second_client.sendOrder(makeNewOrder(md::TradingEngineId{2}, md::OrderId{3}));
    first_client.flush();
    second_client.flush();

    require(server.drainRequests() == 3, "server drains all available requests");
    require(server.drainRequests() == 0, "server drains zero after queues are empty");
}

void testFlushEventsMakesAcksVisible()
{
    md::OrderGatewayServer server;
    const auto channel = makeChannel();
    md::OrderGatewayClient client(md::TradingEngineId{1}, channel);

    server.registerEngine(md::TradingEngineId{1}, channel);
    client.sendOrder(makeNewOrder(md::TradingEngineId{1}, md::OrderId{404}));
    client.flush();

    require(server.drainRequests() == 1, "server drains request before flush visibility test");
    require(!client.tryPopEvent().has_value(), "ack is hidden before server flushEvents");

    server.flushEvents();
    require(client.tryPopEvent().has_value(), "ack is visible after server flushEvents");
}

void testUnregisteredEngineRequestsCannotAppearInvariant()
{
    md::OrderGatewayServer server;
    const auto registered_channel = makeChannel();
    const auto unregistered_channel = makeChannel();
    md::OrderGatewayClient unregistered_client(md::TradingEngineId{99}, unregistered_channel);

    server.registerEngine(md::TradingEngineId{1}, registered_channel);
    unregistered_client.sendOrder(makeNewOrder(md::TradingEngineId{99}, md::OrderId{909}));
    unregistered_client.flush();

    // Invariant: the server only drains channels explicitly registered in its session registry.
    require(server.drainRequests() == 0, "unregistered channel request is unreachable to server");
    server.flushEvents();
    require(!unregistered_client.tryPopEvent().has_value(), "unregistered client receives no server event");
}

} // namespace

int main()
{
    try
    {
        testRegisterEngineStoresOneEngine();
        testRegisterEngineStoresMultipleEngines();
        testDuplicateRegistrationReplacesChannel();
        testNewOrderAckRoutesOnlyToOriginatingEngine();
        testNewOrderAckRoutesOnlyToSecondEngine();
        testModifyAndCancelProduceAcceptedAcks();
        testDrainRequestsReturnsProcessedRequestCount();
        testFlushEventsMakesAcksVisible();
        testUnregisteredEngineRequestsCannotAppearInvariant();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
