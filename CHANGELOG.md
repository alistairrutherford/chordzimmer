# Changelog

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
