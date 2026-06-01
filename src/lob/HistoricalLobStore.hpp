#pragma once

#include "domain/MarketDataEvent.hpp"
#include "lob/HistoricalLOB.hpp"
#include "lob/LobTypes.hpp"

#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace md::lob {

class HistoricalLobStore {
public:
    void apply(const MarketDataEvent& event);

    [[nodiscard]] std::optional<BookLevel> bestBid(InstrumentId instrument_id) const;
    [[nodiscard]] std::optional<BookLevel> bestAsk(InstrumentId instrument_id) const;
    [[nodiscard]] LobSnapshot snapshot(InstrumentId instrument_id, std::size_t depth) const;
    [[nodiscard]] HistoricalLOB bookSnapshot(InstrumentId instrument_id) const;
    [[nodiscard]] std::vector<InstrumentId> instrumentIds() const;
    [[nodiscard]] std::size_t totalRestingOrderCount() const;
    [[nodiscard]] std::size_t instrumentCount() const noexcept;
    [[nodiscard]] std::string stableStateDigest() const;

private:
    std::unordered_map<InstrumentId, HistoricalLOB> books_;
    mutable std::shared_mutex mutex_;
};

} // namespace md::lob
