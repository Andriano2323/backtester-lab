#pragma once

#include "domain/MarketDataEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace md {

struct UnknownOrderDiagnostic {
    std::string operation;
    std::string decision;
    std::uint64_t timestamp{};
    std::uint64_t instrument_id{};
    std::uint64_t order_id{};
    Side side{Side::None};
    std::int64_t price{};
    std::uint64_t size{};
    std::uint32_t source_file_id{};
    std::uint64_t source_sequence{};
    std::size_t line_number{};
};

class LimitOrderBook {
public:
    using BidLevels = std::map<std::int64_t, std::uint64_t, std::greater<>>;
    using AskLevels = std::map<std::int64_t, std::uint64_t>;

    explicit LimitOrderBook(std::uint64_t instrument_id);

    void apply(const MarketDataEvent& event);

    [[nodiscard]] std::optional<std::int64_t> bestBid() const;
    [[nodiscard]] std::optional<std::int64_t> bestAsk() const;

    [[nodiscard]] std::uint64_t volumeAt(Side side, std::int64_t price) const;
    [[nodiscard]] std::size_t restingOrderCount() const noexcept;
    [[nodiscard]] std::size_t skippedUnknownOrderCount() const noexcept;
    [[nodiscard]] std::size_t unknownModifyRecoveredAsAddCount() const noexcept;
    [[nodiscard]] std::size_t unknownModifySkippedCount() const noexcept;
    [[nodiscard]] std::size_t unknownCancelSkippedCount() const noexcept;
    [[nodiscard]] std::size_t tradeCount() const noexcept;
    [[nodiscard]] std::size_t fillCount() const noexcept;
    [[nodiscard]] std::uint64_t instrumentId() const noexcept;
    [[nodiscard]] bool containsOrder(std::uint64_t order_id) const noexcept;
    [[nodiscard]] const BidLevels& bidLevelsView() const noexcept;
    [[nodiscard]] const AskLevels& askLevelsView() const noexcept;
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::uint64_t>> bidLevels() const;
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::uint64_t>> askLevels() const;
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::uint64_t>> bidLevels(std::size_t depth) const;
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::uint64_t>> askLevels(std::size_t depth) const;
    [[nodiscard]] const std::vector<UnknownOrderDiagnostic>& unknownOrderDiagnostics() const noexcept;

    void printSnapshot(std::ostream& out, std::size_t depth) const;

private:
    struct RestingOrder {
        Side side{Side::None};
        std::int64_t price{};
        std::uint64_t size{};
    };

    void applyAdd(const MarketDataEvent& event);
    void applyCancel(const MarketDataEvent& event);
    void applyModify(const MarketDataEvent& event);
    void applyClear(const MarketDataEvent& event);
    void applyTrade(const MarketDataEvent& event);
    void applyFill(const MarketDataEvent& event);
    void removeOrder(std::uint64_t order_id);
    void addLevelVolume(Side side, std::int64_t price, std::uint64_t size);
    void removeLevelVolume(Side side, std::int64_t price, std::uint64_t size);
    void recordUnknownOrderDiagnostic(
        const MarketDataEvent& event,
        const std::string& operation,
        const std::string& decision
    );

    std::uint64_t instrument_id_{};
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<std::uint64_t, RestingOrder> orders_;
    std::size_t unknown_modify_recovered_as_add_count_{};
    std::size_t unknown_modify_skipped_count_{};
    std::size_t unknown_cancel_skipped_count_{};
    std::size_t trade_count_{};
    std::size_t fill_count_{};
    std::vector<UnknownOrderDiagnostic> unknown_order_diagnostics_;
};

} // namespace md
