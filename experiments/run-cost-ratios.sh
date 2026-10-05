#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"
behavioral_args

for workload in uniform normal lognormal zipfian; do
    zipf_args=()
    output_workload="${workload}"
    if [[ "${workload}" == zipfian ]]; then
        zipf_args=(zipf_constant=0.99)
        output_workload=zipf
    fi

    run_experiment "cost-ratios/${output_workload}-half-${SCALE}" \
        "${BEHAVIORAL_ARGS[@]}" "${zipf_args[@]}" \
        query_workload="${workload}" break_even=1 storage_sleep_us=130

    run_experiment "cost-ratios/${output_workload}-twice-${SCALE}" \
        "${BEHAVIORAL_ARGS[@]}" "${zipf_args[@]}" \
        query_workload="${workload}" break_even=3 reverse_sleep_us=130

    run_experiment "cost-ratios/${output_workload}-quad-${SCALE}" \
        "${BEHAVIORAL_ARGS[@]}" "${zipf_args[@]}" \
        query_workload="${workload}" break_even=5 reverse_sleep_us=390
done
