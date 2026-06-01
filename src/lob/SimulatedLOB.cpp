#include "lob/SimulatedLOB.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <vector>

namespace md::lob
{
namespace
{

using BidLevels = std::map<Price, Quantity, std::greater<Price>>;
using AskLevels = std::map<Price, Quantity>;

void addLevel(BidLevels& levels, Price price, Quantity size)
{
    if (size > 0)
    {
        levels[price] += size;
    }
}

void addLevel(AskLevels& levels, Price price, Quantity size)
{
    if (size > 0)
    {
        levels[price] += size;
    }
}

void removeLevel(BidLevels& levels, Price price, Quantity size)
{
    const auto it = levels.find(price);
    if (it == levels.end())
    {
        return;
    }

    if (it->second <= size)
    {
        levels.erase(it);
    }
    else
    {
        it->second -= size;
    }
}

void removeLevel(AskLevels& levels, Price price, Quantity size)
{
    const auto it = levels.find(price);
    if (it == levels.end())
    {
        return;
    }

    if (it->second <= size)
    {
        levels.erase(it);
    }
    else
    {
        it->second -= size;
    }
}

BookLevel toBookLevel(const std::pair<const Price, Quantity>& level)
{
    return BookLevel{
        .price = level.first,
        .size = level.second,
    };
}

template <typename Levels>
std::vector<BookLevel> topLevels(const Levels& levels, std::size_t depth)
{
    std::vector<BookLevel> result;
    result.reserve(std::min(depth, levels.size()));

    std::size_t copied = 0;
    for (const auto& level : levels)
    {
        if (copied++ >= depth)
        {
            break;
        }
        result.push_back(toBookLevel(level));
    }

    return result;
}

BidLevels visibleBids(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id)
{
    BidLevels levels;
    if (historical_snapshot.instrument_id == instrument_id)
    {
        for (const auto& level : historical_snapshot.bids)
        {
            addLevel(levels, level.price, level.size);
        }
    }

    const auto& consumed = view.consumedHistoricalLiquidity(instrument_id);
    for (const auto& level : consumed.bids(consumed.bidLevelCount()))
    {
        removeLevel(levels, level.price, level.size);
    }

    const auto& synthetic = view.syntheticBook(instrument_id);
    for (const auto& level : synthetic.bids(synthetic.bidLevelCount()))
    {
        addLevel(levels, level.price, level.size);
    }

    return levels;
}

AskLevels visibleAsks(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id)
{
    AskLevels levels;
    if (historical_snapshot.instrument_id == instrument_id)
    {
        for (const auto& level : historical_snapshot.asks)
        {
            addLevel(levels, level.price, level.size);
        }
    }

    const auto& consumed = view.consumedHistoricalLiquidity(instrument_id);
    for (const auto& level : consumed.asks(consumed.askLevelCount()))
    {
        removeLevel(levels, level.price, level.size);
    }

    const auto& synthetic = view.syntheticBook(instrument_id);
    for (const auto& level : synthetic.asks(synthetic.askLevelCount()))
    {
        addLevel(levels, level.price, level.size);
    }

    return levels;
}

} // namespace

SimulatedLOB::SimulatedLOB(const HistoricalLOB& historical, const EngineView& view)
    : historical_(&historical), view_(view) {}

SimulatedLOB::SimulatedLOB(const HistoricalLobStore& historical, const EngineView& view)
    : store_(&historical), view_(view) {}

LobSnapshot SimulatedLOB::historicalSnapshot(InstrumentId instrument_id) const
{
    constexpr auto all_levels = std::numeric_limits<std::size_t>::max();
    if (store_ != nullptr)
    {
        return store_->snapshot(instrument_id, all_levels);
    }

    const auto snapshot = historical_->snapshot(all_levels);
    if (snapshot.instrument_id != instrument_id)
    {
        return LobSnapshot{.instrument_id = instrument_id, .bids = {}, .asks = {}};
    }

    return snapshot;
}

std::optional<BookLevel> SimulatedLOB::bestBid(InstrumentId instrument_id) const
{
    const auto levels = visibleBids(historicalSnapshot(instrument_id), view_, instrument_id);
    if (levels.empty())
    {
        return std::nullopt;
    }

    return toBookLevel(*levels.begin());
}

std::optional<BookLevel> SimulatedLOB::bestAsk(InstrumentId instrument_id) const
{
    const auto levels = visibleAsks(historicalSnapshot(instrument_id), view_, instrument_id);
    if (levels.empty())
    {
        return std::nullopt;
    }

    return toBookLevel(*levels.begin());
}

LobSnapshot SimulatedLOB::snapshot(InstrumentId instrument_id, std::size_t depth) const
{
    const auto historical = historicalSnapshot(instrument_id);
    return LobSnapshot{
        .instrument_id = instrument_id,
        .bids = topLevels(visibleBids(historical, view_, instrument_id), depth),
        .asks = topLevels(visibleAsks(historical, view_, instrument_id), depth),
    };
}

} // namespace md::lob
