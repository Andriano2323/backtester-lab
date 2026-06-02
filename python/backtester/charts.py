"""Matplotlib chart helpers for BacktestResult objects."""

from __future__ import annotations

from typing import Any

from .result import BacktestResult


def plot_cumulative_pnl(result: BacktestResult):
    """Plot cumulative PnL and return the matplotlib Figure."""

    import matplotlib.pyplot as plt

    fig, ax = plt.subplots()
    pnl = result.pnl_series.sort_index()
    if not pnl.empty:
        ax.plot(pnl.index, pnl.values, label="PnL")
        ax.legend()
    ax.set_title("Cumulative PnL")
    ax.set_xlabel("timestamp_ns")
    ax.set_ylabel("PnL")
    return fig


def plot_fills_on_price(result: BacktestResult, price_series: Any):
    """Plot a price series with fill prices overlaid and return the Figure."""

    import pandas as pd
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots()
    prices = (
        price_series if isinstance(price_series, pd.Series) else pd.Series(price_series)
    )
    if not prices.empty:
        ax.plot(prices.index, prices.values, label="price")

    fills = result.fills_df
    if not fills.empty:
        buys = fills[fills["side"].isin(["B", "Bid", "BID"])]
        sells = fills[fills["side"].isin(["A", "Ask", "ASK"])]
        if not buys.empty:
            ax.scatter(
                buys["timestamp_ns"], buys["fill_price"], marker="^", label="buy fills"
            )
        if not sells.empty:
            ax.scatter(
                sells["timestamp_ns"],
                sells["fill_price"],
                marker="v",
                label="sell fills",
            )

    ax.set_title("Fills On Price")
    ax.set_xlabel("timestamp_ns")
    ax.set_ylabel("price")
    if ax.get_legend_handles_labels()[0]:
        ax.legend()
    return fig


__all__ = ["plot_cumulative_pnl", "plot_fills_on_price"]
