import os
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

from backtester.adapters import (
    from_cpp_book_snapshot,
    from_cpp_book_update,
    from_cpp_order_ack,
    from_cpp_order_fill,
    from_cpp_order_reject,
    from_cpp_trade,
    python_to_cpp_side,
    to_cpp_book_update,
)
from backtester.types import (
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


cpp = pytest.importorskip("_backtester_cpp")
REPO_ROOT = Path(__file__).resolve().parents[2]


def test_cpp_book_update_converts_to_python_book_update():
    cpp_update = cpp.BookUpdate(
        instrument_id=10,
        timestamp_ns=1_000,
        seq_no=1,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=5,
    )

    update = from_cpp_book_update(cpp_update)

    assert update == BookUpdate(10, 1_000, 1, Side.BID, 101_250_000_000, 5)


def test_cpp_book_snapshot_converts_to_python_book_snapshot_with_levels():
    cpp_snapshot = cpp.BookSnapshot(
        instrument_id=10,
        timestamp_ns=1_001,
        seq_no=2,
        bids=[cpp.PriceLevel(level_index=0, price=101_000_000_000, size=3)],
        asks=[cpp.PriceLevel(level_index=1, price=102_000_000_000, size=4)],
    )

    snapshot = from_cpp_book_snapshot(cpp_snapshot)

    assert snapshot.instrument_id == 10
    assert snapshot.bids == [PriceLevel(level_index=0, price=101_000_000_000, size=3)]
    assert snapshot.asks == [PriceLevel(level_index=1, price=102_000_000_000, size=4)]


def test_cpp_trade_converts_to_python_trade():
    cpp_trade = cpp.Trade(
        instrument_id=10,
        timestamp_ns=1_002,
        seq_no=3,
        price=101_500_000_000,
        size=2,
        aggressor_side=cpp.Side.Ask,
    )

    trade = from_cpp_trade(cpp_trade)

    assert trade == Trade(10, 1_002, 3, 101_500_000_000, 2, Side.ASK)


def test_cpp_order_ack_converts_to_python_order_ack():
    cpp_ack = cpp.OrderAck(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=5,
        timestamp_ns=1_003,
        status=cpp.OrderStatus.Accepted,
        ack_type=cpp.OrderAckType.NewAccepted,
    )

    ack = from_cpp_order_ack(cpp_ack)

    assert ack == OrderAck(
        1,
        100,
        10,
        Side.BID,
        101_250_000_000,
        5,
        1_003,
        OrderStatus.ACCEPTED,
        OrderAckType.NEW_ACCEPTED,
    )


def test_cpp_order_fill_converts_to_python_order_fill():
    cpp_fill = cpp.OrderFill(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=cpp.Side.Bid,
        price=101_250_000_000,
        size=5,
        timestamp_ns=1_004,
        status=cpp.OrderStatus.PartiallyFilled,
        fill_price=101_300_000_000,
        fill_size=2,
        remaining_size=3,
    )

    fill = from_cpp_order_fill(cpp_fill)

    assert fill == OrderFill(
        1,
        100,
        10,
        Side.BID,
        101_250_000_000,
        5,
        1_004,
        OrderStatus.PARTIALLY_FILLED,
        101_300_000_000,
        2,
        3,
    )


def test_cpp_order_reject_converts_to_python_order_reject():
    cpp_reject = cpp.OrderReject(
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side=getattr(cpp.Side, "None"),
        price=cpp.UNDEFINED_PRICE,
        size=0,
        timestamp_ns=1_005,
        status=cpp.OrderStatus.Rejected,
        reason=cpp.OrderRejectReason.InvalidPrice,
        text="invalid price",
    )

    reject = from_cpp_order_reject(cpp_reject)

    assert reject == OrderReject(
        1,
        100,
        10,
        Side.NONE,
        cpp.UNDEFINED_PRICE,
        0,
        1_005,
        OrderStatus.REJECTED,
        OrderRejectReason.INVALID_PRICE,
        "invalid price",
    )


def test_python_book_update_converts_back_to_cpp_book_update():
    update = BookUpdate(
        instrument_id=10,
        timestamp_ns=1_006,
        seq_no=4,
        side=Side.BID,
        price=101_250_000_000,
        size=5,
    )

    cpp_update = to_cpp_book_update(update)

    assert isinstance(cpp_update, cpp.BookUpdate)
    assert cpp_update.instrument_id == 10
    assert cpp_update.timestamp_ns == 1_006
    assert cpp_update.seq_no == 4
    assert cpp_update.side == cpp.Side.Bid
    assert cpp_update.price == 101_250_000_000
    assert cpp_update.size == 5


def test_python_side_bid_maps_to_cpp_side_consistently():
    mapped = python_to_cpp_side(Side.BID)

    assert mapped == cpp.Side.Bid
    assert mapped == cpp.Side.BID


def test_missing_compiled_extension_raises_clear_error_only_when_cpp_conversion_is_requested():
    env = os.environ.copy()
    env["PYTHONPATH"] = str(REPO_ROOT / "python")
    script = textwrap.dedent(
        """
        from backtester.adapters import to_cpp_book_update
        from backtester.types import BookUpdate, Side

        update = BookUpdate(10, 1, 1, Side.BID, 100, 5)
        try:
            to_cpp_book_update(update)
        except RuntimeError as exc:
            assert "_backtester_cpp" in str(exc)
        else:
            raise AssertionError("expected RuntimeError")
        """
    )

    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            script,
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr


def test_pure_python_conversions_still_work_without_cpp_extension():
    env = os.environ.copy()
    env["PYTHONPATH"] = str(REPO_ROOT / "python")
    script = textwrap.dedent(
        """
        from backtester.adapters import from_cpp_book_update
        from backtester.types import Side

        class CppLike:
            instrument_id = 10
            timestamp_ns = 1
            seq_no = 2
            side = Side.BID
            price = 100
            size = 5

        converted = from_cpp_book_update(CppLike())
        assert converted.side is Side.BID
        assert converted.price == 100
        """
    )

    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            script,
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr
