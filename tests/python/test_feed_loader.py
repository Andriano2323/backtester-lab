from pathlib import Path

import pytest

from backtester.feed_loader import load_events
from backtester.types import BookSnapshot, BookUpdate, Side, Trade


FIXTURE = Path(__file__).resolve().parents[1] / "fixtures" / "python_feed" / "sample_feed.jsonl"


def test_loads_book_update_jsonl_record(tmp_path):
    feed = tmp_path / "feed.jsonl"
    feed.write_text(
        '{"type":"book_update","instrument_id":10,"timestamp_ns":1,"seq_no":7,"side":"BID","price":100,"size":5}\n',
        encoding="utf-8",
    )

    events = load_events(feed)

    assert len(events) == 1
    assert isinstance(events[0], BookUpdate)
    assert events[0].instrument_id == 10
    assert events[0].timestamp_ns == 1
    assert events[0].seq_no == 7
    assert events[0].side is Side.BID
    assert events[0].price == 100
    assert events[0].size == 5


def test_loads_book_snapshot_jsonl_record(tmp_path):
    feed = tmp_path / "feed.jsonl"
    feed.write_text(
        (
            '{"type":"book_snapshot","instrument_id":10,"timestamp_ns":2,"seq_no":8,'
            '"bids":[{"level_index":0,"price":99,"size":3}],'
            '"asks":[{"level_index":0,"price":101,"size":4}]}\n'
        ),
        encoding="utf-8",
    )

    events = load_events(feed)

    assert len(events) == 1
    assert isinstance(events[0], BookSnapshot)
    assert events[0].bids[0].level_index == 0
    assert events[0].bids[0].price == 99
    assert events[0].bids[0].size == 3
    assert events[0].asks[0].price == 101


def test_loads_trade_jsonl_record(tmp_path):
    feed = tmp_path / "feed.jsonl"
    feed.write_text(
        '{"type":"trade","instrument_id":10,"timestamp_ns":3,"seq_no":9,"price":100,"size":2,"aggressor_side":"ASK"}\n',
        encoding="utf-8",
    )

    events = load_events(feed)

    assert len(events) == 1
    assert isinstance(events[0], Trade)
    assert events[0].aggressor_side is Side.ASK
    assert events[0].price == 100
    assert events[0].size == 2


def test_rejects_unknown_type_with_clear_value_error(tmp_path):
    feed = tmp_path / "feed.jsonl"
    feed.write_text('{"type":"quote","timestamp_ns":1}\n', encoding="utf-8")

    with pytest.raises(ValueError, match="Unknown market-data record type"):
        load_events(feed)


def test_filters_by_date_range_start_inclusive():
    events = load_events(FIXTURE, date_range=(200, None))

    assert [event.timestamp_ns for event in events] == [200, 300]


def test_filters_by_date_range_end_exclusive():
    events = load_events(FIXTURE, date_range=(None, 300))

    assert [event.timestamp_ns for event in events] == [100, 200]


def test_sorts_events_by_timestamp_ns():
    events = load_events(FIXTURE)

    assert [event.timestamp_ns for event in events] == [100, 200, 300]


def test_loads_all_jsonl_files_from_directory(tmp_path):
    (tmp_path / "a.jsonl").write_text(
        '{"type":"book_update","instrument_id":10,"timestamp_ns":2,"seq_no":2,"side":"BID","price":100,"size":5}\n',
        encoding="utf-8",
    )
    (tmp_path / "b.jsonl").write_text(
        '{"type":"trade","instrument_id":10,"timestamp_ns":1,"seq_no":1,"price":101,"size":2,"aggressor_side":"ASK"}\n',
        encoding="utf-8",
    )
    (tmp_path / "ignored.txt").write_text(
        '{"type":"book_update","instrument_id":10,"timestamp_ns":99,"seq_no":99,"side":"BID","price":100,"size":5}\n',
        encoding="utf-8",
    )

    events = load_events(tmp_path)

    assert [event.timestamp_ns for event in events] == [1, 2]
    assert [type(event) for event in events] == [Trade, BookUpdate]
