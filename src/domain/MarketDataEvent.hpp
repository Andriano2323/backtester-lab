#pragma once

#include "domain/Types.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace md
{

struct MarketDataEvent
{
    RawTimestampNs timestamp{}; // Databento index timestamp: ts_recv if present, otherwise ts_event.
    RawTimestampNs ts_recv{};
    RawTimestampNs ts_event{};

    OrderId order_id{};
    Side side{Side::None};
    Price price{};
    Quantity size{};
    Action action{Action::None};

    InstrumentId instrument_id{};

    // Stable source metadata used as a deterministic tie-breaker during multi-file merges.
    SourceFileId source_file_id{};
    SourceSequence source_sequence{};

    // Kept for diagnostics and for the Standard task's single-file path.
    std::size_t line_number{};
};

char toChar(Side side);
char toChar(Action action);

std::string sideName(Side side);
std::string actionName(Action action);
std::string formatPrice(Price price);
std::string formatEventFields(const MarketDataEvent& event);

bool eventComesBefore(const MarketDataEvent& lhs, const MarketDataEvent& rhs);

std::ostream& operator<<(std::ostream& os, const MarketDataEvent& event);

} // namespace md
