#include "backtest/OrderExecutionBridge.hpp"

#include <functional>

namespace md::backtest
{

std::size_t OrderExecutionBridge::MappingKeyHash::operator()(const MappingKey& key) const noexcept
{
    const std::size_t engine_hash = std::hash<TradingEngineId>{}(key.trading_engine_id);
    const std::size_t order_hash = std::hash<OrderId>{}(key.order_id);
    return engine_hash ^ (order_hash + 0x9e3779b97f4a7c15ULL + (engine_hash << 6U) + (engine_hash >> 2U));
}

OrderExecutionBridge::OrderExecutionBridge(
    OrderGatewayServer& server,
    const lob::HistoricalLobStore& historical_lobs,
    EngineViews& engine_views)
    : server_(server), engine_views_(engine_views), simulator_(historical_lobs, engine_views) {}

OrderExecutionBridgeResult OrderExecutionBridge::drainAndProcessRequests()
{
    OrderExecutionBridgeResult result;
    result.transitions = server_.drainRequestTransitions();
    for (const OrderGatewayTransition& transition : result.transitions)
    {
        processTransition(transition, result);
    }
    return result;
}

std::size_t OrderExecutionBridge::mappingCount() const noexcept
{
    return mappings_.size();
}

std::optional<ExecutionOrderMapping> OrderExecutionBridge::findMapping(
    TradingEngineId trading_engine_id,
    OrderId gateway_order_id) const
{
    const auto it = mappings_.find(MappingKey{
        .trading_engine_id = trading_engine_id,
        .order_id = gateway_order_id,
    });
    if (it == mappings_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void OrderExecutionBridge::processTransition(
    const OrderGatewayTransition& transition,
    OrderExecutionBridgeResult& result)
{
    switch (transition.type)
    {
    case OrderGatewayTransitionType::NewAccepted:
        processNewAccepted(transition, result);
        break;
    case OrderGatewayTransitionType::CancelAccepted:
        processCancelAccepted(transition, result);
        break;
    case OrderGatewayTransitionType::ModifyAccepted:
        processModifyAccepted(transition, result);
        break;
    case OrderGatewayTransitionType::Rejected:
        ++result.rejected_count;
        break;
    }
}

void OrderExecutionBridge::processNewAccepted(
    const OrderGatewayTransition& transition,
    OrderExecutionBridgeResult& result)
{
    ++result.new_accepted_count;
    submitAcceptedOrder(transition, result);
}

void OrderExecutionBridge::processCancelAccepted(
    const OrderGatewayTransition& transition,
    OrderExecutionBridgeResult& result)
{
    ++result.cancel_accepted_count;
    eraseMapping(transition.fields.trading_engine_id, transition.fields.order_id, result);
}

void OrderExecutionBridge::processModifyAccepted(
    const OrderGatewayTransition& transition,
    OrderExecutionBridgeResult& result)
{
    ++result.modify_accepted_count;
    eraseMapping(transition.fields.trading_engine_id, transition.fields.order_id, result);
    submitAcceptedOrder(transition, result);
}

void OrderExecutionBridge::submitAcceptedOrder(
    const OrderGatewayTransition& transition,
    OrderExecutionBridgeResult& result)
{
    const lob::SimulatedOrderResult simulated_result = simulator_.submitLimitOrderResult(lob::SimulatedOrderRequest{
        .engine_id = static_cast<lob::EngineId>(transition.fields.trading_engine_id),
        .instrument_id = transition.fields.instrument_id,
        .side = transition.fields.side,
        .limit_price = transition.fields.price,
        .size = transition.fields.size,
        .timestamp_ns = transition.fields.timestamp_ns,
    });

    for (const lob::SimulatedFill& fill : simulated_result.fills)
    {
        if (server_.emitFill(
                transition.fields.trading_engine_id,
                transition.fields.order_id,
                fill.price,
                fill.size,
                fill.timestamp_ns))
        {
            ++result.fills_emitted;
        }
    }

    storeOrEraseMapping(transition, simulated_result, result);
}

void OrderExecutionBridge::storeOrEraseMapping(
    const OrderGatewayTransition& transition,
    const lob::SimulatedOrderResult& simulated_result,
    OrderExecutionBridgeResult& result)
{
    const MappingKey key{
        .trading_engine_id = transition.fields.trading_engine_id,
        .order_id = transition.fields.order_id,
    };

    if (simulated_result.synthetic_order_id == 0 || simulated_result.remaining_size == 0)
    {
        eraseMapping(key.trading_engine_id, key.order_id, result);
        return;
    }

    const bool existed = mappings_.find(key) != mappings_.end();
    mappings_[key] = ExecutionOrderMapping{
        .trading_engine_id = transition.fields.trading_engine_id,
        .gateway_order_id = transition.fields.order_id,
        .synthetic_order_id = simulated_result.synthetic_order_id,
        .instrument_id = transition.fields.instrument_id,
        .side = transition.fields.side,
        .price = transition.fields.price,
        .remaining_size = simulated_result.remaining_size,
        .last_timestamp_ns = transition.fields.timestamp_ns,
    };
    if (!existed)
    {
        ++result.mappings_added;
    }
}

void OrderExecutionBridge::eraseMapping(
    TradingEngineId trading_engine_id,
    OrderId gateway_order_id,
    OrderExecutionBridgeResult& result)
{
    const MappingKey key{
        .trading_engine_id = trading_engine_id,
        .order_id = gateway_order_id,
    };
    const auto it = mappings_.find(key);
    if (it == mappings_.end())
    {
        return;
    }

    // Synthetic order ids are scoped by EngineView, so the mapping is the only
    // place where gateway ids and synthetic ids meet.
    const auto engine_it = engine_views_.find(static_cast<lob::EngineId>(trading_engine_id));
    if (engine_it != engine_views_.end())
    {
        engine_it->second.cancelSyntheticOrder(it->second.synthetic_order_id);
    }
    mappings_.erase(it);
    ++result.mappings_erased;
}

} // namespace md::backtest
