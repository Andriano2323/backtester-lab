from backtester import Strategy, StrategyContext
from backtester.progress import ProgressMetrics
from backtester.result import BacktestResult
from backtester.types import (
    BookSnapshot,
    BookUpdate,
    OrderAck,
    OrderAckType,
    OrderFill,
    OrderReject,
    OrderRejectReason,
    OrderStatus,
    PriceLevel,
    Side,
    Trade,
)


class FakeGateway:
    def __init__(self) -> None:
        self.next_order_id = 100
        self.sent_orders = []
        self.cancelled_orders = []
        self.modified_orders = []

    def send_order(self, instrument_id, side, price, size, timestamp_ns):
        self.sent_orders.append(
            {
                "instrument_id": instrument_id,
                "side": side,
                "price": price,
                "size": size,
                "timestamp_ns": timestamp_ns,
            }
        )
        self.next_order_id += 1
        return self.next_order_id

    def cancel_order(self, order_id, instrument_id, timestamp_ns):
        self.cancelled_orders.append(
            {
                "order_id": order_id,
                "instrument_id": instrument_id,
                "timestamp_ns": timestamp_ns,
            }
        )

    def modify_order(self, order_id, instrument_id, side, price, size, timestamp_ns):
        self.modified_orders.append(
            {
                "order_id": order_id,
                "instrument_id": instrument_id,
                "side": side,
                "price": price,
                "size": size,
                "timestamp_ns": timestamp_ns,
            }
        )


class FakeState:
    def __init__(self) -> None:
        self.positions = {10: 42}
        self.pnl = 123_456

    def current_position(self, instrument_id):
        return self.positions.get(instrument_id, 0)

    def current_pnl(self):
        return self.pnl


def make_book_update() -> BookUpdate:
    return BookUpdate(
        instrument_id=10,
        timestamp_ns=1_000,
        seq_no=1,
        side=Side.BID,
        price=101_250_000_000,
        size=10,
    )


def test_default_strategy_callbacks_are_noop_and_do_not_throw():
    strategy = Strategy()
    ctx = StrategyContext()

    strategy.on_book_update(make_book_update(), ctx)
    strategy.on_book_snapshot(
        BookSnapshot(
            instrument_id=10,
            timestamp_ns=1_001,
            seq_no=2,
            bids=[PriceLevel(level_index=0, price=101_000_000_000, size=5)],
            asks=[PriceLevel(level_index=0, price=102_000_000_000, size=6)],
        ),
        ctx,
    )
    strategy.on_trade(
        Trade(
            instrument_id=10,
            timestamp_ns=1_002,
            seq_no=3,
            price=101_500_000_000,
            size=3,
            aggressor_side=Side.ASK,
        ),
        ctx,
    )
    strategy.on_fill(
        OrderFill(
            trading_engine_id=1,
            order_id=100,
            instrument_id=10,
            side=Side.BID,
            price=101_250_000_000,
            size=10,
            timestamp_ns=1_003,
            status=OrderStatus.PARTIALLY_FILLED,
            fill_price=101_300_000_000,
            fill_size=4,
            remaining_size=6,
        ),
        ctx,
    )
    strategy.on_reject(
        OrderReject(
            trading_engine_id=1,
            order_id=100,
            instrument_id=10,
            side=Side.BID,
            price=101_250_000_000,
            size=10,
            timestamp_ns=1_004,
            status=OrderStatus.REJECTED,
            reason=OrderRejectReason.INVALID_PRICE,
            text="invalid price",
        ),
        ctx,
    )
    strategy.on_ack(
        OrderAck(
            trading_engine_id=1,
            order_id=100,
            instrument_id=10,
            side=Side.BID,
            price=101_250_000_000,
            size=10,
            timestamp_ns=1_005,
            status=OrderStatus.ACCEPTED,
            ack_type=OrderAckType.NEW_ACCEPTED,
        ),
        ctx,
    )
    strategy.on_progress(ProgressMetrics(processed_events=1, total_events=2), ctx)
    strategy.on_start(ctx)
    strategy.on_finish(BacktestResult(), ctx)


def test_subclassed_on_book_update_is_called_with_update_and_context():
    seen = {}

    class MyStrategy(Strategy):
        def on_book_update(self, update, ctx):
            seen["update"] = update
            seen["ctx"] = ctx

    update = make_book_update()
    ctx = StrategyContext()

    MyStrategy().on_book_update(update, ctx)

    assert seen["update"] is update
    assert seen["ctx"] is ctx


def test_send_order_delegates_to_gateway_send_order():
    gateway = FakeGateway()
    ctx = StrategyContext(gateway=gateway)

    ctx.send_order(10, Side.BID, 101_250_000_000, 5, 1_000)

    assert gateway.sent_orders == [
        {
            "instrument_id": 10,
            "side": Side.BID,
            "price": 101_250_000_000,
            "size": 5,
            "timestamp_ns": 1_000,
        }
    ]


def test_cancel_order_delegates_to_gateway_cancel_order():
    gateway = FakeGateway()
    ctx = StrategyContext(gateway=gateway)

    ctx.cancel_order(101, 10, 1_001)

    assert gateway.cancelled_orders == [
        {
            "order_id": 101,
            "instrument_id": 10,
            "timestamp_ns": 1_001,
        }
    ]


def test_modify_order_delegates_to_gateway_modify_order():
    gateway = FakeGateway()
    ctx = StrategyContext(gateway=gateway)

    ctx.modify_order(101, 10, Side.ASK, 102_000_000_000, 7, 1_002)

    assert gateway.modified_orders == [
        {
            "order_id": 101,
            "instrument_id": 10,
            "side": Side.ASK,
            "price": 102_000_000_000,
            "size": 7,
            "timestamp_ns": 1_002,
        }
    ]


def test_send_order_returns_order_id_from_gateway():
    gateway = FakeGateway()
    ctx = StrategyContext(gateway=gateway)

    order_id = ctx.send_order(10, Side.BID, 101_250_000_000, 5, 1_000)

    assert order_id == 101


def test_current_position_returns_position_from_injected_state():
    ctx = StrategyContext(state=FakeState())

    assert ctx.current_position(10) == 42
    assert ctx.current_position(20) == 0


def test_current_pnl_returns_pnl_from_injected_state():
    ctx = StrategyContext(state=FakeState())

    assert ctx.current_pnl() == 123_456


def test_record_metric_stores_custom_metric():
    ctx = StrategyContext()

    ctx.record_metric("alpha", 12.5, timestamp_ns=1_003)

    assert ctx.metrics == [
        {
            "name": "alpha",
            "value": 12.5,
            "timestamp_ns": 1_003,
        }
    ]


def test_strategy_subclass_can_send_order_from_on_book_update():
    gateway = FakeGateway()
    ctx = StrategyContext(gateway=gateway)

    class SendingStrategy(Strategy):
        def on_book_update(self, update, ctx):
            self.order_id = ctx.send_order(
                update.instrument_id,
                Side.BID,
                update.price,
                update.size,
                update.timestamp_ns,
            )

    strategy = SendingStrategy()
    strategy.on_book_update(make_book_update(), ctx)

    assert strategy.order_id == 101
    assert len(gateway.sent_orders) == 1
    assert gateway.sent_orders[0]["instrument_id"] == 10
