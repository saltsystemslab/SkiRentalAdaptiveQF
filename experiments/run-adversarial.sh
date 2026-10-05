#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"
behavioral_args

for cache_mb in 64 128 256; do
    for frequency in 1 5 10; do
        run_experiment \
            "adversarial/frequency-${frequency}-cache-${cache_mb}-${SCALE}" \
            "${BEHAVIORAL_ARGS[@]}" \
            query_workload=adversarial \
            break_even=2 \
            storage_cache_size_mb="${cache_mb}" \
            adv_freq="${frequency}"
    done
done
