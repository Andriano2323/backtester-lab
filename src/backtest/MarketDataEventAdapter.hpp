#pragma once

#include "domain/MarketDataEvent.hpp"
#include "lob/HistoricalLobStore.hpp"
#include "messaging/MarketDataMessage.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace md::backtest
{

struct MarketDataEventAdapterConfig
{
    std::vector<InstrumentId> instruments{};
    bool publish_book_updates{true};
    bool publish_trades{true};
    std::size_t snapshot_depth{5};
    std::uint64_t snapshot_interval_events{0};
};

struct MarketDataEventAdapterResult
{
    bool accepted{false};
    bool applied_to_lob{false};
    bool published_book_update{false};
    bool published_trade{false};
    bool published_snapshot{false};
    InstrumentId instrument_id{};
    std::uint64_t accepted_event_count{};
    std::vector<MarketDataMessage> messages{};
};

class MarketDataEventAdapter
{
  public:
    explicit MarketDataEventAdapter(MarketDataEventAdapterConfig config = {});

    [[nodiscard]] MarketDataEventAdapterResult process(const MarketDataEvent& event);

    [[nodiscard]] const lob::HistoricalLobStore& store() const noexcept;
    [[nodiscard]] lob::HistoricalLobStore& store() noexcept;

    [[nodiscard]] const MarketDataEventAdapterConfig& config() const noexcept;
    [[nodiscard]] std::uint64_t acceptedEventCount() const noexcept;

  private:
    [[nodiscard]] bool accepts(const MarketDataEvent& event) const;
    [[nodiscard]] bool shouldPublishSnapshot(const MarketDataEvent& event) const noexcept;
    [[nodiscard]] BookSnapshot makeSnapshot(const MarketDataEvent& event) const;
    [[nodiscard]] BookUpdate makeBookUpdate(const MarketDataEvent& event) const;
    [[nodiscard]] Trade makeTrade(const MarketDataEvent& event) const;

    MarketDataEventAdapterConfig config_;
    std::unordered_set<InstrumentId> instrument_filter_;
    lob::HistoricalLobStore store_;
    std::uint64_t accepted_event_count_{};
};

} // namespace md::backtest
