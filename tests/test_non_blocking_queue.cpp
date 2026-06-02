#include "concurrency/NonBlockingQueue.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testTryPopEmptyReturnsNullopt()
{
    md::NonBlockingQueue<int> queue(2);

    require(!queue.tryPop().has_value(), "tryPop on empty queue returns nullopt");
}

void testUnflushedPushIsNotVisible()
{
    md::NonBlockingQueue<int> queue(2);

    queue.push(1);

    require(!queue.tryPop().has_value(), "unflushed partial batch is not visible to tryPop");
}

void testFlushMakesValueVisible()
{
    md::NonBlockingQueue<int> queue(2);

    queue.push(42);
    queue.flush();

    const std::optional<int> value = queue.tryPop();
    require(value.has_value(), "flushed value is visible to tryPop");
    require(*value == 42, "tryPop returns flushed value");
    require(!queue.tryPop().has_value(), "tryPop returns nullopt after drained batch");
}

void testBatchSizeAutoFlush()
{
    md::NonBlockingQueue<int> queue(2);

    queue.push(1);
    require(!queue.tryPop().has_value(), "first item in partial batch is not visible");

    queue.push(2);

    const std::optional<int> first = queue.tryPop();
    const std::optional<int> second = queue.tryPop();
    require(first.has_value() && *first == 1, "auto-flushed batch returns first value");
    require(second.has_value() && *second == 2, "auto-flushed batch returns second value");
    require(!queue.tryPop().has_value(), "auto-flushed batch drains completely");
}

void testBlockingPopStillWorks()
{
    md::NonBlockingQueue<int> queue(2);

    queue.push(7);
    queue.flush();

    require(queue.pop() == 7, "blocking pop returns flushed value");
}

void testFifoOrderIsPreserved()
{
    md::NonBlockingQueue<int> queue(3);

    for (int value = 1; value <= 7; ++value)
    {
        queue.push(value);
    }
    queue.flush();

    for (int expected = 1; expected <= 7; ++expected)
    {
        const std::optional<int> value = queue.tryPop();
        require(value.has_value(), "tryPop returns expected FIFO item");
        require(*value == expected, "tryPop preserves FIFO order");
    }
    require(!queue.tryPop().has_value(), "tryPop returns nullopt after FIFO drain");
}

void testTryPopDrainsAcquiredBatchBeforeNewBatch()
{
    md::NonBlockingQueue<int> queue(2);

    queue.push(1);
    queue.push(2);
    require(queue.tryPop() == std::optional<int>{1}, "tryPop reads first acquired batch item");

    queue.push(3);
    queue.push(4);

    require(queue.tryPop() == std::optional<int>{2}, "tryPop drains acquired batch before acquiring a new one");
    require(queue.tryPop() == std::optional<int>{3}, "tryPop then acquires next batch");
    require(queue.tryPop() == std::optional<int>{4}, "tryPop reads final item");
    require(!queue.tryPop().has_value(), "tryPop returns nullopt after all batches drain");
}

} // namespace

int main()
{
    try
    {
        testTryPopEmptyReturnsNullopt();
        testUnflushedPushIsNotVisible();
        testFlushMakesValueVisible();
        testBatchSizeAutoFlush();
        testBlockingPopStillWorks();
        testFifoOrderIsPreserved();
        testTryPopDrainsAcquiredBatchBeforeNewBatch();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
