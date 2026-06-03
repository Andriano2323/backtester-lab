#include "TestSupport.hpp"

#include "backtest/OrderExecutionBridge.hpp"
#include "gateway/OrderGatewayClient.hpp"
#include "lob/SimulatedLOB.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace md::test
{
namespace
{

MarketDataEvent historical(
    InstrumentId instrument_id,
    OrderId order_id,
    Side side,
    Price price,
    Quantity size)
{
    MarketDataEvent event;
    event.instrument_id = instrument_id;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = Action::Add;
    return event;
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

struct CallbackEvents
{
    std::vector<OrderAck> acks;
    std::vector<OrderFill> fills;
    std::vector<OrderReject> rejects;
    std::vector<std::string> order;

    void attach(OrderGatewayClient& client)
    {
        client.onAck(
            [this](const OrderAck& ack)
            {
                acks.push_back(ack);
                order.push_back("ack");
            });
        client.onFill(
            [this](const OrderFill& fill)
            {
                fills.push_back(fill);
                order.push_back("fill");
            });
        client.onReject(
            [this](const OrderReject& reject)
            {
                rejects.push_back(reject);
                order.push_back("reject");
            });
    }

    void clear()
    {
        acks.clear();
        fills.clear();
        rejects.clear();
        order.clear();
    }
};

struct BridgeFixture
{
    BridgeFixture()
        : channel(std::make_shared<OrderChannel>(1, 1)),
          client(TradingEngineId{1}, channel),
          bridge(server, store, views)
    {
        views.try_emplace(1, 1);
        server.registerEngine(TradingEngineId{1}, channel);
        events.attach(client);
    }

    std::shared_ptr<OrderChannel> channel;
    OrderGatewayClient client;
    OrderGatewayServer server;
    lob::HistoricalLobStore store;
    lob::FillSimulator::EngineViews views;
    backtest::OrderExecutionBridge bridge;
    CallbackEvents events;

    backtest::OrderExecutionBridgeResult drain()
    {
        client.flush();
        auto result = bridge.drainAndProcessRequests();
        server.flushEvents();
        (void)client.drainEvents();
        return result;
    }
};

} // namespace

void testOrderExecutionBridgeBuyLimitCrossesBestAsk()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Ask, 101, 5));

    const OrderId order_id = fixture.client.sendOrder(42, Side::Bid, 101, 2, 1000);
    const auto result = fixture.drain();

    require(result.new_accepted_count == 1, "buy cross accepted count");
    require(result.fills_emitted == 1, "buy cross fill count");
    require(fixture.events.acks.size() == 1, "buy cross ack count");
    require(fixture.events.fills.size() == 1, "buy cross callback fill count");
    require(fixture.events.order == std::vector<std::string>{"ack", "fill"}, "buy cross event order");
    require(orderId(fixture.events.fills[0]) == order_id, "buy cross fill order id");
    require(fixture.events.fills[0].fill_price == 101, "buy cross fill price");
    require(fixture.events.fills[0].fill_size == 2, "buy cross fill size");
    require(fixture.events.fills[0].remaining_size == 0, "buy cross fill remaining");
    require(!fixture.bridge.findMapping(1, order_id).has_value(), "buy cross no resting mapping");
    const auto order = fixture.server.findOrder(1, order_id);
    require(order.has_value(), "buy cross order snapshot");
    require(order->status == OrderStatus::Filled, "buy cross gateway status filled");
    require(fixture.server.openOrderCount() == 0, "buy cross open order count");
}

void testOrderExecutionBridgeSellLimitCrossesBestBid()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Bid, 100, 5));

    const OrderId order_id = fixture.client.sendOrder(42, Side::Ask, 100, 2, 1000);
    const auto result = fixture.drain();

    require(result.fills_emitted == 1, "sell cross fill count");
    require(fixture.events.fills.size() == 1, "sell cross callback fill count");
    require(orderId(fixture.events.fills[0]) == order_id, "sell cross fill order id");
    require(fixture.events.fills[0].fill_price == 100, "sell cross fill price");
    require(fixture.events.fills[0].fill_size == 2, "sell cross fill size");
    require(fixture.server.findOrder(1, order_id)->status == OrderStatus::Filled, "sell cross status filled");
}

