#include "gateway/OrderGatewayServer.hpp"

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace md
{

void OrderGatewayServer::registerEngine(TradingEngineId trading_engine_id, std::shared_ptr<OrderChannel> channel)
{
    if (channel == nullptr)
    {
        throw std::invalid_argument("OrderGatewayServer requires a non-null OrderChannel");
    }

    if (!hasEngine(trading_engine_id))
    {
        engine_order_.push_back(trading_engine_id);
    }

    sessions_by_engine_[trading_engine_id] = OrderGatewaySession{
        .trading_engine_id = trading_engine_id,
        .channel = std::move(channel),
    };
}

bool OrderGatewayServer::hasEngine(TradingEngineId trading_engine_id) const
{
    return sessions_by_engine_.find(trading_engine_id) != sessions_by_engine_.end();
}

std::size_t OrderGatewayServer::engineCount() const
{
    return sessions_by_engine_.size();
}

std::shared_ptr<OrderChannel> OrderGatewayServer::channelFor(TradingEngineId trading_engine_id) const
{
    const auto it = sessions_by_engine_.find(trading_engine_id);
    if (it == sessions_by_engine_.end())
    {
        return nullptr;
    }
    return it->second.channel;
}

std::size_t OrderGatewayServer::drainRequests()
{
    std::size_t drained = 0;
    for (const TradingEngineId trading_engine_id : engine_order_)
    {
        const auto session_it = sessions_by_engine_.find(trading_engine_id);
        if (session_it == sessions_by_engine_.end())
        {
            continue;
        }

        const OrderGatewaySession& session = session_it->second;
        while (std::optional<OrderRequest> request = session.channel->tryPopRequest())
        {
            processRequest(session, *request);
            ++drained;
        }
    }
    return drained;
}

void OrderGatewayServer::flushEvents()
{
    for (const TradingEngineId trading_engine_id : engine_order_)
    {
        const auto session_it = sessions_by_engine_.find(trading_engine_id);
        if (session_it != sessions_by_engine_.end())
        {
            session_it->second.channel->flushEvents();
        }
    }
}

bool OrderGatewayServer::emitFill(
    TradingEngineId trading_engine_id,
    OrderId order_id,
    Price fill_price,
    Quantity fill_size,
    TimestampNs timestamp_ns)
{
    if (fill_size == 0)
    {
        return false;
    }

    const auto session_it = sessions_by_engine_.find(trading_engine_id);
    if (session_it == sessions_by_engine_.end())
    {
        return false;
    }

    const auto order_it = orders_by_key_.find(OrderKey{
        .trading_engine_id = trading_engine_id,
        .order_id = order_id,
    });
    if (order_it == orders_by_key_.end())
    {
        return false;
    }

    OrderSnapshot& order = order_it->second;
    if (isTerminal(order.status))
    {
        return false;
    }

    const Quantity actual_fill_size = fill_size < order.remaining_size ? fill_size : order.remaining_size;
    order.remaining_size -= actual_fill_size;
    order.status = order.remaining_size == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    order.last_timestamp_ns = timestamp_ns;

    OrderFill fill{
        .fields = {
            .trading_engine_id = order.trading_engine_id,
            .order_id = order.order_id,
            .instrument_id = order.instrument_id,
            .side = order.side,
            .price = order.price,
            .size = order.original_size,
            .timestamp_ns = timestamp_ns,
            .status = order.status,
        },
        .fill_price = fill_price,
        .fill_size = actual_fill_size,
        .remaining_size = order.remaining_size,
    };
    session_it->second.channel->pushEvent(std::move(fill));
    return true;
}

std::size_t OrderGatewayServer::openOrderCount() const
{
    std::size_t count = 0;
    for (const auto& [key, order] : orders_by_key_)
    {
        (void)key;
        if (!isTerminal(order.status))
        {
            ++count;
        }
    }
    return count;
}

std::optional<OrderSnapshot> OrderGatewayServer::findOrder(TradingEngineId trading_engine_id, OrderId order_id) const
{
    const auto it = orders_by_key_.find(OrderKey{
        .trading_engine_id = trading_engine_id,
        .order_id = order_id,
    });
    if (it == orders_by_key_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void OrderGatewayServer::processRequest(const OrderGatewaySession& session, const OrderRequest& request)
{
    std::visit(
        [this, &session](const auto& message)
        {
            using Request = std::decay_t<decltype(message)>;
            if constexpr (std::is_same_v<Request, NewOrder>)
            {
                processNewOrder(session, message);
            }
            else if constexpr (std::is_same_v<Request, CancelOrder>)
            {
                processCancelOrder(session, message);
            }
            else if constexpr (std::is_same_v<Request, ModifyOrder>)
            {
                processModifyOrder(session, message);
            }
        },
        request);
}

void OrderGatewayServer::processNewOrder(const OrderGatewaySession& session, const NewOrder& order)
{
    const OrderFields& request_fields = fields(order);
    const OrderKey key{
        .trading_engine_id = session.trading_engine_id,
        .order_id = request_fields.order_id,
    };

    if (orders_by_key_.find(key) != orders_by_key_.end())
    {
        pushReject(session, request_fields, OrderRejectReason::DuplicateOrderId);
        return;
    }
    if (request_fields.instrument_id == 0)
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidInstrument);
        return;
    }
    if (!isValidActiveSide(request_fields.side))
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidSide);
        return;
    }
    if (!isDefinedPrice(request_fields.price))
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidPrice);
        return;
    }
    if (request_fields.size == 0)
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidQuantity);
        return;
    }

    orders_by_key_.emplace(
        key,
        OrderSnapshot{
            .trading_engine_id = session.trading_engine_id,
            .order_id = request_fields.order_id,
            .instrument_id = request_fields.instrument_id,
            .side = request_fields.side,
            .price = request_fields.price,
            .original_size = request_fields.size,
            .remaining_size = request_fields.size,
            .status = OrderStatus::Accepted,
            .last_timestamp_ns = request_fields.timestamp_ns,
        });
    pushAck(session, request_fields, OrderAckType::NewAccepted, OrderStatus::Accepted);
}

