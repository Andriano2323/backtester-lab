#include "backtest/IntegratedBacktestEngine.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace md::backtest
{

IntegratedBacktestEngine::IntegratedBacktestEngine(IntegratedBacktestConfig config)
    : config_(std::move(config)), adapter_(adapterConfig()) {}

MarketDataSubscription IntegratedBacktestEngine::subscribe(
    InstrumentId instrument_id,
    MarketDataCallback callback)
{
    MarketDataSubscription subscription =
        publisher_.subscribe(instrument_id, std::move(callback), config_.subscriber_queue_batch_size);
    subscriptions_.push_back(subscription);
    return subscription;
}

IntegratedBacktestEventResult IntegratedBacktestEngine::processEvent(const MarketDataEvent& event)
{
    ++stats_.input_event_count;

    IntegratedBacktestEventResult result{
        .input_event_count = stats_.input_event_count,
        .accepted_event_count = stats_.accepted_event_count,
    };

    if (last_input_event_.has_value() && eventComesBefore(event, *last_input_event_))
    {
        result.chronological_violation = true;
        ++stats_.chronological_violations;
    }
    last_input_event_ = event;

    if (maxEventsReached())
    {
        result.skipped_by_max_events = true;
        ++stats_.skipped_by_max_events_count;
        return result;
    }

    MarketDataEventAdapterResult adapter_result = adapter_.process(event);
    if (!adapter_result.accepted)
    {
        result.skipped_by_filter = true;
        ++stats_.skipped_by_filter_count;
        return result;
    }

    result.processed = true;
    ++stats_.accepted_event_count;
    result.accepted_event_count = stats_.accepted_event_count;

    result.published_messages.reserve(adapter_result.messages.size());
    for (const MarketDataMessage& message : adapter_result.messages)
    {
        result.published_messages.push_back(publishMessage(message));
    }
    result.published_message_count = result.published_messages.size();
    stats_.published_message_count += result.published_message_count;

    publisher_.flush();
    result.delivered_message_count = drainSubscribers();
    stats_.delivered_message_count += result.delivered_message_count;

    return result;
}

IntegratedBacktestRunResult IntegratedBacktestEngine::run(const std::vector<MarketDataEvent>& events)
{
    for (const MarketDataEvent& event : events)
    {
        (void)processEvent(event);
    }
    (void)finish();
    return stats_;
}

std::size_t IntegratedBacktestEngine::finish()
{
    publisher_.flush();
    const std::size_t delivered = drainSubscribers();
    stats_.delivered_message_count += delivered;
    return delivered;
}

const IntegratedBacktestConfig& IntegratedBacktestEngine::config() const noexcept
{
    return config_;
}

const IntegratedBacktestRunResult& IntegratedBacktestEngine::stats() const noexcept
{
    return stats_;
}

const lob::HistoricalLobStore& IntegratedBacktestEngine::store() const noexcept
{
    return adapter_.store();
}

lob::HistoricalLobStore& IntegratedBacktestEngine::store() noexcept
{
    return adapter_.store();
}

bool IntegratedBacktestEngine::maxEventsReached() const noexcept
{
    return config_.max_events > 0 && stats_.accepted_event_count >= config_.max_events;
}

MarketDataEventAdapterConfig IntegratedBacktestEngine::adapterConfig() const
{
    return MarketDataEventAdapterConfig{
        .instruments = config_.instruments,
        .publish_book_updates = config_.publish_book_updates,
        .publish_trades = config_.publish_trades,
        .snapshot_depth = config_.snapshot_depth,
        .snapshot_interval_events = config_.snapshot_interval_events,
    };
}

MarketDataMessage IntegratedBacktestEngine::publishMessage(const MarketDataMessage& message)
{
    MarketDataMessage published_message = message;
    const SeqNo seq_no = std::visit(
        [this](const auto& payload) -> SeqNo
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, BookUpdate>)
            {
                return publisher_.publishUpdate(payload);
            }
            else if constexpr (std::is_same_v<Payload, BookSnapshot>)
            {
                return publisher_.publishSnapshot(payload);
            }
            else
            {
                return publisher_.publishTrade(payload);
            }
        },
        message);
    setSeqNo(published_message, seq_no);
    return published_message;
}

std::size_t IntegratedBacktestEngine::drainSubscribers()
{
    std::size_t delivered = 0;
    for (const MarketDataSubscription& subscription : subscriptions_)
    {
        delivered += subscription.subscriber->drainAvailable();
    }
    return delivered;
}

} // namespace md::backtest
