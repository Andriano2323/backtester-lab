#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
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

struct Rig
{
    explicit Rig(md::TradingEngineId engine_id_in = md::TradingEngineId{1})
        : engine_id(engine_id_in), channel(makeChannel())
    {
        server.registerEngine(engine_id, channel);
    }

    md::TradingEngineId engine_id{};
    std::shared_ptr<md::OrderChannel> channel;
    md::OrderGatewayServer server;
};

md::NewOrder makeNewOrder(
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101},
    md::InstrumentId instrument_id = md::InstrumentId{42},
    md::Side side = md::Side::Bid,
    md::Price price = md::Price{101'250'000'000LL},
    md::Quantity size = md::Quantity{10},
    md::TimestampNs timestamp_ns = md::TimestampNs{1'000})
{
    return md::NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = side,
            .price = price,
            .size = size,
            .timestamp_ns = timestamp_ns,
            .status = md::OrderStatus::New,
        },
    };
}

md::CancelOrder makeCancelOrder(
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101},
    md::InstrumentId instrument_id = md::InstrumentId{42},
    md::TimestampNs timestamp_ns = md::TimestampNs{2'000})
{
    return md::CancelOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = md::Side::None,
            .price = md::Price{},
            .size = md::Quantity{},
            .timestamp_ns = timestamp_ns,
            .status = md::OrderStatus::CancelRequested,
        },
    };
}

md::ModifyOrder makeModifyOrder(
    md::TradingEngineId trading_engine_id = md::TradingEngineId{1},
    md::OrderId order_id = md::OrderId{101},
    md::InstrumentId instrument_id = md::InstrumentId{42},
    md::Side side = md::Side::Ask,
    md::Price price = md::Price{102'500'000'000LL},
    md::Quantity size = md::Quantity{12},
    md::TimestampNs timestamp_ns = md::TimestampNs{3'000})
{
    return md::ModifyOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = side,
            .price = price,
            .size = size,
            .timestamp_ns = timestamp_ns,
            .status = md::OrderStatus::ModifyRequested,
        },
    };
}

md::OrderEvent submit(
    md::OrderGatewayServer& server,
    const std::shared_ptr<md::OrderChannel>& channel,
    md::OrderRequest request,
    const std::string& case_name)
{
    channel->pushRequest(std::move(request));
    channel->flushRequests();

    require(server.drainRequests() == 1, case_name + ": server drains one request");
    server.flushEvents();

    std::optional<md::OrderEvent> event = channel->tryPopEvent();
    require(event.has_value(), case_name + ": server emits one event");
    require(!channel->tryPopEvent().has_value(), case_name + ": server emits no extra event");
    return *event;
}

md::OrderEvent submit(Rig& rig, md::OrderRequest request, const std::string& case_name)
{
    return submit(rig.server, rig.channel, std::move(request), case_name);
}

md::OrderAck requireAck(const md::OrderEvent& event, const std::string& case_name)
{
    require(md::messageType(event) == md::OrderMessageType::OrderAck, case_name + ": event type is OrderAck");
    return std::get<md::OrderAck>(event);
}

md::OrderReject requireReject(const md::OrderEvent& event, const std::string& case_name)
{
    require(md::messageType(event) == md::OrderMessageType::OrderReject, case_name + ": event type is OrderReject");
    return std::get<md::OrderReject>(event);
}

void acceptNewOrder(Rig& rig, md::OrderId order_id = md::OrderId{101})
{
    const md::OrderAck ack = requireAck(
        submit(rig, makeNewOrder(rig.engine_id, order_id), "setup valid new order"),
        "setup valid new order");
    require(ack.ack_type == md::OrderAckType::NewAccepted, "setup new order ack type");
}

void testValidNewOrderReceivesNewAcceptedAck()
{
    Rig rig;

    const md::OrderAck ack = requireAck(submit(rig, makeNewOrder(), "valid new order"), "valid new order");

    require(ack.ack_type == md::OrderAckType::NewAccepted, "valid new order ack type");
    require(md::orderId(ack) == 101, "valid new order ack order id");
    require(md::status(ack) == md::OrderStatus::Accepted, "valid new order ack status");
}

void testValidNewOrderIsStoredAsOpenOrder()
{
    Rig rig;

    (void)requireAck(submit(rig, makeNewOrder(), "stored new order"), "stored new order");

    require(rig.server.openOrderCount() == 1, "open order count after valid new order");
    const std::optional<md::OrderSnapshot> snapshot = rig.server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "stored new order can be found");
    require(snapshot->status == md::OrderStatus::Accepted, "stored new order status");
    require(snapshot->remaining_size == 10, "stored new order remaining size");
}

