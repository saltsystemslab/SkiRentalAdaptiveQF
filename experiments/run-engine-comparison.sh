#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"

for reverse_engine in wiredTiger splinterDB; do
    run_experiment "engine-comparison/${reverse_engine}-${SCALE}" \
        num_queries="${INSERT_NUM_QUERIES}" \
        num_rounds="${NUM_ROUNDS}" \
        quotient_bits="${INSERT_QUOTIENT_BITS}" \
        storage_engine=wiredTiger \
        reverse_map_engine="${reverse_engine}" \
        storage_cache_size_mb=64 \
        reverse_map_cache_size_mb=64 \
        collect_db_stats=False \
        capture_extra_stats="${CAPTURE_EXTRA_STATS}" \
        query_workload=uniform \
        break_even=2 \
        sort_and_insert_keys=False \
        sort_and_insert_fingerprints=False \
        is_insert_test=True
done
