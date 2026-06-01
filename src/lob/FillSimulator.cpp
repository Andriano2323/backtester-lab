#include "lob/FillSimulator.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <optional>

namespace md::lob {
namespace {

using BidLevels = std::map<Price, Quantity, std::greater<Price>>;
using AskLevels = std::map<Price, Quantity>;

bool hasValidSide(Side side) {
    return side == Side::Bid || side == Side::Ask;
}

bool hasValidPrice(Price price) {
    return price != std::numeric_limits<Price>::max();
}

bool hasValidRequest(const SimulatedOrderRequest& request) {
    return request.instrument_id != 0
        && hasValidSide(request.side)
        && hasValidPrice(request.limit_price)
        && request.size > 0;
}

void addLevel(BidLevels& levels, Price price, Quantity size) {
    if (size > 0) {
        levels[price] += size;
    }
}

void addLevel(AskLevels& levels, Price price, Quantity size) {
    if (size > 0) {
        levels[price] += size;
    }
}

void removeLevel(BidLevels& levels, Price price, Quantity size) {
    const auto it = levels.find(price);
    if (it == levels.end()) {
        return;
    }

    if (it->second <= size) {
        levels.erase(it);
    } else {
        it->second -= size;
    }
}

void removeLevel(AskLevels& levels, Price price, Quantity size) {
    const auto it = levels.find(price);
    if (it == levels.end()) {
        return;
    }

    if (it->second <= size) {
        levels.erase(it);
    } else {
        it->second -= size;
    }
}

BookLevel toBookLevel(const std::pair<const Price, Quantity>& level) {
    return BookLevel{
        .price = level.first,
        .size = level.second,
    };
}

BidLevels visibleHistoricalBids(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id
) {
    BidLevels levels;
    if (historical_snapshot.instrument_id == instrument_id) {
        for (const auto& level : historical_snapshot.bids) {
            addLevel(levels, level.price, level.size);
        }
    }

    const auto& consumed = view.consumedHistoricalLiquidity(instrument_id);
    for (const auto& level : consumed.bids(consumed.bidLevelCount())) {
        removeLevel(levels, level.price, level.size);
    }

    return levels;
}

AskLevels visibleHistoricalAsks(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id
) {
    AskLevels levels;
    if (historical_snapshot.instrument_id == instrument_id) {
        for (const auto& level : historical_snapshot.asks) {
            addLevel(levels, level.price, level.size);
        }
    }

    const auto& consumed = view.consumedHistoricalLiquidity(instrument_id);
    for (const auto& level : consumed.asks(consumed.askLevelCount())) {
        removeLevel(levels, level.price, level.size);
    }

    return levels;
}

std::optional<BookLevel> visibleHistoricalBestBid(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id
) {
    const auto levels = visibleHistoricalBids(historical_snapshot, view, instrument_id);
    if (levels.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*levels.begin());
}

std::optional<BookLevel> visibleHistoricalBestAsk(
    const LobSnapshot& historical_snapshot,
    const EngineView& view,
    InstrumentId instrument_id
) {
    const auto levels = visibleHistoricalAsks(historical_snapshot, view, instrument_id);
    if (levels.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*levels.begin());
}

SimulatedFill makeFill(
    const SimulatedOrderRequest& request,
    SyntheticOrderId order_id,
    Price price,
    Quantity size
) {
    return SimulatedFill{
        .engine_id = request.engine_id,
        .instrument_id = request.instrument_id,
        .order_id = order_id,
        .side = request.side,
        .price = price,
        .size = size,
        .timestamp_ns = request.timestamp_ns,
    };
}

} // namespace

FillSimulator::FillSimulator(const HistoricalLOB& historical, EngineViews& engine_views)
    : historical_(&historical), engine_views_(engine_views) {}

FillSimulator::FillSimulator(const HistoricalLobStore& historical, EngineViews& engine_views)
    : store_(&historical), engine_views_(engine_views) {}

LobSnapshot FillSimulator::historicalSnapshot(InstrumentId instrument_id) const {
    constexpr auto all_levels = std::numeric_limits<std::size_t>::max();
    if (store_ != nullptr) {
        return store_->snapshot(instrument_id, all_levels);
    }

    const auto snapshot = historical_->snapshot(all_levels);
    if (snapshot.instrument_id != instrument_id) {
        return LobSnapshot{.instrument_id = instrument_id, .bids = {}, .asks = {}};
    }

    return snapshot;
}

std::vector<SimulatedFill> FillSimulator::submitLimitOrder(const SimulatedOrderRequest& request) {
    std::vector<SimulatedFill> fills;
    if (!hasValidRequest(request)) {
        return fills;
    }

    const auto engine_it = engine_views_.find(request.engine_id);
    if (engine_it == engine_views_.end()) {
        return fills;
    }

    auto& view = engine_it->second;
    const SyntheticOrderId order_id = view.reserveSyntheticOrderId();
    Quantity remaining = request.size;

    while (remaining > 0) {
        const auto historical_snapshot = historicalSnapshot(request.instrument_id);
        if (request.side == Side::Bid) {
            const auto best_ask = visibleHistoricalBestAsk(historical_snapshot, view, request.instrument_id);
            if (!best_ask.has_value() || request.limit_price < best_ask->price) {
                break;
            }

            const auto fill_size = std::min(remaining, best_ask->size);
            view.consumeHistoricalLiquidity(request.instrument_id, Side::Ask, best_ask->price, fill_size);
            fills.push_back(makeFill(request, order_id, best_ask->price, fill_size));
            remaining -= fill_size;
        } else {
            const auto best_bid = visibleHistoricalBestBid(historical_snapshot, view, request.instrument_id);
            if (!best_bid.has_value() || request.limit_price > best_bid->price) {
                break;
            }

            const auto fill_size = std::min(remaining, best_bid->size);
            view.consumeHistoricalLiquidity(request.instrument_id, Side::Bid, best_bid->price, fill_size);
            fills.push_back(makeFill(request, order_id, best_bid->price, fill_size));
            remaining -= fill_size;
        }
    }

    if (remaining > 0) {
        view.addSyntheticOrderWithId(
            order_id,
            request.instrument_id,
            request.side,
            request.limit_price,
            remaining,
            request.timestamp_ns
        );
    }

    return fills;
}

} // namespace md::lob
