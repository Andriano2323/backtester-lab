from backtester import BacktestResult, BacktestRunner, Strategy
from backtester.types import (
    BookSnapshot,
    BookUpdate,
    OrderAck,
    OrderFill,
    PriceLevel,
    Side,
    Trade,
)


def _book_update(timestamp_ns=3, instrument_id=10):
    return BookUpdate(
        instrument_id=instrument_id,
        timestamp_ns=timestamp_ns,
        seq_no=1,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )


def _book_snapshot(timestamp_ns=2, instrument_id=10):
    return BookSnapshot(
        instrument_id=instrument_id,
        timestamp_ns=timestamp_ns,
        seq_no=2,
        bids=[PriceLevel(level_index=0, price=101_000_000_000, size=3)],
        asks=[PriceLevel(level_index=0, price=102_000_000_000, size=4)],
    )


def _trade(timestamp_ns=1, instrument_id=10):
    return Trade(
        instrument_id=instrument_id,
        timestamp_ns=timestamp_ns,
        seq_no=3,
        price=101_500_000_000,
        size=2,
        aggressor_side=Side.ASK,
    )


def test_run_returns_backtest_result():
    result = BacktestRunner().run(Strategy(), events=[])

    assert isinstance(result, BacktestResult)


def test_on_start_is_called_once():
    class CountingStrategy(Strategy):
        def __init__(self):
            self.starts = 0

        def on_start(self, ctx):
            self.starts += 1

    strategy = CountingStrategy()

    BacktestRunner().run(strategy, events=[_book_update()])

    assert strategy.starts == 1


def test_on_finish_is_called_once():
    class CountingStrategy(Strategy):
        def __init__(self):
            self.finishes = 0

        def on_finish(self, result, ctx):
            self.finishes += 1

    strategy = CountingStrategy()

    BacktestRunner().run(strategy, events=[_book_update()])

    assert strategy.finishes == 1


def test_on_book_update_is_called_for_book_update_events():
    class UpdateStrategy(Strategy):
        def __init__(self):
            self.updates = []

        def on_book_update(self, update, ctx):
            self.updates.append(update)

    strategy = UpdateStrategy()
    update = _book_update()

    BacktestRunner().run(strategy, events=[update])

    assert strategy.updates == [update]


def test_on_book_snapshot_is_called_for_book_snapshot_events():
    class SnapshotStrategy(Strategy):
        def __init__(self):
            self.snapshots = []

        def on_book_snapshot(self, snapshot, ctx):
            self.snapshots.append(snapshot)

    strategy = SnapshotStrategy()
    snapshot = _book_snapshot()

    BacktestRunner().run(strategy, events=[snapshot])

    assert strategy.snapshots == [snapshot]


def test_on_trade_is_called_for_trade_events():
    class TradeStrategy(Strategy):
        def __init__(self):
            self.trades = []

        def on_trade(self, trade, ctx):
            self.trades.append(trade)

    strategy = TradeStrategy()
    trade = _trade()

    BacktestRunner().run(strategy, events=[trade])

    assert strategy.trades == [trade]


def test_events_are_processed_in_timestamp_order():
    class OrderingStrategy(Strategy):
        def __init__(self):
            self.timestamps = []

        def on_book_update(self, update, ctx):
            self.timestamps.append(update.timestamp_ns)

        def on_book_snapshot(self, snapshot, ctx):
            self.timestamps.append(snapshot.timestamp_ns)

        def on_trade(self, trade, ctx):
            self.timestamps.append(trade.timestamp_ns)

    strategy = OrderingStrategy()

    BacktestRunner().run(
        strategy, events=[_book_update(3), _trade(1), _book_snapshot(2)]
    )

    assert strategy.timestamps == [1, 2, 3]


def test_strategy_can_send_order_from_on_book_update():
    class SendingStrategy(Strategy):
        def __init__(self):
            self.order_id = None

        def on_book_update(self, update, ctx):
            self.order_id = ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

    strategy = SendingStrategy()

    BacktestRunner().run(strategy, events=[_book_update()])

    assert strategy.order_id == 1


def test_sent_order_appears_in_result_order_log_df():
    class SendingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

    result = BacktestRunner().run(SendingStrategy(), events=[_book_update()])
    order_log = result.order_log_df
    sent_rows = order_log[order_log["event_type"] == "new_order"]

    assert len(sent_rows) == 1
    assert sent_rows.iloc[0]["order_id"] == 1
    assert sent_rows.iloc[0]["status"] == "New"


def test_accepted_ack_triggers_strategy_on_ack():
    class AckStrategy(Strategy):
        def __init__(self):
            self.acks = []

        def on_book_update(self, update, ctx):
            ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

        def on_ack(self, ack, ctx):
            self.acks.append(ack)

    strategy = AckStrategy()

    BacktestRunner().run(strategy, events=[_book_update()])

    assert len(strategy.acks) == 1
    assert isinstance(strategy.acks[0], OrderAck)
    assert strategy.acks[0].order_id == 1


def test_injected_fill_triggers_strategy_on_fill():
    class FillStrategy(Strategy):
        def __init__(self):
            self.fills = []

        def on_book_update(self, update, ctx):
            ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

        def on_fill(self, fill, ctx):
            self.fills.append(fill)

    strategy = FillStrategy()

    BacktestRunner(fill_at_touch=True).run(strategy, events=[_book_update()])

    assert len(strategy.fills) == 1
    assert isinstance(strategy.fills[0], OrderFill)
    assert strategy.fills[0].fill_size == 5
    assert strategy.fills[0].remaining_size == 0


def test_fill_appears_in_result_fills_df():
    class FillStrategy(Strategy):
        def on_book_update(self, update, ctx):
            ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

    result = BacktestRunner(fill_at_touch=True).run(
        FillStrategy(), events=[_book_update()]
    )
    fills = result.fills_df

    assert len(fills) == 1
    assert fills.iloc[0]["order_id"] == 1
    assert fills.iloc[0]["fill_price"] == 101_250_000_000
    assert fills.iloc[0]["fill_size"] == 5


def test_empty_events_list_returns_empty_but_valid_result():
    result = BacktestRunner().run(Strategy(), events=[])

    assert isinstance(result, BacktestResult)
    assert result.order_log_df.empty
    assert result.fills_df.empty
    assert result.pnl_series.empty
