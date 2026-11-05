#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe all "${1:-full}"
