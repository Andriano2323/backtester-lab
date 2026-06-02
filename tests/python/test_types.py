import pytest

from backtester.types import (
    UNDEFINED_PRICE,
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
    float_to_price,
    is_defined_price,
    price_to_float,
)


def test_book_update_stores_all_fields_correctly():
    update = BookUpdate(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_001,
        seq_no=7,
        side=Side.BID,
        price=101_250_000_000,
        size=12,
    )

    assert update.instrument_id == 10
    assert update.timestamp_ns == 1_700_000_000_000_000_001
    assert update.seq_no == 7
    assert update.side is Side.BID
    assert update.price == 101_250_000_000
    assert update.size == 12


def test_book_snapshot_stores_bid_and_ask_levels_correctly():
    bid = PriceLevel(level_index=0, price=101_000_000_000, size=5)
    ask = PriceLevel(level_index=1, price=102_000_000_000, size=6)
    snapshot = BookSnapshot(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_002,
        seq_no=8,
        bids=[bid],
        asks=[ask],
    )

    assert snapshot.instrument_id == 10
    assert snapshot.timestamp_ns == 1_700_000_000_000_000_002
    assert snapshot.seq_no == 8
    assert snapshot.bids[0].level_index == 0
    assert snapshot.bids[0].price == 101_000_000_000
    assert snapshot.bids[0].size == 5
    assert snapshot.asks[0].level_index == 1
    assert snapshot.asks[0].price == 102_000_000_000
    assert snapshot.asks[0].size == 6


def test_trade_stores_aggressor_side_correctly():
    trade = Trade(
        instrument_id=10,
        timestamp_ns=1_700_000_000_000_000_003,
        seq_no=9,
        price=101_500_000_000,
        size=3,
        aggressor_side=Side.ASK,
    )

    assert trade.aggressor_side is Side.ASK


def test_order_ack_stores_ack_type_and_status():
    ack = OrderAck(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=Side.BID,
        price=101_250_000_000,
        size=12,
        timestamp_ns=1_700_000_000_000_000_004,
        status=OrderStatus.ACCEPTED,
        ack_type=OrderAckType.NEW_ACCEPTED,
    )

    assert ack.ack_type is OrderAckType.NEW_ACCEPTED
    assert ack.status is OrderStatus.ACCEPTED


def test_order_fill_stores_fill_price_size_and_remaining_size():
    fill = OrderFill(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=Side.BID,
        price=101_250_000_000,
        size=12,
        timestamp_ns=1_700_000_000_000_000_005,
        status=OrderStatus.PARTIALLY_FILLED,
        fill_price=101_300_000_000,
        fill_size=4,
        remaining_size=8,
    )

    assert fill.fill_price == 101_300_000_000
    assert fill.fill_size == 4
    assert fill.remaining_size == 8


def test_order_reject_stores_reason_and_text():
    reject = OrderReject(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=Side.NONE,
        price=UNDEFINED_PRICE,
        size=0,
        timestamp_ns=1_700_000_000_000_000_006,
        status=OrderStatus.REJECTED,
        reason=OrderRejectReason.INVALID_PRICE,
        text="invalid price",
    )

    assert reject.reason is OrderRejectReason.INVALID_PRICE
    assert reject.text == "invalid price"


def test_price_to_float_converts_fixed_precision_price():
    assert price_to_float(1_234_567_890) == pytest.approx(1.23456789)


def test_float_to_price_converts_decimal_price_to_fixed_precision_int():
    assert float_to_price(1.23456789) == 1_234_567_890


def test_is_defined_price_returns_false_for_undefined_price():
    assert is_defined_price(UNDEFINED_PRICE) is False


def test_is_defined_price_returns_true_for_zero():
    assert is_defined_price(0) is True


def test_side_enum_values_preserve_abn_representation():
    assert Side.ASK.value == "A"
    assert Side.BID.value == "B"
    assert Side.NONE.value == "N"
