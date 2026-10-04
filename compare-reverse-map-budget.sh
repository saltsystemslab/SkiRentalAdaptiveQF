#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RESULTS_ROOT="./sponge/compare-reverse-map-budget"

COMMON_CONFIG=(
    num_queries=100000000
    num_rounds=100
    storage_engine=wiredTiger
    reverse_map_engine=wiredTiger
    storage_cache_size_mb=64
    break_even=2
    collect_db_stats=True
    quotient_bits=27
    capture_extra_stats=True
)

run_distribution() {
    local reverse_map_cache_size_mb="$1"
    local workload_name="$2"
    local query_workload="$3"
    shift 3

    python3 ./bench/bench_filters.py \
        -F "${RESULTS_ROOT}/reverse-map-${reverse_map_cache_size_mb}mb/${workload_name}" \
        with \
        "${COMMON_CONFIG[@]}" \
        reverse_map_cache_size_mb="${reverse_map_cache_size_mb}" \
        query_workload="${query_workload}" \
        "$@"
}

for reverse_map_cache_size_mb in 4 64; do
    run_distribution "${reverse_map_cache_size_mb}" uniform uniform
    run_distribution "${reverse_map_cache_size_mb}" normal normal
    # run_distribution "${reverse_map_cache_size_mb}" lognormal lognormal
    # run_distribution \
    #    "${reverse_map_cache_size_mb}" \
    #    zipf \
    #    zipfian \
    #    zipf_constant=0.99
done
