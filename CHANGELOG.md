# Changelog

## Unreleased: Phase 3, playback

Engine and processor only (PLAN.md §6, Phase 3); the timeline UI is Phase 4.

- **Timeline playback** (PLAN.md §3.3, `core/engine/ProgressionPlayer.h`, replacing ChordMonkey's
  `PatternPlayer`): **Play** now plays the progression, chord by chord, through the effect chain
  (Voicing, Bass, Voice leading, Chord Octave, Strum, Velocity, PLAN.md §3.6).
  - **Sync mode → Sync** follows Live: locked to the song from **Start bar**; with **Loop** the
    loop region repeats (before the start bar too), without it the progression plays once.
    Host loops, locates and tempo changes land where the timeline would be.
  - **Sync mode → Preview** runs on its own clock at Live's tempo, with Live stopped too: loops
    the region, or plays once and stops; Play off pauses, ⏮ (`rewind`) goes back.
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
