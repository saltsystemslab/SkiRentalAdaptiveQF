#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCALE="${1:-full}"

for recipe in \
    run-distributions.sh \
    run-cost-ratios.sh \
    run-microbenchmarks.sh \
    run-phased.sh \
    run-engine-comparison.sh \
    run-adversarial.sh \
    run-adaptive-adversary.sh; do
    "${SCRIPT_DIR}/${recipe}" "${SCALE}"
done
