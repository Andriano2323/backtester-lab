#pragma once

#include "concurrency/NonBlockingQueue.hpp"
#include "messaging/MarketDataMessage.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

namespace md
{

using MarketDataCallback = std::function<void(const MarketDataMessage&)>;

class MarketDataSubscriber
{
  public:
    explicit MarketDataSubscriber(MarketDataCallback callback, std::size_t batch_size = 256)
        : queue_(batch_size), callback_(std::move(callback))
    {
    }

    void push(MarketDataMessage message)
    {
        queue_.push(std::move(message));
    }

    void flush()
    {
        queue_.flush();
    }

    std::optional<MarketDataMessage> tryPop()
    {
        return queue_.tryPop();
    }

    MarketDataMessage pop()
    {
        return queue_.pop();
    }

    std::size_t drainAvailable()
    {
        std::size_t drained = 0;
        while (std::optional<MarketDataMessage> message = tryPop())
        {
            callback_(*message);
            ++drained;
        }
        return drained;
    }

    [[nodiscard]] std::size_t pendingApprox() const
    {
        return queue_.size();
    }

  private:
    NonBlockingQueue<MarketDataMessage> queue_;
    MarketDataCallback callback_;
};

} // namespace md