void testDuplicateNewOrderReceivesDuplicateOrderIdReject()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderReject reject = requireReject(
        submit(rig, makeNewOrder(), "duplicate new order"),
        "duplicate new order");

    require(reject.reason == md::OrderRejectReason::DuplicateOrderId, "duplicate new order reject reason");
    require(rig.server.openOrderCount() == 1, "duplicate new order does not add order");
}

void testNewOrderWithInstrumentZeroReceivesInvalidInstrumentReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(rig, makeNewOrder(md::TradingEngineId{1}, md::OrderId{102}, md::InstrumentId{0}), "invalid instrument"),
        "invalid instrument");

    require(reject.reason == md::OrderRejectReason::InvalidInstrument, "invalid instrument reject reason");
    require(rig.server.openOrderCount() == 0, "invalid instrument order is not stored");
}

void testNewOrderWithSideNoneReceivesInvalidSideReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeNewOrder(md::TradingEngineId{1}, md::OrderId{103}, md::InstrumentId{42}, md::Side::None),
            "invalid side"),
        "invalid side");

    require(reject.reason == md::OrderRejectReason::InvalidSide, "invalid side reject reason");
}

void testNewOrderWithUndefinedPriceReceivesInvalidPriceReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeNewOrder(
                md::TradingEngineId{1},
                md::OrderId{104},
                md::InstrumentId{42},
                md::Side::Bid,
                md::undefined_price),
            "invalid price"),
        "invalid price");

    require(reject.reason == md::OrderRejectReason::InvalidPrice, "invalid price reject reason");
}

void testNewOrderWithSizeZeroReceivesInvalidQuantityReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeNewOrder(
                md::TradingEngineId{1},
                md::OrderId{105},
                md::InstrumentId{42},
                md::Side::Bid,
                md::Price{101'250'000'000LL},
                md::Quantity{0}),
            "invalid quantity"),
        "invalid quantity");

    require(reject.reason == md::OrderRejectReason::InvalidQuantity, "invalid quantity reject reason");
}

void testCancelUnknownOrderReceivesUnknownOrderIdReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(rig, makeCancelOrder(md::TradingEngineId{1}, md::OrderId{999}), "cancel unknown"),
        "cancel unknown");

    require(reject.reason == md::OrderRejectReason::UnknownOrderId, "cancel unknown reject reason");
}

void testCancelAcceptedOrderReceivesCancelAcceptedAck()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderAck ack = requireAck(
        submit(rig, makeCancelOrder(), "cancel accepted order"),
        "cancel accepted order");

    require(ack.ack_type == md::OrderAckType::CancelAccepted, "cancel accepted ack type");
    require(md::status(ack) == md::OrderStatus::Cancelled, "cancel accepted ack final status");
}

void testCancelAcceptedOrderMarksOrderCancelled()
{
    Rig rig;
    acceptNewOrder(rig);

    (void)requireAck(submit(rig, makeCancelOrder(), "cancel marks cancelled"), "cancel marks cancelled");

    const std::optional<md::OrderSnapshot> snapshot = rig.server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "cancelled order can still be found");
    require(snapshot->status == md::OrderStatus::Cancelled, "cancelled order status");
    require(snapshot->last_timestamp_ns == 2'000, "cancelled order last timestamp");
    require(rig.server.openOrderCount() == 0, "cancelled order is not open");
}

void testCancelAlreadyCancelledOrderReceivesAlreadyTerminalReject()
{
    Rig rig;
    acceptNewOrder(rig);
    (void)requireAck(submit(rig, makeCancelOrder(), "first cancel"), "first cancel");

    const md::OrderReject reject = requireReject(
        submit(rig, makeCancelOrder(md::TradingEngineId{1}, md::OrderId{101}, md::InstrumentId{42}, md::TimestampNs{2'100}), "second cancel"),
        "second cancel");

    require(reject.reason == md::OrderRejectReason::AlreadyTerminal, "already cancelled reject reason");
}

void testModifyUnknownOrderReceivesUnknownOrderIdReject()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(rig, makeModifyOrder(md::TradingEngineId{1}, md::OrderId{999}), "modify unknown"),
        "modify unknown");

    require(reject.reason == md::OrderRejectReason::UnknownOrderId, "modify unknown reject reason");
}

void testModifyWithSideNoneReceivesInvalidSideReject()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeModifyOrder(
                md::TradingEngineId{1},
                md::OrderId{101},
                md::InstrumentId{42},
                md::Side::None),
            "modify invalid side"),
        "modify invalid side");

    require(reject.reason == md::OrderRejectReason::InvalidSide, "modify invalid side reject reason");
}

