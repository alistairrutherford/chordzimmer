# Changelog

## Unreleased: Phase 7, the selection's drag-out

PLAN.md §6, Phase 7; §3.7.

- **Drag MIDI** (the timeline's title row): drag it into a track, or the Finder, for a MIDI clip
  of the selection: the chords and sections selected, back to back in order, or the whole
  progression when nothing is selected. It plays the chords as ChordZimmer would (voicing, bass,
  strum, velocity), at the host's tempo and meter; hold Alt for the chords without strum. The
  file is named after its sections and the key (`Verse-Chorus_C.mid`; one chord: `Dm7_C.mid`),
  and the section names are MIDI markers at their starts. The handle says what it holds.
- **Changed:** **Listen** is now saved with the state (still not in presets): leaving it out made a
  restore keep whatever value it had, which pluginval's state test caught.
- **Removed:** the workspace pattern's drag-out from the processor (it has had no control since
  Phase 4).
- MIDI files: markers are written (and read back).
- Tests: `test_render` checks what a drag-out holds, its name, the markers through the file, and
  that a selection's export is what a host records with it as the loop region; the snapshot
  test drags out the whole progression and a selection.

## Unreleased: Phase 5, Match and Chord-from-notes

PLAN.md §6, Phase 5; §3.4.

- **Match** (the footer's Match button): a popover over the transport and timeline, so the
  palette stays in view.
  - **Listen** (a new parameter, appended; not in presets): notes played in, from the
    keys or a clip, pass through and are kept with their lengths. *Last* picks how much counts
    (1–16 bars of playing; a silence afterwards doesn't push it away). *Use heard* copies them to
    the picker, *Clear* forgets them.
  - **Note picker:** a one-octave strip of 12 keys; teal shows how long each note was heard,
    amber the notes picked. Picked notes, if any, are what's matched; else what was heard.
  - **Matches:** the 8 palette chords that fit best, ranked (`core/theory/Matcher.h`): long notes
    on the root and 3rd count most, a note a semitone above a chord tone counts against, and each
    colour tone (7th, 9th …) has a small cost, so passing notes don't pull in extended chords.
    Ties: in key before borrowed, then the lower tier. *±2 fifths* also searches the palettes two
    fifths either side. Click a chip to hear it, double-click to add it. The palette shows the
    ranks on its orbs and fades the rest while Match is open.
  - **Chord from notes:** the notes as one chord on a root (*Auto*: the one its name suggests)
    and an optional bass, named for any set of notes (C7#9, Fm(maj7), C(b9), Cadd9/D …). *Set*
    replaces the selected timeline chord (keeping its length), *Add* puts it in at the marker.
- Tests: `test_matcher` (melodies, scoring and ties, borrowing, all 12 keys, the note window,
  Listen through the engine, chords from every set of notes); the snapshot test checks Match
  through `processBlock` (no allocation), the palette's ranks, Set and Add, and Listen not
  being saved, with a new reference image (`editor-match`).

## Unreleased: Phase 4, the editor

The palette, the key navigator and the timeline (PLAN.md §6, Phase 4; §3.1–§3.3, §5).

- **Palette** (PLAN.md §3.1): the 7 degrees of the palette's key and mode × the complexity
  rows (power and sus, triads, 6ths and 7ths, colour, extended, altered), as Night Studio orbs:
  teal major, violet minor, slate other; filled in the home key, a ring when not. Numerals
  and roots head the columns; about four rows show, the triads first; the wheel scrolls. Click to
  hear a chord, drag it onto the timeline, double-click (or right-click) to add it at the
  insertion marker. Pointing at one shows its notes and which are out of key in the Info View;
  the chord the timeline plays glows with the beat.
- **Key navigator** (PLAN.md §3.2), in the footer: the home key (Key, Scale), ⌂ Home, ◀ ▶ a
  fifth down or up, the parallel modes, and a circle-of-fifths dial (home in amber, the palette's
  key lit, click a key to go there). The palette position is saved with the plug-in.
- **Timeline** (PLAN.md §3.3): sections side by side as named, coloured lanes of chord blocks
  (width = duration), with the playhead, the loop region and the insertion marker.
  - Drag a chord or a section's tab to move it; **Alt-drag to copy**, also between sections; drop
    a palette chord into a section, between two, or on empty space (a new section).
  - Drag a chord's right edge to change its length, or a section's to scale its chords, on the
    snap grid (Off, 1/16 … Bar; default 1/8).
  - Click selects, Shift/Cmd-click adds; the selection is the loop region. Delete, Cmd-C / X / V
    (pasting at the insertion marker), Cmd-D (duplicate in place), ← →, Cmd +/- (zoom),
    Alt+Shift+wheel zoom, the wheel scrolls.
  - Right-click: inversion, octave, bass note, duplicate, copy, cut, paste, delete; a section's
    rename (or double-click its tab) and colour.
  - **Undo / redo** (Cmd-Z, Shift-Cmd-Z, and buttons): every edit, 100 steps. A restore or a
    preset with a progression starts the history again.
- **Transport bar** (PLAN.md §3.3): ⏮ rewind, Play (Space), Loop (L), Sync mode, Start bar,
  Snap, undo and redo, and the chord now with its numeral and the bar.
- **Performance popover** (PLAN.md §3.6), from the footer: Strum, Velocity, Voicing (with
  Octave), and Keys in (Chord Group, Progression, Hold, Key octave, Heard, Detect).
- **Changed:** ChordMonkey's chord pads, workspace panel (patterns A–D, the pattern drag-out) and
  effects cards are gone from the editor; the workspace is still read from and saved in the
  state. **Detect**'s *Use result* now puts the chords heard on the end of the timeline as a
  section "Detected" (one undo step) instead of into a workspace pattern. The **Pattern**
  parameter has no control any more (kept for older sets).
- Tests: `test_timeline` (scripted drags on the model); the editor snapshot test is back
  (`Tests/run_snapshot.sh`: reference images, every parameter reachable, the Info View, the
  timeline's and palette's mouse gestures, undo, state, Detect, no allocation while playing).

## Unreleased: Phase 3, playback

Engine and processor only (PLAN.md §6, Phase 3); the timeline UI is Phase 4.

- **Timeline playback** (PLAN.md §3.3, `core/engine/ProgressionPlayer.h`, replacing ChordMonkey's
  `PatternPlayer`): **Play** now plays the progression, chord by chord, through the effect chain
  (Voicing, Bass, Voice leading, Chord Octave, Strum, Velocity, PLAN.md §3.6).
  - **Sync mode → Sync** follows Live: locked to the song from **Start bar**; with **Loop** the
    loop region repeats (before the start bar too), without it the progression plays once.
    Host loops, locates and tempo changes land where the timeline would be.
  - **Sync mode → Preview** runs on its own clock at Live's tempo, with Live stopped too: loops
    the region, or plays once and stops; Play off pauses, ⏮ (`rewind`) goes back. With Live
    stopped, the instrument track hears it when armed or set to Monitor In (checked in Live
    12.4.16).
  - The **loop region** is the selection (chords or sections), else everything.
  - Every chord change is restruck on its beat; an edit to the sounding chord retunes it; an
    edit that moves the chord lines restrikes; all notes end on stop, Play off, Enable off and
    when the progression empties.
- **Previews** (PLAN.md §3.1): the processor's `previewChord` / `endPreview` audition a chord
  through the same effects, held or for a number of beats, over the timeline if it plays.
- The timeline and previews play at **Velocity peak** when the Velocity curve is Off.
- New parameters (appended): **Sync mode**, **Loop**, **Start bar** (26 in all); not in presets.
- **Changed:** the workspace's pattern no longer plays (Play is the timeline's); the pattern
  drag-out now renders the pattern as a one-section progression through the same player.
- Test rig: with Test workspace on, `Progression.txt` (one section per line, `Name | numerals`)
  loads the progression, for checks in Live.
- Tests: `test_player` rewritten for the timeline; `test_render` renders through it.

## Unreleased: Phase 2, progression model

Core and state only (PLAN.md §6, Phase 2); the timeline UI is Phase 4 and playback Phase 3.

- **Progression** (PLAN.md §3.3, `core/model/Progression.h`): `ChordProgression`, a list of named,
  coloured sections (8 hues) of chords. A chord is a `WorkspaceChord` (root + voiced tones) with
  bass, inversion, octave and a duration in ticks (96 to a beat; default a bar). Palette cells
  become chords with the same notes (`progressionChordFromCell`).
- **Edits:** insert a palette chord on empty space, into a section at a point, or between two
  sections; resize a chord or a whole section (its chords scale) with snap (off, 1/16 … 1 bar,
  default 1/8); move; duplicate anywhere, including Alt-duplicate in place between sections;
  multi-select delete; copy and paste at the insertion marker; inversion, octave, bass, replace;
  rename, recolour and delete sections. Queries: length, the chord at a beat, the range of a
  selection (the whole progression when nothing is selected).
- **Undo** (`core/model/UndoList.h`): every edit undoable and redoable, 100 steps.
- **Saved** with the plug-in state (so with the Live set) and in presets, as a versioned one-line
  text (`czp1 …`) in a `PROGRESSION` element; damaged or unknown text loads as an empty
  progression. States and presets from before have none and load empty.
- Tests: `test_progression`.

## Unreleased: Phase 1, palette theory

Core only (PLAN.md §6, Phase 1); the editor doesn't use it yet (Phase 4).

- **Scale families** (PLAN.md §3.2): the 7 major modes, the 7 harmonic-minor modes and the 7
  melodic-minor modes; Harmonic major, Hungarian minor and Double harmonic stay single-mode
  families. Each scale knows its family, its mode number and its parallel modes.
- **11 new scales**, appended after the 13 inherited ones (the Scale parameter saves the index, so
  existing sets and presets are unchanged): Locrian Natural 6, Ionian #5, Dorian #4, Lydian #2,
  Altered bb7, Dorian b2, Lydian Augmented, Lydian Dominant, Mixolydian b6, Locrian Natural 2,
  Altered. The Scale menu now lists 24.
- **Palette** (PLAN.md §3.1): `PaletteTiers.h` holds the 17 rows of the six complexity tiers as
  data; `PaletteBuilder.h` builds the 7-column grid for a palette key and mode against a home
  key and scale: notes, root, name, Roman numeral relative to home, colour family (from the 3rd
  and 5th) and filled/hollow (every note in the home scale).
- **Circle of fifths** (PLAN.md §3.2): `CircleOfFifths.h` steps the palette key a fifth either
  way, reports the distance from home (±1 … ±6) and goes home.
- Tests: `test_palette` (hand-written grids for C major, A minor, A harmonic minor and D Dorian),
  `test_navigator`; the music21 fixture covers the new modes.

## 0.0.1 (2026-11-24): Phase 0, fork and identity

Forked from ChordMonkey 1.1.0 (PLAN.md §6, Phase 0).

### Identity

- Renamed throughout: namespace `chordzimmer`, `ChordZimmerProcessor`, `ChordZimmerEditor`,
  `ChordZimmer.jucer` (project id `CzimPj`), bundle ID `com.alistairrutherford.chordzimmer`,
  plug-in code `Czim` (manufacturer `ARut`), presets in `~/Music/ChordZimmer/Presets/`, the test
  fixture variable `CHORDZIMMER_FIXTURES`. Comments from ChordMonkey cite *ChordMonkey's PLAN.md*.

### Removed (PLAN.md §2)

- Bind and Keys-Lock (`KeyRouter`, `KeysLock`); the All-keys keyboard stays, in `KeyMap.h`.
- Styles (with Swing and Humanize), Arp, Repeat and Step seq, from the engine, the parameters
  (40 → 23), the editor and the tests.
- The chord-set browser and the 41 factory chord sets (`ChordSetLibrary`, `ChordSetManager`), and
  the factory presets that used them or the effects above (Init and 10 factory presets remain). The
  numeral parser (`ChordSets.h`) stays.
- The snapshot test rig and CPU benchmark (`Tests/Snapshot`, `run_snapshot.sh`, `run_bench.sh`)
  and the Chordism preset dump tool; the snapshot tests come back with the new UI in Phase 4.

### Night Studio look (PLAN.md §5.1)

- `ui/Theme` holds the tokens: the navy gradient background with a 4 % grain, glass cards
  (14 px radius, top highlight), family gradients (major teal → sky, minor violet → magenta,
  other slate), the amber accent, radii and glows, with helpers for each.
- The chord pads are orbs in their family's colour, filled in key and hollow out of key, glowing
  brighter on hover and while they play; workspace slots are family-coloured pills.
- Space Grotesk (headings, chord names) and Inter (UI text), SIL OFL, embedded via BinaryData.
- The editor opens at 1000 × 640.

### Fixed (in code inherited from ChordMonkey)

- A strum that crossed a loop wrap or a locate lost or bunched its later notes: they were placed
  from the strum's start in the old song position.
- Velocity curves read the bar position a sample on (was half a sample), so a strummed note on a
  sixteenth line gets the same velocity whatever the block size and sample rate.
