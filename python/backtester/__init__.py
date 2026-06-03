"""Pure Python Strategy API package for the backtester project."""

from .context import StrategyContext
from .gateway import CppOrderGatewayFacade
from .integrated_runner import IntegratedBacktestError, IntegratedBacktestRunner
from .portfolio import Portfolio
from .progress import ProgressMetrics
from .result import BacktestResult
from .risk import RiskLimitExceeded, RiskLimits, RiskRejectReason
from .runner import BacktestRunner, run, run_many
from .strategy import Strategy
from .types import Action, MarketDataEvent

__all__ = [
    "Action",
    "BacktestResult",
    "BacktestRunner",
    "CppOrderGatewayFacade",
    "IntegratedBacktestError",
    "IntegratedBacktestRunner",
    "MarketDataEvent",
    "Portfolio",
    "ProgressMetrics",
    "RiskLimitExceeded",
    "RiskLimits",
    "RiskRejectReason",
    "Strategy",
    "StrategyContext",
    "run",
    "run_many",
]
