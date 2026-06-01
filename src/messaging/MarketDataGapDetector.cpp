#include "messaging/MarketDataGapDetector.hpp"

namespace md
{

SequenceCheckResult MarketDataGapDetector::observe(const MarketDataMessage& message)
{
    const InstrumentId instrument_id = instrumentId(message);
    const SeqNo observed_seq_no = seqNo(message);

    auto last_it = last_seq_no_by_instrument_.find(instrument_id);
    if (last_it == last_seq_no_by_instrument_.end())
    {
        constexpr SeqNo expected_seq_no = 1;
        if (observed_seq_no == expected_seq_no)
        {
            last_seq_no_by_instrument_[instrument_id] = observed_seq_no;
            return {
                .status = SequenceCheckStatus::Ok,
                .instrument_id = instrument_id,
                .expected_seq_no = expected_seq_no,
                .observed_seq_no = observed_seq_no,
            };
        }

        if (observed_seq_no > expected_seq_no)
        {
            last_seq_no_by_instrument_[instrument_id] = observed_seq_no;
            return {
                .status = SequenceCheckStatus::Gap,
                .instrument_id = instrument_id,
                .expected_seq_no = expected_seq_no,
                .observed_seq_no = observed_seq_no,
            };
        }

        return {
            .status = SequenceCheckStatus::DuplicateOrOutOfOrder,
            .instrument_id = instrument_id,
            .expected_seq_no = expected_seq_no,
            .observed_seq_no = observed_seq_no,
        };
    }

    const SeqNo expected_seq_no = last_it->second + 1;
    if (observed_seq_no == expected_seq_no)
    {
        last_it->second = observed_seq_no;
        return {
            .status = SequenceCheckStatus::Ok,
            .instrument_id = instrument_id,
            .expected_seq_no = expected_seq_no,
            .observed_seq_no = observed_seq_no,
        };
    }

    if (observed_seq_no > expected_seq_no)
    {
        last_it->second = observed_seq_no;
        return {
            .status = SequenceCheckStatus::Gap,
            .instrument_id = instrument_id,
            .expected_seq_no = expected_seq_no,
            .observed_seq_no = observed_seq_no,
        };
    }

    return {
        .status = SequenceCheckStatus::DuplicateOrOutOfOrder,
        .instrument_id = instrument_id,
        .expected_seq_no = expected_seq_no,
        .observed_seq_no = observed_seq_no,
    };
}

void MarketDataGapDetector::reset()
{
    last_seq_no_by_instrument_.clear();
}

void MarketDataGapDetector::reset(InstrumentId instrument_id)
{
    last_seq_no_by_instrument_.erase(instrument_id);
}

} // namespace md
