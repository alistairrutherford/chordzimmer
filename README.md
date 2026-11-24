# ChordZimmer

A MIDI chord plug-in (VST3) for Ableton Live on macOS that brings Klimper 2's way of making chords
to Live: a **chord palette** by scale degree and complexity, colour-coded by chord family and
marked in or out of key; **borrowing** around the circle of fifths and from parallel modes; a
section-based **progression timeline**; **matching chords to a melody**; and drag-out to MIDI
clips. It makes no sound itself: it sends MIDI to an instrument on another track. It's a fork of
[ChordMonkey](../chordmonkey). The plan and its status are in [PLAN.md](PLAN.md).

ChordZimmer copies Klimper's *behaviour* only: no code, artwork, sounds, manual text or name.

**Version 0.0.1** (Phase 0, 2026-11-24). What changed: [CHANGELOG.md](CHANGELOG.md).

![The ChordZimmer editor in the Night Studio theme](docs/editor.png)

**Status:** Phases 0–7 of 9 are done: the fork, the new identity and the **Night Studio** look
(PLAN.md §5.1); the palette theory; the progression model; timeline playback (Play follows Live
from a start bar, or previews on its own clock); the editor: the **palette** (click to hear,
drag or double-click onto the timeline), the **key navigator** (◀ ▶ around the circle of fifths,
the parallel modes, ⌂ Home), the **timeline** (sections of chords: drag to move, Alt-drag to
copy, drag an edge to resize, snap, undo), the transport and a performance popover; **Match**
(play a phrase in, or pick notes, and see the palette chords that fit them, or turn the notes
into a chord); **Chord Relations** (each chord's function, shared notes and voice-leading
distance from the one chosen, what usually comes next, and replacements); and **Drag MIDI**,
the selection (or everything) as a clip with section markers. Hardening (Phase 8) is next.

## Build, test, install

Needs Xcode and JUCE 8.0.14 at `~/Development/JUCE` (or `--juce DIR`).

```bash
Tests/run_tests.sh
```

```bash
Tools/install.sh --validate
```

```bash
Tests/run_snapshot.sh
```

The first runs the core tests (plain C++, no JUCE). The second builds the VST3 and the Standalone
(Release, universal), signs them ad hoc, installs them in `~/Library/Audio/Plug-Ins/VST3` and
`~/Applications`, and runs `pluginval` at strictness 10. The third builds the editor snapshot
test (JUCE): it renders the editor against the reference images in `Tests/fixtures/snapshot`,
scripts the timeline's and the palette's mouse gestures, and checks state and real-time safety.
User presets live in `~/Music/ChordZimmer/Presets/`.

## In Live

Turn on *Use VST3 Plug-In System Folders* (Settings → Plug-Ins) and rescan. Put ChordZimmer on a
MIDI track, and give an instrument track *MIDI From: <that track> → ChordZimmer* with Monitor *In*.

## Licence

Apache License 2.0 ([LICENSE](LICENSE)). The embedded fonts, Space Grotesk and Inter, are under
the SIL Open Font License 1.1 ([Resources/Fonts](Resources/Fonts)).
