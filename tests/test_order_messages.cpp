#include "gateway/OrderMessage.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
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

md::OrderFields makeFields(md::OrderStatus status = md::OrderStatus::New)
{
    return md::OrderFields{
        .trading_engine_id = md::TradingEngineId{7},
        .order_id = md::OrderId{1001},
        .instrument_id = md::InstrumentId{42},
        .side = md::Side::Bid,
        .price = md::Price{101'250'000'000LL},
        .size = md::Quantity{11},
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'001LL},
        .status = status,
    };
}

void requireCommonFields(const md::OrderFields& fields, md::OrderStatus expected_status, const std::string& case_name)
{
    require(fields.trading_engine_id == 7, case_name + ": trading_engine_id");
    require(fields.order_id == 1001, case_name + ": order_id");
    require(fields.instrument_id == 42, case_name + ": instrument_id");
    require(fields.side == md::Side::Bid, case_name + ": side");
    require(fields.price == 101'250'000'000LL, case_name + ": price");
    require(fields.size == 11, case_name + ": size");
    require(fields.timestamp_ns == 1'700'000'000'000'000'001LL, case_name + ": timestamp_ns");
    require(fields.status == expected_status, case_name + ": status");
}

void testOrderFieldTypesUseDomainAliases()
{
    static_assert(std::is_same_v<decltype(md::OrderFields{}.trading_engine_id), md::TradingEngineId>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.order_id), md::OrderId>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.instrument_id), md::InstrumentId>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.side), md::Side>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.price), md::Price>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.size), md::Quantity>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.timestamp_ns), md::TimestampNs>);
    static_assert(std::is_same_v<decltype(md::OrderFields{}.status), md::OrderStatus>);
}

void testRequestMessagesCarryCommonFields()
{
    const md::NewOrder new_order{.fields = makeFields(md::OrderStatus::New)};
    const md::CancelOrder cancel_order{.fields = makeFields(md::OrderStatus::CancelRequested)};
    const md::ModifyOrder modify_order{.fields = makeFields(md::OrderStatus::ModifyRequested)};

    requireCommonFields(new_order.fields, md::OrderStatus::New, "new order");
    requireCommonFields(cancel_order.fields, md::OrderStatus::CancelRequested, "cancel order");
    requireCommonFields(modify_order.fields, md::OrderStatus::ModifyRequested, "modify order");
}

void testOrderAckCarriesFieldsAndAckType()
{
    const md::OrderAck ack{
        .fields = makeFields(md::OrderStatus::Accepted),
        .ack_type = md::OrderAckType::NewAccepted,
    };

    requireCommonFields(ack.fields, md::OrderStatus::Accepted, "order ack");
    require(ack.ack_type == md::OrderAckType::NewAccepted, "order ack type");
}

