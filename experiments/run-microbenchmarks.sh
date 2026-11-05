#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe microbenchmarks "${1:-full}"
