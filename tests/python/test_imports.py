import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_import_backtester_works():
    import backtester

    assert backtester is not None


def test_strategy_import():
    from backtester import Strategy

    assert Strategy.__name__ == "Strategy"


def test_strategy_context_import():
    from backtester import StrategyContext

    assert StrategyContext.__name__ == "StrategyContext"


def test_backtest_runner_import():
    from backtester import BacktestRunner

    assert BacktestRunner.__name__ == "BacktestRunner"


def test_backtest_result_import():
    from backtester import BacktestResult

    assert BacktestResult.__name__ == "BacktestResult"


def test_progress_metrics_import():
    from backtester import ProgressMetrics

    assert ProgressMetrics.__name__ == "ProgressMetrics"


def test_import_does_not_require_compiled_cpp_bindings():
    env = os.environ.copy()
    env["PYTHONPATH"] = str(REPO_ROOT / "python")

    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import sys; import backtester; "
            "assert '_backtester_cpp' not in sys.modules; "
            "assert 'backtester._backtester_cpp' not in sys.modules",
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr
