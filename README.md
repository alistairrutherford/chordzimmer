# ChordZimmer

A MIDI chord plug-in (VST3) for Ableton Live on macOS for writing chord progressions: a **chord
palette** by scale degree and complexity, colour-coded by chord family and marked in or out of
key; **borrowing** around the circle of fifths and from parallel modes; a section-based
**progression timeline**; **matching chords to a melody**; **chord relations**; and drag-out to
MIDI clips. It makes no sound itself: it sends MIDI to an instrument on another track.
It's a fork of [ChordMonkey](../chordmonkey). The plan and its status are in [PLAN.md](PLAN.md).

**Version 1.0.0** (2026-11-25: all of PLAN.md's phases done, the optional Phase 9 included). What changed:
[CHANGELOG.md](CHANGELOG.md).

![The ChordZimmer editor in the Night Studio theme](docs/editor.png)

## What it does

- **Palette:** the chords of the palette's key and mode, by degree (columns, headed by their
  numerals in the home key) and complexity (rows: power and sus, triads, 6ths and 7ths, colour,
  extended, altered). Teal: major family; violet: minor; slate: the rest. Filled: every note in
  the home key; a ring: not. Click to hear, drag onto the timeline, double-click to add.
- **Key navigator** (the footer): the home key (Key, Scale), ⌂ Home, ◀ ▶ a fifth down or up to
  borrow, the parallel modes, and a circle-of-fifths dial.
- **Timeline:** sections of chords. Drag a chord or a section's tab to move it (Alt: copy, also
  between sections), drag an edge to resize (snap 1/16 … a bar), click and Shift-click to select
  (the selection is the loop region), right-click for inversion, octave, bass, *Replace with ▸*,
  rename, colour. Undo and redo for every edit (Cmd-Z, Shift-Cmd-Z).
- **Transport:** Play, Loop, **Sync** (locked to Live's song position from a start bar) or
  **Preview** (its own clock at Live's tempo, also with Live stopped), Start bar, Snap.
- **Match** (the footer): turn **Listen** on and play a phrase in, from the keys or a clip on a
  track routed in, or pick notes on the strip: the 8 palette chords that fit best are ranked, on
  the palette too. *Chord from notes* turns any notes into a chord (Set replaces the selected one,
  Add puts it in).
- **Relations** (the palette's title row): for the chord last chosen, each palette chord's
  function (V/vi, IV, rel, sub), notes in common and voice-leading distance, and **Next ▸**, five
  usual follow-ups.
- **Drag MIDI** (the timeline's title row): the selection, or everything, as a MIDI clip with the
  section names as markers. And the other way: **Import MIDI…** (beside it) opens a `.mid` and puts
  its chords in at the insertion marker (its markers become sections; drum parts are left out, on
  any channel); **Open .mid…** on Match matches its melody. In the Standalone you can also drop the
  file on either. (In Live, use the buttons: Live keeps a clip's drag to itself, and hides the
  plug-in window while the Finder is in front, so drops don't reach it. Drag a clip to the desktop
  first to get a file.)
- **Performance** (the footer): strum, velocity, voicing, bass, voice leading, octave, stack, and
  what keys played in do (chords of the home key, Heard, Detect).
- **Presets:** the factory presets, your own, and **Templates** (Four Chords, Minor Axis,
  Two-Five-One, Twelve-Bar Blues, Andalusian Cadence, Canon Ground, Fifties, Dorian Vamp), which
  load a key, a scale and a progression to start from.

The Info View along the bottom says what anything under the mouse does.

## Build, test, install

Needs Xcode and JUCE 8.0.14 at `~/Development/JUCE` (or `--juce DIR`; for the tests, `JUCE=DIR`).

```bash
Tests/run_tests.sh
```

```bash
Tools/install.sh --validate
```

```bash
Tests/run_snapshot.sh
```

```bash
Tests/run_bench.sh
```

- `run_tests.sh`: the core tests (plain C++, no JUCE): theory, palette, navigator, progression,
  timeline gestures, playback, Match, Relations, export.
- `install.sh --validate`: builds the VST3 and the Standalone (Release, universal), signs them ad
  hoc, installs them in `~/Library/Audio/Plug-Ins/VST3` and `~/Applications`, and runs `pluginval`
  at strictness 10.
- `run_snapshot.sh`: the editor snapshot test (JUCE): renders the editor against the reference
  images in `Tests/fixtures/snapshot` (`--update` after a deliberate UI change, `--no-pixels` on
  another Mac), scripts the mouse gestures, and checks state, presets, real-time safety (no
  allocation in `processBlock`), damaged states and host edge cases.
- `run_bench.sh`: CPU per instance for 8 instances (PLAN.md's target: under 1%).

CI (`.github/workflows/build.yml`) runs the core tests, the clean install with pluginval, the
snapshot test without pixel comparison, and the benchmark. User presets live in
`~/Music/ChordZimmer/Presets/`.

## In Live

1. Turn on *Use VST3 Plug-In System Folders* (Settings → Plug-Ins) and rescan.
2. Put ChordZimmer on a MIDI track, and an instrument on another with *MIDI From: <that track> →
   ChordZimmer*. Arm that track, or set its Monitor to *In*, to hear ChordZimmer while Live is
   stopped (Preview, the palette).
3. Open the plug-in window, pick a key (the footer) or a template (the presets), and drag or
   double-click chords onto the timeline. Play in Sync follows Live's transport; Preview plays on
   its own.
4. Drag *Drag MIDI* onto a track for a clip, or record the instrument track while it plays.

Notes:

- After installing a new version, quit and restart Live: it keeps the old plug-in loaded until it
  quits.
- A ChordZimmer saved in a set keeps that set's list of automatable parameters: one saved before a
  version that added parameters (Listen, in Phase 5) doesn't show the new ones until the device is
  put in again.
- Live hides plug-in windows while it isn't the front app.

## Licence

Apache License 2.0 ([LICENSE](LICENSE)). The embedded fonts, Space Grotesk and Inter, are under
the SIL Open Font License 1.1 ([Resources/Fonts](Resources/Fonts)).
