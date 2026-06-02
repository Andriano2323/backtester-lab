import pytest

from backtester import BacktestResult, RiskLimitExceeded, StrategyContext
from backtester.portfolio import Portfolio
from backtester.risk import RiskLimits, RiskRejectReason
from backtester.types import Side


class FakeGateway:
    def __init__(self):
        self.sent_orders = []
        self.next_order_id = 100

    def send_order(self, instrument_id, side, price, size, timestamp_ns):
        self.sent_orders.append(
            {
                "instrument_id": instrument_id,
                "side": side,
                "price": price,
                "size": size,
                "timestamp_ns": timestamp_ns,
            }
        )
        self.next_order_id += 1
        return self.next_order_id


def _context(risk_limits, portfolio=None, result=None):
    return StrategyContext(
        gateway=FakeGateway(),
        portfolio=portfolio or Portfolio(),
        risk_limits=risk_limits,
        result=result,
        metadata={"trading_engine_id": 7},
    )


def test_max_order_size_rejects_oversized_order():
    ctx = _context(RiskLimits(max_order_size=5))

    with pytest.raises(RiskLimitExceeded) as exc:
        ctx.send_order(10, Side.BID, 100, 6, 1)

    assert exc.value.reason is RiskRejectReason.MAX_ORDER_SIZE
    assert ctx.gateway.sent_orders == []


def test_max_position_per_instrument_rejects_order_exceeding_limit():
    portfolio = Portfolio()
    portfolio.apply_fill(10, Side.BID, 100, 3)
    ctx = _context(RiskLimits(max_position_per_instrument=5), portfolio=portfolio)

    with pytest.raises(RiskLimitExceeded) as exc:
        ctx.send_order(10, Side.BID, 100, 3, 1)

    assert exc.value.reason is RiskRejectReason.MAX_POSITION_PER_INSTRUMENT
    assert ctx.gateway.sent_orders == []


def test_allow_short_false_rejects_sell_that_would_create_short_position():
    ctx = _context(RiskLimits(allow_short=False))

    with pytest.raises(RiskLimitExceeded) as exc:
        ctx.send_order(10, Side.ASK, 100, 1, 1)

    assert exc.value.reason is RiskRejectReason.SHORT_NOT_ALLOWED
    assert ctx.gateway.sent_orders == []


def test_accepted_order_passes_to_gateway():
    ctx = _context(RiskLimits(max_order_size=5, max_position_per_instrument=10, allow_short=False))

    order_id = ctx.send_order(10, Side.BID, 100, 5, 1)

    assert order_id == 101
    assert ctx.gateway.sent_orders == [
        {
            "instrument_id": 10,
            "side": Side.BID,
            "price": 100,
            "size": 5,
            "timestamp_ns": 1,
        }
    ]


def test_rejected_risk_order_appears_in_order_log_df():
    result = BacktestResult()
    ctx = _context(RiskLimits(max_order_size=5), result=result)

    with pytest.raises(RiskLimitExceeded):
        ctx.send_order(10, Side.BID, 100, 6, 1)

    order_log = result.order_log_df
    assert len(order_log) == 1
    assert order_log.loc[0, "trading_engine_id"] == 7
    assert order_log.loc[0, "order_id"] == 0
    assert order_log.loc[0, "instrument_id"] == 10
    assert order_log.loc[0, "status"] == "Rejected"
    assert order_log.loc[0, "event_type"] == "risk_reject"
    assert order_log.loc[0, "reason"] == "MaxOrderSize"


def test_strategy_context_send_order_raises_documented_exception_on_risk_reject():
    ctx = _context(RiskLimits(max_order_size=1))

    with pytest.raises(RiskLimitExceeded, match="order size exceeds max_order_size") as exc:
        ctx.send_order(10, Side.BID, 100, 2, 1)

    assert exc.value.reason is RiskRejectReason.MAX_ORDER_SIZE
    assert exc.value.text == "order size exceeds max_order_size"
