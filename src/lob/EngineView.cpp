#include "lob/EngineView.hpp"

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

bool hasValidSyntheticOrder(Side side, Price price, Quantity size) {
    return hasValidSide(side) && hasValidPrice(price) && size > 0;
}

BookLevel toBookLevel(const std::pair<const Price, Quantity>& level) {
    return BookLevel{
        .price = level.first,
        .size = level.second,
    };
}

} // namespace

std::optional<BookLevel> SyntheticBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*bids_.begin());
}

std::optional<BookLevel> SyntheticBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    return toBookLevel(*asks_.begin());
}

std::vector<BookLevel> SyntheticBook::bids(std::size_t depth) const {
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

std::vector<BookLevel> SyntheticBook::asks(std::size_t depth) const {
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

std::size_t SyntheticBook::bidLevelCount() const noexcept {
    return bids_.size();
}

std::size_t SyntheticBook::askLevelCount() const noexcept {
    return asks_.size();
}

void SyntheticBook::addLevelVolume(Side side, Price price, Quantity size) {
    if (side == Side::Bid) {
        bids_[price] += size;
    } else if (side == Side::Ask) {
        asks_[price] += size;
    }
}

void SyntheticBook::removeLevelVolume(Side side, Price price, Quantity size) {
    if (side == Side::Bid) {
        const auto it = bids_.find(price);
        if (it == bids_.end()) {
            return;
        }
        if (it->second <= size) {
            bids_.erase(it);
        } else {
            it->second -= size;
        }
    } else if (side == Side::Ask) {
        const auto it = asks_.find(price);
        if (it == asks_.end()) {
            return;
        }
        if (it->second <= size) {
            asks_.erase(it);
        } else {
            it->second -= size;
        }
    }
}

void ConsumedLiquidityBook::consume(Side side, Price price, Quantity size) {
    if (side == Side::Bid) {
        bids_[price] += size;
    } else if (side == Side::Ask) {
        asks_[price] += size;
    }
}

Quantity ConsumedLiquidityBook::consumedAt(Side side, Price price) const {
    if (side == Side::Bid) {
        const auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second;
    }

    if (side == Side::Ask) {
        const auto it = asks_.find(price);
        return it == asks_.end() ? 0 : it->second;
    }

    return 0;
}

std::vector<BookLevel> ConsumedLiquidityBook::bids(std::size_t depth) const {
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

std::vector<BookLevel> ConsumedLiquidityBook::asks(std::size_t depth) const {
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

std::size_t ConsumedLiquidityBook::bidLevelCount() const noexcept {
    return bids_.size();
}

std::size_t ConsumedLiquidityBook::askLevelCount() const noexcept {
    return asks_.size();
}

EngineView::EngineView(EngineId engine_id)
    : engine_id_(engine_id) {}

EngineId EngineView::engineId() const noexcept {
    return engine_id_;
}

SyntheticOrderId EngineView::addSyntheticOrder(
    InstrumentId instrument_id,
    Side side,
    Price price,
    Quantity size,
    TimestampNs timestamp_ns
) {
    if (instrument_id == 0 || !hasValidSyntheticOrder(side, price, size)) {
        return 0;
    }

    std::lock_guard lock{mutex_};
    const SyntheticOrderId order_id = next_order_id_++;
    own_orders_[order_id] = SyntheticOrder{
        .instrument_id = instrument_id,
        .side = side,
        .price = price,
        .size = size,
        .timestamp_ns = timestamp_ns,
    };
    synthetic_books_[instrument_id].addLevelVolume(side, price, size);
    return order_id;
}

SyntheticOrderId EngineView::reserveSyntheticOrderId() noexcept {
    std::lock_guard lock{mutex_};
    return next_order_id_++;
}

void EngineView::addSyntheticOrderWithId(
    SyntheticOrderId order_id,
    InstrumentId instrument_id,
    Side side,
    Price price,
    Quantity size,
    TimestampNs timestamp_ns
) {
    std::lock_guard lock{mutex_};
    own_orders_[order_id] = SyntheticOrder{
        .instrument_id = instrument_id,
        .side = side,
        .price = price,
        .size = size,
        .timestamp_ns = timestamp_ns,
    };
    synthetic_books_[instrument_id].addLevelVolume(side, price, size);
}

void EngineView::cancelSyntheticOrder(SyntheticOrderId order_id) {
    std::lock_guard lock{mutex_};
    const auto it = own_orders_.find(order_id);
    if (it == own_orders_.end()) {
        return;
    }

    const auto& order = it->second;
    synthetic_books_[order.instrument_id].removeLevelVolume(order.side, order.price, order.size);
    own_orders_.erase(it);
}

const SyntheticBook& EngineView::syntheticBook(InstrumentId instrument_id) const {
    std::lock_guard lock{mutex_};
    static thread_local SyntheticBook snapshot;
    const auto it = synthetic_books_.find(instrument_id);
    if (it == synthetic_books_.end()) {
        snapshot = SyntheticBook{};
    } else {
        snapshot = it->second;
    }

    return snapshot;
}

const ConsumedLiquidityBook& EngineView::consumedHistoricalLiquidity(InstrumentId instrument_id) const {
    std::lock_guard lock{mutex_};
    static thread_local ConsumedLiquidityBook snapshot;
    const auto it = consumed_historical_liquidity_.find(instrument_id);
    if (it == consumed_historical_liquidity_.end()) {
        snapshot = ConsumedLiquidityBook{};
    } else {
        snapshot = it->second;
    }

    return snapshot;
}

void EngineView::consumeHistoricalLiquidity(InstrumentId instrument_id, Side side, Price price, Quantity size) {
    if (instrument_id == 0 || size == 0) {
        return;
    }

    std::lock_guard lock{mutex_};
    consumed_historical_liquidity_[instrument_id].consume(side, price, size);
}

} // namespace md::lob
