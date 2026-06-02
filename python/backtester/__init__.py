"""Pure Python Strategy API package for the backtester project."""

from .context import StrategyContext
from .gateway import CppOrderGatewayFacade
from .portfolio import Portfolio
from .progress import ProgressMetrics
from .result import BacktestResult
from .risk import RiskLimitExceeded, RiskLimits, RiskRejectReason
from .runner import BacktestRunner, run
from .strategy import Strategy

__all__ = [
    "BacktestResult",
    "BacktestRunner",
    "CppOrderGatewayFacade",
    "Portfolio",
    "ProgressMetrics",
    "RiskLimitExceeded",
    "RiskLimits",
    "RiskRejectReason",
    "Strategy",
    "StrategyContext",
    "run",
]
