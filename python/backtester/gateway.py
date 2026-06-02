"""Python-facing gateway facades for strategies."""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from .adapters import from_cpp_order_ack, from_cpp_order_fill, from_cpp_order_reject, python_to_cpp_side
from .types import InstrumentId, OrderId, Price, Quantity, Side, TimestampNs


class CppOrderGatewayFacade:
    """Thin Strategy API wrapper around a C++ OrderGatewayClient binding."""

    def __init__(self, client: Any) -> None:
        self._client = client

    @property
    def client(self) -> Any:
        """Return the wrapped C++ client object."""

        return self._client

    def send_order(
        self,
        instrument_id: InstrumentId,
        side: Side,
        price: Price,
        size: Quantity,
        timestamp_ns: TimestampNs,
    ) -> OrderId:
        return int(
            self._client.send_order(
                instrument_id=instrument_id,
                side=self._to_cpp_side(side),
                price=price,
                size=size,
                timestamp_ns=timestamp_ns,
            )
        )

    def cancel_order(self, order_id: OrderId, instrument_id: InstrumentId, timestamp_ns: TimestampNs) -> None:
        self._client.cancel_order(
            order_id=order_id,
            instrument_id=instrument_id,
            timestamp_ns=timestamp_ns,
        )

    def modify_order(
        self,
        order_id: OrderId,
        instrument_id: InstrumentId,
        side: Side,
        price: Price,
        size: Quantity,
        timestamp_ns: TimestampNs,
    ) -> None:
        self._client.modify_order(
            order_id=order_id,
            instrument_id=instrument_id,
            side=self._to_cpp_side(side),
            price=price,
            size=size,
            timestamp_ns=timestamp_ns,
        )

    def drain_events(self) -> int:
        return int(self._client.drain_events())

    def on_ack(self, callback: Callable[[Any], None]) -> None:
        def dispatch(ack: Any) -> None:
            callback(from_cpp_order_ack(ack))

        self._client.on_ack(dispatch)

    def on_fill(self, callback: Callable[[Any], None]) -> None:
        def dispatch(fill: Any) -> None:
            callback(from_cpp_order_fill(fill))

        self._client.on_fill(dispatch)

    def on_reject(self, callback: Callable[[Any], None]) -> None:
        def dispatch(reject: Any) -> None:
            callback(from_cpp_order_reject(reject))

        self._client.on_reject(dispatch)

    @staticmethod
    def _to_cpp_side(side: Any) -> Any:
        if isinstance(side, Side):
            return python_to_cpp_side(side)
        return side


__all__ = ["CppOrderGatewayFacade"]
