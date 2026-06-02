"""Simple JSONL market-data feed loader for Python Strategy API tests."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .types import BookSnapshot, BookUpdate, PriceLevel, Side, Trade

MarketDataEvent = BookUpdate | BookSnapshot | Trade
DateRange = tuple[int | str | None, int | str | None] | None


def load_events(data_path: str | Path, date_range: DateRange = None) -> list[MarketDataEvent]:
    """Load synthetic market-data events from a JSONL file or directory."""

    path = Path(data_path)
    if not path.exists():
        raise FileNotFoundError(f"Market-data path does not exist: {path}")

    start_ns, end_ns = _parse_date_range(date_range)
    events: list[MarketDataEvent] = []

    for file_path in _jsonl_files(path):
        events.extend(_load_jsonl_file(file_path))

    filtered = [
        event
        for event in events
        if (start_ns is None or event.timestamp_ns >= start_ns) and (end_ns is None or event.timestamp_ns < end_ns)
    ]
    return sorted(filtered, key=lambda event: event.timestamp_ns)


def _jsonl_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    if path.is_dir():
        return sorted(file_path for file_path in path.iterdir() if file_path.is_file() and file_path.suffix == ".jsonl")
    raise FileNotFoundError(f"Market-data path is neither a file nor a directory: {path}")


def _load_jsonl_file(path: Path) -> list[MarketDataEvent]:
    events: list[MarketDataEvent] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                record = json.loads(stripped)
                events.append(_event_from_record(record))
            except json.JSONDecodeError as exc:
                raise ValueError(f"Invalid JSON in {path}:{line_number}: {exc.msg}") from exc
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"Invalid market-data record in {path}:{line_number}: {exc}") from exc
    return events


def _event_from_record(record: dict[str, Any]) -> MarketDataEvent:
    record_type = _required(record, "type")
    if record_type == "book_update":
        return BookUpdate(
            instrument_id=int(_required(record, "instrument_id")),
            timestamp_ns=int(_required(record, "timestamp_ns")),
            seq_no=int(_required(record, "seq_no")),
            side=_side(_required(record, "side")),
            price=int(_required(record, "price")),
            size=int(_required(record, "size")),
        )
    if record_type == "book_snapshot":
        return BookSnapshot(
            instrument_id=int(_required(record, "instrument_id")),
            timestamp_ns=int(_required(record, "timestamp_ns")),
            seq_no=int(_required(record, "seq_no")),
            bids=[_price_level(level) for level in record.get("bids", [])],
            asks=[_price_level(level) for level in record.get("asks", [])],
        )
    if record_type == "trade":
        return Trade(
            instrument_id=int(_required(record, "instrument_id")),
            timestamp_ns=int(_required(record, "timestamp_ns")),
            seq_no=int(_required(record, "seq_no")),
            price=int(_required(record, "price")),
            size=int(_required(record, "size")),
            aggressor_side=_side(_required(record, "aggressor_side")),
        )
    raise ValueError(f"Unknown market-data record type: {record_type!r}")


def _price_level(record: dict[str, Any]) -> PriceLevel:
    return PriceLevel(
        level_index=int(_required(record, "level_index")),
        price=int(_required(record, "price")),
        size=int(_required(record, "size")),
    )


def _side(value: Any) -> Side:
    if isinstance(value, Side):
        return value
    text = str(value).strip()
    upper = text.upper()
    if upper in {"A", "ASK"}:
        return Side.ASK
    if upper in {"B", "BID"}:
        return Side.BID
    if upper in {"N", "NONE"}:
        return Side.NONE
    return Side(text)


def _required(record: dict[str, Any], name: str) -> Any:
    if name not in record:
        raise KeyError(f"missing required field {name!r}")
    return record[name]


def _parse_date_range(date_range: DateRange) -> tuple[int | None, int | None]:
    if date_range is None:
        return None, None
    if not isinstance(date_range, tuple) or len(date_range) != 2:
        raise ValueError("date_range must be a (start_ns, end_ns) tuple")
    return _parse_bound(date_range[0], "start"), _parse_bound(date_range[1], "end")


def _parse_bound(value: int | str | None, name: str) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        stripped = value.strip()
        if not stripped:
            return None
        try:
            return int(stripped)
        except ValueError as exc:
            raise ValueError(f"date_range {name} must be an integer nanosecond timestamp") from exc
    raise ValueError(f"date_range {name} must be an integer nanosecond timestamp")


__all__ = ["load_events"]