void testOrderFillCarriesFieldsAndFillData()
{
    const md::OrderFill fill{
        .fields = makeFields(md::OrderStatus::PartiallyFilled),
        .fill_price = md::Price{101'300'000'000LL},
        .fill_size = md::Quantity{4},
        .remaining_size = md::Quantity{7},
    };

    requireCommonFields(fill.fields, md::OrderStatus::PartiallyFilled, "order fill");
    require(fill.fill_price == 101'300'000'000LL, "order fill price");
    require(fill.fill_size == 4, "order fill size");
    require(fill.remaining_size == 7, "order fill remaining size");
}

void testOrderRejectCarriesFieldsReasonAndText()
{
    const md::OrderReject reject{
        .fields = makeFields(md::OrderStatus::Rejected),
        .reason = md::OrderRejectReason::InvalidPrice,
        .text = "price is undefined",
    };

    requireCommonFields(reject.fields, md::OrderStatus::Rejected, "order reject");
    require(reject.reason == md::OrderRejectReason::InvalidPrice, "order reject reason");
    require(reject.text == "price is undefined", "order reject text");
}

void testOrderRequestVariantReportsMessageType()
{
    const md::OrderRequest new_order = md::NewOrder{.fields = makeFields()};
    const md::OrderRequest cancel_order = md::CancelOrder{.fields = makeFields()};
    const md::OrderRequest modify_order = md::ModifyOrder{.fields = makeFields()};

    require(md::messageType(new_order) == md::OrderMessageType::NewOrder, "new order message type");
    require(md::messageType(cancel_order) == md::OrderMessageType::CancelOrder, "cancel order message type");
    require(md::messageType(modify_order) == md::OrderMessageType::ModifyOrder, "modify order message type");
}

void testOrderEventVariantReportsMessageType()
{
    const md::OrderEvent ack = md::OrderAck{.fields = makeFields()};
    const md::OrderEvent fill = md::OrderFill{.fields = makeFields()};
    const md::OrderEvent reject = md::OrderReject{.fields = makeFields()};

    require(md::messageType(ack) == md::OrderMessageType::OrderAck, "order ack message type");
    require(md::messageType(fill) == md::OrderMessageType::OrderFill, "order fill message type");
    require(md::messageType(reject) == md::OrderMessageType::OrderReject, "order reject message type");
}

void testHelpersWorkForRequestsAndEvents()
{
    const md::OrderRequest requests[]{
        md::NewOrder{.fields = makeFields(md::OrderStatus::New)},
        md::CancelOrder{.fields = makeFields(md::OrderStatus::CancelRequested)},
        md::ModifyOrder{.fields = makeFields(md::OrderStatus::ModifyRequested)},
    };
    const md::OrderStatus request_statuses[]{
        md::OrderStatus::New,
        md::OrderStatus::CancelRequested,
        md::OrderStatus::ModifyRequested,
    };

    for (std::size_t i = 0; i < std::size(requests); ++i)
    {
        require(md::tradingEngineId(requests[i]) == 7, "request tradingEngineId");
        require(md::orderId(requests[i]) == 1001, "request orderId");
        require(md::instrumentId(requests[i]) == 42, "request instrumentId");
        require(md::timestampNs(requests[i]) == 1'700'000'000'000'000'001LL, "request timestampNs");
        require(md::status(requests[i]) == request_statuses[i], "request status");
    }

    const md::OrderEvent events[]{
        md::OrderAck{.fields = makeFields(md::OrderStatus::Accepted), .ack_type = md::OrderAckType::NewAccepted},
        md::OrderFill{
            .fields = makeFields(md::OrderStatus::Filled),
            .fill_price = md::Price{101'250'000'000LL},
            .fill_size = md::Quantity{11},
            .remaining_size = md::Quantity{0},
        },
        md::OrderReject{
            .fields = makeFields(md::OrderStatus::Rejected),
            .reason = md::OrderRejectReason::InternalError,
            .text = "internal error",
        },
    };
    const md::OrderStatus event_statuses[]{
        md::OrderStatus::Accepted,
        md::OrderStatus::Filled,
        md::OrderStatus::Rejected,
    };

    for (std::size_t i = 0; i < std::size(events); ++i)
    {
        require(md::tradingEngineId(events[i]) == 7, "event tradingEngineId");
        require(md::orderId(events[i]) == 1001, "event orderId");
        require(md::instrumentId(events[i]) == 42, "event instrumentId");
        require(md::timestampNs(events[i]) == 1'700'000'000'000'000'001LL, "event timestampNs");
        require(md::status(events[i]) == event_statuses[i], "event status");
    }
}

void testFieldsHelperReturnsMutableFields()
{
    md::OrderRequest request = md::NewOrder{.fields = makeFields()};
    md::fields(request).status = md::OrderStatus::Accepted;
    md::fields(request).order_id = md::OrderId{2002};

    require(md::status(request) == md::OrderStatus::Accepted, "mutable request fields status");
    require(md::orderId(request) == 2002, "mutable request fields order id");

    md::OrderEvent event = md::OrderReject{.fields = makeFields(), .reason = md::OrderRejectReason::InternalError};
    md::fields(event).status = md::OrderStatus::Rejected;

    require(md::status(event) == md::OrderStatus::Rejected, "mutable event fields status");
}

void testOrderStatusLifecycleStatesCompile()
{
    static_assert(std::is_same_v<std::underlying_type_t<md::OrderStatus>, std::uint8_t>);

    const md::OrderStatus states[]{
        md::OrderStatus::New,
        md::OrderStatus::Accepted,
        md::OrderStatus::ModifyRequested,
        md::OrderStatus::CancelRequested,
        md::OrderStatus::PartiallyFilled,
        md::OrderStatus::Filled,
        md::OrderStatus::Cancelled,
        md::OrderStatus::Rejected,
    };

    require(states[0] == md::OrderStatus::New, "order status new");
    require(states[1] == md::OrderStatus::Accepted, "order status accepted");
    require(states[2] == md::OrderStatus::ModifyRequested, "order status modify requested");
    require(states[3] == md::OrderStatus::CancelRequested, "order status cancel requested");
    require(states[4] == md::OrderStatus::PartiallyFilled, "order status partially filled");
    require(states[5] == md::OrderStatus::Filled, "order status filled");
    require(states[6] == md::OrderStatus::Cancelled, "order status cancelled");
    require(states[7] == md::OrderStatus::Rejected, "order status rejected");
}

} // namespace

int main()
{
    try
    {
        testOrderFieldTypesUseDomainAliases();
        testRequestMessagesCarryCommonFields();
        testOrderAckCarriesFieldsAndAckType();
        testOrderFillCarriesFieldsAndFillData();
        testOrderRejectCarriesFieldsReasonAndText();
        testOrderRequestVariantReportsMessageType();
        testOrderEventVariantReportsMessageType();
        testHelpersWorkForRequestsAndEvents();
        testFieldsHelperReturnsMutableFields();
        testOrderStatusLifecycleStatesCompile();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
