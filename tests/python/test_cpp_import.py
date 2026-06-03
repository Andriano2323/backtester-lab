import os
import subprocess
import sys
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_import_backtester_works_without_compiled_module():
    env = os.environ.copy()
    env["PYTHONPATH"] = str(REPO_ROOT / "python")

    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import sys; import backtester; "
            "assert '_backtester_cpp' not in sys.modules; "
            "assert 'backtester._backtester_cpp' not in sys.modules",
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )

    assert completed.returncode == 0, completed.stderr


@pytest.fixture(scope="module")
def cpp_module():
    return pytest.importorskip("_backtester_cpp")


def test_import_cpp_extension_after_cmake_build(cpp_module):
    assert cpp_module is not None


def test_cpp_version_returns_non_empty_string(cpp_module):
    assert isinstance(cpp_module.version(), str)
    assert cpp_module.version()


def test_cpp_price_scale_matches_domain_contract(cpp_module):
    assert cpp_module.PRICE_SCALE == 1_000_000_000


def test_cpp_undefined_price_matches_domain_contract(cpp_module):
    assert cpp_module.UNDEFINED_PRICE == 9_223_372_036_854_775_807


def test_cpp_reports_arrow_feature_flag(cpp_module):
    assert isinstance(cpp_module.ARROW_ENABLED, bool)
