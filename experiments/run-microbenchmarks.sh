#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"

for workload in uniform normal lognormal zipfian; do
    zipf_args=()
    output_workload="${workload}"
    if [[ "${workload}" == zipfian ]]; then
        zipf_args=(zipf_constant=0.99)
        output_workload=zipf
    fi

    run_experiment "microbenchmarks/${output_workload}-${SCALE}" \
        num_queries="${MICROBENCH_QUERIES}" \
        num_rounds="${NUM_ROUNDS}" \
        quotient_bits="${QUOTIENT_BITS}" \
        storage_engine=wiredTiger \
        reverse_map_engine=wiredTiger \
        storage_cache_size_mb=64 \
        reverse_map_cache_size_mb=64 \
        collect_db_stats=False \
        capture_extra_stats=False \
        filter_names="${FILTER_NAMES}" \
        microbench=True \
        query_workload="${workload}" \
        break_even=2 \
        "${zipf_args[@]}"
done
