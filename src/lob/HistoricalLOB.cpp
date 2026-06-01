#include "lob/HistoricalLOB.hpp"

#include <algorithm>
#include <limits>

namespace md::lob {
namespace {

bool hasValidSide(Side side) {
    return side == Side::Bid || side == Side::Ask;
}

bool hasValidPrice(Price price) {
    return price != std::numeric_limits<Price>::max();
}

bool hasValidRestingState(const MarketDataEvent& event) {
    return event.order_id != 0
        && hasValidSide(event.side)
        && hasValidPrice(event.price)
        && event.size > 0;
}

BookLevel toBookLevel(const std::pair<const Price, Quantity>& level) {
    return BookLevel{
        .price = level.first,
        .size = level.second,
    };
}

} // namespace

void HistoricalLOB::apply(const MarketDataEvent& event) {
    if (event.instrument_id != 0) {
        instrument_id_ = event.instrument_id;
    }

    switch (event.action) {
        case Action::Add:
            applyAdd(event);
            break;
        case Action::Modify:
            applyModify(event);
            break;
        case Action::Cancel:
            applyCancel(event);
            break;
        case Action::Clear:
            applyClear(event);
            break;
        case Action::Trade:
        case Action::Fill:
        case Action::None:
            break;
    }
}

void HistoricalLOB::applyAdd(const MarketDataEvent& event) {
    if (!hasValidRestingState(event)) {
        return;
    }

    removeOrder(event.order_id);

    orders_by_id_[event.order_id] = HistoricalOrder{
        .instrument_id = event.instrument_id,
        .side = event.side,
        .price = event.price,
        .quantity = event.size,
    };
    addLevelVolume(event.side, event.price, event.size);
}

void HistoricalLOB::applyModify(const MarketDataEvent& event) {
    const auto it = orders_by_id_.find(event.order_id);
    if (it == orders_by_id_.end() || !hasValidRestingState(event)) {
        return;
    }

    removeLevelVolume(it->second.side, it->second.price, it->second.quantity);
    it->second = HistoricalOrder{
        .instrument_id = event.instrument_id,
        .side = event.side,
        .price = event.price,
        .quantity = event.size,
    };
    addLevelVolume(event.side, event.price, event.size);
}

void HistoricalLOB::applyCancel(const MarketDataEvent& event) {
    const auto it = orders_by_id_.find(event.order_id);
    if (it == orders_by_id_.end()) {
        return;
    }

    auto& order = it->second;
    const auto cancel_quantity = event.size == 0
        ? order.quantity
        : std::min<Quantity>(event.size, order.quantity);

    removeLevelVolume(order.side, order.price, cancel_quantity);
    order.quantity -= cancel_quantity;

    if (order.quantity == 0) {
        orders_by_id_.erase(it);
    }
}

void HistoricalLOB::applyClear(const MarketDataEvent& event) {
    if (event.instrument_id == 0) {
        bids_.clear();
        asks_.clear();
        orders_by_id_.clear();
        return;
    }

    for (auto it = orders_by_id_.begin(); it != orders_by_id_.end();) {
        if (it->second.instrument_id != event.instrument_id) {
            ++it;
            continue;
        }

        removeLevelVolume(it->second.side, it->second.price, it->second.quantity);
        it = orders_by_id_.erase(it);
    }
}

void HistoricalLOB::removeOrder(HistoricalOrderId order_id) {
    const auto it = orders_by_id_.find(order_id);
    if (it == orders_by_id_.end()) {
        return;
    }

    removeLevelVolume(it->second.side, it->second.price, it->second.quantity);
    orders_by_id_.erase(it);
}

void HistoricalLOB::addLevelVolume(Side side, Price price, Quantity quantity) {
    if (side == Side::Bid) {
        bids_[price] += quantity;
    } else if (side == Side::Ask) {
        asks_[price] += quantity;
    }
}

void HistoricalLOB::removeLevelVolume(Side side, Price price, Quantity quantity) {
    if (side == Side::Bid) {
        const auto it = bids_.find(price);
        if (it == bids_.end()) {
            return;
        }
        if (it->second <= quantity) {
            bids_.erase(it);
        } else {
            it->second -= quantity;
        }
    } else if (side == Side::Ask) {
        const auto it = asks_.find(price);
        if (it == asks_.end()) {
            return;
        }
        if (it->second <= quantity) {
            asks_.erase(it);
        } else {
            it->second -= quantity;
        }
    }
}

std::optional<BookLevel> HistoricalLOB::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*bids_.begin());
}

std::optional<BookLevel> HistoricalLOB::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*asks_.begin());
}

std::vector<BookLevel> HistoricalLOB::bids(std::size_t depth) const {
    std::vector<BookLevel> levels;
    levels.reserve(std::min(depth, bids_.size()));

    std::size_t copied = 0;
    for (const auto& level : bids_) {
        if (copied++ >= depth) {
            break;
        }
        levels.push_back(toBookLevel(level));
    }

    return levels;
}

std::vector<BookLevel> HistoricalLOB::asks(std::size_t depth) const {
    std::vector<BookLevel> levels;
    levels.reserve(std::min(depth, asks_.size()));

    std::size_t copied = 0;
    for (const auto& level : asks_) {
        if (copied++ >= depth) {
            break;
        }
        levels.push_back(toBookLevel(level));
    }

    return levels;
}

LobSnapshot HistoricalLOB::snapshot(std::size_t depth) const {
    return LobSnapshot{
        .instrument_id = instrument_id_,
        .bids = bids(depth),
        .asks = asks(depth),
    };
}

std::size_t HistoricalLOB::restingOrderCount() const noexcept {
    return orders_by_id_.size();
}

std::size_t HistoricalLOB::bidLevelCount() const noexcept {
    return bids_.size();
}

std::size_t HistoricalLOB::askLevelCount() const noexcept {
    return asks_.size();
}

} // namespace md::lob
