#pragma once

#include "lob/EngineView.hpp"
#include "lob/HistoricalLOB.hpp"
#include "lob/HistoricalLobStore.hpp"
#include "lob/LobTypes.hpp"

#include <unordered_map>
#include <vector>

namespace md::lob
{

struct SimulatedOrderRequest
{
    EngineId engine_id{};
    InstrumentId instrument_id{};
    Side side{Side::None};
    Price limit_price{};
    Quantity size{};
    TimestampNs timestamp_ns{};
};

struct SimulatedFill
{
    EngineId engine_id{};
    InstrumentId instrument_id{};
    SyntheticOrderId order_id{};
    Side side{Side::None};
    Price price{};
    Quantity size{};
    TimestampNs timestamp_ns{};
};

class FillSimulator
{
  public:
    using EngineViews = std::unordered_map<EngineId, EngineView>;

    FillSimulator(const HistoricalLOB& historical, EngineViews& engine_views);
    FillSimulator(const HistoricalLobStore& historical, EngineViews& engine_views);

    [[nodiscard]] std::vector<SimulatedFill> submitLimitOrder(const SimulatedOrderRequest& request);

  private:
    [[nodiscard]] LobSnapshot historicalSnapshot(InstrumentId instrument_id) const;

    const HistoricalLOB* historical_{};
    const HistoricalLobStore* store_{};
    EngineViews& engine_views_;
};

} // namespace md::lob