void testModifyWithUndefinedPriceReceivesInvalidPriceReject()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeModifyOrder(
                md::TradingEngineId{1},
                md::OrderId{101},
                md::InstrumentId{42},
                md::Side::Ask,
                md::undefined_price),
            "modify invalid price"),
        "modify invalid price");

    require(reject.reason == md::OrderRejectReason::InvalidPrice, "modify invalid price reject reason");
}

void testModifyWithSizeZeroReceivesInvalidQuantityReject()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeModifyOrder(
                md::TradingEngineId{1},
                md::OrderId{101},
                md::InstrumentId{42},
                md::Side::Ask,
                md::Price{102'500'000'000LL},
                md::Quantity{0}),
            "modify invalid quantity"),
        "modify invalid quantity");

    require(reject.reason == md::OrderRejectReason::InvalidQuantity, "modify invalid quantity reject reason");
}

void testModifyAcceptedOrderReceivesModifyAcceptedAck()
{
    Rig rig;
    acceptNewOrder(rig);

    const md::OrderAck ack = requireAck(
        submit(rig, makeModifyOrder(), "modify accepted order"),
        "modify accepted order");

    require(ack.ack_type == md::OrderAckType::ModifyAccepted, "modify accepted ack type");
    require(md::status(ack) == md::OrderStatus::Accepted, "modify accepted ack final status");
}

