#include "messaging/MarketDataPublisher.hpp"

#include <utility>

namespace md
{

MarketDataSubscription MarketDataPublisher::subscribe(
    InstrumentId instrument_id,
    MarketDataCallback callback,
    std::size_t queue_batch_size)
{
    MarketDataSubscription subscription{
        .subscriber_id = next_subscriber_id_++,
        .instrument_id = instrument_id,
        .subscriber = std::make_shared<MarketDataSubscriber>(std::move(callback), queue_batch_size),
    };

    subscribers_by_instrument_[instrument_id].push_back(subscription);
    ++subscriber_count_;

    return subscription;
}

SeqNo MarketDataPublisher::publishUpdate(BookUpdate update)
{
    update.seq_no = nextSeqNo(update.instrument_id);
    const SeqNo assigned_seq_no = update.seq_no;
    publish(MarketDataMessage{std::move(update)});
    return assigned_seq_no;
}

SeqNo MarketDataPublisher::publishSnapshot(BookSnapshot snapshot)
{
    snapshot.seq_no = nextSeqNo(snapshot.instrument_id);
    const SeqNo assigned_seq_no = snapshot.seq_no;
    publish(MarketDataMessage{std::move(snapshot)});
    return assigned_seq_no;
}

SeqNo MarketDataPublisher::publishTrade(Trade trade)
{
    trade.seq_no = nextSeqNo(trade.instrument_id);
    const SeqNo assigned_seq_no = trade.seq_no;
    publish(MarketDataMessage{std::move(trade)});
    return assigned_seq_no;
}

void MarketDataPublisher::flush()
{
    for (auto& [instrument_id, subscriptions] : subscribers_by_instrument_)
    {
        (void)instrument_id;
        for (const MarketDataSubscription& subscription : subscriptions)
        {
            subscription.subscriber->flush();
        }
    }
}

std::size_t MarketDataPublisher::subscriberCount() const
{
    return subscriber_count_;
}

std::size_t MarketDataPublisher::subscriberCount(InstrumentId instrument_id) const
{
    const auto it = subscribers_by_instrument_.find(instrument_id);
    if (it == subscribers_by_instrument_.end())
    {
        return 0;
    }
    return it->second.size();
}

SeqNo MarketDataPublisher::nextSeqNo(InstrumentId instrument_id)
{
    SeqNo& last_seq_no = last_seq_no_by_instrument_[instrument_id];
    ++last_seq_no;
    return last_seq_no;
}

void MarketDataPublisher::publish(MarketDataMessage message)
{
    const auto it = subscribers_by_instrument_.find(instrumentId(message));
    if (it == subscribers_by_instrument_.end())
    {
        return;
    }

    for (const MarketDataSubscription& subscription : it->second)
    {
        subscription.subscriber->push(message);
    }
}

} // namespace md
