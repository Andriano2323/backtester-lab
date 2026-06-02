#pragma once

#include "domain/Types.hpp"
#include "gateway/OrderChannel.hpp"
#include "gateway/OrderMessage.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace md
{

class OrderGatewayClient
{
  public:
    OrderGatewayClient(TradingEngineId trading_engine_id, std::shared_ptr<OrderChannel> channel);

    OrderId sendOrder(
        InstrumentId instrument_id,
        Side side,
        Price price,
        Quantity size,
        TimestampNs timestamp_ns);
    void sendOrder(NewOrder order);

    void cancelOrder(OrderId order_id, InstrumentId instrument_id, TimestampNs timestamp_ns);
    void modifyOrder(
        OrderId order_id,
        InstrumentId instrument_id,
        Side side,
        Price new_price,
        Quantity new_size,
        TimestampNs timestamp_ns);

    void flush();

    std::size_t drainEvents();
    std::optional<OrderEvent> tryPopEvent();
    OrderEvent popEvent();

    void onAck(std::function<void(const OrderAck&)> callback);
    void onFill(std::function<void(const OrderFill&)> callback);
    void onReject(std::function<void(const OrderReject&)> callback);

  private:
    [[nodiscard]] OrderId nextOrderId();
    void dispatchEvent(const OrderEvent& event);

    TradingEngineId trading_engine_id_{};
    std::shared_ptr<OrderChannel> channel_;
    OrderId next_order_id_{1};

    std::function<void(const OrderAck&)> on_ack_;
    std::function<void(const OrderFill&)> on_fill_;
    std::function<void(const OrderReject&)> on_reject_;
};

} // namespace md
