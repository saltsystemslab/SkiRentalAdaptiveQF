#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
configure_scale "${1:-full}"
behavioral_args

if [[ "${SCALE}" == full ]]; then
    DEFAULT_ADVERSARY_WINDOW=100000
else
    DEFAULT_ADVERSARY_WINDOW=1000
fi
ADVERSARY_WINDOW_SIZE="${ADVERSARY_WINDOW_SIZE:-${DEFAULT_ADVERSARY_WINDOW}}"

run_experiment "adaptive-adversary/window-${ADVERSARY_WINDOW_SIZE}-${SCALE}" \
    "${BEHAVIORAL_ARGS[@]}" \
    filter_names=hybridSkiQF \
    query_workload=adaptive-adversarial \
    break_even=2 \
    adv_freq=5 \
    adversary_window_size="${ADVERSARY_WINDOW_SIZE}"
