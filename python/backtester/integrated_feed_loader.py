"""Databento-style JSONL loader for integrated Python backtests."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ._cpp import require_cpp

DateRange = tuple[int | str | None, int | str | None] | None


def load_integrated_events(
    data_path: str | Path,
    *,
    input_mode: str | None = None,
    input_format: str | None = None,
    date_range: DateRange = None,
) -> list[Any]:
    """Load C++ MarketDataEvent objects using the project JSON parser."""

    path = Path(data_path)
    if not path.exists():
        raise FileNotFoundError(f"Market-data path does not exist: {path}")

    mode = _input_mode(path, input_mode)
    format_name = _input_format(path, input_format)
    start_ns, end_ns = _parse_date_range(date_range)
    if path.is_file():
        if mode not in {"standard", "flat", "hierarchy"}:
            raise ValueError("input_mode must be 'standard', 'flat', or 'hierarchy'")
        events = _load_file(path, source_file_id=0, input_format=format_name)
        return _filter_date_range(events, start_ns, end_ns)

    if not path.is_dir():
        raise FileNotFoundError(
            f"Market-data path is neither a file nor a directory: {path}"
        )
    if mode == "standard":
        raise ValueError("input_mode='standard' requires a single JSONL/NDJSON file")
    if mode not in {"flat", "hierarchy"}:
        raise ValueError("input_mode must be 'standard', 'flat', or 'hierarchy'")

    events: list[Any] = []
    files = _data_files(path, format_name)
    if not files:
        raise FileNotFoundError(
            f"input folder contains no supported {format_name} market-data files: {path}"
        )
    for source_file_id, file_path in enumerate(files):
        events.extend(
            _load_file(
                file_path,
                source_file_id=source_file_id,
                input_format=format_name,
            )
        )

    filtered = _filter_date_range(events, start_ns, end_ns)
    return sorted(
        filtered,
        key=lambda event: (
            int(event.timestamp),
            int(event.source_file_id),
            int(event.source_sequence),
        ),
    )


def _input_mode(path: Path, input_mode: str | None) -> str:
    if input_mode is not None:
        return str(input_mode).lower()
    return "standard" if path.is_file() else "flat"


def _input_format(path: Path, input_format: str | None) -> str:
    if input_format is not None:
        format_name = str(input_format).lower()
        if format_name not in {"json", "feather"}:
            raise ValueError("input_format must be 'json' or 'feather'")
        return format_name
    if path.is_file():
        return "feather" if _is_feather_file(path) else "json"

    files = list(path.rglob("*"))
    feather_files = [file_path for file_path in files if _is_feather_file(file_path)]
    json_files = [file_path for file_path in files if _is_json_file(file_path)]
    if feather_files and not json_files:
        return "feather"
    return "json"


def _data_files(path: Path, input_format: str) -> list[Path]:
    return sorted(
        file_path
        for file_path in path.rglob("*")
        if file_path.is_file()
        and (
            (input_format == "json" and _is_json_file(file_path))
            or (input_format == "feather" and _is_feather_file(file_path))
        )
    )


def _is_json_file(path: Path) -> bool:
    filename = path.name.lower()
    if not filename or filename.startswith("."):
        return False
    if filename in {
        "condition",
        "condition.json",
        "manifest",
        "manifest.json",
        "metadata",
        "metadata.json",
    }:
        return False
    return filename.endswith((".mbo.json", ".mbo", ".ndjson", ".jsonl", ".json"))


def _is_feather_file(path: Path) -> bool:
    filename = path.name.lower()
    return (
        bool(filename)
        and not filename.startswith(".")
        and filename.endswith((".feather", ".ftr"))
    )


def _load_file(path: Path, source_file_id: int, input_format: str) -> list[Any]:
    if input_format == "feather":
        return _load_feather_file(path, source_file_id)
    return _load_jsonl_file(path, source_file_id)


def _load_jsonl_file(path: Path, source_file_id: int) -> list[Any]:
    cpp = require_cpp()
    events: list[Any] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                events.append(
                    cpp.parse_market_data_event_line(
                        stripped,
                        line_number=line_number,
                        source_file_id=source_file_id,
                        source_sequence=line_number,
                    )
                )
            except Exception as exc:
                raise ValueError(
                    f"Invalid Databento JSONL record in {path}:{line_number}: {exc}"
                ) from exc
    return events


def _load_feather_file(path: Path, source_file_id: int) -> list[Any]:
    cpp = require_cpp()
    if not bool(getattr(cpp, "ARROW_ENABLED", False)):
        raise RuntimeError(
            "Feather integrated input requires an Arrow-enabled C++ extension. "
            "Rebuild with -DENABLE_ARROW=ON -DBACKTESTER_BUILD_PYTHON=ON."
        )
    if not hasattr(cpp, "read_feather_market_data_events"):
        raise RuntimeError(
            "Feather integrated input is unavailable in this C++ extension build."
        )
    try:
        return list(
            cpp.read_feather_market_data_events(
                str(path),
                source_file_id=source_file_id,
            )
        )
    except Exception as exc:
        raise ValueError(f"Invalid Feather market-data file {path}: {exc}") from exc


def _filter_date_range(
    events: list[Any], start_ns: int | None, end_ns: int | None
) -> list[Any]:
    return [
        event
        for event in events
        if (start_ns is None or int(event.timestamp) >= start_ns)
        and (end_ns is None or int(event.timestamp) < end_ns)
    ]


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
            return int(require_cpp().parse_timestamp_text(stripped))
        except Exception as exc:
            raise ValueError(
                f"date_range {name} must be an integer or ISO nanosecond timestamp"
            ) from exc
    raise ValueError(f"date_range {name} must be an integer nanosecond timestamp")


__all__ = ["load_integrated_events"]
