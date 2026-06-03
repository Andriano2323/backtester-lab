import sys

import pandas as pd

from backtester.result import BacktestResult, OrderStatistics


EXPECTED_FILL_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "instrument_id",
    "side",
    "fill_price",
    "fill_size",
    "remaining_size",
]

EXPECTED_ORDER_LOG_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "instrument_id",
    "side",
    "price",
    "size",
    "status",
    "event_type",
    "reason",
    "text",
]

EXPECTED_METRIC_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "name",
    "value",
]

EXPECTED_PNL_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "pnl",
]

EXPECTED_POSITION_COLUMNS = [
    "timestamp_ns",
    "trading_engine_id",
    "strategy_name",
    "instrument_id",
    "position",
    "realized_pnl",
    "unrealized_pnl",
    "pnl",
]

EXPECTED_TRACE_COLUMNS = [
    "timestamp_ns",
    "sequence",
    "stage",
    "explain_stage",
    "event_type",
    "trading_engine_id",
    "strategy_name",
    "order_id",
    "synthetic_order_id",
    "activation_time_ns",
    "instrument_id",
    "side",
    "price",
    "size",
    "best_bid_before",
    "best_ask_before",
    "best_bid_after",
    "best_ask_after",
    "fill_price",
    "fill_size",
    "remaining_size",
    "reason",
    "text",
]


def test_empty_pnl_series_is_empty_pandas_series():
    series = BacktestResult().pnl_series

    assert isinstance(series, pd.Series)
    assert series.empty
    assert series.name == "pnl"
    assert series.index.name == "timestamp_ns"


def test_empty_fills_df_has_expected_columns():
    df = BacktestResult().fills_df

    assert isinstance(df, pd.DataFrame)
    assert list(df.columns) == EXPECTED_FILL_COLUMNS
    assert df.empty


def test_empty_order_log_df_has_expected_columns():
    df = BacktestResult().order_log_df

    assert isinstance(df, pd.DataFrame)
    assert list(df.columns) == EXPECTED_ORDER_LOG_COLUMNS
    assert df.empty


def test_empty_pnl_df_has_expected_columns():
    df = BacktestResult().pnl_df

    assert isinstance(df, pd.DataFrame)
    assert list(df.columns) == EXPECTED_PNL_COLUMNS
    assert df.empty


def test_empty_positions_df_has_expected_columns():
    df = BacktestResult().positions_df

    assert isinstance(df, pd.DataFrame)
    assert list(df.columns) == EXPECTED_POSITION_COLUMNS
    assert df.empty


def test_empty_trace_df_has_expected_columns():
    df = BacktestResult().trace_df

    assert isinstance(df, pd.DataFrame)
    assert list(df.columns) == EXPECTED_TRACE_COLUMNS
    assert df.empty


def test_add_fill_adds_one_row_to_fills_df():
    result = BacktestResult()

    result.add_fill(
        timestamp_ns=1_000,
        trading_engine_id=1,
        strategy_name="alpha",
        order_id=101,
        instrument_id=10,
        side="B",
        fill_price=101_300_000_000,
        fill_size=4,
        remaining_size=6,
    )

    df = result.fills_df
    assert len(df) == 1
    assert df.loc[0, "strategy_name"] == "alpha"
    assert df.loc[0, "order_id"] == 101
    assert df.loc[0, "fill_price"] == 101_300_000_000
    assert df.loc[0, "fill_size"] == 4
    assert df.loc[0, "remaining_size"] == 6


def test_add_order_log_adds_one_row_to_order_log_df():
    result = BacktestResult()

    result.add_order_log(
        timestamp_ns=1_001,
        trading_engine_id=1,
        strategy_name="alpha",
        order_id=101,
        instrument_id=10,
        side="B",
        price=101_250_000_000,
        size=10,
        status="Accepted",
        event_type="new_order",
    )

    df = result.order_log_df
    assert len(df) == 1
    assert df.loc[0, "strategy_name"] == "alpha"
    assert df.loc[0, "order_id"] == 101
    assert df.loc[0, "status"] == "Accepted"
    assert df.loc[0, "event_type"] == "new_order"


def test_add_pnl_point_adds_one_point_indexed_by_timestamp_ns():
    result = BacktestResult()

    result.add_pnl_point(
        timestamp_ns=1_002,
        pnl=12_345,
        trading_engine_id=1,
        strategy_name="alpha",
    )

    series = result.pnl_series
    assert len(series) == 1
    assert series.index[0] == 1_002
    assert series.loc[1_002] == 12_345
    df = result.pnl_df
    assert list(df.columns) == EXPECTED_PNL_COLUMNS
    assert df.loc[0, "trading_engine_id"] == 1
    assert df.loc[0, "strategy_name"] == "alpha"


def test_add_metric_adds_one_row_to_metrics_df():
    result = BacktestResult()

    result.add_metric(
        "alpha",
        0.42,
        timestamp_ns=1_003,
        trading_engine_id=1,
        strategy_name="alpha",
    )

    df = result.metrics_df
    assert list(df.columns) == EXPECTED_METRIC_COLUMNS
    assert len(df) == 1
    assert df.loc[0, "timestamp_ns"] == 1_003
    assert df.loc[0, "trading_engine_id"] == 1
    assert df.loc[0, "strategy_name"] == "alpha"
    assert df.loc[0, "name"] == "alpha"
    assert df.loc[0, "value"] == 0.42


