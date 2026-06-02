#include "gateway/OrderChannel.hpp"
#include "gateway/OrderGatewayClient.hpp"
#include "gateway/OrderGatewayServer.hpp"
#include "gateway/OrderMessage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

struct BenchmarkConfig
{
    std::size_t orders{100'000};
    std::size_t batch_size{256};
    bool threaded{true};
};

struct BenchmarkState
{
    std::vector<Clock::time_point> send_times;
    std::vector<std::uint64_t> latencies_ns;
    std::size_t total_acks{0};
    std::size_t total_rejects{0};
    std::size_t invalid_acks{0};

    explicit BenchmarkState(std::size_t orders)
        : send_times(orders + 1), latencies_ns()
    {
        latencies_ns.reserve(orders);
    }

    [[nodiscard]] std::size_t totalEvents() const noexcept
    {
        return total_acks + total_rejects;
    }
};

struct LatencySummary
{
    std::uint64_t avg_latency_ns{};
    std::uint64_t min_latency_ns{};
    std::uint64_t p50_latency_ns{};
    std::uint64_t p95_latency_ns{};
    std::uint64_t p99_latency_ns{};
    std::uint64_t max_latency_ns{};
};

std::size_t parsePositiveSize(std::string_view text, std::string_view flag)
{
    std::size_t value = 0;
    for (const char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            throw std::invalid_argument(std::string(flag) + " must be a positive integer");
        }

        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
        {
            throw std::invalid_argument(std::string(flag) + " is too large");
        }
        value = value * 10 + digit;
    }

    if (value == 0)
    {
        throw std::invalid_argument(std::string(flag) + " must be greater than zero");
    }

    return value;
}

bool parseThreaded(std::string_view text)
{
    if (text == "0")
    {
        return false;
    }
    if (text == "1")
    {
        return true;
    }
    throw std::invalid_argument("--threaded must be 0 or 1");
}

BenchmarkConfig parseArgs(int argc, char** argv)
{
    BenchmarkConfig config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--help")
        {
            std::cout << "Usage: order_gateway_latency_benchmark [--orders N] [--batch-size N] [--threaded 0|1]\n";
            std::exit(0);
        }

        if (i + 1 >= argc)
        {
            throw std::invalid_argument(std::string(arg) + " requires a value");
        }

        const std::string_view value = argv[++i];
        if (arg == "--orders")
        {
            config.orders = parsePositiveSize(value, arg);
        }
        else if (arg == "--batch-size")
        {
            config.batch_size = parsePositiveSize(value, arg);
        }
        else if (arg == "--threaded")
        {
            config.threaded = parseThreaded(value);
        }
        else
        {
            throw std::invalid_argument("unknown argument: " + std::string(arg));
        }
    }

    return config;
}

md::NewOrder makeNewOrder(md::TradingEngineId trading_engine_id, md::OrderId order_id)
{
    return md::NewOrder{
        .fields = {
            .trading_engine_id = trading_engine_id,
            .order_id = order_id,
            .instrument_id = md::InstrumentId{42},
            .side = md::Side::Bid,
            .price = md::Price{101'250'000'000LL},
            .size = md::Quantity{10},
            .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(order_id)},
            .status = md::OrderStatus::New,
        },
    };
}

void recordAck(BenchmarkState& state, const md::OrderAck& ack)
{
    ++state.total_acks;

    const md::OrderId order_id = md::orderId(ack);
    if (order_id == 0 || order_id >= state.send_times.size())
    {
        ++state.invalid_acks;
        return;
    }

    const Clock::time_point sent_at = state.send_times[order_id];
    if (sent_at == Clock::time_point{})
    {
        ++state.invalid_acks;
        return;
    }

    const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - sent_at);
    state.latencies_ns.push_back(static_cast<std::uint64_t>(latency.count()));
}

void verifyState(const BenchmarkConfig& config, const BenchmarkState& state)
{
    if (state.total_acks != config.orders)
    {
        throw std::runtime_error(
            "received " + std::to_string(state.total_acks) + " acks, expected " + std::to_string(config.orders));
    }
    if (state.total_rejects != 0)
    {
        throw std::runtime_error("received " + std::to_string(state.total_rejects) + " rejects");
    }
    if (state.invalid_acks != 0)
    {
        throw std::runtime_error("received " + std::to_string(state.invalid_acks) + " invalid ack order ids");
    }
    if (state.latencies_ns.size() != config.orders)
    {
        throw std::runtime_error(
            "recorded " + std::to_string(state.latencies_ns.size()) + " latencies, expected " +
            std::to_string(config.orders));
    }
}

void waitForCompletion(md::OrderGatewayClient& client, BenchmarkState& state, std::size_t expected_events)
{
    std::size_t last_seen = state.totalEvents();
    auto last_progress = Clock::now();

    while (state.totalEvents() < expected_events)
    {
        (void)client.drainEvents();

        const std::size_t current_seen = state.totalEvents();
        if (current_seen != last_seen)
        {
            last_seen = current_seen;
            last_progress = Clock::now();
            continue;
        }

        if (Clock::now() - last_progress > std::chrono::seconds(10))
        {
            throw std::runtime_error(
                "timed out waiting for order gateway events: received " + std::to_string(current_seen) +
                ", expected " + std::to_string(expected_events));
        }

        std::this_thread::yield();
    }
}

