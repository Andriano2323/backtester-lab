from pathlib import Path

import pandas as pd
import pytest

import backtester as bt
from backtester._cpp import require_cpp
from backtester import BacktestResult, BacktestRunner, IntegratedBacktestError, Strategy
from backtester.types import Action, MarketDataEvent, OrderAck, OrderFill, Side


PRICE = 102_000_000_000
FIXTURE_DIR = Path(__file__).resolve().parents[1] / "fixtures" / "integrated_l3"


def _integrated_config():
    return {
        "instruments": [10],
        "publish_book_updates": False,
        "publish_trades": False,
        "snapshot_depth": 1,
        "snapshot_interval_events": 1,
    }


def _add_ask_event(timestamp_ns=1):
    return MarketDataEvent(
        timestamp=timestamp_ns,
        ts_recv=timestamp_ns,
        ts_event=timestamp_ns,
        order_id=9001,
        side=Side.ASK,
        price=PRICE,
        size=4,
        action=Action.ADD,
        instrument_id=10,
        source_file_id=1,
        source_sequence=1,
    )


def _modify_ask_event(timestamp_ns=2, price=101_000_000_000):
    return MarketDataEvent(
        timestamp=timestamp_ns,
        ts_recv=timestamp_ns,
        ts_event=timestamp_ns,
        order_id=9001,
        side=Side.ASK,
        price=price,
        size=4,
        action=Action.MODIFY,
        instrument_id=10,
        source_file_id=1,
        source_sequence=2,
    )


class BuyFirstAskStrategy(Strategy):
    def __init__(self):
        self.callbacks = []
        self.snapshots = []
        self.acks = []
        self.fills = []
        self._sent = False

    def on_start(self, ctx):
        self.callbacks.append("start")

    def on_book_snapshot(self, snapshot, ctx):
        self.callbacks.append(f"snapshot:{snapshot.instrument_id}:{snapshot.timestamp_ns}")
        self.snapshots.append(snapshot)
        if self._sent or snapshot.instrument_id != 10 or not snapshot.asks:
            return
        self._sent = True
        ctx.send_order(
            snapshot.instrument_id,
            Side.BID,
            snapshot.asks[0].price,
            2,
            snapshot.timestamp_ns,
        )

    def on_ack(self, ack, ctx):
        self.callbacks.append("ack")
        self.acks.append(ack)

    def on_fill(self, fill, ctx):
        self.callbacks.append("fill")
        self.fills.append(fill)

    def on_finish(self, result, ctx):
        self.callbacks.append("finish")


class SnapshotRecorder(Strategy):
    def __init__(self):
        self.snapshots = []

    def on_book_snapshot(self, snapshot, ctx):
        self.snapshots.append(snapshot)


class PlannedBuyerStrategy(Strategy):
    def __init__(self, orders_by_timestamp):
        self.orders_by_timestamp = dict(orders_by_timestamp)
        self.snapshots = []
        self.acks = []
        self.fills = []
        self.rejects = []
        self.order_ids = []

    def on_book_snapshot(self, snapshot, ctx):
        self.snapshots.append(snapshot)
        size = self.orders_by_timestamp.get(snapshot.timestamp_ns)
        if size is None or not snapshot.asks:
            return
        self.order_ids.append(
            ctx.send_order(
                snapshot.instrument_id,
                Side.BID,
                snapshot.asks[0].price,
                size,
                snapshot.timestamp_ns,
            )
        )

    def on_ack(self, ack, ctx):
        self.acks.append(ack)

    def on_fill(self, fill, ctx):
        self.fills.append(fill)

    def on_reject(self, reject, ctx):
        self.rejects.append(reject)


