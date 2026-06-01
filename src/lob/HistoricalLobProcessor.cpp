#include "lob/HistoricalLobProcessor.hpp"

namespace md::lob
{

void HistoricalLobProcessor::processMarketDataEvent(const MarketDataEvent& event)
{
    books_.apply(event);
}

const HistoricalLOB& HistoricalLobProcessor::book(InstrumentId instrument_id) const
{
    static thread_local HistoricalLOB snapshot;
    snapshot = books_.bookSnapshot(instrument_id);
    return snapshot;
}

std::size_t HistoricalLobProcessor::instrumentCount() const noexcept
{
    return books_.instrumentCount();
}

const HistoricalLobStore& HistoricalLobProcessor::store() const noexcept
{
    return books_;
}

} // namespace md::lob
