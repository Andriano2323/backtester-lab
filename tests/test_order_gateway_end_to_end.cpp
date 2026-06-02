#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
#include "gateway/OrderGatewayClient.hpp"
#include "gateway/OrderGatewayServer.hpp"
#include "gateway/OrderMessage.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

std::shared_ptr<md::OrderChannel> makeChannel()
{
    return std::make_shared<md::OrderChannel>(16, 16);
}

md::NewOrder makeNewOrder(
    md::TradingEngineId trading_engine_id,
    md::OrderId order_id,
    md::Quantity size = md::Quantity{10},
    md::TimestampNs timestamp_ns = md::TimestampNs{1'000})
{
    return md::NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::Bid,
            .price = md::Price{101'250'000'000LL},
            .size = size,
            .timestamp_ns = timestamp_ns,
            .status = md::OrderStatus::New,
        },
    };
}

struct ClientSession
{
    explicit ClientSession(md::TradingEngineId trading_engine_id_in)
        : trading_engine_id(trading_engine_id_in), channel(makeChannel()), client(trading_engine_id, channel)
    {
    }

    md::TradingEngineId trading_engine_id{};
    std::shared_ptr<md::OrderChannel> channel;
    md::OrderGatewayClient client;
};

struct CallbackEvents
{
    std::vector<md::OrderAck> acks;
    std::vector<md::OrderFill> fills;
    std::vector<md::OrderReject> rejects;

    void attach(md::OrderGatewayClient& client)
    {
        client.onAck(
            [this](const md::OrderAck& ack)
            {
                acks.push_back(ack);
            });
        client.onFill(
            [this](const md::OrderFill& fill)
            {
                fills.push_back(fill);
            });
        client.onReject(
            [this](const md::OrderReject& reject)
            {
                rejects.push_back(reject);
            });
    }

    void clear()
    {
        acks.clear();
        fills.clear();
        rejects.clear();
    }
};

void registerClient(md::OrderGatewayServer& server, const ClientSession& session)
{
    server.registerEngine(session.trading_engine_id, session.channel);
}

void pumpRequests(md::OrderGatewayServer& server, md::OrderGatewayClient& client, std::size_t expected_count)
{
    client.flush();
    require(server.drainRequests() == expected_count, "server drainRequests count");
    require(server.drainRequests() == 0, "server drainRequests empty after drain");
}

void flushAndDrainEvents(
    md::OrderGatewayServer& server,
    md::OrderGatewayClient& client,
    std::size_t expected_count,
    const std::string& case_name)
{
    server.flushEvents();
    require(client.drainEvents() == expected_count, case_name + ": client drainEvents count");
    require(client.drainEvents() == 0, case_name + ": client drainEvents empty after drain");
}

void acceptExplicitOrder(
    md::OrderGatewayServer& server,
    ClientSession& session,
    CallbackEvents& events,
    md::OrderId order_id)
{
    session.client.sendOrder(makeNewOrder(session.trading_engine_id, order_id));
    pumpRequests(server, session.client, 1);
    flushAndDrainEvents(server, session.client, 1, "accept explicit order");
    require(events.acks.size() == 1, "accepted explicit order ack count");
    require(events.acks.back().ack_type == md::OrderAckType::NewAccepted, "accepted explicit order ack type");
    events.clear();
}

void testClientSendOrderAcceptedAckCallback()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);

    const md::OrderId order_id = session.client.sendOrder(
        md::InstrumentId{42},
        md::Side::Bid,
        md::Price{101'250'000'000LL},
        md::Quantity{10},
        md::TimestampNs{1'001});

    pumpRequests(server, session.client, 1);
    flushAndDrainEvents(server, session.client, 1, "new order accepted");

    require(events.acks.size() == 1, "new order accepted ack callback count");
    require(events.rejects.empty(), "new order accepted no rejects");
    require(events.fills.empty(), "new order accepted no fills");
    require(events.acks[0].ack_type == md::OrderAckType::NewAccepted, "new order accepted ack type");
    require(md::orderId(events.acks[0]) == order_id, "new order accepted order id");
    require(md::status(events.acks[0]) == md::OrderStatus::Accepted, "new order accepted status");
}

void testExplicitDuplicateNewOrderRejected()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);

    acceptExplicitOrder(server, session, events, md::OrderId{77});

    session.client.sendOrder(makeNewOrder(session.trading_engine_id, md::OrderId{77}));
    pumpRequests(server, session.client, 1);
    flushAndDrainEvents(server, session.client, 1, "duplicate new order");

    require(events.acks.empty(), "duplicate new order no ack");
    require(events.fills.empty(), "duplicate new order no fill");
    require(events.rejects.size() == 1, "duplicate new order reject callback count");
    require(events.rejects[0].reason == md::OrderRejectReason::DuplicateOrderId, "duplicate new order reject reason");
    require(md::orderId(events.rejects[0]) == 77, "duplicate new order reject order id");
}

