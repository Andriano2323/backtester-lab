import pytest


cpp = pytest.importorskip("_backtester_cpp")


def test_can_construct_cpp_book_update_from_python():
    update = cpp.BookUpdate(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_001,
        seq_no=7,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=12,
    )

    assert update.instrument_id == 10
    assert update.timestamp_ns == 1_700_000_000_000_000_001
    assert update.seq_no == 7
    assert update.side == cpp.Side.Bid
    assert update.price == 101_250_000_000
    assert update.size == 12


def test_book_update_fields_are_readable_and_writable():
    update = cpp.BookUpdate()

    update.instrument_id = 20
    update.timestamp_ns = 123
    update.seq_no = 9
    update.side = cpp.Side.Ask
    update.price = 102_000_000_000
    update.size = 15

    assert update.instrument_id == 20
    assert update.timestamp_ns == 123
    assert update.seq_no == 9
    assert update.side == cpp.Side.Ask
    assert update.price == 102_000_000_000
    assert update.size == 15


def test_can_construct_book_snapshot_with_bid_and_ask_price_level_lists():
    bid = cpp.PriceLevel(level_index=0, price=101_000_000_000, size=5)
    ask = cpp.PriceLevel(level_index=1, price=102_000_000_000, size=6)
    snapshot = cpp.BookSnapshot(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_002,
        seq_no=8,
        bids=[bid],
        asks=[ask],
    )

    assert snapshot.bids[0].level_index == 0
    assert snapshot.bids[0].price == 101_000_000_000
    assert snapshot.bids[0].size == 5
    assert snapshot.asks[0].level_index == 1
    assert snapshot.asks[0].price == 102_000_000_000
    assert snapshot.asks[0].size == 6


def test_can_construct_trade():
    trade = cpp.Trade(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_003,
        seq_no=9,
        price=101_500_000_000,
        size=3,
        aggressor_side=cpp.Side.Ask,
    )

    assert trade.instrument_id == 10
    assert trade.price == 101_500_000_000
    assert trade.size == 3
    assert trade.aggressor_side == cpp.Side.Ask


def test_can_construct_order_ack():
    ack = cpp.OrderAck(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=12,
        timestamp_ns=1_700_000_000_000_000_004,
        status=cpp.OrderStatus.Accepted,
        ack_type=cpp.OrderAckType.NewAccepted,
    )

    assert ack.trading_engine_id == 1
    assert ack.order_id == 100
    assert ack.instrument_id == 10
    assert ack.side == cpp.Side.Bid
    assert ack.status == cpp.OrderStatus.Accepted
    assert ack.ack_type == cpp.OrderAckType.NewAccepted

    ack.status = cpp.OrderStatus.Cancelled
    assert ack.status == cpp.OrderStatus.Cancelled


def test_can_construct_order_fill():
    fill = cpp.OrderFill(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=12,
        timestamp_ns=1_700_000_000_000_000_005,
        status=cpp.OrderStatus.PartiallyFilled,
        fill_price=101_300_000_000,
        fill_size=4,
        remaining_size=8,
    )

    assert fill.fill_price == 101_300_000_000
    assert fill.fill_size == 4
    assert fill.remaining_size == 8
    assert fill.status == cpp.OrderStatus.PartiallyFilled

    fill.remaining_size = 7
    assert fill.remaining_size == 7


def test_can_construct_order_reject():
    reject = cpp.OrderReject(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=getattr(cpp.Side, "None"),
        price=cpp.UNDEFINED_PRICE,
        size=0,
        timestamp_ns=1_700_000_000_000_000_006,
        status=cpp.OrderStatus.Rejected,
        reason=cpp.OrderRejectReason.InvalidPrice,
        text="invalid price",
    )

    assert reject.reason == cpp.OrderRejectReason.InvalidPrice
    assert reject.text == "invalid price"
    assert reject.status == cpp.OrderStatus.Rejected

    reject.text = "still invalid"
    assert reject.text == "still invalid"


def test_side_enum_maps_consistently_to_title_and_uppercase_names():
    assert cpp.Side.Ask == cpp.Side.ASK
    assert cpp.Side.Bid == cpp.Side.BID
    assert getattr(cpp.Side, "None") == cpp.Side.NONE


def test_price_remains_scaled_integer_not_float():
    update = cpp.BookUpdate(price=1_234_567_890)

    assert update.price == 1_234_567_890
    assert isinstance(update.price, int)


def test_cpp_message_values_can_be_converted_to_pure_python_dataclasses():
    from backtester.adapters import from_cpp_book_update
    from backtester.types import BookUpdate, Side

    update = cpp.BookUpdate(side=cpp.Side.Bid, price=1_234_567_890)
    converted = from_cpp_book_update(update)

    assert isinstance(converted, BookUpdate)
    assert converted.side is Side.BID
    assert converted.price == 1_234_567_890
