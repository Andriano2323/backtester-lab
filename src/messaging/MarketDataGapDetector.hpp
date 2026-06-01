#pragma once

#include "domain/Types.hpp"
#include "messaging/MarketDataMessage.hpp"

#include <unordered_map>

namespace md
{

enum class SequenceCheckStatus
{
    Ok,
    Gap,
    DuplicateOrOutOfOrder
};

struct SequenceCheckResult
{
    SequenceCheckStatus status{SequenceCheckStatus::Ok};
    InstrumentId instrument_id{};
    SeqNo expected_seq_no{};
    SeqNo observed_seq_no{};
};

class MarketDataGapDetector
{
  public:
    SequenceCheckResult observe(const MarketDataMessage& message);

    void reset();
    void reset(InstrumentId instrument_id);

  private:
    std::unordered_map<InstrumentId, SeqNo> last_seq_no_by_instrument_;
};

} // namespace md
