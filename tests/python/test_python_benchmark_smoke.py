import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BENCHMARK = REPO_ROOT / "benchmarks" / "python_strategy_callback_benchmark.py"


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
