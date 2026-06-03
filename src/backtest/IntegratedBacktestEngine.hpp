#pragma once

#include "backtest/MarketDataEventAdapter.hpp"
#include "domain/MarketDataEvent.hpp"
#include "messaging/MarketDataPublisher.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace md::backtest
{

struct IntegratedBacktestConfig
{
    std::vector<InstrumentId> instruments{};
    bool publish_book_updates{true};
    bool publish_trades{true};
    std::size_t snapshot_depth{5};
    std::uint64_t snapshot_interval_events{0};
    std::uint64_t max_events{0};
    std::size_t subscriber_queue_batch_size{256};
};

struct IntegratedBacktestEventResult
{
    bool processed{false};
    bool skipped_by_filter{false};
    bool skipped_by_max_events{false};
    bool chronological_violation{false};
    std::uint64_t input_event_count{};
    std::uint64_t accepted_event_count{};
    std::uint64_t published_message_count{};
    std::uint64_t delivered_message_count{};
    std::vector<MarketDataMessage> published_messages{};
};

struct IntegratedBacktestRunResult
{
    std::uint64_t input_event_count{};
    std::uint64_t accepted_event_count{};
    std::uint64_t skipped_by_filter_count{};
    std::uint64_t skipped_by_max_events_count{};
    std::uint64_t chronological_violations{};
    std::uint64_t published_message_count{};
    std::uint64_t delivered_message_count{};
};

class IntegratedBacktestEngine
{
  public:
    explicit IntegratedBacktestEngine(IntegratedBacktestConfig config = {});

    MarketDataSubscription subscribe(InstrumentId instrument_id, MarketDataCallback callback);

    [[nodiscard]] IntegratedBacktestEventResult processEvent(const MarketDataEvent& event);
    [[nodiscard]] IntegratedBacktestRunResult run(const std::vector<MarketDataEvent>& events);
    std::size_t finish();

    [[nodiscard]] const IntegratedBacktestConfig& config() const noexcept;
    [[nodiscard]] const IntegratedBacktestRunResult& stats() const noexcept;
    [[nodiscard]] const lob::HistoricalLobStore& store() const noexcept;
    [[nodiscard]] lob::HistoricalLobStore& store() noexcept;

  private:
    [[nodiscard]] bool maxEventsReached() const noexcept;
    [[nodiscard]] MarketDataEventAdapterConfig adapterConfig() const;
    [[nodiscard]] MarketDataMessage publishMessage(const MarketDataMessage& message);
    [[nodiscard]] std::size_t drainSubscribers();

    IntegratedBacktestConfig config_;
    MarketDataEventAdapter adapter_;
    MarketDataPublisher publisher_;
    std::vector<MarketDataSubscription> subscriptions_;
    IntegratedBacktestRunResult stats_{};
    std::optional<MarketDataEvent> last_input_event_;
};

} // namespace md::backtest
