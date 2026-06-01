#include "TestSupport.hpp"

#include "lob/HistoricalLobProcessor.hpp"
#include "runners/StandardRunner.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

namespace md::test
{
namespace
{

void requireLevel(
    const std::optional<md::lob::BookLevel>& level,
    md::lob::Price price,
    md::lob::Quantity quantity,
    const std::string& message)
{
    require(level.has_value(), message + ": missing level");
    require(level->price == price, message + ": unexpected price");
    require(level->size == quantity, message + ": unexpected size");
}

} // namespace

void testHistoricalLobProcessorBuildsBooksPerInstrument()
{
    const auto file = testDataDir().parent_path() / "data" / "lob_basic.ndjson";

    std::ostringstream err;
    md::lob::HistoricalLobProcessor processor;
    const auto result = StandardRunner{}.run(file, processor, false, err);

    require(result.summary.total_messages_processed == 3, "historical processor message count");
    require(result.summary.chronological_violations == 0, "historical processor chronological violations");
    require(processor.instrumentCount() == 2, "historical processor instrument count");

    const auto& book42 = processor.book(42);
    requireLevel(book42.bestBid(), 100000000000LL, 10, "instrument 42 best bid");
    requireLevel(book42.bestAsk(), 101000000000LL, 7, "instrument 42 best ask");

    const auto& book43 = processor.book(43);
    requireLevel(book43.bestBid(), 200000000000LL, 3, "instrument 43 best bid");
    require(!book43.bestAsk().has_value(), "instrument 43 ask side empty");
}

} // namespace md::test
