from pathlib import Path

import pytest

import backtester as backtest
from backtester import BacktestResult, Strategy
from backtester.risk import RiskLimits
from backtester.types import BookUpdate, Side


FIXTURE = Path(__file__).resolve().parents[1] / "fixtures" / "python_feed" / "sample_feed.jsonl"


def _book_update(timestamp_ns=1):
    return BookUpdate(
        instrument_id=10,
        timestamp_ns=timestamp_ns,
        seq_no=timestamp_ns,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )


class RecordingStrategy(Strategy):
    def __init__(self):
        self.timestamps = []

    def on_book_update(self, update, ctx):
        self.timestamps.append(update.timestamp_ns)

    def on_book_snapshot(self, snapshot, ctx):
        self.timestamps.append(snapshot.timestamp_ns)

    def on_trade(self, trade, ctx):
        self.timestamps.append(trade.timestamp_ns)


def test_import_backtester_as_backtest_works():
    assert backtest.run is not None


def test_top_level_run_with_events_returns_backtest_result():
    result = backtest.run(Strategy(), events=[_book_update()])

    assert isinstance(result, BacktestResult)


def test_top_level_run_with_data_path_and_date_range_works():
    strategy = RecordingStrategy()

    result = backtest.run(strategy, data_path=FIXTURE, date_range=(200, 301))

    assert isinstance(result, BacktestResult)
    assert strategy.timestamps == [200, 300]


def test_kwargs_are_passed_to_backtest_runner():
    callbacks = []

    class SendingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(update.instrument_id, Side.BID, update.price, update.size, update.timestamp_ns)

    result = backtest.run(
        SendingStrategy(),
        events=[_book_update()],
        progress_callback=callbacks.append,
        progress_interval_events=1,
        fill_at_touch=True,
        risk_limits=RiskLimits(max_order_size=10),
    )

    assert callbacks[-1].orders_sent == 1
    assert callbacks[-1].orders_filled == 1
    assert len(result.fills_df) == 1


def test_invalid_strategy_type_raises_clear_type_error():
    with pytest.raises(TypeError, match="strategy must be an instance of backtester.Strategy"):
        backtest.run(object(), events=[])


def test_result_has_pandas_output_properties():
    result = backtest.run(Strategy(), events=[])

    assert result.pnl_series.empty
    assert result.fills_df.empty
    assert result.order_log_df.empty
