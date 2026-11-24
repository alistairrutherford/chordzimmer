#!/usr/bin/env bash
# Builds the editor snapshot app (Tests/Snapshot, Release) for run_snapshot.sh and run_bench.sh,
# and prints the binary's path on its last line. Sourced, not run.
#
# The .jucer names this Mac's JUCE (~/Development/JUCE). With $JUCE elsewhere (CI checks it out
# in its workspace), it builds from a copy with the module paths rewritten, into
# Tests/Snapshot/Builds/CI (ignored), so the tracked Builds/MacOSX project stays as it is.
snapshot_build () {
    local here="$1"
    local juce="${JUCE:-$HOME/Development/JUCE}"
    local projucer="${PROJUCER:-$juce/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer}"
    local jucer="$here/Snapshot/ChordZimmerSnapshot.jucer" folder="MacOSX"
    if ! grep -q "path=\"$juce/modules\"" "$jucer"; then
        folder="CI"
        sed -e "s#<MODULEPATH id=\"\([a-z_]*\)\" path=\"[^\"]*\"/>#<MODULEPATH id=\"\1\" path=\"$juce/modules\"/>#" \
            -e 's#targetFolder="Builds/MacOSX"#targetFolder="Builds/CI"#' "$jucer" > "$here/Snapshot/ChordZimmerSnapshot.ci.jucer"
        jucer="$here/Snapshot/ChordZimmerSnapshot.ci.jucer"
    fi
    "$projucer" --resave "$jucer" > /dev/null
    mkdir -p "$here/build"
    if ! xcodebuild -project "$here/Snapshot/Builds/$folder/ChordZimmerSnapshot.xcodeproj" -configuration Release build \
            > "$here/build/snapshot-build.log" 2>&1; then
        grep -E "error:" "$here/build/snapshot-build.log" | sort -u | head -20 >&2
        echo "the snapshot app didn't build (see $here/build/snapshot-build.log)" >&2
        return 1
    fi
    echo "$here/Snapshot/Builds/$folder/build/Release/ChordZimmerSnapshot"
}
