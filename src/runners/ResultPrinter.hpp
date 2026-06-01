#pragma once

#include "lob/HistoricalLobStore.hpp"
#include "runners/RunResult.hpp"

#include <iosfwd>
#include <vector>

namespace md {

void printRunResult(const RunResult& result, std::ostream& out, bool verbose, std::size_t max_events_to_print);
void printHistoricalLobSummary(const lob::HistoricalLobStore& store, std::ostream& out, std::size_t depth);
void printBenchmarkResults(const std::vector<BenchmarkResult>& results, std::ostream& out);
void printLobBenchmarkResults(const std::vector<BenchmarkResult>& results, std::ostream& out);

} // namespace md
