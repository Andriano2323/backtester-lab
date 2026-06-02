import pytest

from backtester import CppOrderGatewayFacade, Strategy, StrategyContext
from backtester.types import (
    BookUpdate,
    OrderAck,
    OrderAckType,
    OrderFill,
    OrderReject,
    OrderRejectReason,
    OrderStatus,
    Side,
)


cpp = pytest.importorskip("_backtester_cpp")


def _new_gateway(trading_engine_id=1):
    channel = cpp.OrderChannel(request_batch_size=1, event_batch_size=1)
    client = cpp.OrderGatewayClient(
        trading_engine_id=trading_engine_id, channel=channel
    )
    server = cpp.OrderGatewayServer()
    server.register_engine(trading_engine_id, channel)
    return CppOrderGatewayFacade(client), server


def _accept_order(facade, server):
    order_id = facade.send_order(
        instrument_id=10,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
        timestamp_ns=1,
    )
    assert server.drain_requests() == 1
    server.flush_events()
    facade.drain_events()
    return order_id


def _book_update():
    return BookUpdate(
        instrument_id=10,
        timestamp_ns=1_000,
        seq_no=1,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )


def test_facade_send_order_delegates_to_cpp_client_and_returns_order_id():
    facade, server = _new_gateway()

    order_id = facade.send_order(10, Side.BID, 101_250_000_000, 5, 1)

    assert order_id == 1
    assert server.drain_requests() == 1


def test_facade_cancel_order_delegates_to_cpp_client():
    facade, server = _new_gateway()
    acks = []
    facade.on_ack(acks.append)
    order_id = _accept_order(facade, server)

    facade.cancel_order(order_id, 10, 2)

    assert server.drain_requests() == 1
    server.flush_events()
    assert facade.drain_events() == 1
    assert acks[-1].ack_type is OrderAckType.CANCEL_ACCEPTED
    assert acks[-1].status is OrderStatus.CANCELLED


def test_facade_modify_order_delegates_to_cpp_client():
    facade, server = _new_gateway()
    acks = []
    facade.on_ack(acks.append)
    order_id = _accept_order(facade, server)

    facade.modify_order(order_id, 10, Side.ASK, 101_500_000_000, 7, 2)

    assert server.drain_requests() == 1
    server.flush_events()
    assert facade.drain_events() == 1
    assert acks[-1].ack_type is OrderAckType.MODIFY_ACCEPTED
    assert acks[-1].side is Side.ASK
    assert acks[-1].price == 101_500_000_000
    assert acks[-1].size == 7


def test_on_ack_receives_pure_python_order_ack_dataclass():
    facade, server = _new_gateway()
    acks = []
    facade.on_ack(acks.append)

    facade.send_order(10, Side.BID, 101_250_000_000, 5, 1)
    assert server.drain_requests() == 1
    server.flush_events()

    assert facade.drain_events() == 1
    assert isinstance(acks[0], OrderAck)
    assert acks[0].ack_type is OrderAckType.NEW_ACCEPTED
    assert acks[0].status is OrderStatus.ACCEPTED


def test_on_fill_receives_pure_python_order_fill_dataclass():
    facade, server = _new_gateway()
    fills = []
    facade.on_fill(fills.append)
    order_id = _accept_order(facade, server)

    assert server.emit_fill(1, order_id, 101_300_000_000, 2, 2) is True
    server.flush_events()

    assert facade.drain_events() == 1
    assert isinstance(fills[0], OrderFill)
    assert fills[0].fill_price == 101_300_000_000
    assert fills[0].fill_size == 2
    assert fills[0].remaining_size == 3
    assert fills[0].status is OrderStatus.PARTIALLY_FILLED


def test_on_reject_receives_pure_python_order_reject_dataclass():
    facade, server = _new_gateway()
    rejects = []
    facade.on_reject(rejects.append)

    facade.send_order(0, Side.BID, 101_250_000_000, 5, 1)
    assert server.drain_requests() == 1
    server.flush_events()

    assert facade.drain_events() == 1
    assert isinstance(rejects[0], OrderReject)
    assert rejects[0].reason is OrderRejectReason.INVALID_INSTRUMENT
    assert rejects[0].status is OrderStatus.REJECTED


def test_strategy_context_can_use_cpp_order_gateway_facade():
    facade, server = _new_gateway()
    ctx = StrategyContext(gateway=facade)

    order_id = ctx.send_order(10, Side.BID, 101_250_000_000, 5, 1)

    assert order_id == 1
    assert server.drain_requests() == 1


def test_strategy_can_send_order_from_on_book_update_through_strategy_context():
    facade, server = _new_gateway()
    ctx = StrategyContext(gateway=facade)

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
    strategy.on_book_update(_book_update(), ctx)

    assert strategy.order_id == 1
    assert server.drain_requests() == 1
