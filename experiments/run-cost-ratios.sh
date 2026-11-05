#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe cost-ratios "${1:-full}"
