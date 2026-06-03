#pragma once

#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
#include "gateway/OrderMessage.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace md
{

struct OrderGatewaySession
{
    TradingEngineId trading_engine_id{};
    std::shared_ptr<OrderChannel> channel;
};

struct OrderSnapshot
{
    TradingEngineId trading_engine_id{};
    OrderId order_id{};
    InstrumentId instrument_id{};
    Side side{Side::None};
    Price price{};
    Quantity original_size{};
    Quantity remaining_size{};
    OrderStatus status{OrderStatus::New};
    TimestampNs last_timestamp_ns{};
};

enum class OrderGatewayTransitionType
{
    NewAccepted,
    ModifyAccepted,
    CancelAccepted,
    Rejected
};

struct OrderGatewayTransition
{
    OrderGatewayTransitionType type{OrderGatewayTransitionType::Rejected};
    OrderMessageType request_type{OrderMessageType::NewOrder};
    OrderFields fields{};
    OrderSnapshot snapshot{};
    OrderRejectReason reject_reason{OrderRejectReason::InternalError};
    std::string reject_text{};
};

class OrderGatewayServer
{
  public:
    void registerEngine(TradingEngineId trading_engine_id, std::shared_ptr<OrderChannel> channel);

    [[nodiscard]] bool hasEngine(TradingEngineId trading_engine_id) const;
    [[nodiscard]] std::size_t engineCount() const;
    [[nodiscard]] std::shared_ptr<OrderChannel> channelFor(TradingEngineId trading_engine_id) const;

    std::size_t drainRequests();
    [[nodiscard]] std::vector<OrderGatewayTransition> drainRequestTransitions();
    std::size_t drainRequests(std::vector<OrderGatewayTransition>& transitions);
    void flushEvents();
    bool emitFill(
        TradingEngineId trading_engine_id,
        OrderId order_id,
        Price fill_price,
        Quantity fill_size,
        TimestampNs timestamp_ns);

    [[nodiscard]] std::size_t openOrderCount() const;
    [[nodiscard]] std::optional<OrderSnapshot> findOrder(TradingEngineId trading_engine_id, OrderId order_id) const;

  private:
    struct OrderKey
    {
        TradingEngineId trading_engine_id{};
        OrderId order_id{};

        [[nodiscard]] bool operator==(const OrderKey& other) const noexcept = default;
    };

    struct OrderKeyHash
    {
        [[nodiscard]] std::size_t operator()(const OrderKey& key) const noexcept
        {
            const std::size_t engine_hash = std::hash<TradingEngineId>{}(key.trading_engine_id);
            const std::size_t order_hash = std::hash<OrderId>{}(key.order_id);
            return engine_hash ^ (order_hash + 0x9e3779b97f4a7c15ULL + (engine_hash << 6U) + (engine_hash >> 2U));
        }
    };

    [[nodiscard]] OrderGatewayTransition processRequest(const OrderGatewaySession& session, const OrderRequest& request);
    [[nodiscard]] OrderGatewayTransition processNewOrder(const OrderGatewaySession& session, const NewOrder& order);
    [[nodiscard]] OrderGatewayTransition processCancelOrder(const OrderGatewaySession& session, const CancelOrder& order);
    [[nodiscard]] OrderGatewayTransition processModifyOrder(const OrderGatewaySession& session, const ModifyOrder& order);

    void pushAck(
        const OrderGatewaySession& session,
        const OrderFields& request_fields,
        OrderAckType ack_type,
        OrderStatus final_status);
    [[nodiscard]] OrderGatewayTransition pushReject(
        const OrderGatewaySession& session,
        const OrderFields& request_fields,
        OrderMessageType request_type,
        OrderRejectReason reason);
    [[nodiscard]] OrderGatewayTransition acceptedTransition(
        OrderGatewayTransitionType type,
        OrderMessageType request_type,
        const OrderFields& request_fields,
        const OrderSnapshot& snapshot,
        OrderStatus final_status) const;

    [[nodiscard]] static bool isTerminal(OrderStatus status) noexcept;
    [[nodiscard]] static bool isValidActiveSide(Side side) noexcept;
    [[nodiscard]] static const char* rejectText(OrderRejectReason reason) noexcept;

    std::vector<TradingEngineId> engine_order_;
    std::unordered_map<TradingEngineId, OrderGatewaySession> sessions_by_engine_;
    std::unordered_map<OrderKey, OrderSnapshot, OrderKeyHash> orders_by_key_;
};

} // namespace md
