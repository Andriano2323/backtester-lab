from pathlib import Path

import pytest

from backtester import BacktestRunner, Strategy
from backtester.types import BookUpdate, Side, Trade


FIXTURE_DIR = Path(__file__).resolve().parents[1] / "fixtures" / "python_feed"
FIXTURE_FILE = FIXTURE_DIR / "sample_feed.jsonl"


class RecordingStrategy(Strategy):
    def __init__(self):
        self.seen = []

    def on_book_update(self, update, ctx):
        self.seen.append(("book_update", update.timestamp_ns))

    def on_book_snapshot(self, snapshot, ctx):
        self.seen.append(("book_snapshot", snapshot.timestamp_ns))

    def on_trade(self, trade, ctx):
        self.seen.append(("trade", trade.timestamp_ns))


def test_run_with_data_path_processes_fixture_events():
    strategy = RecordingStrategy()

    BacktestRunner().run(strategy, data_path=FIXTURE_FILE)

    assert strategy.seen == [
        ("book_update", 100),
        ("book_snapshot", 200),
        ("trade", 300),
    ]


def test_date_range_filters_events_before_strategy_receives_them():
    strategy = RecordingStrategy()

    BacktestRunner().run(strategy, data_path=FIXTURE_FILE, date_range=(200, 301))

    assert strategy.seen == [
        ("book_snapshot", 200),
        ("trade", 300),
    ]


def test_data_path_can_be_a_file():
    strategy = RecordingStrategy()

    BacktestRunner().run(strategy, data_path=FIXTURE_FILE)

    assert len(strategy.seen) == 3


def test_data_path_can_be_a_directory():
    strategy = RecordingStrategy()

    BacktestRunner().run(strategy, data_path=FIXTURE_DIR)

    assert len(strategy.seen) == 3
    assert strategy.seen[0] == ("book_update", 100)


def test_missing_data_path_raises_clear_file_not_found_error(tmp_path):
    missing = tmp_path / "missing.jsonl"

    with pytest.raises(FileNotFoundError, match="Market-data path does not exist"):
        BacktestRunner().run(RecordingStrategy(), data_path=missing)


def test_passing_events_and_data_path_uses_events():
    strategy = RecordingStrategy()
    events = [
        Trade(
            instrument_id=20,
            timestamp_ns=900,
            seq_no=1,
            price=10,
            size=1,
            aggressor_side=Side.ASK,
        ),
        BookUpdate(
            instrument_id=20,
            timestamp_ns=800,
            seq_no=2,
            side=Side.BID,
            price=9,
            size=2,
        ),
    ]

    BacktestRunner().run(
        strategy, data_path=FIXTURE_FILE, date_range=(100, 101), events=events
    )

    assert strategy.seen == [
        ("book_update", 800),
        ("trade", 900),
    ]
