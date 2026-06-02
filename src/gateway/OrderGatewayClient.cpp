#include "gateway/OrderGatewayClient.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace md
{

OrderGatewayClient::OrderGatewayClient(TradingEngineId trading_engine_id, std::shared_ptr<OrderChannel> channel)
    : trading_engine_id_(trading_engine_id), channel_(std::move(channel))
{
    if (channel_ == nullptr)
    {
        throw std::invalid_argument("OrderGatewayClient requires a non-null OrderChannel");
    }
}

OrderId OrderGatewayClient::sendOrder(
    InstrumentId instrument_id,
    Side side,
    Price price,
    Quantity size,
    TimestampNs timestamp_ns)
{
    const OrderId order_id = nextOrderId();
    sendOrder(NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id_,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = side,
            .price = price,
            .size = size,
            .timestamp_ns = timestamp_ns,
            .status = OrderStatus::New,
        },
    });
    return order_id;
}

void OrderGatewayClient::sendOrder(NewOrder order)
{
    order.fields.trading_engine_id = trading_engine_id_;
    channel_->pushRequest(std::move(order));
}

void OrderGatewayClient::cancelOrder(OrderId order_id, InstrumentId instrument_id, TimestampNs timestamp_ns)
{
    channel_->pushRequest(CancelOrder{
        .fields = {
            .trading_engine_id = trading_engine_id_,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = Side::None,
            .price = Price{},
            .size = Quantity{},
            .timestamp_ns = timestamp_ns,
            .status = OrderStatus::CancelRequested,
        },
    });
}

void OrderGatewayClient::modifyOrder(
    OrderId order_id,
    InstrumentId instrument_id,
    Side side,
    Price new_price,
    Quantity new_size,
    TimestampNs timestamp_ns)
{
    channel_->pushRequest(ModifyOrder{
        .fields = {
            .trading_engine_id = trading_engine_id_,
            .order_id = order_id,
            .instrument_id = instrument_id,
            .side = side,
            .price = new_price,
            .size = new_size,
            .timestamp_ns = timestamp_ns,
            .status = OrderStatus::ModifyRequested,
        },
    });
}

void OrderGatewayClient::flush()
{
    channel_->flushRequests();
}

std::size_t OrderGatewayClient::drainEvents()
{
    std::size_t drained = 0;
    while (std::optional<OrderEvent> event = tryPopEvent())
    {
        dispatchEvent(*event);
        ++drained;
    }
    return drained;
}

std::optional<OrderEvent> OrderGatewayClient::tryPopEvent()
{
    return channel_->tryPopEvent();
}

OrderEvent OrderGatewayClient::popEvent()
{
    return channel_->popEvent();
}

void OrderGatewayClient::onAck(std::function<void(const OrderAck&)> callback)
{
    on_ack_ = std::move(callback);
}

void OrderGatewayClient::onFill(std::function<void(const OrderFill&)> callback)
{
    on_fill_ = std::move(callback);
}

void OrderGatewayClient::onReject(std::function<void(const OrderReject&)> callback)
{
    on_reject_ = std::move(callback);
}

OrderId OrderGatewayClient::nextOrderId()
{
    return next_order_id_++;
}

void OrderGatewayClient::dispatchEvent(const OrderEvent& event)
{
    std::visit(
        [this](const auto& payload)
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, OrderAck>)
            {
                if (on_ack_)
                {
                    on_ack_(payload);
                }
            }
            else if constexpr (std::is_same_v<Payload, OrderFill>)
            {
                if (on_fill_)
                {
                    on_fill_(payload);
                }
            }
            else if constexpr (std::is_same_v<Payload, OrderReject>)
            {
                if (on_reject_)
                {
                    on_reject_(payload);
                }
            }
        },
        event);
}

} // namespace md
