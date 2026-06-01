#pragma once

#include "lob/HistoricalLobStore.hpp"
#include "processing/IMarketDataEventProcessor.hpp"

#include <cstddef>

namespace md::lob
{

class HistoricalLobProcessor final : public IMarketDataEventProcessor
{
  public:
    void processMarketDataEvent(const MarketDataEvent& event) override;

    [[nodiscard]] const HistoricalLOB& book(InstrumentId instrument_id) const;
    [[nodiscard]] std::size_t instrumentCount() const noexcept;
    [[nodiscard]] const HistoricalLobStore& store() const noexcept;

  private:
    HistoricalLobStore books_;
};

} // namespace md::lob
