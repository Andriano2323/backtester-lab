#include "messaging/MarketDataGapDetector.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

md::MarketDataMessage makeMessage(md::InstrumentId instrument_id, md::SeqNo seq_no)
{
    return md::BookUpdate{
        .instrument_id = instrument_id,
        .timestamp_ns = md::TimestampNs{1'700'000'000'000'000'000LL + static_cast<md::TimestampNs>(seq_no)},
        .seq_no = seq_no,
        .side = md::Side::Bid,
        .price = md::Price{100'000'000'000LL},
        .size = md::Quantity{10},
    };
}

void requireResult(
    const md::SequenceCheckResult& result,
    md::SequenceCheckStatus status,
    md::InstrumentId instrument_id,
    md::SeqNo expected_seq_no,
    md::SeqNo observed_seq_no,
    const std::string& case_name)
{
    require(result.status == status, case_name + ": status");
    require(result.instrument_id == instrument_id, case_name + ": instrument_id");
    require(result.expected_seq_no == expected_seq_no, case_name + ": expected_seq_no");
    require(result.observed_seq_no == observed_seq_no, case_name + ": observed_seq_no");
}

void testContiguousSequenceReturnsOk()
{
    md::MarketDataGapDetector detector;

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{1},
        md::SeqNo{1},
        "seq 1");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{2})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{2},
        md::SeqNo{2},
        "seq 2");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{3})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{3},
        md::SeqNo{3},
        "seq 3");
}

void testFirstMessageGreaterThanOneReportsGap()
{
    md::MarketDataGapDetector detector;

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{3})),
        md::SequenceCheckStatus::Gap,
        md::InstrumentId{10},
        md::SeqNo{1},
        md::SeqNo{3},
        "first seq 3");
}

void testMissingMiddleSequenceReportsGap()
{
    md::MarketDataGapDetector detector;

    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1}));

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{3})),
        md::SequenceCheckStatus::Gap,
        md::InstrumentId{10},
        md::SeqNo{2},
        md::SeqNo{3},
        "seq 1 then 3");
}

void testDuplicateReportsDuplicateOrOutOfOrder()
{
    md::MarketDataGapDetector detector;

    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1}));

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1})),
        md::SequenceCheckStatus::DuplicateOrOutOfOrder,
        md::InstrumentId{10},
        md::SeqNo{2},
        md::SeqNo{1},
        "seq 1 then 1");
}

void testOutOfOrderReportsDuplicateOrOutOfOrder()
{
    md::MarketDataGapDetector detector;

    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1}));
    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{2}));
    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{3}));

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{2})),
        md::SequenceCheckStatus::DuplicateOrOutOfOrder,
        md::InstrumentId{10},
        md::SeqNo{4},
        md::SeqNo{2},
        "seq 2 after 3");
}

void testSeparateInstrumentsAreTrackedIndependently()
{
    md::MarketDataGapDetector detector;

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{1},
        md::SeqNo{1},
        "instrument 10 seq 1");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{20}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{20},
        md::SeqNo{1},
        md::SeqNo{1},
        "instrument 20 seq 1");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{2})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{2},
        md::SeqNo{2},
        "instrument 10 seq 2");
}

void testResetInstrumentResetsOnlyThatInstrument()
{
    md::MarketDataGapDetector detector;

    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1}));
    detector.observe(makeMessage(md::InstrumentId{20}, md::SeqNo{1}));
    detector.reset(md::InstrumentId{10});

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{1},
        md::SeqNo{1},
        "reset instrument 10");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{20}, md::SeqNo{1})),
        md::SequenceCheckStatus::DuplicateOrOutOfOrder,
        md::InstrumentId{20},
        md::SeqNo{2},
        md::SeqNo{1},
        "instrument 20 remains tracked");
}

void testResetAllInstruments()
{
    md::MarketDataGapDetector detector;

    detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1}));
    detector.observe(makeMessage(md::InstrumentId{20}, md::SeqNo{1}));
    detector.reset();

    requireResult(
        detector.observe(makeMessage(md::InstrumentId{10}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{10},
        md::SeqNo{1},
        md::SeqNo{1},
        "reset all instrument 10");
    requireResult(
        detector.observe(makeMessage(md::InstrumentId{20}, md::SeqNo{1})),
        md::SequenceCheckStatus::Ok,
        md::InstrumentId{20},
        md::SeqNo{1},
        md::SeqNo{1},
        "reset all instrument 20");
}

} // namespace

int main()
{
    try
    {
        testContiguousSequenceReturnsOk();
        testFirstMessageGreaterThanOneReportsGap();
        testMissingMiddleSequenceReportsGap();
        testDuplicateReportsDuplicateOrOutOfOrder();
        testOutOfOrderReportsDuplicateOrOutOfOrder();
        testSeparateInstrumentsAreTrackedIndependently();
        testResetInstrumentResetsOnlyThatInstrument();
        testResetAllInstruments();
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
