#pragma once

#include "domain/MarketDataEvent.hpp"

#include <cstdint>

namespace md::lob
{

using InstrumentId = std::uint64_t;
using HistoricalOrderId = std::uint64_t;
using SyntheticOrderId = std::uint64_t;
using EngineId = std::uint64_t;
using TimestampNs = std::int64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;

using Side = md::Side;
using Action = md::Action;

} // namespace md::lob
