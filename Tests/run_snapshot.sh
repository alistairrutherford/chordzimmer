#!/usr/bin/env bash
# The editor snapshot test (PLAN.md §6 Phase 4, §7; ChordMonkey's Tests/run_snapshot.sh brought
# back): builds Tests/Snapshot (a console app with the processor and editor), renders the editor
# and compares it with Tests/fixtures/snapshot, checks that every parameter has a control, drives
# the timeline, palette and navigator with scripted mouse gestures, checks that state restores,
# and the hardening checks (Phase 8).
#
#   Tests/run_snapshot.sh              # compare
#   Tests/run_snapshot.sh --update     # write new reference images (after a deliberate UI change)
#   Tests/run_snapshot.sh --no-pixels  # render without comparing (another Mac's font rendering; CI)
#
# JUCE: $JUCE, else ~/Development/JUCE (snapshot_build.sh). The images of each run are in
# Tests/build/snapshot.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/snapshot_build.sh"
APP="$(snapshot_build "$HERE" | tail -1)"
echo "** BUILD SUCCEEDED **"

cd "$HERE"
"$APP" fixtures/snapshot build/snapshot "$@"