def test_add_position_adds_one_row_to_positions_df():
    result = BacktestResult()

    result.add_position(
        timestamp_ns=1_004,
        trading_engine_id=1,
        strategy_name="alpha",
        instrument_id=10,
        position=5,
        realized_pnl=100,
        unrealized_pnl=25,
        pnl=125,
    )

    df = result.positions_df
    assert list(df.columns) == EXPECTED_POSITION_COLUMNS
    assert len(df) == 1
    assert df.loc[0, "strategy_name"] == "alpha"
    assert df.loc[0, "instrument_id"] == 10
    assert df.loc[0, "position"] == 5
    assert df.loc[0, "pnl"] == 125


def test_add_trace_adds_one_row_to_trace_df_with_auto_sequence():
    result = BacktestResult()

    result.add_trace(
        timestamp_ns=1_005,
        stage="order_fill",
        event_type="fill",
        trading_engine_id=1,
        strategy_name="alpha",
        order_id=101,
        activation_time_ns=1_005,
        instrument_id=10,
        side="B",
        price=102_000_000_000,
        size=2,
        best_bid_before=101_000_000_000,
        best_ask_before=102_000_000_000,
        fill_price=102_000_000_000,
        fill_size=2,
        remaining_size=0,
        reason="crossed_best_ask",
    )

    df = result.trace_df
    assert list(df.columns) == EXPECTED_TRACE_COLUMNS
    assert len(df) == 1
    assert df.loc[0, "sequence"] == 1
    assert df.loc[0, "strategy_name"] == "alpha"
    assert df.loc[0, "stage"] == "order_fill"
    assert df.loc[0, "explain_stage"] == "fill"
    assert df.loc[0, "activation_time_ns"] == 1_005
    assert df.loc[0, "reason"] == "crossed_best_ask"


def test_trace_can_be_disabled():
    result = BacktestResult(trace_enabled=False)

    record = result.add_trace(1, "market_event", "book_snapshot")

    assert record.stage == "market_event"
    assert result.trace_df.empty


def test_order_statistics_counts_sent_cancelled_filled_and_rejected():
    result = BacktestResult()

    result.add_order_log(1, 1, 101, 10, "B", 100, 10, "Accepted", "new_order")
    result.add_order_log(2, 1, 101, 10, "B", 100, 10, "Cancelled", "cancel_order")
    result.add_order_log(3, 1, 102, 10, "B", 100, 10, "Filled", "fill")
    result.add_order_log(
        4,
        1,
        103,
        10,
        "B",
        100,
        10,
        "Rejected",
        "reject",
        "InvalidPrice",
        "invalid price",
    )

    assert result.order_statistics == OrderStatistics(
        sent=1, cancelled=1, filled=1, rejected=1
    )


def test_result_filters_by_engine_and_strategy():
    result = BacktestResult()
    result.add_order_log(
        1, 1, 101, 10, "B", 100, 1, "Accepted", "new_order", strategy_name="alpha"
    )
    result.add_order_log(
        1, 2, 101, 10, "B", 100, 1, "Accepted", "new_order", strategy_name="beta"
    )
    result.add_fill(2, 1, 101, 10, "B", 100, 1, 0, strategy_name="alpha")
    result.add_fill(2, 2, 101, 10, "B", 100, 1, 0, strategy_name="beta")
    result.add_pnl_point(2, 10, trading_engine_id=1, strategy_name="alpha")
    result.add_pnl_point(2, 20, trading_engine_id=2, strategy_name="beta")
    result.add_trace(
        2, "order_fill", "fill", trading_engine_id=1, strategy_name="alpha"
    )
    result.add_trace(2, "order_fill", "fill", trading_engine_id=2, strategy_name="beta")

    alpha = result.for_strategy("alpha")
    engine_two = result.for_engine(2)

    assert alpha.order_log_df["strategy_name"].tolist() == ["alpha"]
    assert alpha.fills_df["trading_engine_id"].tolist() == [1]
    assert alpha.pnl_df["pnl"].tolist() == [10]
    assert engine_two.order_log_df["strategy_name"].tolist() == ["beta"]
    assert engine_two.fills_df["order_id"].tolist() == [101]
    assert sorted(result.by_strategy()) == ["alpha", "beta"]
    assert sorted(result.by_engine()) == [1, 2]


def test_dataframe_column_order_is_stable():
    result = BacktestResult()

    assert list(result.fills_df.columns) == EXPECTED_FILL_COLUMNS
    assert list(result.order_log_df.columns) == EXPECTED_ORDER_LOG_COLUMNS
    assert list(result.pnl_df.columns) == EXPECTED_PNL_COLUMNS
    assert list(result.metrics_df.columns) == EXPECTED_METRIC_COLUMNS
    assert list(result.positions_df.columns) == EXPECTED_POSITION_COLUMNS
    assert list(result.trace_df.columns) == EXPECTED_TRACE_COLUMNS


def test_result_can_be_created_and_read_without_cpp_binding():
    sys.modules.pop("_backtester_cpp", None)
    sys.modules.pop("backtester._backtester_cpp", None)

    result = BacktestResult()

    assert result.pnl_series.empty
    assert result.fills_df.empty
    assert result.order_log_df.empty
    assert result.metrics_df.empty
    assert result.positions_df.empty
    assert result.trace_df.empty
    assert "_backtester_cpp" not in sys.modules
    assert "backtester._backtester_cpp" not in sys.modules