LatencySummary summarizeLatencies(std::vector<std::uint64_t> latencies)
{
    std::sort(latencies.begin(), latencies.end());

    const auto percentile = [&latencies](std::size_t percent)
    {
        const std::size_t index = ((latencies.size() - 1) * percent) / 100;
        return latencies[index];
    };

    const std::uint64_t total = std::accumulate(latencies.begin(), latencies.end(), std::uint64_t{0});
    return LatencySummary{
        .avg_latency_ns = total / static_cast<std::uint64_t>(latencies.size()),
        .min_latency_ns = latencies.front(),
        .p50_latency_ns = percentile(50),
        .p95_latency_ns = percentile(95),
        .p99_latency_ns = percentile(99),
        .max_latency_ns = latencies.back(),
    };
}

void printResult(const BenchmarkConfig& config, const BenchmarkState& state, Clock::duration wall_clock)
{
    const double seconds = std::chrono::duration<double>(wall_clock).count();
    const double orders_per_second = static_cast<double>(config.orders) / seconds;
    const LatencySummary summary = summarizeLatencies(state.latencies_ns);

    std::cout << "orders,batch_size,threaded,total_acks,wall_clock_seconds,orders_per_second,"
                 "avg_latency_ns,min_latency_ns,p50_latency_ns,p95_latency_ns,p99_latency_ns,max_latency_ns\n";
    std::cout << config.orders << ','
              << config.batch_size << ','
              << (config.threaded ? 1 : 0) << ','
              << state.total_acks << ','
              << std::fixed << std::setprecision(6) << seconds << ','
              << std::setprecision(2) << orders_per_second << ','
              << summary.avg_latency_ns << ','
              << summary.min_latency_ns << ','
              << summary.p50_latency_ns << ','
              << summary.p95_latency_ns << ','
              << summary.p99_latency_ns << ','
              << summary.max_latency_ns << '\n';
}

Clock::duration runSingleThreaded(
    const BenchmarkConfig& config,
    md::OrderGatewayServer& server,
    md::OrderGatewayClient& client,
    BenchmarkState& state,
    md::TradingEngineId trading_engine_id)
{
    const auto start = Clock::now();
    for (std::size_t i = 1; i <= config.orders; ++i)
    {
        const md::OrderId order_id = static_cast<md::OrderId>(i);
        state.send_times[order_id] = Clock::now();
        client.sendOrder(makeNewOrder(trading_engine_id, order_id));

        if (i % config.batch_size == 0)
        {
            client.flush();
            (void)server.drainRequests();
            server.flushEvents();
            (void)client.drainEvents();
        }
    }

    client.flush();
    while (state.totalEvents() < config.orders)
    {
        (void)server.drainRequests();
        server.flushEvents();
        waitForCompletion(client, state, config.orders);
    }

    return Clock::now() - start;
}

Clock::duration runThreaded(
    const BenchmarkConfig& config,
    md::OrderGatewayServer& server,
    md::OrderGatewayClient& client,
    BenchmarkState& state,
    md::TradingEngineId trading_engine_id)
{
    std::atomic<bool> stop_requested{false};
    std::thread server_thread(
        [&server, &stop_requested]
        {
            while (!stop_requested.load(std::memory_order_acquire))
            {
                const std::size_t drained = server.drainRequests();
                server.flushEvents();
                if (drained == 0)
                {
                    std::this_thread::yield();
                }
            }

            (void)server.drainRequests();
            server.flushEvents();
        });

    const auto start = Clock::now();
    try
    {
        for (std::size_t i = 1; i <= config.orders; ++i)
        {
            const md::OrderId order_id = static_cast<md::OrderId>(i);
            state.send_times[order_id] = Clock::now();
            client.sendOrder(makeNewOrder(trading_engine_id, order_id));
            (void)client.drainEvents();
        }

        client.flush();
        waitForCompletion(client, state, config.orders);
    }
    catch (...)
    {
        stop_requested.store(true, std::memory_order_release);
        server_thread.join();
        throw;
    }

    const auto stop = Clock::now();
    stop_requested.store(true, std::memory_order_release);
    server_thread.join();
    return stop - start;
}

int runBenchmark(const BenchmarkConfig& config)
{
    constexpr md::TradingEngineId trading_engine_id{1};

    const std::shared_ptr<md::OrderChannel> channel =
        std::make_shared<md::OrderChannel>(config.batch_size, config.batch_size);
    md::OrderGatewayServer server;
    md::OrderGatewayClient client(trading_engine_id, channel);
    BenchmarkState state(config.orders);

    server.registerEngine(trading_engine_id, channel);
    client.onAck(
        [&state](const md::OrderAck& ack)
        {
            recordAck(state, ack);
        });
    client.onReject(
        [&state](const md::OrderReject&)
        {
            ++state.total_rejects;
        });

    const Clock::duration wall_clock = config.threaded
        ? runThreaded(config, server, client, state, trading_engine_id)
        : runSingleThreaded(config, server, client, state, trading_engine_id);

    verifyState(config, state);
    printResult(config, state, wall_clock);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        return runBenchmark(parseArgs(argc, argv));
    }
    catch (const std::exception& e)
    {
        std::cerr << "order_gateway_latency_benchmark: " << e.what() << '\n';
        return 1;
    }
}
