#pragma once

#include "lob/EngineView.hpp"
#include "lob/HistoricalLOB.hpp"
#include "lob/HistoricalLobStore.hpp"
#include "lob/LobTypes.hpp"

#include <cstddef>
#include <optional>

namespace md::lob {

class SimulatedLOB {
public:
    SimulatedLOB(const HistoricalLOB& historical, const EngineView& view);
    SimulatedLOB(const HistoricalLobStore& historical, const EngineView& view);

    [[nodiscard]] std::optional<BookLevel> bestBid(InstrumentId instrument_id) const;
    [[nodiscard]] std::optional<BookLevel> bestAsk(InstrumentId instrument_id) const;
    [[nodiscard]] LobSnapshot snapshot(InstrumentId instrument_id, std::size_t depth) const;

private:
    [[nodiscard]] LobSnapshot historicalSnapshot(InstrumentId instrument_id) const;

    const HistoricalLOB* historical_{};
    const HistoricalLobStore* store_{};
    const EngineView& view_;
};

} // namespace md::lob
