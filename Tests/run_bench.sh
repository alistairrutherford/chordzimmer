#!/usr/bin/env bash
# CPU benchmark (PLAN.md §6, Phase 8: 8 instances under 1% CPU each; ChordMonkey's Tests/run_bench.sh).
# Builds Tests/Snapshot (the real processor, Release) and times processBlock for 8 instances run
# one after another, as a host's audio thread runs them: idle, the timeline playing in Sync,
# everything on (every effect, the timeline, keys coming in), Preview with the host stopped,
# Listen and Detect with a tune coming in, at 32-, 128- and 512-sample blocks (44.1 kHz). Prints
# each instance's mean CPU as a share of real time (100% = one core) and its worst block.
#
#   Tests/run_bench.sh [seconds of audio per case, default 20]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/snapshot_build.sh"
APP="$(snapshot_build "$HERE" | tail -1)"
"$APP" --bench "${1:-20}"
