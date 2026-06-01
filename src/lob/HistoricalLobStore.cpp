#include "lob/HistoricalLobStore.hpp"

#include "domain/MarketDataEvent.hpp"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <shared_mutex>

namespace md::lob {
namespace {

std::vector<InstrumentId> sortedInstrumentIds(const std::unordered_map<InstrumentId, HistoricalLOB>& books) {
    std::vector<InstrumentId> ids;
    ids.reserve(books.size());
    for (const auto& [instrument_id, book] : books) {
        (void)book;
        ids.push_back(instrument_id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string formatOptionalLevel(const std::optional<BookLevel>& level) {
    if (!level.has_value()) {
        return "<none>";
    }

    return formatPrice(level->price) + "x" + std::to_string(level->size);
}

} // namespace

void HistoricalLobStore::apply(const MarketDataEvent& event) {
    std::unique_lock lock{mutex_};
    if (event.action == Action::Clear && event.instrument_id == 0) {
        books_.clear();
        return;
    }
    if (event.instrument_id == 0) {
        return;
    }

    books_[event.instrument_id].apply(event);
}

std::optional<BookLevel> HistoricalLobStore::bestBid(InstrumentId instrument_id) const {
    std::shared_lock lock{mutex_};
    const auto it = books_.find(instrument_id);
    return it == books_.end() ? std::nullopt : it->second.bestBid();
}

std::optional<BookLevel> HistoricalLobStore::bestAsk(InstrumentId instrument_id) const {
    std::shared_lock lock{mutex_};
    const auto it = books_.find(instrument_id);
    return it == books_.end() ? std::nullopt : it->second.bestAsk();
}

LobSnapshot HistoricalLobStore::snapshot(InstrumentId instrument_id, std::size_t depth) const {
    std::shared_lock lock{mutex_};
    const auto it = books_.find(instrument_id);
    if (it == books_.end()) {
        return LobSnapshot{.instrument_id = instrument_id, .bids = {}, .asks = {}};
    }

    return it->second.snapshot(depth);
}

HistoricalLOB HistoricalLobStore::bookSnapshot(InstrumentId instrument_id) const {
    std::shared_lock lock{mutex_};
    const auto it = books_.find(instrument_id);
    return it == books_.end() ? HistoricalLOB{} : it->second;
}

std::vector<InstrumentId> HistoricalLobStore::instrumentIds() const {
    std::shared_lock lock{mutex_};
    return sortedInstrumentIds(books_);
}

std::size_t HistoricalLobStore::totalRestingOrderCount() const {
    std::shared_lock lock{mutex_};
    std::size_t total = 0;
    for (const auto& [instrument_id, book] : books_) {
        (void)instrument_id;
        total += book.restingOrderCount();
    }
    return total;
}

std::size_t HistoricalLobStore::instrumentCount() const noexcept {
    std::shared_lock lock{mutex_};
    return books_.size();
}

std::string HistoricalLobStore::stableStateDigest() const {
    std::shared_lock lock{mutex_};

    std::ostringstream out;
    out << "instrument_count=" << books_.size()
        << ";resting_orders=";

    std::size_t total_resting_orders = 0;
    for (const auto& [instrument_id, book] : books_) {
        (void)instrument_id;
        total_resting_orders += book.restingOrderCount();
    }
    out << total_resting_orders;

    for (const auto instrument_id : sortedInstrumentIds(books_)) {
        const auto& book = books_.at(instrument_id);
        out << "|instrument=" << instrument_id
            << ",orders=" << book.restingOrderCount()
            << ",best_bid=" << formatOptionalLevel(book.bestBid())
            << ",best_ask=" << formatOptionalLevel(book.bestAsk());

        out << ",bids=[";
        bool first = true;
        for (const auto& level : book.bids(book.bidLevelCount())) {
            if (!first) {
                out << ';';
            }
            first = false;
            out << formatPrice(level.price) << 'x' << level.size;
        }
        out << "]";

        out << ",asks=[";
        first = true;
        for (const auto& level : book.asks(book.askLevelCount())) {
            if (!first) {
                out << ';';
            }
            first = false;
            out << formatPrice(level.price) << 'x' << level.size;
        }
        out << "]";
    }

    return out.str();
}

} // namespace md::lob