void testModifyOrderAcceptedAfterNewOrder()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);
    acceptExplicitOrder(server, session, events, md::OrderId{301});

    session.client.modifyOrder(
        md::OrderId{301},
        md::InstrumentId{42},
        md::Side::Ask,
        md::Price{102'500'000'000LL},
        md::Quantity{12},
        md::TimestampNs{2'001});
    pumpRequests(server, session.client, 1);
    flushAndDrainEvents(server, session.client, 1, "modify accepted");

    require(events.acks.size() == 1, "modify accepted ack callback count");
    require(events.acks[0].ack_type == md::OrderAckType::ModifyAccepted, "modify accepted ack type");
    require(md::orderId(events.acks[0]) == 301, "modify accepted order id");
    require(md::status(events.acks[0]) == md::OrderStatus::Accepted, "modify accepted status");
    require(events.rejects.empty(), "modify accepted no reject");
}

void testCancelOrderAcceptedAfterNewOrder()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);
    acceptExplicitOrder(server, session, events, md::OrderId{401});

    session.client.cancelOrder(md::OrderId{401}, md::InstrumentId{42}, md::TimestampNs{3'001});
    pumpRequests(server, session.client, 1);
    flushAndDrainEvents(server, session.client, 1, "cancel accepted");

    require(events.acks.size() == 1, "cancel accepted ack callback count");
    require(events.acks[0].ack_type == md::OrderAckType::CancelAccepted, "cancel accepted ack type");
    require(md::orderId(events.acks[0]) == 401, "cancel accepted order id");
    require(md::status(events.acks[0]) == md::OrderStatus::Cancelled, "cancel accepted status");
    require(events.rejects.empty(), "cancel accepted no reject");
}

void testOrderFillCallbackAfterServerEmitFill()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);
    acceptExplicitOrder(server, session, events, md::OrderId{501});

    require(
        server.emitFill(
            session.trading_engine_id,
            md::OrderId{501},
            md::Price{101'300'000'000LL},
            md::Quantity{4},
            md::TimestampNs{4'001}),
        "server emitFill succeeds");
    flushAndDrainEvents(server, session.client, 1, "emit fill callback");

    require(events.fills.size() == 1, "emit fill callback count");
    require(events.fills[0].fill_size == 4, "emit fill callback fill size");
    require(events.fills[0].remaining_size == 6, "emit fill callback remaining size");
    require(md::status(events.fills[0]) == md::OrderStatus::PartiallyFilled, "emit fill callback status");
    require(events.acks.empty(), "emit fill no ack");
    require(events.rejects.empty(), "emit fill no reject");
}

void testTwoClientsCanUseSameOrderIdWithoutCollision()
{
    md::OrderGatewayServer server;
    ClientSession first(md::TradingEngineId{1});
    ClientSession second(md::TradingEngineId{2});
    CallbackEvents first_events;
    CallbackEvents second_events;
    first_events.attach(first.client);
    second_events.attach(second.client);
    registerClient(server, first);
    registerClient(server, second);

    first.client.sendOrder(makeNewOrder(first.trading_engine_id, md::OrderId{900}));
    second.client.sendOrder(makeNewOrder(second.trading_engine_id, md::OrderId{900}));
    first.client.flush();
    second.client.flush();

    require(server.drainRequests() == 2, "same order id server drains two requests");
    require(server.openOrderCount() == 2, "same order id creates two open orders");
    server.flushEvents();
    require(first.client.drainEvents() == 1, "same order id first client drain count");
    require(second.client.drainEvents() == 1, "same order id second client drain count");

    require(first_events.acks.size() == 1, "same order id first ack count");
    require(second_events.acks.size() == 1, "same order id second ack count");
    require(first_events.rejects.empty(), "same order id first no reject");
    require(second_events.rejects.empty(), "same order id second no reject");
    require(md::orderId(first_events.acks[0]) == 900, "same order id first ack order id");
    require(md::orderId(second_events.acks[0]) == 900, "same order id second ack order id");
    require(md::tradingEngineId(first_events.acks[0]) == 1, "same order id first trading engine id");
    require(md::tradingEngineId(second_events.acks[0]) == 2, "same order id second trading engine id");
}

void testServerRoutesEventsOnlyToCorrectClient()
{
    md::OrderGatewayServer server;
    ClientSession first(md::TradingEngineId{1});
    ClientSession second(md::TradingEngineId{2});
    CallbackEvents first_events;
    CallbackEvents second_events;
    first_events.attach(first.client);
    second_events.attach(second.client);
    registerClient(server, first);
    registerClient(server, second);

    first.client.sendOrder(makeNewOrder(first.trading_engine_id, md::OrderId{11}));
    pumpRequests(server, first.client, 1);
    server.flushEvents();
    require(first.client.drainEvents() == 1, "route ack first client drain count");
    require(second.client.drainEvents() == 0, "route ack second client empty");
    require(first_events.acks.size() == 1, "route ack first receives ack");
    require(second_events.acks.empty(), "route ack second receives no ack");

    first_events.clear();
    second_events.clear();
    second.client.sendOrder(makeNewOrder(second.trading_engine_id, md::OrderId{22}));
    pumpRequests(server, second.client, 1);
    server.flushEvents();
    require(first.client.drainEvents() == 0, "route second ack first client empty");
    require(second.client.drainEvents() == 1, "route second ack second client drain count");
    require(first_events.acks.empty(), "route second ack first receives no ack");
    require(second_events.acks.size() == 1, "route second ack second receives ack");

    first_events.clear();
    second_events.clear();
    first.client.sendOrder(makeNewOrder(first.trading_engine_id, md::OrderId{11}));
    pumpRequests(server, first.client, 1);
    server.flushEvents();
    require(first.client.drainEvents() == 1, "route reject first client drain count");
    require(second.client.drainEvents() == 0, "route reject second client empty");
    require(first_events.rejects.size() == 1, "route reject first receives reject");
    require(second_events.rejects.empty(), "route reject second receives no reject");
    require(first_events.rejects[0].reason == md::OrderRejectReason::DuplicateOrderId, "route reject reason");

    first_events.clear();
    second_events.clear();
    require(
        server.emitFill(
            second.trading_engine_id,
            md::OrderId{22},
            md::Price{101'300'000'000LL},
            md::Quantity{5},
            md::TimestampNs{5'001}),
        "route fill emit succeeds");
    server.flushEvents();
    require(first.client.drainEvents() == 0, "route fill first client empty");
    require(second.client.drainEvents() == 1, "route fill second client drain count");
    require(first_events.fills.empty(), "route fill first receives no fill");
    require(second_events.fills.size() == 1, "route fill second receives fill");
    require(md::orderId(second_events.fills[0]) == 22, "route fill order id");
}

void testNewOrderAckFifoForManyOrders()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);

    std::vector<md::OrderId> order_ids;
    for (std::size_t i = 0; i < 100; ++i)
    {
        order_ids.push_back(session.client.sendOrder(
            md::InstrumentId{42},
            md::Side::Bid,
            md::Price{101'250'000'000LL + static_cast<md::Price>(i)},
            md::Quantity{10},
            md::TimestampNs{10'000 + static_cast<md::TimestampNs>(i)}));
    }

    pumpRequests(server, session.client, 100);
    flushAndDrainEvents(server, session.client, 100, "fifo many new orders");

    require(events.acks.size() == 100, "fifo many new orders ack count");
    require(events.rejects.empty(), "fifo many new orders no rejects");
    for (std::size_t i = 0; i < order_ids.size(); ++i)
    {
        require(events.acks[i].ack_type == md::OrderAckType::NewAccepted, "fifo ack type");
        require(md::orderId(events.acks[i]) == order_ids[i], "fifo ack order id");
    }
}

void testDrainRequestsAndDrainEventsReturnProcessedCounts()
{
    md::OrderGatewayServer server;
    ClientSession session(md::TradingEngineId{7});
    CallbackEvents events;
    events.attach(session.client);
    registerClient(server, session);

    session.client.sendOrder(makeNewOrder(session.trading_engine_id, md::OrderId{701}));
    session.client.sendOrder(makeNewOrder(session.trading_engine_id, md::OrderId{702}));
    session.client.sendOrder(makeNewOrder(session.trading_engine_id, md::OrderId{703}));
    session.client.flush();

    require(server.drainRequests() == 3, "processed count drainRequests three");
    require(server.drainRequests() == 0, "processed count drainRequests empty");
    server.flushEvents();
    require(session.client.drainEvents() == 3, "processed count drainEvents three");
    require(session.client.drainEvents() == 0, "processed count drainEvents empty");
    require(events.acks.size() == 3, "processed count ack callbacks");
    require(events.rejects.empty(), "processed count no rejects");
    require(events.fills.empty(), "processed count no fills");
}

} // namespace

int main()
{
    try
    {
        testClientSendOrderAcceptedAckCallback();
        testExplicitDuplicateNewOrderRejected();
        testModifyOrderAcceptedAfterNewOrder();
        testCancelOrderAcceptedAfterNewOrder();
        testOrderFillCallbackAfterServerEmitFill();
        testTwoClientsCanUseSameOrderIdWithoutCollision();
        testServerRoutesEventsOnlyToCorrectClient();
        testNewOrderAckFifoForManyOrders();
        testDrainRequestsAndDrainEventsReturnProcessedCounts();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
