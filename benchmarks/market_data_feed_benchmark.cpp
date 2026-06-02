#include "messaging/MarketDataGapDetector.hpp"
#include "messaging/MarketDataPublisher.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

struct BenchmarkConfig
{
    std::size_t subscribers{4};
    std::size_t messages{100'000};
    std::size_t batch_size{256};
};

struct SubscriberStats
{
    md::MarketDataGapDetector gap_detector;
    std::size_t received{0};
    bool sequence_ok{true};
    md::SequenceCheckResult first_error{};
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

BenchmarkConfig parseArgs(int argc, char** argv)
{
    BenchmarkConfig config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--help")
        {
            std::cout << "Usage: market_data_feed_benchmark [--subscribers N] [--messages M] [--batch-size N]\n";
            std::exit(0);
        }

        if (i + 1 >= argc)
        {
            throw std::invalid_argument(std::string(arg) + " requires a value");
        }

        const std::string_view value = argv[++i];
        if (arg == "--subscribers")
        {
            config.subscribers = parsePositiveSize(value, arg);
        }
        else if (arg == "--messages")
        {
            config.messages = parsePositiveSize(value, arg);
        }
        else if (arg == "--batch-size")
        {
            config.batch_size = parsePositiveSize(value, arg);
        }
        else
        {
            throw std::invalid_argument("unknown argument: " + std::string(arg));
        }
    }

    return config;
}

md::BookUpdate makeUpdate(md::InstrumentId instrument_id, std::size_t index)
{
    return md::BookUpdate{
        .instrument_id = instrument_id,
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(index)},
        .seq_no = md::SeqNo{0},
        .side = md::Side::Bid,
        .price = md::Price{100'000'000'000LL},
        .size = md::Quantity{10},
    };
}

void verifyStats(const BenchmarkConfig& config, const std::vector<SubscriberStats>& stats)
{
    for (std::size_t i = 0; i < stats.size(); ++i)
    {
        if (stats[i].received != config.messages)
        {
            throw std::runtime_error(
                "subscriber " + std::to_string(i) + " received " + std::to_string(stats[i].received) +
                " messages, expected " + std::to_string(config.messages));
        }

        if (!stats[i].sequence_ok)
        {
            const md::SequenceCheckResult& error = stats[i].first_error;
            throw std::runtime_error(
                "subscriber " + std::to_string(i) + " sequence check failed: expected " +
                std::to_string(error.expected_seq_no) + ", observed " + std::to_string(error.observed_seq_no));
        }
    }
}

int runBenchmark(const BenchmarkConfig& config)
{
    constexpr md::InstrumentId instrument_id{10};

    md::MarketDataPublisher publisher;
    std::vector<SubscriberStats> stats(config.subscribers);
    std::vector<md::MarketDataSubscription> subscriptions;
    subscriptions.reserve(config.subscribers);

    for (std::size_t i = 0; i < config.subscribers; ++i)
    {
        subscriptions.push_back(publisher.subscribe(
            instrument_id,
            [&stats, i](const md::MarketDataMessage& message)
            {
                md::SequenceCheckResult result = stats[i].gap_detector.observe(message);
                if (result.status != md::SequenceCheckStatus::Ok && stats[i].sequence_ok)
                {
                    stats[i].sequence_ok = false;
                    stats[i].first_error = result;
                }
                ++stats[i].received;
            },
            config.batch_size));
    }

    std::atomic<bool> publishing_done{false};
    std::atomic<bool> stop_requested{false};

    std::vector<std::thread> consumer_threads;
    consumer_threads.reserve(config.subscribers);
    for (std::size_t i = 0; i < config.subscribers; ++i)
    {
        const std::shared_ptr<md::MarketDataSubscriber> subscriber = subscriptions[i].subscriber;
        consumer_threads.emplace_back(
            [subscriber, &stats, &config, &publishing_done, &stop_requested, i]
            {
                auto last_progress = std::chrono::steady_clock::now();
                while (stats[i].received < config.messages && !stop_requested.load(std::memory_order_relaxed))
                {
                    const std::size_t drained = subscriber->drainAvailable();
                    if (drained > 0)
                    {
                        last_progress = std::chrono::steady_clock::now();
                        continue;
                    }

                    if (publishing_done.load(std::memory_order_acquire) &&
                        std::chrono::steady_clock::now() - last_progress > std::chrono::seconds(10))
                    {
                        stop_requested.store(true, std::memory_order_relaxed);
                        break;
                    }

                    std::this_thread::yield();
                }
            });
    }

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < config.messages; ++i)
    {
        publisher.publishUpdate(makeUpdate(instrument_id, i));
    }
    publisher.flush();
    publishing_done.store(true, std::memory_order_release);

    for (std::thread& thread : consumer_threads)
    {
        thread.join();
    }
    const auto stop = std::chrono::steady_clock::now();

    verifyStats(config, stats);

    const std::size_t total_deliveries = config.messages * config.subscribers;
    const double seconds = std::chrono::duration<double>(stop - start).count();
    const double publishes_per_second = static_cast<double>(config.messages) / seconds;
    const double deliveries_per_second = static_cast<double>(total_deliveries) / seconds;

    std::cout << "subscribers,messages,total_deliveries,wall_clock_seconds,publishes_per_second,deliveries_per_second\n";
    std::cout << config.subscribers << ','
              << config.messages << ','
              << total_deliveries << ','
              << std::fixed << std::setprecision(6) << seconds << ','
              << std::setprecision(2) << publishes_per_second << ','
              << deliveries_per_second << '\n';

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
        std::cerr << "market_data_feed_benchmark: " << e.what() << '\n';
        return 1;
    }
}
