from backtester import BacktestRunner, Strategy
from backtester.progress import ProgressMetrics
from backtester.result import OrderStatistics
from backtester.types import BookUpdate, Side


def _book_update(timestamp_ns=1, instrument_id=10):
    return BookUpdate(
        instrument_id=instrument_id,
        timestamp_ns=timestamp_ns,
        seq_no=timestamp_ns,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )


def test_progress_metrics_can_be_constructed():
    metrics = ProgressMetrics(
        processed_events=1,
        total_events=4,
        last_timestamp_ns=100,
        current_pnl=123,
        orders_sent=1,
        orders_cancelled=2,
        orders_filled=3,
        orders_rejected=4,
        per_instrument_order_stats={10: OrderStatistics(sent=1)},
    )

    assert metrics.processed_events == 1
    assert metrics.progress_percent == 25.0
    assert metrics.progress_fraction == 0.25
    assert metrics.current_pnl == 123
    assert metrics.per_instrument_order_stats[10].sent == 1


def test_progress_percent_is_zero_to_one_hundred_when_total_events_is_known():
    assert ProgressMetrics(processed_events=0, total_events=4).progress_percent == 0.0
    assert ProgressMetrics(processed_events=2, total_events=4).progress_percent == 50.0
    assert ProgressMetrics(processed_events=5, total_events=4).progress_percent == 100.0


def test_progress_callback_is_called_when_progress_interval_events_threshold_is_reached():
    callbacks = []

    BacktestRunner().run(
        Strategy(),
        events=[_book_update(1), _book_update(2), _book_update(3)],
        progress_callback=callbacks.append,
        progress_interval_events=2,
    )

    assert [metrics.processed_events for metrics in callbacks] == [2, 3]


def test_strategy_on_progress_is_called():
    class ProgressStrategy(Strategy):
        def __init__(self):
            self.progress = []

        def on_progress(self, metrics, ctx):
            self.progress.append(metrics)

    strategy = ProgressStrategy()

    BacktestRunner().run(strategy, events=[_book_update(1)], progress_interval_events=1)

    assert len(strategy.progress) == 1
    assert strategy.progress[0].processed_events == 1


def test_last_timestamp_ns_is_updated():
    callbacks = []

    BacktestRunner().run(
        Strategy(),
        events=[_book_update(10), _book_update(20)],
        progress_callback=callbacks.append,
        progress_interval_events=1,
    )

    assert [metrics.last_timestamp_ns for metrics in callbacks] == [10, 20]


def test_orders_sent_count_is_updated():
    class SendingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(update.instrument_id, Side.BID, update.price, update.size, update.timestamp_ns)

    callbacks = []

    BacktestRunner().run(
        SendingStrategy(),
        events=[_book_update(1)],
        progress_callback=callbacks.append,
        progress_interval_events=1,
    )

    assert callbacks[-1].orders_sent == 1


def test_orders_cancelled_count_is_updated():
    class CancellingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            order_id = ctx.send_order(update.instrument_id, Side.BID, update.price, update.size, update.timestamp_ns)
            ctx.cancel_order(order_id, update.instrument_id, update.timestamp_ns)

    callbacks = []

    BacktestRunner().run(
        CancellingStrategy(),
        events=[_book_update(1)],
        progress_callback=callbacks.append,
        progress_interval_events=1,
    )

    assert callbacks[-1].orders_cancelled == 1


def test_orders_filled_count_is_updated():
    class FillingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(update.instrument_id, Side.BID, update.price, update.size, update.timestamp_ns)

    callbacks = []

    BacktestRunner(fill_at_touch=True).run(
        FillingStrategy(),
        events=[_book_update(1)],
        progress_callback=callbacks.append,
        progress_interval_events=1,
    )

    assert callbacks[-1].orders_filled == 1


def test_orders_rejected_count_is_updated():
    class RejectingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(0, Side.BID, update.price, update.size, update.timestamp_ns)

    callbacks = []

    BacktestRunner().run(
        RejectingStrategy(),
        events=[_book_update(1)],
        progress_callback=callbacks.append,
        progress_interval_events=1,
    )

    assert callbacks[-1].orders_rejected == 1


def test_per_instrument_order_stats_contains_separate_stats_per_instrument():
    class MultiInstrumentStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(update.instrument_id, Side.BID, update.price, update.size, update.timestamp_ns)

    callbacks = []

    BacktestRunner().run(
        MultiInstrumentStrategy(),
        events=[_book_update(1, instrument_id=10), _book_update(2, instrument_id=20)],
        progress_callback=callbacks.append,
        progress_interval_events=2,
    )

    stats = callbacks[-1].per_instrument_order_stats
    assert stats[10] == OrderStatistics(sent=1)
    assert stats[20] == OrderStatistics(sent=1)


def test_progress_callback_receives_final_progress_at_end_of_run():
    callbacks = []

    BacktestRunner().run(
        Strategy(),
        events=[_book_update(1), _book_update(2)],
        progress_callback=callbacks.append,
        progress_interval_events=10,
    )

    assert len(callbacks) == 1
    assert callbacks[0].processed_events == 2
    assert callbacks[0].progress_percent == 100.0
    assert callbacks[0].last_timestamp_ns == 2
