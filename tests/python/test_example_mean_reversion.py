import importlib.util
from pathlib import Path

from backtester import BacktestResult


EXAMPLE_PATH = (
    Path(__file__).resolve().parents[2]
    / "examples"
    / "python"
    / "mean_reversion_strategy.py"
)


def _load_example_module():
    spec = importlib.util.spec_from_file_location(
        "mean_reversion_strategy_example", EXAMPLE_PATH
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_example_module_imports():
    module = _load_example_module()

    assert module is not None


def test_mean_reversion_strategy_can_be_instantiated():
    module = _load_example_module()

    strategy = module.MeanReversionStrategy()

    assert strategy.window == 3
    assert strategy.order_size == 1


def test_example_run_completes_without_exception():
    module = _load_example_module()

    module.run_example()


def test_example_result_is_backtest_result():
    module = _load_example_module()

    result = module.run_example()

    assert isinstance(result, BacktestResult)


def test_order_log_df_is_not_empty_for_provided_fixture():
    module = _load_example_module()

    result = module.run_example()

    assert not result.order_log_df.empty


def test_pnl_series_exists():
    module = _load_example_module()

    result = module.run_example()

    assert result.pnl_series is not None


def test_example_is_deterministic_across_repeated_runs():
    module = _load_example_module()

    first = module.run_example()
    second = module.run_example()

    assert first.order_log_df.to_dict("records") == second.order_log_df.to_dict(
        "records"
    )
    assert first.fills_df.to_dict("records") == second.fills_df.to_dict("records")
    assert first.pnl_series.to_dict() == second.pnl_series.to_dict()
