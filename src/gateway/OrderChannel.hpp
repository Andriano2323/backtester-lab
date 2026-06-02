#pragma once

#include "concurrency/NonBlockingQueue.hpp"
#include "gateway/OrderMessage.hpp"

#include <cstddef>
#include <optional>
#include <utility>

namespace md
{

class OrderChannel
{
  public:
    explicit OrderChannel(std::size_t request_batch_size = 256, std::size_t event_batch_size = 256)
        : requests_(request_batch_size), events_(event_batch_size)
    {
    }

    void pushRequest(OrderRequest request)
    {
        requests_.push(std::move(request));
    }

    void flushRequests()
    {
        requests_.flush();
    }

    std::optional<OrderRequest> tryPopRequest()
    {
        return requests_.tryPop();
    }

    OrderRequest popRequest()
    {
        return requests_.pop();
    }

    void pushEvent(OrderEvent event)
    {
        events_.push(std::move(event));
    }

    void flushEvents()
    {
        events_.flush();
    }

    std::optional<OrderEvent> tryPopEvent()
    {
        return events_.tryPop();
    }

    OrderEvent popEvent()
    {
        return events_.pop();
    }

  private:
    NonBlockingQueue<OrderRequest> requests_;
    NonBlockingQueue<OrderEvent> events_;
};

} // namespace md
