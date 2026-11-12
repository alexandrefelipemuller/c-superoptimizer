#!/usr/bin/env bash
cd "$(dirname "$0")/.."
scripts/run_sorting_prefix3_battery.sh data/sorting_prefix3_20261111
scripts/run_prefix_joint_battery.sh data/prefix_joint_battery_20261111
