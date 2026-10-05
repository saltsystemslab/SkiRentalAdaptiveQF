#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"
behavioral_args

for workload in uniform normal lognormal; do
    run_experiment "distributions/${workload}-${SCALE}" \
        "${BEHAVIORAL_ARGS[@]}" \
        query_workload="${workload}" \
        break_even=2
done

run_experiment "distributions/zipf-${SCALE}" \
    "${BEHAVIORAL_ARGS[@]}" \
    query_workload=zipfian \
    zipf_constant=0.99 \
    break_even=2
