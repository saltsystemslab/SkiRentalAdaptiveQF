#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"
behavioral_args

run_experiment "phased/uniform-first-${SCALE}" \
    "${BEHAVIORAL_ARGS[@]}" \
    query_workload=uniform \
    break_even=2 \
    is_phased_test=True \
    num_phases=4 \
    start_with_adversarial_phase=False

run_experiment "phased/adversarial-first-${SCALE}" \
    "${BEHAVIORAL_ARGS[@]}" \
    query_workload=uniform \
    break_even=2 \
    is_phased_test=True \
    num_phases=4 \
    start_with_adversarial_phase=True
