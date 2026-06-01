#pragma once

#include "lob/HistoricalLOB.hpp"
#include "lob/LobTypes.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace md::lob {

class FillSimulator;
class SimulatedLOB;

class SyntheticBook {
public:
    [[nodiscard]] std::optional<BookLevel> bestBid() const;
    [[nodiscard]] std::optional<BookLevel> bestAsk() const;
    [[nodiscard]] std::vector<BookLevel> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<BookLevel> asks(std::size_t depth) const;
    [[nodiscard]] std::size_t bidLevelCount() const noexcept;
    [[nodiscard]] std::size_t askLevelCount() const noexcept;

private:
    using BidLevels = std::map<Price, Quantity, std::greater<Price>>;
    using AskLevels = std::map<Price, Quantity>;

    void addLevelVolume(Side side, Price price, Quantity size);
    void removeLevelVolume(Side side, Price price, Quantity size);

    BidLevels bids_;
    AskLevels asks_;

    friend class EngineView;
};

class ConsumedLiquidityBook {
public:
    void consume(Side side, Price price, Quantity size);
    [[nodiscard]] Quantity consumedAt(Side side, Price price) const;
    [[nodiscard]] std::vector<BookLevel> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<BookLevel> asks(std::size_t depth) const;
    [[nodiscard]] std::size_t bidLevelCount() const noexcept;
    [[nodiscard]] std::size_t askLevelCount() const noexcept;

private:
    using BidLevels = std::map<Price, Quantity, std::greater<Price>>;
    using AskLevels = std::map<Price, Quantity>;

    BidLevels bids_;
    AskLevels asks_;

    friend class SimulatedLOB;
};

class EngineView {
public:
    explicit EngineView(EngineId engine_id);

    [[nodiscard]] EngineId engineId() const noexcept;

    SyntheticOrderId addSyntheticOrder(
        InstrumentId instrument_id,
        Side side,
        Price price,
        Quantity size,
        TimestampNs timestamp_ns
    );

    void cancelSyntheticOrder(SyntheticOrderId order_id);

    [[nodiscard]] const SyntheticBook& syntheticBook(InstrumentId instrument_id) const;
    [[nodiscard]] const ConsumedLiquidityBook& consumedHistoricalLiquidity(InstrumentId instrument_id) const;

private:
    struct SyntheticOrder {
        InstrumentId instrument_id{};
        Side side{Side::None};
        Price price{};
        Quantity size{};
        TimestampNs timestamp_ns{};
    };

    EngineId engine_id_{};
    mutable std::mutex mutex_;
    SyntheticOrderId next_order_id_{1};
    SyntheticOrderId reserveSyntheticOrderId() noexcept;
    void addSyntheticOrderWithId(
        SyntheticOrderId order_id,
        InstrumentId instrument_id,
        Side side,
        Price price,
        Quantity size,
        TimestampNs timestamp_ns
    );
    void consumeHistoricalLiquidity(InstrumentId instrument_id, Side side, Price price, Quantity size);

    std::unordered_map<SyntheticOrderId, SyntheticOrder> own_orders_;
    std::unordered_map<InstrumentId, SyntheticBook> synthetic_books_;
    std::unordered_map<InstrumentId, ConsumedLiquidityBook> consumed_historical_liquidity_;

    friend class FillSimulator;
    friend class SimulatedLOB;
};

} // namespace md::lob
