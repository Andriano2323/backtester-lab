#pragma once

#include "domain/Types.hpp"
#include "messaging/MarketDataMessage.hpp"
#include "messaging/MarketDataSubscriber.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace md
{

using SubscriberId = std::uint64_t;

struct MarketDataSubscription
{
    SubscriberId subscriber_id{};
    InstrumentId instrument_id{};
    std::shared_ptr<MarketDataSubscriber> subscriber;
};

class MarketDataPublisher
{
  public:
    MarketDataSubscription subscribe(
        InstrumentId instrument_id,
        MarketDataCallback callback,
        std::size_t queue_batch_size = 256);

    SeqNo publishUpdate(BookUpdate update);
    SeqNo publishSnapshot(BookSnapshot snapshot);
    SeqNo publishTrade(Trade trade);

    void flush();

    [[nodiscard]] std::size_t subscriberCount() const;
    [[nodiscard]] std::size_t subscriberCount(InstrumentId instrument_id) const;

  private:
    SeqNo nextSeqNo(InstrumentId instrument_id);
    void publish(MarketDataMessage message);

    SubscriberId next_subscriber_id_{1};
    std::size_t subscriber_count_{0};
    std::unordered_map<InstrumentId, SeqNo> last_seq_no_by_instrument_;
    std::unordered_map<InstrumentId, std::vector<MarketDataSubscription>> subscribers_by_instrument_;
};

} // namespace md
