#include "backtest/MarketDataEventAdapter.hpp"

#include <limits>
#include <utility>

namespace md::backtest
{
namespace
{

bool isLobMutatingAction(Action action) noexcept
{
    return action == Action::Add || action == Action::Modify || action == Action::Cancel || action == Action::Clear;
}

bool isTradeLikeAction(Action action) noexcept
{
    return action == Action::Trade || action == Action::Fill;
}

TimestampNs toTimestampNs(RawTimestampNs timestamp) noexcept
{
    if (timestamp == raw_undefined_timestamp)
    {
        return undefined_timestamp;
    }

    constexpr auto max_timestamp = static_cast<RawTimestampNs>(std::numeric_limits<TimestampNs>::max());
    if (timestamp > max_timestamp)
    {
        return undefined_timestamp;
    }

    return static_cast<TimestampNs>(timestamp);
}

std::vector<PriceLevel> toPriceLevels(const std::vector<lob::BookLevel>& levels)
{
    std::vector<PriceLevel> result;
    result.reserve(levels.size());
    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        result.push_back(PriceLevel{
            .level_index = static_cast<std::uint32_t>(index),
            .price = levels[index].price,
            .size = levels[index].size,
        });
    }
    return result;
}

} // namespace

MarketDataEventAdapter::MarketDataEventAdapter(MarketDataEventAdapterConfig config)
    : config_(std::move(config))
{
    instrument_filter_.reserve(config_.instruments.size());
    for (const InstrumentId instrument_id : config_.instruments)
    {
        if (instrument_id != 0)
        {
            instrument_filter_.insert(instrument_id);
        }
    }
}

MarketDataEventAdapterResult MarketDataEventAdapter::process(const MarketDataEvent& event)
{
    MarketDataEventAdapterResult result{
        .instrument_id = event.instrument_id,
        .accepted_event_count = accepted_event_count_,
    };

    if (!accepts(event))
    {
        return result;
    }

    result.accepted = true;
    ++accepted_event_count_;
    result.accepted_event_count = accepted_event_count_;

    if (isLobMutatingAction(event.action))
    {
        store_.apply(event);
        result.applied_to_lob = true;

        if (config_.publish_book_updates && event.instrument_id != 0)
        {
            result.messages.push_back(MarketDataMessage{makeBookUpdate(event)});
            result.published_book_update = true;
        }
    }
    else if (isTradeLikeAction(event.action) && config_.publish_trades && event.instrument_id != 0)
    {
        result.messages.push_back(MarketDataMessage{makeTrade(event)});
        result.published_trade = true;
    }

    if (shouldPublishSnapshot(event))
    {
        result.messages.push_back(MarketDataMessage{makeSnapshot(event)});
        result.published_snapshot = true;
    }

    return result;
}

const lob::HistoricalLobStore& MarketDataEventAdapter::store() const noexcept
{
    return store_;
}

lob::HistoricalLobStore& MarketDataEventAdapter::store() noexcept
{
    return store_;
}

const MarketDataEventAdapterConfig& MarketDataEventAdapter::config() const noexcept
{
    return config_;
}

std::uint64_t MarketDataEventAdapter::acceptedEventCount() const noexcept
{
    return accepted_event_count_;
}

bool MarketDataEventAdapter::accepts(const MarketDataEvent& event) const
{
    if (instrument_filter_.empty())
    {
        return true;
    }

    if (event.action == Action::Clear && event.instrument_id == 0)
    {
        return true;
    }

    return instrument_filter_.find(event.instrument_id) != instrument_filter_.end();
}

bool MarketDataEventAdapter::shouldPublishSnapshot(const MarketDataEvent& event) const noexcept
{
    return config_.snapshot_interval_events > 0 && event.instrument_id != 0 && event.action != Action::None &&
           accepted_event_count_ % config_.snapshot_interval_events == 0;
}

BookSnapshot MarketDataEventAdapter::makeSnapshot(const MarketDataEvent& event) const
{
    const lob::LobSnapshot snapshot = store_.snapshot(event.instrument_id, config_.snapshot_depth);
    return BookSnapshot{
        .instrument_id = event.instrument_id,
        .timestamp_ns = toTimestampNs(event.timestamp),
        .seq_no = 0,
        .bids = toPriceLevels(snapshot.bids),
        .asks = toPriceLevels(snapshot.asks),
    };
}

BookUpdate MarketDataEventAdapter::makeBookUpdate(const MarketDataEvent& event) const
{
    return BookUpdate{
        .instrument_id = event.instrument_id,
        .timestamp_ns = toTimestampNs(event.timestamp),
        .seq_no = 0,
        .side = event.side,
        .price = event.price,
        .size = event.size,
    };
}

Trade MarketDataEventAdapter::makeTrade(const MarketDataEvent& event) const
{
    return Trade{
        .instrument_id = event.instrument_id,
        .timestamp_ns = toTimestampNs(event.timestamp),
        .seq_no = 0,
        .price = event.price,
        .size = event.size,
        .aggressor_side = event.side,
    };
}

} // namespace md::backtest
