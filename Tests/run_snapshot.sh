#!/usr/bin/env bash
# The editor snapshot test (PLAN.md §6 Phase 4, §7; ChordMonkey's Tests/run_snapshot.sh brought
# back): builds Tests/Snapshot (a console app with the processor and editor), renders the editor
# and compares it with Tests/fixtures/snapshot, checks that every parameter has a control, drives
# the timeline, palette and navigator with scripted mouse gestures, and checks that state restores.
#
#   Tests/run_snapshot.sh              # compare
#   Tests/run_snapshot.sh --update     # write new reference images (after a deliberate UI change)
#   Tests/run_snapshot.sh --no-pixels  # render without comparing (another Mac's font rendering)
#
# The images of each run are in Tests/build/snapshot.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JUCE="${JUCE:-$HOME/Development/JUCE}"
PROJUCER="${PROJUCER:-$JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"

"$PROJUCER" --resave "$HERE/Snapshot/ChordZimmerSnapshot.jucer" > /dev/null
mkdir -p "$HERE/build"
set +e
xcodebuild -project "$HERE/Snapshot/Builds/MacOSX/ChordZimmerSnapshot.xcodeproj" -configuration Release build > "$HERE/build/snapshot-build.log" 2>&1
status=$?
set -e
grep -E "error:|BUILD (SUCCEEDED|FAILED)" "$HERE/build/snapshot-build.log" | sort -u
[[ $status -eq 0 ]] || exit $status

cd "$HERE"
"$HERE/Snapshot/Builds/MacOSX/build/Release/ChordZimmerSnapshot" fixtures/snapshot build/snapshot "$@"