void OrderGatewayServer::processCancelOrder(const OrderGatewaySession& session, const CancelOrder& order)
{
    const OrderFields& request_fields = fields(order);
    const OrderKey key{
        .trading_engine_id = session.trading_engine_id,
        .order_id = request_fields.order_id,
    };
    const auto it = orders_by_key_.find(key);
    if (it == orders_by_key_.end())
    {
        pushReject(session, request_fields, OrderRejectReason::UnknownOrderId);
        return;
    }
    if (isTerminal(it->second.status))
    {
        pushReject(session, request_fields, OrderRejectReason::AlreadyTerminal);
        return;
    }

    it->second.status = OrderStatus::Cancelled;
    it->second.last_timestamp_ns = request_fields.timestamp_ns;
    pushAck(session, request_fields, OrderAckType::CancelAccepted, OrderStatus::Cancelled);
}

void OrderGatewayServer::processModifyOrder(const OrderGatewaySession& session, const ModifyOrder& order)
{
    const OrderFields& request_fields = fields(order);
    const OrderKey key{
        .trading_engine_id = session.trading_engine_id,
        .order_id = request_fields.order_id,
    };
    const auto it = orders_by_key_.find(key);
    if (it == orders_by_key_.end())
    {
        pushReject(session, request_fields, OrderRejectReason::UnknownOrderId);
        return;
    }
    if (isTerminal(it->second.status))
    {
        pushReject(session, request_fields, OrderRejectReason::AlreadyTerminal);
        return;
    }
    if (!isValidActiveSide(request_fields.side))
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidSide);
        return;
    }
    if (!isDefinedPrice(request_fields.price))
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidPrice);
        return;
    }
    if (request_fields.size == 0)
    {
        pushReject(session, request_fields, OrderRejectReason::InvalidQuantity);
        return;
    }

    it->second.side = request_fields.side;
    it->second.price = request_fields.price;
    it->second.original_size = request_fields.size;
    it->second.remaining_size = request_fields.size;
    it->second.status = OrderStatus::Accepted;
    it->second.last_timestamp_ns = request_fields.timestamp_ns;
    pushAck(session, request_fields, OrderAckType::ModifyAccepted, OrderStatus::Accepted);
}

void OrderGatewayServer::pushAck(
    const OrderGatewaySession& session,
    const OrderFields& request_fields,
    OrderAckType ack_type,
    OrderStatus final_status)
{
    OrderAck ack{
        .fields = request_fields,
        .ack_type = ack_type,
    };
    ack.fields.trading_engine_id = session.trading_engine_id;
    ack.fields.status = final_status;
    session.channel->pushEvent(std::move(ack));
}

void OrderGatewayServer::pushReject(
    const OrderGatewaySession& session,
    const OrderFields& request_fields,
    OrderRejectReason reason)
{
    OrderReject reject{
        .fields = request_fields,
        .reason = reason,
        .text = rejectText(reason),
    };
    reject.fields.trading_engine_id = session.trading_engine_id;
    reject.fields.status = OrderStatus::Rejected;
    session.channel->pushEvent(std::move(reject));
}

bool OrderGatewayServer::isTerminal(OrderStatus status) noexcept
{
    return status == OrderStatus::Filled || status == OrderStatus::Cancelled || status == OrderStatus::Rejected;
}

bool OrderGatewayServer::isValidActiveSide(Side side) noexcept
{
    return side == Side::Bid || side == Side::Ask;
}

const char* OrderGatewayServer::rejectText(OrderRejectReason reason) noexcept
{
    switch (reason)
    {
    case OrderRejectReason::DuplicateOrderId:
        return "duplicate order id";
    case OrderRejectReason::UnknownOrderId:
        return "unknown order id";
    case OrderRejectReason::InvalidInstrument:
        return "invalid instrument";
    case OrderRejectReason::InvalidSide:
        return "invalid side";
    case OrderRejectReason::InvalidPrice:
        return "invalid price";
    case OrderRejectReason::InvalidQuantity:
        return "invalid quantity";
    case OrderRejectReason::AlreadyTerminal:
        return "order already terminal";
    case OrderRejectReason::InternalError:
        return "internal error";
    }
    return "internal error";
}

} // namespace md
