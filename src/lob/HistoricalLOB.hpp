#pragma once

#include "domain/MarketDataEvent.hpp"
#include "lob/LobTypes.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace md::lob {

struct BookLevel {
    Price price{};
    Quantity size{};
};

struct LobSnapshot {
    InstrumentId instrument_id{};
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

class HistoricalLOB {
public:
    void apply(const MarketDataEvent& event);

    [[nodiscard]] std::optional<BookLevel> bestBid() const;
    [[nodiscard]] std::optional<BookLevel> bestAsk() const;
    [[nodiscard]] std::vector<BookLevel> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<BookLevel> asks(std::size_t depth) const;
    [[nodiscard]] LobSnapshot snapshot(std::size_t depth) const;

    [[nodiscard]] std::size_t restingOrderCount() const noexcept;
    [[nodiscard]] std::size_t bidLevelCount() const noexcept;
    [[nodiscard]] std::size_t askLevelCount() const noexcept;

private:
    struct HistoricalOrder {
        InstrumentId instrument_id{};
        Side side{Side::None};
        Price price{};
        Quantity quantity{};
    };

    using BidLevels = std::map<Price, Quantity, std::greater<Price>>;
    using AskLevels = std::map<Price, Quantity>;

    void applyAdd(const MarketDataEvent& event);
    void applyModify(const MarketDataEvent& event);
    void applyCancel(const MarketDataEvent& event);
    void applyClear(const MarketDataEvent& event);
    void removeOrder(HistoricalOrderId order_id);
    void addLevelVolume(Side side, Price price, Quantity quantity);
    void removeLevelVolume(Side side, Price price, Quantity quantity);

    InstrumentId instrument_id_{};
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<HistoricalOrderId, HistoricalOrder> orders_by_id_;
};

} // namespace md::lob
