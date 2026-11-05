#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe distributions "${1:-full}"