void testOrderExecutionBridgeBuyBelowAskRestsBid()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Ask, 101, 5));

    const OrderId order_id = fixture.client.sendOrder(42, Side::Bid, 100, 2, 1000);
    const auto result = fixture.drain();

    require(result.new_accepted_count == 1, "resting buy accepted count");
    require(result.fills_emitted == 0, "resting buy no fill");
    require(fixture.events.acks.size() == 1, "resting buy ack count");
    require(fixture.events.fills.empty(), "resting buy no fill callback");
    require(fixture.bridge.findMapping(1, order_id).has_value(), "resting buy mapping");
    requireLevel(fixture.views.at(1).syntheticBook(42).bestBid(), 100, 2, "resting buy synthetic bid");
}

void testOrderExecutionBridgeSellAboveBidRestsAsk()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Bid, 100, 5));

    const OrderId order_id = fixture.client.sendOrder(42, Side::Ask, 101, 2, 1000);
    const auto result = fixture.drain();

    require(result.new_accepted_count == 1, "resting sell accepted count");
    require(result.fills_emitted == 0, "resting sell no fill");
    require(fixture.bridge.findMapping(1, order_id).has_value(), "resting sell mapping");
    requireLevel(fixture.views.at(1).syntheticBook(42).bestAsk(), 101, 2, "resting sell synthetic ask");
}

void testOrderExecutionBridgePartialFillLeavesRemainingResting()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Ask, 101, 2));

    const OrderId order_id = fixture.client.sendOrder(42, Side::Bid, 101, 5, 1000);
    const auto result = fixture.drain();

    require(result.fills_emitted == 1, "partial fill emitted");
    require(fixture.events.fills.size() == 1, "partial fill callback count");
    require(fixture.events.fills[0].fill_size == 2, "partial fill size");
    require(fixture.events.fills[0].remaining_size == 3, "partial gateway remaining");
    const auto order = fixture.server.findOrder(1, order_id);
    require(order.has_value(), "partial order snapshot");
    require(order->status == OrderStatus::PartiallyFilled, "partial gateway status");
    require(order->remaining_size == 3, "partial gateway remaining snapshot");
    const auto mapping = fixture.bridge.findMapping(1, order_id);
    require(mapping.has_value(), "partial remaining mapping");
    require(mapping->remaining_size == 3, "partial mapping remaining");
    requireLevel(fixture.views.at(1).syntheticBook(42).bestBid(), 101, 3, "partial resting bid");
}

void testOrderExecutionBridgeInvalidOrderRejectedWithoutEngineViewMutation()
{
    BridgeFixture fixture;

    (void)fixture.client.sendOrder(0, Side::Bid, 101, 2, 1000);
    const auto result = fixture.drain();

    require(result.rejected_count == 1, "invalid order rejected count");
    require(result.fills_emitted == 0, "invalid order no fill");
    require(fixture.events.rejects.size() == 1, "invalid order reject callback");
    require(fixture.events.rejects[0].reason == OrderRejectReason::InvalidInstrument, "invalid order reason");
    require(fixture.bridge.mappingCount() == 0, "invalid order no mapping");
    require(!fixture.views.at(1).syntheticBook(42).bestBid().has_value(), "invalid order no synthetic bid");
}

void testOrderExecutionBridgeCancelRemovesRestingSyntheticOrder()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Ask, 101, 5));
    const OrderId order_id = fixture.client.sendOrder(42, Side::Bid, 100, 2, 1000);
    (void)fixture.drain();
    fixture.events.clear();

    fixture.client.cancelOrder(order_id, 42, 1001);
    const auto result = fixture.drain();

    require(result.cancel_accepted_count == 1, "cancel accepted count");
    require(result.mappings_erased == 1, "cancel erases mapping");
    require(fixture.events.acks.size() == 1, "cancel ack callback");
    require(fixture.events.acks[0].ack_type == OrderAckType::CancelAccepted, "cancel ack type");
    require(!fixture.bridge.findMapping(1, order_id).has_value(), "cancel mapping removed");
    require(!fixture.views.at(1).syntheticBook(42).bestBid().has_value(), "cancel synthetic bid removed");
    require(fixture.server.findOrder(1, order_id)->status == OrderStatus::Cancelled, "cancel gateway status");
}

