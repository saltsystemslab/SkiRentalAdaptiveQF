#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe phased "${1:-full}"
