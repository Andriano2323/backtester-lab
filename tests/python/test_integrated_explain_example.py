import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from backtester import BacktestResult


REPO_ROOT = Path(__file__).resolve().parents[2]
EXAMPLE_PATH = REPO_ROOT / "examples" / "python" / "integrated_explain_strategy.py"
NOTEBOOK_PATH = REPO_ROOT / "notebooks" / "integrated_explainability_walkthrough.ipynb"
EXPECTED_TRACE_PATH = (
    REPO_ROOT / "tests" / "fixtures" / "integrated_l3" / "explain_trace_expected.json"
)


def _load_example_module():
    spec = importlib.util.spec_from_file_location(
        "integrated_explain_strategy_example", EXAMPLE_PATH
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_integrated_explain_example_script_smoke_runs():
    completed = subprocess.run(
        [sys.executable, str(EXAMPLE_PATH)],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )

    assert "trace_summary" in completed.stdout
    assert "crossed_best_ask" in completed.stdout


def test_integrated_explain_example_returns_backtest_result():
    module = _load_example_module()

    result = module.run_example(explain=True)

    assert isinstance(result, BacktestResult)
    assert len(result.fills_df) == 1
    assert len(result.order_log_df) == 3
    assert not result.trace_df.empty


def test_integrated_explain_false_keeps_results_but_disables_trace():
    module = _load_example_module()

    result = module.run_example(explain=False)

    assert len(result.fills_df) == 1
    assert result.trace_df.empty


def test_integrated_explain_trace_has_required_ux_stages():
    module = _load_example_module()

    result = module.run_example(explain=True)

    assert {
        "market",
        "strategy_order",
        "ack",
        "fill",
        "portfolio_update",
    }.issubset(set(result.trace_df["explain_stage"].tolist()))


def test_integrated_explain_golden_trace_matches_expected_fixture():
    module = _load_example_module()
    expected = json.loads(EXPECTED_TRACE_PATH.read_text())

    result = module.run_example(explain=True)

    assert module.trace_summary(result) == expected


def test_integrated_explain_notebook_smoke_loads_and_references_script():
    notebook = json.loads(NOTEBOOK_PATH.read_text())

    source = "\n".join(
        line
        for cell in notebook["cells"]
        for line in cell.get("source", [])
    )
    assert "integrated_explain_strategy" in source
    assert "run_example(explain=True)" in source
    assert "trace_summary(result)" in source