class InvalidOrderStrategy(Strategy):
    def __init__(self):
        self.acks = []
        self.fills = []
        self.rejects = []
        self._sent = False

    def on_book_snapshot(self, snapshot, ctx):
        if self._sent:
            return
        self._sent = True
        ctx.send_order(0, Side.BID, PRICE, 1, snapshot.timestamp_ns)

    def on_ack(self, ack, ctx):
        self.acks.append(ack)

    def on_fill(self, fill, ctx):
        self.fills.append(fill)

    def on_reject(self, reject, ctx):
        self.rejects.append(reject)


def _metric(result, name):
    rows = result.metrics_df[result.metrics_df["name"] == name]
    assert len(rows) == 1
    return rows.iloc[0]["value"]


def _require_pyarrow_feather():
    cpp = require_cpp()
    if not bool(getattr(cpp, "ARROW_ENABLED", False)):
        pytest.skip("Arrow-enabled C++ extension is required for Feather input")
    pa = pytest.importorskip("pyarrow")
    feather = pytest.importorskip("pyarrow.feather")
    return pa, feather


def _write_feather(path, rows):
    pa, feather = _require_pyarrow_feather()
    table = pa.table(
        {
            "ts_recv": [str(row["ts_recv"]) for row in rows],
            "ts_event": [str(row["ts_event"]) for row in rows],
            "instrument_id": [row["instrument_id"] for row in rows],
            "order_id": [str(row["order_id"]) for row in rows],
            "side": [row["side"] for row in rows],
            "price": [row["price"] for row in rows],
            "size": [row["size"] for row in rows],
            "action": [row["action"] for row in rows],
        }
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    feather.write_feather(table, path)


def _tiny_rows():
    return [
        {
            "ts_recv": 100,
            "ts_event": 100,
            "instrument_id": 10,
            "order_id": "1001",
            "side": "A",
            "price": "102.000000000",
            "size": 4,
            "action": "A",
        }
    ]


def _deterministic_rows():
    return [
        [
            {
                "ts_recv": 100,
                "ts_event": 100,
                "instrument_id": 10,
                "order_id": "2001",
                "side": "A",
                "price": "102000000000",
                "size": 4,
                "action": "A",
            },
            {
                "ts_recv": 400,
                "ts_event": 400,
                "instrument_id": 10,
                "order_id": "2004",
                "side": "B",
                "price": "100000000000",
                "size": 3,
                "action": "A",
            },
        ],
        [
            {
                "ts_recv": 200,
                "ts_event": 200,
                "instrument_id": 10,
                "order_id": "2002",
                "side": "B",
                "price": "101000000000",
                "size": 5,
                "action": "A",
            },
            {
                "ts_recv": 300,
                "ts_event": 300,
                "instrument_id": 11,
                "order_id": "2003",
                "side": "A",
                "price": "201000000000",
                "size": 6,
                "action": "A",
            },
        ],
    ]


def test_integrated_mode_delivers_snapshot_order_ack_fill_and_result():
    strategy = BuyFirstAskStrategy()

    result = BacktestRunner(config=_integrated_config()).run(
        strategy,
        events=[_add_ask_event()],
        mode="integrated",
    )

    assert isinstance(result, BacktestResult)
    assert strategy.callbacks == ["start", "snapshot:10:1", "ack", "fill", "finish"]
    assert len(strategy.snapshots) == 1
    assert strategy.snapshots[0].asks[0].price == PRICE
    assert len(strategy.acks) == 1
    assert isinstance(strategy.acks[0], OrderAck)
    assert len(strategy.fills) == 1
    assert isinstance(strategy.fills[0], OrderFill)
    assert strategy.fills[0].fill_price == PRICE
    assert strategy.fills[0].fill_size == 2

    assert len(result.fills_df) == 1
    assert result.fills_df.loc[0, "fill_price"] == PRICE
    assert result.order_log_df["event_type"].tolist() == ["new_order", "ack", "fill"]
    assert result.positions_df.loc[0, "position"] == 2
    assert not result.pnl_series.empty


def test_integrated_mode_accepts_explain_flag():
    explained = BacktestRunner(config=_integrated_config()).run(
        BuyFirstAskStrategy(),
        events=[_add_ask_event()],
        mode="integrated",
        explain=True,
    )
    quiet = BacktestRunner(config=_integrated_config()).run(
        BuyFirstAskStrategy(),
        events=[_add_ask_event()],
        mode="integrated",
        explain=False,
    )

    assert not explained.trace_df.empty
    assert quiet.trace_df.empty
    assert len(quiet.fills_df) == 1


def test_integrated_immediate_order_fills_against_current_book():
    strategy = BuyFirstAskStrategy()

    result = BacktestRunner(
        config={**_integrated_config(), "execution_model": "immediate"}
    ).run(
        strategy,
        events=[_add_ask_event(timestamp_ns=100), _modify_ask_event(timestamp_ns=200)],
        mode="integrated",
    )

    assert result.fills_df.loc[0, "timestamp_ns"] == 100
    assert result.fills_df.loc[0, "fill_price"] == PRICE
    assert result.trace_df[result.trace_df["stage"] == "order_fill"].iloc[0][
        "activation_time_ns"
    ] == 100


def test_integrated_latency_order_waits_then_fills_on_next_book_state():
    strategy = BuyFirstAskStrategy()

    result = BacktestRunner(
        config={**_integrated_config(), "execution_model": "latency", "latency_ns": 50}
    ).run(
        strategy,
        events=[_add_ask_event(timestamp_ns=100), _modify_ask_event(timestamp_ns=200)],
        mode="integrated",
    )

    assert strategy.callbacks == [
        "start",
        "snapshot:10:100",
        "ack",
        "snapshot:10:200",
        "fill",
        "finish",
    ]
    assert result.fills_df.loc[0, "timestamp_ns"] == 200
    assert result.fills_df.loc[0, "fill_price"] == 101_000_000_000

    not_active = result.trace_df[result.trace_df["event_type"] == "not_active"].iloc[0]
    assert not_active["reason"] == "activation_pending"
    assert not_active["activation_time_ns"] == 150
    assert "current_timestamp_ns=100" in not_active["text"]

    fill_trace = result.trace_df[result.trace_df["stage"] == "order_fill"].iloc[0]
    assert fill_trace["activation_time_ns"] == 150
    assert fill_trace["reason"] == "crossed_best_ask"


def test_integrated_immediate_and_latency_have_different_fills_and_pnl():
    events = [_add_ask_event(timestamp_ns=100), _modify_ask_event(timestamp_ns=200)]

    immediate = BacktestRunner(
        config={**_integrated_config(), "execution_model": "immediate"}
    ).run(BuyFirstAskStrategy(), events=events, mode="integrated")
    latency = BacktestRunner(
        config={**_integrated_config(), "execution_model": "latency", "latency_ns": 50}
    ).run(BuyFirstAskStrategy(), events=events, mode="integrated")

    assert immediate.fills_df.to_dict("records") != latency.fills_df.to_dict(
        "records"
    )
    assert immediate.pnl_series.to_dict() != latency.pnl_series.to_dict()
    assert immediate.fills_df.loc[0, "fill_price"] == PRICE
    assert latency.fills_df.loc[0, "fill_price"] == 101_000_000_000


def _multi_engine_events():
    return [
        _add_ask_event(timestamp_ns=100),
        _modify_ask_event(timestamp_ns=200, price=PRICE),
    ]


def _multi_engine_config():
    return {
        **_integrated_config(),
        "strategy_engine_ids": {
            "alpha": 11,
            "beta": 22,
        },
    }


def _run_multi_engine_fixture():
    alpha = PlannedBuyerStrategy({100: 2, 200: 4})
    beta = PlannedBuyerStrategy({200: 4})
    result = BacktestRunner(config=_multi_engine_config()).run_many(
        {"alpha": alpha, "beta": beta},
        events=_multi_engine_events(),
        mode="integrated",
    )
    return result, alpha, beta


def test_integrated_run_many_feed_scoped_orders_private_liquidity_and_filtering():
    result, alpha, beta = _run_multi_engine_fixture()

    assert [snapshot.timestamp_ns for snapshot in alpha.snapshots] == [100, 200]
    assert [snapshot.timestamp_ns for snapshot in beta.snapshots] == [100, 200]
    assert [snapshot.asks[0].price for snapshot in alpha.snapshots] == [
        snapshot.asks[0].price for snapshot in beta.snapshots
    ]

    assert alpha.order_ids == [1, 2]
    assert beta.order_ids == [1]
    assert [ack.trading_engine_id for ack in alpha.acks] == [11, 11]
    assert [ack.trading_engine_id for ack in beta.acks] == [22]
    assert [fill.trading_engine_id for fill in alpha.fills] == [11, 11]
    assert [fill.trading_engine_id for fill in beta.fills] == [22]

    alpha_fills = result.for_strategy("alpha").fills_df
    beta_fills = result.for_engine(22).fills_df
    assert alpha_fills["fill_size"].tolist() == [2, 2]
    assert beta_fills["fill_size"].tolist() == [4]
    assert alpha_fills["strategy_name"].unique().tolist() == ["alpha"]
    assert beta_fills["strategy_name"].unique().tolist() == ["beta"]
    assert sorted(result.by_strategy()) == ["alpha", "beta"]
    assert sorted(result.by_engine()) == [11, 22]


def test_integrated_run_many_routes_rejects_to_correct_strategy():
    good = PlannedBuyerStrategy({})
    bad = InvalidOrderStrategy()

    result = BacktestRunner(
        config={
            **_integrated_config(),
            "strategy_engine_ids": {"good": 31, "bad": 32},
        }
    ).run_many(
        {"good": good, "bad": bad},
        events=[_add_ask_event(timestamp_ns=100)],
        mode="integrated",
    )

    assert good.acks == []
    assert good.fills == []
    assert good.rejects == []
    assert bad.acks == []
    assert bad.fills == []
    assert len(bad.rejects) == 1
    assert bad.rejects[0].trading_engine_id == 32

    bad_log = result.for_strategy("bad").order_log_df
    assert bad_log["event_type"].tolist() == ["new_order", "reject"]
    assert bad_log["trading_engine_id"].tolist() == [32, 32]
    assert result.for_strategy("good").order_log_df.empty


def test_integrated_run_many_is_deterministic_on_same_fixture():
    first, _, _ = _run_multi_engine_fixture()
    second, _, _ = _run_multi_engine_fixture()

    assert first.order_log_df.to_dict("records") == second.order_log_df.to_dict(
        "records"
    )
    assert first.fills_df.to_dict("records") == second.fills_df.to_dict("records")
    assert first.positions_df.to_dict("records") == second.positions_df.to_dict(
        "records"
    )
    assert first.pnl_df.to_dict("records") == second.pnl_df.to_dict("records")
    pd.testing.assert_frame_equal(first.trace_df, second.trace_df)


def test_top_level_run_many_accepts_integrated_mode():
    result = bt.run_many(
        {"alpha": PlannedBuyerStrategy({})},
        events=[],
        mode="integrated",
        config={"instruments": [], "strategy_engine_ids": {"alpha": 1}},
    )

    assert isinstance(result, BacktestResult)
    assert _metric(result, "integrated_strategy_count") == 1


def test_integrated_mode_tiny_real_format_standard_file_runs_full_loop():
    strategy = BuyFirstAskStrategy()

    result = BacktestRunner(config=_integrated_config()).run(
        strategy,
        data_path=FIXTURE_DIR / "tiny_real.ndjson",
        mode="integrated",
    )

    assert strategy.callbacks == ["start", "snapshot:10:100", "ack", "fill", "finish"]
    assert strategy.snapshots[0].asks[0].price == PRICE
    assert result.order_log_df["event_type"].tolist() == ["new_order", "ack", "fill"]
    assert result.fills_df.loc[0, "fill_price"] == PRICE
    assert result.positions_df.loc[0, "position"] == 2
    assert _metric(result, "integrated_chronological_violations") == 0


def test_integrated_standard_file_matches_direct_fixture_behavior():
    direct_strategy = BuyFirstAskStrategy()
    file_strategy = BuyFirstAskStrategy()

    direct = BacktestRunner(config=_integrated_config()).run(
        direct_strategy,
        events=[_add_ask_event(timestamp_ns=100)],
        mode="integrated",
    )
    from_file = BacktestRunner(config=_integrated_config()).run(
        file_strategy,
        data_path=FIXTURE_DIR / "tiny_real.ndjson",
        mode="integrated",
    )

    assert direct.order_log_df.to_dict("records") == from_file.order_log_df.to_dict(
        "records"
    )
    assert direct.fills_df.to_dict("records") == from_file.fills_df.to_dict("records")
    assert direct.positions_df.to_dict("records") == from_file.positions_df.to_dict(
        "records"
    )


def test_integrated_flat_and_hierarchy_folder_are_deterministic():
    config = {
        **_integrated_config(),
        "instruments": [10, 11],
    }
    flat_strategy = BuyFirstAskStrategy()
    hierarchy_strategy = BuyFirstAskStrategy()

    flat = BacktestRunner(config={**config, "input_mode": "flat"}).run(
        flat_strategy,
        data_path=FIXTURE_DIR / "deterministic",
        mode="integrated",
    )
    hierarchy = BacktestRunner(config={**config, "input_mode": "hierarchy"}).run(
        hierarchy_strategy,
        data_path=FIXTURE_DIR / "deterministic",
        mode="integrated",
    )

    assert _metric(flat, "integrated_lob_digest") == _metric(
        hierarchy, "integrated_lob_digest"
    )
    assert flat.order_log_df.to_dict("records") == hierarchy.order_log_df.to_dict(
        "records"
    )
    assert flat.fills_df.to_dict("records") == hierarchy.fills_df.to_dict("records")
    assert flat.positions_df.to_dict("records") == hierarchy.positions_df.to_dict(
        "records"
    )
    assert _metric(flat, "integrated_chronological_violations") == 0
    assert _metric(hierarchy, "integrated_chronological_violations") == 0


def test_integrated_instrument_filter_excludes_other_instruments():
    strategy = SnapshotRecorder()

    result = BacktestRunner(config=_integrated_config()).run(
        strategy,
        data_path=FIXTURE_DIR / "deterministic",
        mode="integrated",
    )

    assert {snapshot.instrument_id for snapshot in strategy.snapshots} == {10}
    assert _metric(result, "integrated_skipped_by_filter_count") == 1


def test_integrated_date_range_cuts_standard_file_events():
    strategy = SnapshotRecorder()

    result = BacktestRunner(config=_integrated_config()).run(
        strategy,
        data_path=FIXTURE_DIR / "range.ndjson",
        date_range=(200, 300),
        mode="integrated",
    )

    assert [snapshot.timestamp_ns for snapshot in strategy.snapshots] == [200]
    assert _metric(result, "integrated_accepted_event_count") == 1


def test_integrated_max_events_limits_replay():
    strategy = SnapshotRecorder()

    result = BacktestRunner(config={**_integrated_config(), "max_events": 1}).run(
        strategy,
        data_path=FIXTURE_DIR / "range.ndjson",
        mode="integrated",
    )

    assert [snapshot.timestamp_ns for snapshot in strategy.snapshots] == [100]
    assert _metric(result, "integrated_accepted_event_count") == 1
    assert _metric(result, "integrated_skipped_by_max_events_count") == 1


def test_integrated_mode_default_does_not_publish_every_l3_event():
    strategy = SnapshotRecorder()

    result = BacktestRunner(config={"instruments": [10]}).run(
        strategy,
        data_path=FIXTURE_DIR / "range.ndjson",
        mode="integrated",
    )

    assert strategy.snapshots == []
    assert result.order_log_df.empty
    assert _metric(result, "integrated_accepted_event_count") == 3


def test_integrated_feather_input_requires_arrow_enabled_when_disabled(tmp_path):
    cpp = require_cpp()
    if bool(getattr(cpp, "ARROW_ENABLED", False)):
        pytest.skip("Arrow-enabled build should read Feather instead")

    feather_path = tmp_path / "tiny.feather"
    feather_path.write_bytes(b"not a real feather file")

    with pytest.raises(RuntimeError, match="Feather integrated input requires"):
        BacktestRunner(config={**_integrated_config(), "input_format": "feather"}).run(
            Strategy(),
            data_path=feather_path,
            mode="integrated",
        )


def test_integrated_feather_tiny_fixture_matches_jsonl(tmp_path):
    feather_path = tmp_path / "tiny.feather"
    _write_feather(feather_path, _tiny_rows())

    json_strategy = BuyFirstAskStrategy()
    feather_strategy = BuyFirstAskStrategy()
    json_result = BacktestRunner(config=_integrated_config()).run(
        json_strategy,
        data_path=FIXTURE_DIR / "tiny_real.ndjson",
        mode="integrated",
    )
    feather_result = BacktestRunner(
        config={**_integrated_config(), "input_format": "feather"}
    ).run(
        feather_strategy,
        data_path=feather_path,
        mode="integrated",
    )

    assert json_result.order_log_df.to_dict("records") == feather_result.order_log_df.to_dict(
        "records"
    )
    assert json_result.fills_df.to_dict("records") == feather_result.fills_df.to_dict(
        "records"
    )
    assert _metric(json_result, "integrated_lob_digest") == _metric(
        feather_result, "integrated_lob_digest"
    )


def test_integrated_jsonl_vs_feather_deterministic_results_match(tmp_path):
    feather_dir = tmp_path / "deterministic_feather"
    for index, rows in enumerate(_deterministic_rows()):
        _write_feather(feather_dir / f"file_{index}.feather", rows)

    config = {
        **_integrated_config(),
        "instruments": [10, 11],
    }
    json_result = BacktestRunner(config={**config, "input_mode": "flat"}).run(
        BuyFirstAskStrategy(),
        data_path=FIXTURE_DIR / "deterministic",
        mode="integrated",
    )
    feather_result = BacktestRunner(
        config={**config, "input_mode": "flat", "input_format": "feather"}
    ).run(
        BuyFirstAskStrategy(),
        data_path=feather_dir,
        mode="integrated",
    )

    assert _metric(json_result, "integrated_lob_digest") == _metric(
        feather_result, "integrated_lob_digest"
    )
    assert json_result.order_log_df.to_dict("records") == feather_result.order_log_df.to_dict(
        "records"
    )
    assert json_result.fills_df.to_dict("records") == feather_result.fills_df.to_dict(
        "records"
    )


def test_top_level_run_accepts_integrated_mode():
    result = bt.run(
        Strategy(),
        events=[],
        mode="integrated",
        config={"instruments": []},
    )

    assert isinstance(result, BacktestResult)


def test_integrated_mode_wraps_strategy_callback_exception():
    class ExplodingStrategy(Strategy):
        def on_book_snapshot(self, snapshot, ctx):
            raise ValueError("snapshot boom")

    with pytest.raises(
        IntegratedBacktestError,
        match="on_book_snapshot.*snapshot boom",
    ):
        BacktestRunner(config=_integrated_config()).run(
            ExplodingStrategy(),
            events=[_add_ask_event()],
            mode="integrated",
        )
