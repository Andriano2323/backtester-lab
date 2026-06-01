#include "TestSupport.hpp"

#include "lob/FillSimulator.hpp"
#include "lob/HistoricalLobStore.hpp"
#include "lob/SimulatedLOB.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace md::test
{
namespace
{

MarketDataEvent addHistorical(
    std::uint64_t order_id,
    Side side,
    md::lob::Price price,
    md::lob::Quantity size,
    std::uint64_t timestamp)
{
    MarketDataEvent event;
    event.timestamp = timestamp;
    event.ts_recv = timestamp;
    event.ts_event = timestamp;
    event.instrument_id = 42;
    event.order_id = order_id;
    event.side = side;
    event.price = price;
    event.size = size;
    event.action = Action::Add;
    return event;
}

void rethrowFirstFailure(const std::vector<std::exception_ptr>& failures)
{
    if (!failures.empty())
    {
        std::rethrow_exception(failures.front());
    }
}

} // namespace

void testConcurrentEngineViewsNoCrashesNoLostIsolation()
{
    constexpr std::size_t engine_count = 8;
    constexpr std::size_t writer_events = 10'000;
    constexpr std::size_t reader_iterations = 250;

    md::lob::HistoricalLobStore store;
    store.apply(addHistorical(1, Side::Ask, 101, 100'000, 1));

    md::lob::FillSimulator::EngineViews engine_views;
    for (md::lob::EngineId engine_id = 1; engine_id <= engine_count; ++engine_id)
    {
        engine_views.try_emplace(engine_id, engine_id);
    }

    std::atomic<bool> start{false};
    std::mutex failures_mutex;
    std::vector<std::exception_ptr> failures;

    auto captureFailure = [&failures_mutex, &failures]()
    {
        std::lock_guard lock{failures_mutex};
        failures.push_back(std::current_exception());
    };

    std::thread writer{[&]()
                       {
                           try
                           {
                               while (!start.load(std::memory_order_acquire))
                               {
                                   std::this_thread::yield();
                               }

                               for (std::size_t i = 0; i < writer_events; ++i)
                               {
                                   store.apply(addHistorical(
                                       10'000 + i,
                                       Side::Ask,
                                       101,
                                       10,
                                       static_cast<std::uint64_t>(2 + i)));
                               }
                           }
                           catch (...)
                           {
                               captureFailure();
                           }
                       }};

    std::vector<std::thread> readers;
    readers.reserve(engine_count);
    for (md::lob::EngineId engine_id = 1; engine_id <= engine_count; ++engine_id)
    {
        readers.emplace_back([&, engine_id]()
                             {
            try {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                md::lob::FillSimulator simulator{store, engine_views};
                auto& view = engine_views.at(engine_id);
                const md::lob::Price own_bid_price = 80 + static_cast<md::lob::Price>(engine_id);

                for (std::size_t i = 0; i < reader_iterations; ++i) {
                    const md::lob::SimulatedLOB visible_before{store, view};
                    const auto snapshot = visible_before.snapshot(42, 2);
                    require(snapshot.instrument_id == 42, "concurrent snapshot instrument id");

                    if (i % 2 == 0) {
                        const auto fills = simulator.submitLimitOrder(md::lob::SimulatedOrderRequest{
                            .engine_id = engine_id,
                            .instrument_id = 42,
                            .side = Side::Bid,
                            .limit_price = own_bid_price,
                            .size = 1,
                            .timestamp_ns = static_cast<md::lob::TimestampNs>(i),
                        });
                        require(fills.empty(), "non-crossing concurrent order should rest");
                    } else {
                        const auto fills = simulator.submitLimitOrder(md::lob::SimulatedOrderRequest{
                            .engine_id = engine_id,
                            .instrument_id = 42,
                            .side = Side::Bid,
                            .limit_price = 101,
                            .size = 1,
                            .timestamp_ns = static_cast<md::lob::TimestampNs>(i),
                        });
                        require(fills.size() == 1, "crossing concurrent order should fill");
                        require(fills[0].engine_id == engine_id, "concurrent fill engine id");
                        require(fills[0].price == 101, "concurrent fill price");
                        require(fills[0].size == 1, "concurrent fill size");
                    }

                    const md::lob::SimulatedLOB visible_after{store, view};
                    const auto own_best_bid = visible_after.bestBid(42);
                    require(own_best_bid.has_value(), "engine sees its own rested bid");
                    require(own_best_bid->price == own_bid_price, "engine sees only its own synthetic bid price");
                    require(own_best_bid->size > 0, "engine own synthetic bid has positive size");

                    const auto other_engine_id = engine_id == engine_count ? 1 : engine_id + 1;
                    const md::lob::SimulatedLOB other_visible{store, engine_views.at(other_engine_id)};
                    const auto other_best_bid = other_visible.bestBid(42);
                    require(
                        !other_best_bid.has_value() || other_best_bid->price != own_bid_price,
                        "other engine does not see this engine's private bid"
                    );
                }
            } catch (...) {
                captureFailure();
            } });
    }

    start.store(true, std::memory_order_release);

    writer.join();
    for (auto& reader : readers)
    {
        reader.join();
    }

    rethrowFirstFailure(failures);
}

} // namespace md::test
