#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
run_recipe adversarial "${1:-full}"
