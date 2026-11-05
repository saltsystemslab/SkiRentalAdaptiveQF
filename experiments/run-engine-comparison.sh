#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe engine-comparison "${1:-full}"
