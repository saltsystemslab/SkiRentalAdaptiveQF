#!/usr/bin/env bash

set -euo pipefail

EXPERIMENTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${EXPERIMENTS_DIR}/.." && pwd)"

RESULTS_ROOT="${RESULTS_ROOT:-${REPO_ROOT}/sponge/tods}"
SEED="${SEED:-741262640}"
FILTER_NAMES="${FILTER_NAMES:-adaptive,nonAdaptive,skiQF,randomizedSkiQF,hybridSkiQF}"

configure_scale() {
    local scale="${1:-full}"
    case "${scale}" in
        full)
            QUOTIENT_BITS=27
            NUM_QUERIES=100000000
            MICROBENCH_QUERIES=500000000
            NUM_ROUNDS=100
            INSERT_QUOTIENT_BITS=24
            INSERT_NUM_QUERIES=1000000
            CAPTURE_EXTRA_STATS=True
            ;;
        smoke)
            QUOTIENT_BITS=14
            NUM_QUERIES=20000
            MICROBENCH_QUERIES=50000
            NUM_ROUNDS=10
            INSERT_QUOTIENT_BITS=14
            INSERT_NUM_QUERIES=20000
            CAPTURE_EXTRA_STATS=False
            ;;
        *)
            echo "Usage: $0 [full|smoke]" >&2
            return 2
            ;;
    esac
    SCALE="${scale}"
}

run_experiment() {
    local output_name="$1"
    shift

    mkdir -p "$(dirname "${RESULTS_ROOT}/${output_name}")"
    (
        cd "${REPO_ROOT}"
        python3 ./bench/bench_filters.py \
            -F "${RESULTS_ROOT}/${output_name}" \
            with \
            seed="${SEED}" \
            "$@"
    )
}

behavioral_args() {
    BEHAVIORAL_ARGS=(
        num_queries="${NUM_QUERIES}"
        num_rounds="${NUM_ROUNDS}"
        quotient_bits="${QUOTIENT_BITS}"
        storage_engine=wiredTiger
        reverse_map_engine=wiredTiger
        storage_cache_size_mb=64
        reverse_map_cache_size_mb=4
        collect_db_stats=True
        capture_extra_stats="${CAPTURE_EXTRA_STATS}"
        filter_names="${FILTER_NAMES}"
    )
}
