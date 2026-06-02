#pragma once

#include "domain/Types.hpp"

#include <string>
#include <variant>

namespace md
{

enum class OrderMessageType
{
    NewOrder,
    CancelOrder,
    ModifyOrder,
    OrderAck,
    OrderFill,
    OrderReject
};

enum class OrderAckType
{
    NewAccepted,
    ModifyAccepted,
    CancelAccepted
};

enum class OrderRejectReason
{
    DuplicateOrderId,
    UnknownOrderId,
    InvalidInstrument,
    InvalidSide,
    InvalidPrice,
    InvalidQuantity,
    AlreadyTerminal,
    InternalError
};

struct OrderFields
{
    TradingEngineId trading_engine_id{};
    OrderId order_id{};
    InstrumentId instrument_id{};
    Side side{Side::None};
    Price price{};
    Quantity size{};
    TimestampNs timestamp_ns{};
    OrderStatus status{OrderStatus::New};
};

struct NewOrder
{
    OrderFields fields{};
};

struct CancelOrder
{
    OrderFields fields{};
};

struct ModifyOrder
{
    OrderFields fields{};
};

struct OrderAck
{
    OrderFields fields{};
    OrderAckType ack_type{OrderAckType::NewAccepted};
};

struct OrderFill
{
    OrderFields fields{};
    Price fill_price{};
    Quantity fill_size{};
    Quantity remaining_size{};
};

struct OrderReject
{
    OrderFields fields{};
    OrderRejectReason reason{OrderRejectReason::InternalError};
    std::string text{};
};

using OrderRequest = std::variant<NewOrder, CancelOrder, ModifyOrder>;
using OrderEvent = std::variant<OrderAck, OrderFill, OrderReject>;

[[nodiscard]] inline OrderMessageType messageType(const NewOrder&) noexcept
{
    return OrderMessageType::NewOrder;
}

[[nodiscard]] inline OrderMessageType messageType(const CancelOrder&) noexcept
{
    return OrderMessageType::CancelOrder;
}

[[nodiscard]] inline OrderMessageType messageType(const ModifyOrder&) noexcept
{
    return OrderMessageType::ModifyOrder;
}

[[nodiscard]] inline OrderMessageType messageType(const OrderAck&) noexcept
{
    return OrderMessageType::OrderAck;
}

[[nodiscard]] inline OrderMessageType messageType(const OrderFill&) noexcept
{
    return OrderMessageType::OrderFill;
}

[[nodiscard]] inline OrderMessageType messageType(const OrderReject&) noexcept
{
    return OrderMessageType::OrderReject;
}

[[nodiscard]] inline OrderMessageType messageType(const OrderRequest& request)
{
    return std::visit([](const auto& message) { return messageType(message); }, request);
}

[[nodiscard]] inline OrderMessageType messageType(const OrderEvent& event)
{
    return std::visit([](const auto& message) { return messageType(message); }, event);
}

[[nodiscard]] inline OrderFields& fields(NewOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const NewOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(CancelOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const CancelOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(ModifyOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const ModifyOrder& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(OrderAck& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const OrderAck& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(OrderFill& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const OrderFill& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(OrderReject& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline const OrderFields& fields(const OrderReject& message) noexcept
{
    return message.fields;
}

[[nodiscard]] inline OrderFields& fields(OrderRequest& request)
{
    return std::visit([](auto& message) -> OrderFields& { return fields(message); }, request);
}

[[nodiscard]] inline const OrderFields& fields(const OrderRequest& request)
{
    return std::visit([](const auto& message) -> const OrderFields& { return fields(message); }, request);
}

[[nodiscard]] inline OrderFields& fields(OrderEvent& event)
{
    return std::visit([](auto& message) -> OrderFields& { return fields(message); }, event);
}

[[nodiscard]] inline const OrderFields& fields(const OrderEvent& event)
{
    return std::visit([](const auto& message) -> const OrderFields& { return fields(message); }, event);
}

template <typename Message>
    requires requires(const Message& message) { fields(message); }
[[nodiscard]] TradingEngineId tradingEngineId(const Message& message)
{
    return fields(message).trading_engine_id;
}

template <typename Message>
    requires requires(const Message& message) { fields(message); }
[[nodiscard]] OrderId orderId(const Message& message)
{
    return fields(message).order_id;
}

template <typename Message>
    requires requires(const Message& message) { fields(message); }
[[nodiscard]] InstrumentId instrumentId(const Message& message)
{
    return fields(message).instrument_id;
}

template <typename Message>
    requires requires(const Message& message) { fields(message); }
[[nodiscard]] TimestampNs timestampNs(const Message& message)
{
    return fields(message).timestamp_ns;
}

template <typename Message>
    requires requires(const Message& message) { fields(message); }
[[nodiscard]] OrderStatus status(const Message& message)
{
    return fields(message).status;
}

} // namespace md
