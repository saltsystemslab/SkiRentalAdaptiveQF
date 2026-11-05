#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
run_recipe() {
    local family="$1" scale="${2:-full}"
    case "$scale" in
        full) scale=large ;;
        smoke) scale=small ;;
        small|medium|large) ;;
        *) echo "Usage: $0 [small|medium|large|full|smoke]" >&2; return 2 ;;
    esac
    local python="${PYTHON:-${REPO_ROOT}/.venv/bin/python}"
    [[ -x "$python" ]] || python=python3
    local output="${RESULTS_ROOT:-${REPO_ROOT}/sponge/tods}/${RUN_NAME:-$(date -u +%Y%m%dT%H%M%S)-${family}-${scale}}"
    exec "$python" "${REPO_ROOT}/experiments/run.py" run --tests "$family" \
        --size "$scale" --seed "${SEED:-741262640}" --output "$output"
}
