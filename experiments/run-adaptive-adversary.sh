#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe adaptive-adversary "${1:-full}"
