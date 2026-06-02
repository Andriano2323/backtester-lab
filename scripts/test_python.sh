#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

export PYTHONPATH="$repo_root/python${PYTHONPATH:+:$PYTHONPATH}"

if [[ "$#" -eq 0 ]]; then
    set -- -q tests/python
fi

python_bin="${PYTHON:-}"
if [[ -z "$python_bin" ]]; then
    if command -v python >/dev/null 2>&1; then
        python_bin="python"
    elif [[ -x "$repo_root/.venv/bin/python" ]]; then
        python_bin="$repo_root/.venv/bin/python"
    else
        python_bin="python3"
    fi
fi

"$python_bin" -m pytest "$@"
