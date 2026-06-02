from backtester.portfolio import Portfolio
from backtester.types import BookUpdate, Side, Trade


PRICE_SCALE = 1_000_000_000


def _book_update(instrument_id=10, price=101 * PRICE_SCALE):
    return BookUpdate(
        instrument_id=instrument_id,
        timestamp_ns=1,
        seq_no=1,
        side=Side.BID,
        price=price,
        size=5,
    )


def _trade(instrument_id=10, price=102 * PRICE_SCALE):
    return Trade(
        instrument_id=instrument_id,
        timestamp_ns=2,
        seq_no=2,
        price=price,
        size=3,
        aggressor_side=Side.ASK,
    )


def test_empty_portfolio_position_is_zero():
    portfolio = Portfolio()

    assert portfolio.position(10) == 0
    assert portfolio.current_position(10) == 0


def test_buy_fill_increases_position():
    portfolio = Portfolio()

    portfolio.apply_fill(10, Side.BID, 100 * PRICE_SCALE, 5)

    assert portfolio.position(10) == 5


def test_sell_fill_decreases_position():
    portfolio = Portfolio()
    portfolio.apply_fill(10, Side.BID, 100 * PRICE_SCALE, 5)

    portfolio.apply_fill(10, Side.ASK, 101 * PRICE_SCALE, 2)

    assert portfolio.position(10) == 3


def test_last_price_is_updated_on_book_update_and_trade():
    portfolio = Portfolio()

    portfolio.update_market_data(_book_update(price=101 * PRICE_SCALE))
    assert portfolio.last_price[10] == 101 * PRICE_SCALE

    portfolio.update_market_data(_trade(price=102 * PRICE_SCALE))
    assert portfolio.last_price[10] == 102 * PRICE_SCALE


def test_mark_to_market_pnl_is_computed_from_scaled_integer_prices():
    portfolio = Portfolio()
    portfolio.apply_fill(10, Side.BID, 100 * PRICE_SCALE, 5)
    portfolio.update_market_data(_trade(price=103 * PRICE_SCALE))

    assert portfolio.mark_to_market_pnl() == 15 * PRICE_SCALE
    assert portfolio.current_pnl() == 15 * PRICE_SCALE


def test_realized_pnl_updates_on_closing_trade():
    portfolio = Portfolio()
    portfolio.apply_fill(10, Side.BID, 100 * PRICE_SCALE, 5)

    portfolio.apply_fill(10, Side.ASK, 110 * PRICE_SCALE, 2)

    assert portfolio.realized_pnl == 20 * PRICE_SCALE
    assert portfolio.position(10) == 3


def test_portfolio_supports_multiple_instruments_independently():
    portfolio = Portfolio()

    portfolio.apply_fill(10, Side.BID, 100 * PRICE_SCALE, 5)
    portfolio.apply_fill(20, Side.ASK, 200 * PRICE_SCALE, 7)
    portfolio.update_market_data(_trade(instrument_id=10, price=101 * PRICE_SCALE))
    portfolio.update_market_data(_trade(instrument_id=20, price=198 * PRICE_SCALE))

    assert portfolio.position(10) == 5
    assert portfolio.position(20) == -7
    assert portfolio.unrealized_pnl(10) == 5 * PRICE_SCALE
    assert portfolio.unrealized_pnl(20) == 14 * PRICE_SCALE
