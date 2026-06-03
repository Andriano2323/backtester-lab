import os
import importlib.util
import subprocess
import sys
import tracemalloc
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BENCHMARK = REPO_ROOT / "benchmarks" / "python_strategy_callback_benchmark.py"
INTEGRATED_BENCHMARK = REPO_ROOT / "benchmarks" / "integrated_benchmark.py"


def _load_integrated_benchmark_module():
    spec = importlib.util.spec_from_file_location(
        "integrated_benchmark", INTEGRATED_BENCHMARK
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_python_callback_benchmark_smoke_runs_with_expected_csv_output():
    env = os.environ.copy()
    env["PYTHONPATH"] = f"{REPO_ROOT / 'python'}:{REPO_ROOT / 'build' / 'python'}"

    completed = subprocess.run(
        [sys.executable, str(BENCHMARK), "--events", "1000"],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr
    lines = completed.stdout.strip().splitlines()
    assert lines[0] == "events,wall_clock_seconds,callbacks_per_second"

    events, wall_clock_seconds, callbacks_per_second = lines[1].split(",")
    assert int(events) == 1000
    assert float(wall_clock_seconds) > 0.0
    assert float(callbacks_per_second) > 0.0


def test_integrated_benchmark_smoke_runs_with_expected_csv_output():
    env = os.environ.copy()
    env["PYTHONPATH"] = f"{REPO_ROOT / 'python'}:{REPO_ROOT / 'build' / 'python'}"

    completed = subprocess.run(
        [
            sys.executable,
            str(INTEGRATED_BENCHMARK),
            "--events",
            "100",
            "--snapshot-interval",
            "10",
            "--callback-interval",
            "1",
            "--orders-every",
            "2",
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr
    lines = completed.stdout.strip().splitlines()
    assert lines[0].startswith("mode,event_count,wall_clock_seconds")
    assert len(lines) == 5
    rows = [_csv_row(line) for line in lines[1:]]
    assert {row["mode"] for row in rows} == {
        "ingestion_only",
        "lob_only",
        "integrated_no_strategy",
        "integrated_strategy",
    }
    ingestion = next(row for row in rows if row["mode"] == "ingestion_only")
    assert int(ingestion["event_count"]) == 100
    assert float(ingestion["events_per_second"]) > 0.0


def test_integrated_no_strategy_benchmark_has_no_python_callbacks():
    module = _load_integrated_benchmark_module()
    events = module.make_events(200)

    row = module.run_integrated_no_strategy(events, snapshot_interval_events=1)

    assert row.event_count == 200
    assert row.events_per_second > 0.0
    assert row.callbacks == 0
    assert row.market_callbacks == 0
    assert row.orders == 0
    assert row.fills == 0


def test_integrated_snapshot_interval_throttles_python_callbacks():
    module = _load_integrated_benchmark_module()
    events = module.make_events(120)

    every_event = module.run_integrated_strategy(
        events,
        snapshot_interval_events=1,
        market_callback_interval_events=1,
        orders_every=0,
        explain=False,
    )
    every_tenth = module.run_integrated_strategy(
        events,
        snapshot_interval_events=10,
        market_callback_interval_events=1,
        orders_every=0,
        explain=False,
    )

    assert every_event.market_callbacks == 120
    assert every_tenth.market_callbacks == 12
    assert every_tenth.callbacks < every_event.callbacks


def test_integrated_market_callback_interval_throttles_callbacks():
    module = _load_integrated_benchmark_module()
    events = module.make_events(120)

    every_snapshot = module.run_integrated_strategy(
        events,
        snapshot_interval_events=1,
        market_callback_interval_events=1,
        orders_every=0,
        explain=False,
    )
    every_tenth_callback = module.run_integrated_strategy(
        events,
        snapshot_interval_events=1,
        market_callback_interval_events=10,
        orders_every=0,
        explain=False,
    )

    assert every_snapshot.market_callbacks == 120
    assert every_tenth_callback.market_callbacks == 12


def test_integrated_no_strategy_memory_usage_is_bounded_on_replay():
    module = _load_integrated_benchmark_module()
    events = module.make_events(2_000)

    tracemalloc.start()
    try:
        row = module.run_integrated_no_strategy(events, snapshot_interval_events=0)
        _, peak = tracemalloc.get_traced_memory()
    finally:
        tracemalloc.stop()

    assert row.event_count == 2_000
    assert row.callbacks == 0
    assert peak < 20_000_000


def test_integrated_trace_disabled_does_not_accumulate_trace_rows_on_long_run():
    module = _load_integrated_benchmark_module()
    events = module.make_events(1_000)

    row = module.run_integrated_strategy(
        events,
        snapshot_interval_events=1,
        market_callback_interval_events=1,
        orders_every=0,
        explain=False,
    )

    assert row.market_callbacks == 1_000
    assert row.trace_rows == 0


def _csv_row(line):
    header = (
        "mode,event_count,wall_clock_seconds,events_per_second,callbacks,"
        "callbacks_per_second,market_callbacks,orders,orders_per_second,"
        "fills,fills_per_second,trace_rows"
    ).split(",")
    return dict(zip(header, line.split(",")))
