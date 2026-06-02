"""Lazy access to the optional C++ extension module."""

from __future__ import annotations

from importlib import import_module
from types import ModuleType
from typing import Any

_extension: ModuleType | None = None
_import_error: BaseException | None = None

try:
    _extension = import_module("_backtester_cpp")
except BaseException as exc:  # pragma: no cover - exercised when extension is absent
    _import_error = exc


def is_available() -> bool:
    """Return whether the optional C++ extension module is importable."""

    return _extension is not None


def require_cpp() -> ModuleType:
    """Return the C++ extension or raise a clear runtime error."""

    if _extension is not None:
        return _extension

    raise RuntimeError(
        "The optional C++ extension module '_backtester_cpp' is not available. "
        "Build with -DBACKTESTER_BUILD_PYTHON=ON and include build/python on PYTHONPATH."
    ) from _import_error


def version() -> str:
    """Return the compiled extension version."""

    return str(require_cpp().version())


def __getattr__(name: str) -> Any:
    if name in {"PRICE_SCALE", "UNDEFINED_PRICE"}:
        return getattr(require_cpp(), name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = [
    "is_available",
    "require_cpp",
    "version",
]
