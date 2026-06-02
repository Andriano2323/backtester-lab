import matplotlib

matplotlib.use("Agg", force=True)

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.figure import Figure

from backtester.charts import plot_cumulative_pnl, plot_fills_on_price
from backtester.result import BacktestResult


def _result_with_pnl_and_fill() -> BacktestResult:
    result = BacktestResult()
    result.add_pnl_point(timestamp_ns=1, pnl=0)
    result.add_pnl_point(timestamp_ns=2, pnl=10)
    result.add_fill(
        timestamp_ns=2,
        trading_engine_id=1,
        order_id=100,
        instrument_id=10,
        side="B",
        fill_price=101_250_000_000,
        fill_size=1,
        remaining_size=0,
    )
    return result


def test_plot_cumulative_pnl_returns_matplotlib_figure():
    fig = plot_cumulative_pnl(_result_with_pnl_and_fill())

    assert isinstance(fig, Figure)
    plt.close(fig)


def test_plot_cumulative_pnl_works_with_empty_result():
    fig = plot_cumulative_pnl(BacktestResult())

    assert isinstance(fig, Figure)
    plt.close(fig)


def test_plot_fills_on_price_returns_matplotlib_figure():
    price_series = pd.Series([101_000_000_000, 101_500_000_000], index=[1, 2])

    fig = plot_fills_on_price(_result_with_pnl_and_fill(), price_series)

    assert isinstance(fig, Figure)
    plt.close(fig)


def test_plot_fills_on_price_works_with_no_fills():
    price_series = pd.Series([101_000_000_000, 101_500_000_000], index=[1, 2])

    fig = plot_fills_on_price(BacktestResult(), price_series)

    assert isinstance(fig, Figure)
    plt.close(fig)


def test_chart_functions_do_not_call_show(monkeypatch):
    called = False

    def fake_show(*args, **kwargs):
        nonlocal called
        called = True

    monkeypatch.setattr(plt, "show", fake_show)

    fig1 = plot_cumulative_pnl(_result_with_pnl_and_fill())
    fig2 = plot_fills_on_price(_result_with_pnl_and_fill(), pd.Series([1, 2], index=[1, 2]))

    assert called is False
    plt.close(fig1)
    plt.close(fig2)