void testModifyAcceptedOrderUpdatesPriceAndSize()
{
    Rig rig;
    acceptNewOrder(rig);

    (void)requireAck(
        submit(
            rig,
            makeModifyOrder(
                md::TradingEngineId{1},
                md::OrderId{101},
                md::InstrumentId{42},
                md::Side::Ask,
                md::Price{103'750'000'000LL},
                md::Quantity{77},
                md::TimestampNs{3'500}),
            "modify updates state"),
        "modify updates state");

    const std::optional<md::OrderSnapshot> snapshot = rig.server.findOrder(md::TradingEngineId{1}, md::OrderId{101});
    require(snapshot.has_value(), "modified order can be found");
    require(snapshot->side == md::Side::Ask, "modified order side");
    require(snapshot->price == md::Price{103'750'000'000LL}, "modified order price");
    require(snapshot->original_size == md::Quantity{77}, "modified order size");
    require(snapshot->remaining_size == md::Quantity{77}, "modified order remaining size");
    require(snapshot->status == md::OrderStatus::Accepted, "modified order status");
    require(snapshot->last_timestamp_ns == 3'500, "modified order last timestamp");
}

void testModifyCancelledOrderReceivesAlreadyTerminalReject()
{
    Rig rig;
    acceptNewOrder(rig);
    (void)requireAck(submit(rig, makeCancelOrder(), "cancel before modify"), "cancel before modify");

    const md::OrderReject reject = requireReject(
        submit(rig, makeModifyOrder(), "modify cancelled"),
        "modify cancelled");

    require(reject.reason == md::OrderRejectReason::AlreadyTerminal, "modify cancelled reject reason");
}

void testSameOrderIdCanExistForTwoDifferentTradingEngines()
{
    md::OrderGatewayServer server;
    const std::shared_ptr<md::OrderChannel> first_channel = makeChannel();
    const std::shared_ptr<md::OrderChannel> second_channel = makeChannel();

    server.registerEngine(md::TradingEngineId{1}, first_channel);
    server.registerEngine(md::TradingEngineId{2}, second_channel);

    (void)requireAck(
        submit(server, first_channel, makeNewOrder(md::TradingEngineId{1}, md::OrderId{700}), "engine 1 same order id"),
        "engine 1 same order id");
    (void)requireAck(
        submit(server, second_channel, makeNewOrder(md::TradingEngineId{2}, md::OrderId{700}), "engine 2 same order id"),
        "engine 2 same order id");

    require(server.openOrderCount() == 2, "same order id across engines creates two open orders");
    require(server.findOrder(md::TradingEngineId{1}, md::OrderId{700}).has_value(), "engine 1 order exists");
    require(server.findOrder(md::TradingEngineId{2}, md::OrderId{700}).has_value(), "engine 2 order exists");
}

void testRejectMessagesPreserveCommonFieldsAndRejectedStatus()
{
    Rig rig;

    const md::OrderReject reject = requireReject(
        submit(
            rig,
            makeNewOrder(
                md::TradingEngineId{1},
                md::OrderId{808},
                md::InstrumentId{55},
                md::Side::None,
                md::Price{101'250'000'000LL},
                md::Quantity{4},
                md::TimestampNs{9'999}),
            "reject field preservation"),
        "reject field preservation");

    require(md::orderId(reject) == 808, "reject preserves order id");
    require(md::instrumentId(reject) == 55, "reject preserves instrument id");
    require(md::timestampNs(reject) == 9'999, "reject preserves timestamp");
    require(md::status(reject) == md::OrderStatus::Rejected, "reject final status");
    require(reject.reason == md::OrderRejectReason::InvalidSide, "reject field preservation reason");
    require(!reject.text.empty(), "reject contains text");
}

void testAcksPreserveCommonFieldsAndFinalStatus()
{
    Rig rig;

    const md::OrderAck new_ack = requireAck(
        submit(
            rig,
            makeNewOrder(
                md::TradingEngineId{1},
                md::OrderId{909},
                md::InstrumentId{77},
                md::Side::Bid,
                md::Price{104'000'000'000LL},
                md::Quantity{6},
                md::TimestampNs{12'345}),
            "ack field preservation new"),
        "ack field preservation new");
    require(md::orderId(new_ack) == 909, "new ack preserves order id");
    require(md::instrumentId(new_ack) == 77, "new ack preserves instrument id");
    require(md::timestampNs(new_ack) == 12'345, "new ack preserves timestamp");
    require(md::status(new_ack) == md::OrderStatus::Accepted, "new ack final status");

    const md::OrderAck cancel_ack = requireAck(
        submit(
            rig,
            makeCancelOrder(md::TradingEngineId{1}, md::OrderId{909}, md::InstrumentId{77}, md::TimestampNs{12'999}),
            "ack field preservation cancel"),
        "ack field preservation cancel");
    require(md::orderId(cancel_ack) == 909, "cancel ack preserves order id");
    require(md::instrumentId(cancel_ack) == 77, "cancel ack preserves instrument id");
    require(md::timestampNs(cancel_ack) == 12'999, "cancel ack preserves timestamp");
    require(md::status(cancel_ack) == md::OrderStatus::Cancelled, "cancel ack final status");
}

} // namespace

int main()
{
    try
    {
        testValidNewOrderReceivesNewAcceptedAck();
        testValidNewOrderIsStoredAsOpenOrder();
        testDuplicateNewOrderReceivesDuplicateOrderIdReject();
        testNewOrderWithInstrumentZeroReceivesInvalidInstrumentReject();
        testNewOrderWithSideNoneReceivesInvalidSideReject();
        testNewOrderWithUndefinedPriceReceivesInvalidPriceReject();
        testNewOrderWithSizeZeroReceivesInvalidQuantityReject();
        testCancelUnknownOrderReceivesUnknownOrderIdReject();
        testCancelAcceptedOrderReceivesCancelAcceptedAck();
        testCancelAcceptedOrderMarksOrderCancelled();
        testCancelAlreadyCancelledOrderReceivesAlreadyTerminalReject();
        testModifyUnknownOrderReceivesUnknownOrderIdReject();
        testModifyWithSideNoneReceivesInvalidSideReject();
        testModifyWithUndefinedPriceReceivesInvalidPriceReject();
        testModifyWithSizeZeroReceivesInvalidQuantityReject();
        testModifyAcceptedOrderReceivesModifyAcceptedAck();
        testModifyAcceptedOrderUpdatesPriceAndSize();
        testModifyCancelledOrderReceivesAlreadyTerminalReject();
        testSameOrderIdCanExistForTwoDifferentTradingEngines();
        testRejectMessagesPreserveCommonFieldsAndRejectedStatus();
        testAcksPreserveCommonFieldsAndFinalStatus();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