void testOrderExecutionBridgeModifyReplacesRestingSyntheticOrder()
{
    BridgeFixture fixture;
    fixture.store.apply(historical(42, 1001, Side::Ask, 101, 5));
    const OrderId order_id = fixture.client.sendOrder(42, Side::Bid, 100, 2, 1000);
    (void)fixture.drain();
    const auto old_mapping = fixture.bridge.findMapping(1, order_id);
    require(old_mapping.has_value(), "modify setup mapping");
    fixture.events.clear();

    fixture.client.modifyOrder(order_id, 42, Side::Bid, 99, 3, 1001);
    const auto result = fixture.drain();

    require(result.modify_accepted_count == 1, "modify accepted count");
    require(result.mappings_erased == 1, "modify erases old mapping");
    require(result.mappings_added == 1, "modify adds new mapping");
    require(fixture.events.acks.size() == 1, "modify ack callback");
    require(fixture.events.acks[0].ack_type == OrderAckType::ModifyAccepted, "modify ack type");
    const auto new_mapping = fixture.bridge.findMapping(1, order_id);
    require(new_mapping.has_value(), "modify new mapping");
    require(new_mapping->synthetic_order_id != old_mapping->synthetic_order_id, "modify synthetic id replaced");
    require(new_mapping->price == 99, "modify mapping price");
    require(new_mapping->remaining_size == 3, "modify mapping remaining");
    requireLevel(fixture.views.at(1).syntheticBook(42).bestBid(), 99, 3, "modify synthetic bid replaced");
}

void testOrderExecutionBridgeUnknownCancelAndModifyReject()
{
    BridgeFixture fixture;

    fixture.client.cancelOrder(999, 42, 1000);
    auto cancel_result = fixture.drain();
    require(cancel_result.rejected_count == 1, "unknown cancel rejected count");
    require(fixture.events.rejects.size() == 1, "unknown cancel reject callback");
    require(fixture.events.rejects[0].reason == OrderRejectReason::UnknownOrderId, "unknown cancel reason");
    fixture.events.clear();

    fixture.client.modifyOrder(999, 42, Side::Bid, 100, 1, 1001);
    auto modify_result = fixture.drain();
    require(modify_result.rejected_count == 1, "unknown modify rejected count");
    require(fixture.events.rejects.size() == 1, "unknown modify reject callback");
    require(fixture.events.rejects[0].reason == OrderRejectReason::UnknownOrderId, "unknown modify reason");
    require(fixture.bridge.mappingCount() == 0, "unknown requests no mapping");
}

void testOrderExecutionBridgePrivateLiquidityPerEngine()
{
    auto channel1 = std::make_shared<OrderChannel>(1, 1);
    auto channel2 = std::make_shared<OrderChannel>(1, 1);
    OrderGatewayClient client1(TradingEngineId{1}, channel1);
    OrderGatewayClient client2(TradingEngineId{2}, channel2);
    OrderGatewayServer server;
    server.registerEngine(TradingEngineId{1}, channel1);
    server.registerEngine(TradingEngineId{2}, channel2);
    lob::HistoricalLobStore store;
    store.apply(historical(42, 1001, Side::Ask, 101, 5));
    lob::FillSimulator::EngineViews views;
    views.try_emplace(1, 1);
    views.try_emplace(2, 2);
    backtest::OrderExecutionBridge bridge(server, store, views);

    (void)client1.sendOrder(42, Side::Bid, 101, 2, 1000);
    client1.flush();
    const auto result = bridge.drainAndProcessRequests();
    server.flushEvents();
    (void)client1.drainEvents();
    (void)client2.drainEvents();

    require(result.fills_emitted == 1, "private liquidity fill count");
    const lob::SimulatedLOB engine1_lob{store, views.at(1)};
    const lob::SimulatedLOB engine2_lob{store, views.at(2)};
    requireLevel(engine1_lob.bestAsk(42), 101, 3, "engine 1 consumed ask");
    requireLevel(engine2_lob.bestAsk(42), 101, 5, "engine 2 full ask");
}

} // namespace md::test
