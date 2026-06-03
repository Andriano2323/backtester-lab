#pragma once

#include "gateway/OrderGatewayServer.hpp"
#include "lob/FillSimulator.hpp"
#include "lob/HistoricalLobStore.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace md::backtest
{

struct ExecutionOrderMapping
{
    TradingEngineId trading_engine_id{};
    OrderId gateway_order_id{};
    lob::SyntheticOrderId synthetic_order_id{};
    InstrumentId instrument_id{};
    Side side{Side::None};
    Price price{};
    Quantity remaining_size{};
    TimestampNs last_timestamp_ns{};
};

struct OrderExecutionBridgeResult
{
    std::vector<OrderGatewayTransition> transitions{};
    std::uint64_t new_accepted_count{};
    std::uint64_t cancel_accepted_count{};
    std::uint64_t modify_accepted_count{};
    std::uint64_t rejected_count{};
    std::uint64_t fills_emitted{};
    std::uint64_t mappings_added{};
    std::uint64_t mappings_erased{};
};

class OrderExecutionBridge
{
  public:
    using EngineViews = lob::FillSimulator::EngineViews;

    OrderExecutionBridge(
        OrderGatewayServer& server,
        const lob::HistoricalLobStore& historical_lobs,
        EngineViews& engine_views);

    [[nodiscard]] OrderExecutionBridgeResult drainAndProcessRequests();

    [[nodiscard]] std::size_t mappingCount() const noexcept;
    [[nodiscard]] std::optional<ExecutionOrderMapping> findMapping(
        TradingEngineId trading_engine_id,
        OrderId gateway_order_id) const;

  private:
    struct MappingKey
    {
        TradingEngineId trading_engine_id{};
        OrderId order_id{};

        [[nodiscard]] bool operator==(const MappingKey& other) const noexcept = default;
    };

    struct MappingKeyHash
    {
        [[nodiscard]] std::size_t operator()(const MappingKey& key) const noexcept;
    };

    void processTransition(const OrderGatewayTransition& transition, OrderExecutionBridgeResult& result);
    void processNewAccepted(const OrderGatewayTransition& transition, OrderExecutionBridgeResult& result);
    void processCancelAccepted(const OrderGatewayTransition& transition, OrderExecutionBridgeResult& result);
    void processModifyAccepted(const OrderGatewayTransition& transition, OrderExecutionBridgeResult& result);
    void submitAcceptedOrder(const OrderGatewayTransition& transition, OrderExecutionBridgeResult& result);
    void storeOrEraseMapping(
        const OrderGatewayTransition& transition,
        const lob::SimulatedOrderResult& simulated_result,
        OrderExecutionBridgeResult& result);
    void eraseMapping(TradingEngineId trading_engine_id, OrderId gateway_order_id, OrderExecutionBridgeResult& result);

    OrderGatewayServer& server_;
    EngineViews& engine_views_;
    lob::FillSimulator simulator_;
    std::unordered_map<MappingKey, ExecutionOrderMapping, MappingKeyHash> mappings_;
};

} // namespace md::backtest
