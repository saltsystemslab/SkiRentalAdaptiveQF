#!/usr/bin/env bash
source "$(dirname "$0")/experiments/common.sh"
run_recipe reverse-map-comparison "${1:-full}"
