#include "TestSupport.hpp"

#include "lob/HistoricalLobProcessor.hpp"
#include "runners/FlatMergeRunner.hpp"
#include "runners/HierarchicalMergeRunner.hpp"
#include "runners/ResultPrinter.hpp"
#include "runners/StandardRunner.hpp"

#include <sstream>

namespace md::test
{
namespace
{

std::filesystem::path lobDataDir()
{
    return testDataDir().parent_path() / "data";
}

std::filesystem::path lobBasicFile()
{
    return lobDataDir() / "lob_basic.ndjson";
}

std::filesystem::path lobMultiDir()
{
    return lobDataDir() / "lob_multi";
}

} // namespace

void testStandardRunnerCanBuildLobSummary()
{
    std::ostringstream err;
    std::ostringstream out;
    md::lob::HistoricalLobProcessor processor;

    const auto result = StandardRunner{}.run(lobBasicFile(), processor, false, err);
    printHistoricalLobSummary(processor.store(), out, 5);

    require(result.summary.total_messages_processed == 3, "lob summary standard message count");
    require(result.summary.chronological_violations == 0, "lob summary standard chronological violations");
    require(processor.instrumentCount() == 2, "lob summary standard instrument count");

    const auto text = out.str();
    requireContains(text, "LOB Summary", "lob summary marker");
    requireContains(text, "instruments=2", "lob summary instrument count");
    requireContains(text, "resting_orders=3", "lob summary resting order count");
    requireContains(text, "instrument_id=42", "lob summary instrument 42");
    requireContains(text, "best_bid=100.000000000x10", "lob summary instrument 42 best bid");
    requireContains(text, "best_ask=101.000000000x7", "lob summary instrument 42 best ask");
    requireContains(text, "instrument_id=43", "lob summary instrument 43");
    requireContains(text, "best_bid=200.000000000x3", "lob summary instrument 43 best bid");
    requireContains(text, "lob_digest=", "lob summary digest");
}

void testFlatAndHierarchyBuildSameLobDigest()
{
    std::ostringstream flat_err;
    md::lob::HistoricalLobProcessor flat_processor;
    const auto flat_result = FlatMergeRunner{}.run(lobMultiDir(), flat_processor, false, flat_err);

    std::ostringstream hierarchy_err;
    md::lob::HistoricalLobProcessor hierarchy_processor;
    const auto hierarchy_result = HierarchicalMergeRunner{}.run(
        lobMultiDir(),
        hierarchy_processor,
        false,
        hierarchy_err);

    require(flat_result.summary.total_messages_processed == 4, "flat lob summary message count");
    require(hierarchy_result.summary.total_messages_processed == 4, "hierarchy lob summary message count");
    require(flat_result.summary.chronological_violations == 0, "flat lob summary chronological violations");
    require(hierarchy_result.summary.chronological_violations == 0, "hierarchy lob summary chronological violations");

    const auto flat_digest = flat_processor.store().stableStateDigest();
    const auto hierarchy_digest = hierarchy_processor.store().stableStateDigest();
    require(flat_digest == hierarchy_digest, "flat and hierarchy hw3 lob digest");
    requireContains(flat_digest, "instrument_count=2", "hw3 digest instrument count");
    requireContains(flat_digest, "instrument=42", "hw3 digest instrument 42");
    requireContains(flat_digest, "instrument=43", "hw3 digest instrument 43");
    requireContains(flat_digest, "best_bid=100.000000000x10", "hw3 digest best bid 42");
    requireContains(flat_digest, "best_ask=201.000000000x8", "hw3 digest best ask 43");
}

} // namespace md::test
