# ChordZimmer: Project Plan

A macOS VST3 MIDI plug-in for Ableton Live that emulates the **chord creation features of Klimper 2**
(Polydigm Software): a **chord palette** laid out by scale degree and complexity, colour-coded by
chord quality and marked in-key or out-of-key; **borrowing** from adjacent keys around the circle
of fifths and from parallel modes; a **progression timeline** of sections and chords with
durations; **matching chords to a melody**; building a chord from chosen notes; previewing through
any instrument in Live; and **drag-out to a MIDI clip**. ChordZimmer makes no sound of its own. It
sends MIDI to an instrument on another track, as ChordChimp, Chordism and ChordMonkey do.

**Starting point:** a fork of **ChordMonkey** (`../chordmonkey`, v1.1.0), which already has the
chord theory (13 scales, `ChordTypes`, `ChordRecognizer`, `VoiceLeading`, `ChordNamer`,
`RomanNumeral`), a palette, a workspace model, host-synced `PatternPlayer`, the offline Export
renderer and drag-out, presets, `Tools/install.sh` and the JUCE-free core test setup. ChordZimmer
replaces ChordMonkey's Scaler-style surface (fixed pads, A–D patterns, Bind, Keys-Lock, Styles)
with Klimper's: the degree × complexity palette, the key navigator, and a section-based timeline.

**Clean room.** As with the siblings, ChordZimmer copies *behaviour* only. No code, artwork,
sounds, manual text or product name come from Klimper. Its look (dark navy, round chord buttons)
is a general style, and ours gets its own palette and layout.

**What Klimper 2 does (from its product page and online manual, read 2026-11-24):**

| Klimper feature | What the manual says |
|---|---|
| Chord palette | The chords of the project's scale, organised so you can try combinations "without digging through submenus". Columns are the scale degrees; rows go up in complexity: *Other* (non-triadic: power chords, sus) at the bottom, then *Triads*, then 6ths/7ths, then *Extended* chords (Δ7, add9 …) above, with a vertical *Complexity* scroll. Click to preview. |
| Colours | Green: major triads and their extensions. Purple: minor triads and their extensions. Grey: everything else (power, suspended, augmented, diminished). |
| In key | A **filled** circle: every note is in the project's key. **Hollow**: at least one note is out of key. |
| Progressions | Drag a chord to the sequence bar: it makes a **section**. Drop more chords into it, or to the left/right of a section to start a new one. Drag a chord's or section's end to change its **duration**. Shift selects several; Alt/Option-drag duplicates (also in place, between sections). Transport: rewind, play, loop; snap (1/8); the current chord and its Roman numeral shown. |
| Match a melody | Select melody notes in Klimper's piano roll, then *Match against selected notes* in the palette menu. |
| Borrowing | The palette follows the **home key** (house icon). **◀ ▶** step along the circle of fifths to adjacent keys ("the more steps, the more exotic"). Clicking the mode switches to **parallel modes** (users report major modes and harmonic-minor modes). |
| Chord from notes | In the piano roll, select notes, right-click the root, *Selected notes → add / set* chord: chords not in the palette. |
| Chord Relations | A button in the palette footer. The manual doesn't describe it; a reviewer asks "why it is suggesting certain choices", so it shows relations between chords (our design: §3.5). |
| Preview and export | Plays chords through the DAW's instruments; drag the export button to a track or the desktop; the clip holds the current selection (chord, section(s) or whole track). |
| Also in Klimper, **not chords** | Melody tracks with a piano roll, *harmonic guides* (root / chord / extension / in-key rows), ghost notes of other tracks, a *Klimper Track* companion plug-in per melody track, a built-in piano. |

---

## 1. Goals

| # | Requirement | Acceptance criterion |
|---|-------------|----------------------|
| G1 | Loads in Live 12 as a VST3 and sends MIDI to an instrument on another track | `pluginval` strictness 10 passes. In Live, an instrument with *MIDI From: ChordZimmer* plays and records the output sample-accurately. |
| G2 | **Palette:** for any key and mode, 7 degree columns × complexity tiers of chords, each colour-coded and marked in/out of the home key | Theory tests against the music21 fixture (inherited), plus a palette table test: for C major and A harmonic minor, every cell's name, notes, colour and in-key flag match a hand-written table (§3.1). |
| G3 | **Borrowing:** ◀ ▶ walk the circle of fifths; the mode menu lists the parallel modes of the current scale family; Home returns | Navigator tests: 12 steps return home; every key × mode family; in-key flags always against the **home** key. |
| G4 | **Progression timeline:** sections of chords with durations, insert/move/resize/duplicate/delete, undo | Model tests: every edit, including insert between sections and Alt-duplicate in place; snap; save/restore round trip; undo/redo of every edit. |
| G5 | **Plays in time with Live** (and free-running for preview), loop on a selection | Fake-transport tests (inherited from ChordMonkey's `test_player`): each chord change on its beat at 32–2048 samples, 44.1–96 kHz, across loops, locates and tempo changes. |
| G6 | **Match against notes:** rank palette chords by how well they fit a set of notes | Matcher tests on hand-made melodies (§3.4): the expected chord is top-ranked; ties broken by the stated rule. |
| G7 | **Chord from notes:** add or set a timeline chord from chosen notes and root | Recognizer tests (inherited) plus naming of chords outside the palette (e.g. C7♯9, Fm(maj7)). |
| G8 | **Export:** drag the selection (chord, sections, or the whole progression) into Live as a MIDI clip | The exported file equals the notes recorded live from the same selection. |
| G9 | Real-time safe, no stuck notes | ChordMonkey's rules: no allocation or locks in `processBlock`, all-notes-off on stop, bypass, edit and deletion; the stuck-note fuzz passes with timeline edits during playback. |
| G10 | ≤ 64 host parameters | Live auto-populates them all; no `Options.txt` change. |

**Out of scope (first version):** Windows, AU, AAX, a built-in sound, melody tracks and the piano
roll, harmonic guides and ghost notes, the per-track companion plug-in, MIDI import of melodies
(Klimper doesn't have it either; see Phase 9), MPE.

---

## 2. Feature map (ChordMonkey → ChordZimmer)

| Area | ChordMonkey | ChordZimmer |
|---|---|---|
| Palette | 7 scale pads + 5 borrowed pads, shaped by one Chord Group | **Replaced:** degree × complexity grid (§3.1), colours, filled/hollow in-key marks |
| Key / scale | Key, Scale (13) | **Kept** as the **home key**; plus a palette key/mode navigator (§3.2); scales grouped into **families** (major modes, harmonic-minor modes, melodic-minor modes) with the missing modes added |
| Workspace | Patterns A–D × 8 slots | **Replaced:** one progression of **sections** of chords with beat durations (§3.3) |
| Playback | `PatternPlayer`, pattern switch at the bar | **Kept**, driven by the timeline; loop region from the selection |
| Performance | Voicing, Bass, 17 Styles, Swing, Humanize, Strum/Arp/Repeat/Velocity/Hold | **Simplified:** Voicing, Bass, Voice leading, Velocity, Strum. Styles, Arp, Repeat, Step seq **dropped** (Klimper writes block chords; open question 2) |
| Keyboard | Bind, Keys-Lock | **Dropped.** MIDI in feeds Match and Chord-from-notes; optional "play palette column" mode (open question 3) |
| Chord sets | 41 factory sets | **Dropped** as a browser; a few factory **template** progressions on the presets menu |
| Detect | `ChordListener` | **Reused** as Chord-from-notes (§3.4) |
| Substitutes | Slot right-click list | **Reused** inside Chord Relations (§3.5) |
| Export | Pattern drag-out | **Kept**; content follows the selection |

---

## 3. Functional design

### 3.1 Palette

- **Columns:** the 7 degrees of the *palette* key and mode (I … vii°), headed by Roman numerals
  relative to the **home** key, so a borrowed column reads ♭VII, iv etc.
- **Rows (complexity tiers),** bottom to top. Each degree column holds the variants of its tier
  that are built on that degree; a cell is left empty when the variant doesn't exist for that
  degree (as in Klimper's screenshot, where some cells are blank):

  | Tier | Name | Variants (built from scale steps where possible) |
  |---|---|---|
  | 0 | Other | 5 (power), sus2, sus4 |
  | 1 | Triads | the diatonic triad (maj, min, dim, aug) |
  | 2 | Sixths & sevenths | 6 / m6, 7 / m7 / ø7 / °7 (diatonic 7th) |
  | 3 | Colour | maj7, add9 / madd9, 7sus4 |
  | 4 | Extended | 9 / m9 / maj9, 11, 13, 6/9 |
  | 5 | Altered | 7♭9, 7♯9, aug, dim (non-diatonic options; usually hollow) |

  The table lives in `core/theory/PaletteTiers.h` as data, so tiers can be tuned without code.
- **Colour:** green for chords whose triad is major, purple minor, grey for the rest. Computed from
  the chord's 3rd and 5th, not its name.
- **Filled / hollow:** filled if every pitch class is in the **home** scale, hollow otherwise.
  Borrowed palettes therefore show which chords stay safe.
- **Preview:** click a cell to sound it (note-on to the output until mouse-up, or for 1 beat; voice
  led from the last chord heard). Hover shows notes, name, numeral and the tones out of key.
- **Scroll:** vertical, showing about 4 tiers; triads in view by default.

### 3.2 Key navigator (borrowing)

Footer bar: **⌂ Home**, **◀ key ▶**, **mode ▾**, **Chord Relations**, **Match ▾**.

- ◀ ▶ move the palette key a fifth down/up (C → F / G), keeping the mode. The bar shows how many
  steps from home (±1 … ±6).
- *mode ▾* lists the 7 modes of the current **family** on the same tonic (parallel modes): C
  Ionian ⇄ C Dorian ⇄ … ⇄ C Locrian; for harmonic minor: harmonic minor, Locrian ♮6, Ionian ♯5,
  Dorian ♯4, Phrygian dominant, Lydian ♯2, altered ♭♭7. Melodic-minor modes as a third family.
  The rest of the existing scales (Hungarian minor, Double harmonic) stay as single-mode families.
- ⌂ restores the home key and mode. The home key is the **Key** and **Scale** parameters; the
  palette key is UI state saved with the plug-in, not a parameter.
- Optional: "Use DAW key" when the host reports one (Live 12 doesn't via VST3; skip unless found).

### 3.3 Progression timeline

- One **progression**, a list of **sections**, each a list of **chords**. A chord stores its notes
  as pitch classes + root + bass + inversion + octave + a duration in beats (default 4); a section
  stores a name (Verse, Chorus …, editable) and colour. Positions are derived from the order.
- **Editing** (all in `core/model/Progression.h`, all undoable via a command list):
  - drop a palette chord on empty space → new section; on a section → insert at that point;
    between two sections → new section there;
  - drag a chord's right edge to change its duration, snapped (1/16 … 1 bar; default 1/8, as
    Klimper); drag a section's edge to scale all its chords;
  - drag to move; **Alt/Option-drag to duplicate**, dropping anywhere including between sections
    (the complaint in Klimper's reviews is handled from the start);
  - Shift/Cmd-click multi-select; Delete; Cmd-C/V pasting **at the insertion marker**, not the end;
  - right-click a chord: inversion, octave, bass note, *Replace with ▸* (relations, §3.5), rename
    section, delete.
- **Transport bar:** ⏮ rewind, ▶ play, ⟲ loop (loops the selected chords/sections, else all),
  snap, current chord name and numeral. **Sync** follows Live's transport (position = song
  position modulo the progression length, or from a start bar); **Preview** runs its own clock at
  the host tempo so you can audition with Live stopped.
- The progression is saved in the plug-in state (and so with the Live set) and in presets.

### 3.4 Match and Chord-from-notes

Klimper takes notes from its own piano roll. ChordZimmer has no piano roll, so the notes come from:

1. **Held / played notes** on ChordZimmer's MIDI input (play a phrase into it; a *Listen* toggle
   collects the last N bars into a note set with durations);
2. a **mini note picker**: a one-octave strip of 12 toggle keys in the Match panel;
3. (Phase 9) a dropped `.mid` file.

**Match against notes** scores every palette cell (the current key's, and optionally all keys
within ±2 fifths):
`score = Σ duration-weight of notes that are chord tones (root/3rd weighted higher) − penalty × Σ
weight of notes that clash (a semitone above a chord tone) − small complexity cost`. The top 8
cells are highlighted with a rank badge; non-matches dim. Ties: in-key before borrowed, then lower
tier.

**Chord from notes:** in the Match panel, pick notes (or use the held ones), choose the root, then
**Set** (replace the selected timeline chord) or **Add** (insert after it). Named by
`ChordRecognizer` / `ChordNamer`; any pitch-class set is allowed, named as a slash or `(add…)`
chord when it has no standard name.

### 3.5 Chord Relations

Our design, since the manual doesn't describe Klimper's: with a chord selected (palette or
timeline), *Chord Relations* overlays the palette with arrows/badges for
**dominant** (V/x), **subdominant**, **relative** major/minor, **tritone substitute**, **common-tone**
chords (sharing 2+ notes), and **voice-leading distance** (number of semitone moves). The same list
appears in the timeline chord's *Replace with ▸* menu (reusing ChordMonkey's `Substitutes.h`), and a
"Next ▸" strip suggests the 5 most common follow-ups from a small transition table of our own.

### 3.6 Performance and output

- **Voicing:** Close / Open / Spread / Drop-2; **Bass:** off / root / root+5 one octave down;
  **Voice leading:** off / smooth (inherited); **Octave**; **Velocity** (fixed, 1–127);
  **Strum** (0–100 ms, up/down). Applied at playback and in Export.
- One MIDI channel out. All-notes-off on stop, bypass, edit of the playing chord, and deletion.

### 3.7 Export

A drag handle at the bottom right. The clip holds the **selection**: one chord, chosen sections,
or (nothing selected / track clicked again) the whole progression, rendered through §3.6 by the
offline renderer from ChordMonkey. Written to a temp `.mid` named after the sections (e.g.
`Verse-Chorus_C.mid`); dragging to Finder also works. Section names become MIDI markers.

### 3.8 Parameters (target: about 14)

Key, Scale (home), Play, Loop, Sync mode, Octave, Voicing, Bass, Voice leading, Velocity, Strum,
Strum direction, Listen, Enable (pass input through). The palette position, progression and
selections are state, not parameters.

---

## 4. Architecture

Same stack as the siblings: **JUCE 8.0.14, C++17, Projucer + xcodebuild**, JUCE at
`~/Development/JUCE`. VST3 + Standalone. Header-only core in plain C++ (no JUCE) so the core tests
run without a host.

```
 UI (message thread)
   Palette ─┐                  ┌─ Progression (+ UndoList) ─► SnapshotExchange ─┐
   Navigator┼─► PaletteBuilder │                                                 ▼
   Match ◄──┘   (key,mode,home)│        Transport ─► Clock ─► ProgressionPlayer ─► Performer ─► VoiceTracker ─► EventQueue ─► MIDI out
                               │                                                     ▲
 MIDI in ─► InputRouter ─► NoteCollector (FIFO) ─► Match / Chord-from-notes          │
                       └─► Preview requests (FIFO) ──────────────────────────────────┘
```

### 4.1 Modules

| Module | From | Notes |
|---|---|---|
| `core/theory`: `Scales`, `ChordTypes`, `ChordModel`, `ChordNamer`, `RomanNumeral`, `VoiceLeading`, `ChordRecognizer`, `Voicings`, `Substitutes` | ChordMonkey | `Scales` gains families and the missing harmonic/melodic-minor modes |
| `core/theory/PaletteTiers.h`, `PaletteBuilder.h`, `CircleOfFifths.h` | **New** (Phase 1) | Tier table; builds the grid for (palette key, mode, home key); in-key and colour |
| `core/theory/Matcher.h`, `Relations.h` | **New** (Phases 5, 6) | Scoring (§3.4); relations and the transition table (§3.5) |
| `core/model/Progression.h`, `UndoList.h` | **New** (Phase 2) | Sections, chords, edits as commands, the saved text |
| `core/engine/ProgressionPlayer.h` | ChordMonkey `PatternPlayer`, reworked (Phase 3) | Timeline from beat position; loop region; Sync / Preview clocks |
| `core/engine/NoteCollector.h` | ChordMonkey `ChordListener`, reworked (Phase 5) | Collects notes with durations for Match |
| `core/engine/*` (Clock, Transport, EventQueue, VoiceTracker, Strum, Velocity, SnapshotExchange) | ChordMonkey | As is. Style, Arp, Repeat, KeysLock, KeyRouter **deleted** |
| `core/midiio/MidiFile.h`, offline renderer | ChordMonkey | Markers added |
| `plugin/*` | ChordMonkey, renamed | Progression in the state XML |
| `ui/*` | ChordMonkey's `Theme`, `ParamControl`, `PresetBar`, `InfoView` | New `PalettePanel`, `NavigatorBar`, `TimelinePanel`, `MatchPanel`, `RelationsOverlay` |

**Copy, don't share**, as the siblings do. Code comments cite `PLAN.md §…`; copied code cites
*ChordMonkey's PLAN.md §…* (`../chordmonkey/`).

### 4.2 Identity

| | ChordZimmer |
|---|---|
| Bundle ID | `com.alistairrutherford.chordzimmer` |
| Manufacturer / plug-in code | `ARut` / `Czim` |
| Installed as | `~/Library/Audio/Plug-Ins/VST3/ChordZimmer.vst3`, `~/Applications/ChordZimmer.app` |
| User data | `~/Music/ChordZimmer/Presets/` |

Fixed in Phase 0; never change them.

---

## 5. UI

About 1000 × 640, resizable 80–150 %. Top to bottom:

1. **Preset bar** (inherited) and the **transport bar** (§3.3).
2. **Timeline**: sections as labelled lanes of chord blocks, width = duration; playhead; insertion
   marker; zoom with Alt+Shift+wheel / Cmd ±.
3. **Palette**: the scrollable grid (§3.1), round cells, numeral headers, tier labels on the left.
4. **Footer**: navigator (§3.2), Chord Relations, Match, performance popover (§3.6), Export handle.
5. **Info View** strip (inherited from ChordMonkey).

Keyboard: Space play/stop, Delete, Cmd-Z / Shift-Cmd-Z, Cmd-C/V, ← → move selection, L loop.

### 5.1 Visual design: "Night Studio"

The brief is for it to **look cool**: a dark, glowing, instrument-like surface, clearly its own
and not a copy of Klimper's flat navy. All colours, radii, glows and fonts live in `ui/Theme` as
named tokens, so the look can be retuned in one place.

| Token | Value | Use |
|---|---|---|
| `bg` | `#0A0D16` → `#121829` vertical gradient, faint 4 % noise texture | Editor background |
| `panel` | `#161C2E` at 85 % with a 1 px `#FFFFFF14` top highlight, 14 px radius | Timeline, palette and footer cards (a "glass" look) |
| `text` / `textDim` | `#E8ECF8` / `#7C86A6` | Labels; numerals and tier labels dimmed |
| `major` | gradient `#2EE6C5` → `#1A9FD9` (teal to sky) | Major-family chords |
| `minor` | gradient `#B26BFF` → `#E04FB0` (violet to magenta) | Minor-family chords |
| `other` | gradient `#6B7699` → `#454E6B` (slate) | Power, sus, dim, aug |
| `accent` | `#FFB547` (amber) | Playhead, selection, Home button when away from home, Export handle |
| `danger` | `#FF5C7A` | Out-of-key tones in hover cards |

- **Chord cells** are orbs: a radial gradient of their family colour, a soft outer glow (blurred
  drop shadow in the family colour at 35 %), name in semibold with the extension as a superscript.
  **In key:** filled orb. **Out of key:** a 2 px gradient ring with a dark centre and a dashed
  inner hairline, so it reads "hollow" at a glance. Hover lifts the orb (scale 1.06, glow 60 %);
  pressing it ripples outward once.
- **Playing:** the sounding chord's orb and timeline block pulse with the beat (glow 35 → 80 % on
  each beat, eased), driven by `VBlankAttachment`; nothing animates when idle, so it costs no CPU.
- **Timeline:** sections are rounded lanes tinted with their section colour (8 hues, low
  saturation) and a label tab; chord blocks are pill-shaped with their family gradient at 70 %;
  the playhead is an amber line with a short fading trail; the loop region is an amber-tinted band.
- **Navigator:** the ◀ ▶ key stepper sits beside a small **circle-of-fifths dial** (12 dots,
  home in amber, current palette key lit, the arc between them drawn), so borrowing distance is
  visible.
- **Match:** ranked cells get an amber rank badge; non-matching cells fade to 30 %.
- **Type:** *Space Grotesk* for headings and chord names, *Inter* for UI text (both SIL OFL,
  embedded via BinaryData).
- **Motion rules:** 120–180 ms ease-out for hovers and drags, no motion longer than 300 ms, and
  a *Reduce motion* option in the settings menu that turns pulses and ripples off.
- Snapshot reference images (Phase 4) are taken with this theme; Phase 0 brings in the tokens
  and fonts, so even the forked UI already looks like ChordZimmer.

---

## 6. Milestones

| Phase | Est. | Deliverable | Exit criteria |
|---|---|---|---|
| **0. Fork and identity** | ½ day | `git init`; copy ChordMonkey; rename (namespace, classes, bundle ID, `Czim`, folders); delete Bind/Keys-Lock/Style/Arp/Repeat/chord sets; `Theme` tokens and fonts from §5.1 | Builds; `pluginval` 10; loads in Live next to ChordMonkey; core tests pass; editor shows the §5.1 colours and fonts |
| **1. Palette theory** | 1 day | Scale families + new modes, `PaletteTiers`, `PaletteBuilder`, `CircleOfFifths` | Palette table tests (G2); navigator tests (G3); modes against music21 |
| **2. Progression model** | 1 day | `Progression`, `UndoList`, state XML | Model tests (G4), including insert-between and duplicate-in-place |
| **3. Playback** | 1 day | `ProgressionPlayer`, Sync/Preview, loop region, preview requests, §3.6 performance | Fake-transport tests (G5); stuck-note fuzz with edits during play (G9) |
| **4. UI: palette, navigator, timeline** | 3 days | `PalettePanel`, `NavigatorBar`, `TimelinePanel` with drag/drop, resize, select, duplicate, snap, transport | Snapshot tests: every parameter has a control; reference images; scripted drag tests on the model |
| **5. Match and Chord-from-notes** | 1½ days | `NoteCollector`, `Matcher`, `MatchPanel` (Listen, note picker, Set/Add) | Matcher tests (G6); recognizer naming tests (G7) |
| **6. Chord Relations** | 1 day | `Relations`, overlay, *Replace with ▸*, Next strip | Relations for C major / A minor checked by hand |
| **7. Export and Live check** | 1 day | Selection-aware export with markers; bridge tests and manual checklist | Export = render (G8); recorded in Live at 0.000 ms; set save/reopen restores the progression |
| **8. Hardening and install** | ½ day | Benchmark, CI, README, screenshot, factory templates | 8 instances < 1 % CPU each; `Tools/install.sh --validate` clean |
| *9. Optional* | 1–2 days | Drop a `.mid` in: melody → Match, chords → timeline (the feature Klimper users ask for) | Import tests on fixtures |

**Total: about 10½ working days** to Phase 8.

---

## 7. Testing

Inherited from ChordMonkey (core tests via `Tests/run_tests.sh` with Xcode's clang, the music21
fixture, `pluginval`, the snapshot rig, the stuck-note fuzz and malloc hook, the Ableton MCP bridge
checks), plus:

- `test_palette`: hand-written grids for C major, A minor, A harmonic minor, D Dorian; colours and
  filled/hollow flags; empty cells where a variant doesn't exist.
- `test_navigator`: ±12 fifths return home; parallel-mode lists per family; in-key always vs home.
- `test_progression`: every edit and its undo; snap; insert-between; duplicate-in-place; paste at
  marker; XML round trip; garbage XML rejected without a crash.
- `test_player`: chord changes on beat (buffer sizes × sample rates × loops × locates × tempo).
- `test_matcher`: ~20 melodies with an expected best chord; tie rules.
- `test_export`: exported file equals the offline render and the recorded output.
- **Live (bridge):** load, route to an instrument, play a 3-section progression, record, compare;
  drag-out lands as a clip with the right length.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Timeline drag-and-drop UI is the bulk of the work and easy to get fiddly | Every gesture is a `Progression` command first, tested without UI; the UI only maps mouse to commands |
| Tier table produces odd or clashing chords in exotic modes | It's data; tune per family with the palette tests as the guard |
| Matching without a piano roll is less direct than Klimper's | Listen + note picker in Phase 5, `.mid` import in Phase 9; revisit after use |
| Preview while Live is stopped: Live may not process a MIDI effect's output with the transport stopped | Monitor *In* on the instrument track (as Klimper's own setup page asks). **Retired in Phase 3 (Live 12.4.16):** Live does pass a stopped MIDI effect's output on; the instrument track hears it when armed (Monitor Auto) or with Monitor In, not when unarmed on Auto |
| Similarity to Klimper's look | Own colours, layout and names; behaviour only |

---

## 9. Open questions

1. **Scope:** chord features only (this plan), or also Klimper's melody side (piano roll with
   harmonic guides, ghost notes, a companion per-track plug-in)? The latter is roughly another
   1½–2 weeks and a second plug-in.
2. **Rhythm:** Klimper writes block chords. Keep it that way, or keep one of ChordMonkey's Styles as
   an optional performance?
3. **Keyboard playing:** should incoming MIDI also *play* chords (e.g. white keys → the current
   palette's triad row, black keys → tier above), or only feed Match? Klimper doesn't do it.
4. **Built-in sound:** none (as siblings), or a simple sine/piano in the Standalone only?
5. **Fork base:** ChordMonkey (recommended: theory, player, export, recognizer, UI kit) vs a lighter
   fork of ChordChimp.

---

## 10. Repository layout

```
chordzimmer/
  ChordZimmer.jucer   Builds/   JuceLibraryCode/
  Source/core/{theory,model,engine,midiio}/   Source/plugin/   Source/ui/
  Tests/   Tools/install.sh   docs/
  PLAN.md   README.md   CHANGELOG.md
```

## 11. Research sources

- Product page: https://www.klimper-plugin.com/products/klimper-2 (features, requirements, reviews)
- Manual, chords: https://www.klimper-plugin.com/pages/chords-manual
- Manual, piano roll: https://www.klimper-plugin.com/pages/piano-roll-manual
- Manual, export: https://www.klimper-plugin.com/pages/exporting-midi
- Manual, Ableton Live setup: https://www.klimper-plugin.com/pages/using-klimper-with-ableton-live

## 12. Status

| Phase | Status | Detail |
|---|---|---|
| 0. Fork and identity | **Done (2026-11-24)** | Copied from ChordMonkey 1.1.0 at `1a9a315` (its tracked `Source/`, `Tests/`, `Tools/`, CI, `.jucer`, LICENSE; not its `Builds/`, `dist/`, `Tests/build/`). **Renamed** per §4.2: namespace `chordzimmer`, `ChordZimmerProcessor`/`ChordZimmerEditor`, `ChordZimmer.jucer` (id `CzimPj`), bundle ID `com.alistairrutherford.chordzimmer`, `ARut`/`Czim`, `~/Music/ChordZimmer/Presets/`, `CHORDZIMMER_FIXTURES`; old bare `PLAN.md` citations now read *ChordMonkey's PLAN.md*. **Removed** (§2, §4.1): Bind and Keys-Lock (`KeyRouter.h` → `KeyMap.h` with only the All-keys map; `KeysLock.h`), `Style.h` (with Swing, Humanize), `Arp.h`, `Repeat.h`, `StepSequencer.h`, the arp/repeat rates, the chord-set browser (`ChordSetManager`) and factory sets (`ChordSetLibrary.h`), the presets built on them (Init + 10 remain), their 17 parameters (23 left), the Keyboard row, the Arp/Style/Repeat/Step seq cards, `StepSeqView`, `test_bind`, `test_style`, `test_chordsets`, and the snapshot rig/benchmark (`Tests/Snapshot`, `run_snapshot.sh`, `run_bench.sh`; Phase 4 brings snapshots back) and the Chordism preset dump tool. **Kept:** the theory, `ChordSets.h` (numeral parser), `PatternPlayer` (its jump API now unused, for Phase 3), Strum, Velocity, Hold, Stack, Voicings, VoiceTracker, EventQueue, SnapshotExchange, `ChordListener`/Detect, `KeyListener`/Analyse, `Substitutes`, MidiFile + export renderer, presets, InfoView. **Night Studio** (§5.1) in `ui/Theme`: tokens, gradient + grain background, glass cards, family gradients, amber accent, radii, glow and orb helpers; pads are family orbs (filled in key, hollow out of key, hover/play glow), workspace slots family pills; Space Grotesk + Inter (OFL, `Resources/Fonts`, BinaryData); editor 1000 × 640. **Fixed in inherited code** (found by the tests rewritten for strums): a strum across a loop wrap or locate placed its later notes from the stale start; Velocity now reads the bar a sample on, so render and host agree on a strum note on a grid line. **Checks:** `Tests/run_tests.sh`: all 12 binaries passed; Projucer + xcodebuild Debug and Release (universal) built with no warnings from `Source/`; `Tools/install.sh --validate` installed `ChordZimmer.vst3` and `ChordZimmer.app` (ad hoc signed) and `pluginval` strictness 10 passed; the Standalone shows the Night Studio editor (`docs/editor.png`). **Live 12.4.16 (checked afterwards, by the Phase 0 session):** ChordZimmer is listed separately from ChordMonkey and loads next to it; it shows 25 parameters (23 + Program + Device On), none of the removed ones; a test-pattern take into Drift recorded 54 notes, C-major triads with slot = beat mod 7, each an eighth long and exactly on the beat (0.000); the meters read 0 after the stop. The plug-in window opens from the device with the Night Studio look, as in the Standalone, and reflects the take ("Export 4 bars", last-played stripe on IV). **Not checked:** reopening a saved set; the §8 risk "preview while Live is stopped" stays open for Phase 3. |
| 1. Palette theory | **Done (2026-11-24)** | **Built** (all header-only, JUCE-free, in `core/theory`): **scale families** in `Scales.h` (`ModeFamily`, `modeFamilyInfo`, `scaleMode`, `parallelModes`; named *Mode*Family because `KeyProfiles.h` already has a `ScaleFamily` for the tonic triad's quality); **11 new modes appended** after the 13 inherited scales, whose indices are unchanged (Locrian Natural 6, Ionian #5, Dorian #4, Lydian #2, Altered bb7; Dorian b2, Lydian Augmented, Lydian Dominant, Mixolydian b6, Locrian Natural 2, Altered), so the Scale parameter now has 24 choices; `PaletteTiers.h` (17 rows in the six tiers); `PaletteBuilder.h` (`buildPalette`, `ChordFamily`/`chordFamily`, which is to replace `ui::familyOf` in Phase 4); `CircleOfFifths.h`. **Decisions beyond the plan:** (1) *One rule for every mode.* A row stacks scale steps on the degree and exists only if the result is one of its accepted shapes, so what a mode shows follows from the table: triad = maj/min/dim/aug (a stack like Hungarian minor's D# F A leaves the cell empty); "6" = 6/m6 with a major 6th; a diatonic 7th chord sits in "7" if its 7th is minor or diminished (7, m7, ø7, °7, 7b5, 7#5) and in "maj7" (tier 3) if major (maj7, m(maj7), maj7#5), so it is never shown twice; "add9" = add9/madd9; "7sus4"; "9" = 9/m9/maj9; "11" = 11/m11; "13" = 13/m13/maj13 (no 11th); "6/9" = 6/9 and m6/9. Tier 5 plays fixed shapes on the root: 7b9 and 7#9 on every degree with a perfect 5th (secondary dominants), aug on degrees whose triad is major, dim on degrees whose triad is minor (so neither repeats a diatonic aug/dim). (2) Harmonic major is a single-mode family too (§3.2 didn't list it). (3) *Numerals are relative to the home **scale**,* not to major: A minor reads i ii° III iv v VI VII, D Dorian i ii III IV v vi° VII. A root in the home scale has no accidental; any other keeps its letter in the palette's spelling (#iv° for F# from C, bVII for Bb); suffixes drop a minor chord's "m" and use °, ø7 and + (iiø7, III+maj7, i(maj7), V7b9). (4) The palette ignores natural minor's default raised V (Em stays v; E comes from the harmonic-minor palette). (5) Notes start from the palette tonic in the chord octave (48 + key) and rise as stacked. (6) The navigator keeps the palette as fifths from home in -6 … +6 (the tritone is +6 sharpwards, -6 flatwards; 12 steps either way return home); a palette at home follows a Key/Scale change, one away keeps its distance and mode. (7) The fixture builds each new mode by rotating the interval names of music21's own harmonic- and melodic-minor scales, and interval-built scales are now walked with music21's interval arithmetic: `ConcreteScale` put Altered bb7's B# an octave low. The 13 inherited scales' reference data is unchanged; the fixture grows to 24 scales (958 KB). **Checks:** `Tests/run_tests.sh`: all 14 binaries passed, including the new `test_palette` (every cell of C major, A minor, A harmonic minor and D Dorian: name, notes in order, family, filled/hollow, empty; numerals; invariants over 12 keys × 24 scales: stacked cells in key at home, every cell named on its own root with no slash, the triad row full in all 21 modes of the three 7-mode families) and `test_navigator` (12 steps from every key, distances, Home, families as rotations, parallel-mode lists, borrowed numerals and in-key flags against four homes for every palette key and mode); `test_theory` checks the 11 new modes against music21 in all 12 keys × 17 Chord Groups; `test_keys`' scale-family counts updated for the new scales. `Tools/install.sh --validate`: built, installed, `pluginval` strictness 10 passed. music21 10.5.0 came from uv's cache (`uv run --offline`), so nothing was downloaded. **Not checked:** nothing in the editor uses the palette yet (Phase 4); with the palette at -6 the names are still spelled F# rather than Gb (`makeSpelling` picks the key's spelling, not the direction); the 24-choice Scale menu wasn't looked at in Live. |
| 2. Progression model | **Done (2026-11-24)** | **Built** (header-only, JUCE-free, `core/model`): `Progression.h` and `UndoList.h`, plus the state wiring. **Decisions beyond the plan:** (1) *Name:* the type is `ChordProgression` (with `ProgressionChord`, `ProgressionSection`), because the inherited `theory/Progressions.h` already has `enum class Progression` (the white keys' degree order, still a parameter); the files keep the plan's names. (2) *Chords reuse `WorkspaceChord`* (root pitch class + voiced tones above C2) for their notes, adding bass (pitch class or none), inversion, octave and duration; notes come from Workspace.h's `slotNotes`, then the bass as the nearest note of that pitch class below the lowest. Palette cells convert with `progressionChordFromCell`, which keeps the cell's exact notes (tested for every cell of 12 keys × 4 scales). (3) *Time in ticks,* 96 to a beat (every snap and a later triplet grid are whole numbers); a chord lasts 1/16 … 16 bars of 4/4, default a bar. Snap: Off, 1/16, 1/8 (default), 1/4, 1/2, Bar (`beatsPerBar` given; 4 by default). Snapping is relative to the item's start, which is the grid while everything before it is on the grid. (4) *Resizing a section* moves each inner chord boundary in proportion and snaps it, keeping every chord between one grid step and 16 bars, so the section's length is clamped to what its chords can hold. (5) *A section always has a chord:* an edit that empties one removes it, and the text rejects empty sections. (6) *Addressing:* `ChordRef` (section, index), `Selection` (sections + chords; a chord inside a selected section counts as that section), and `InsertPoint` (the insertion marker and drop target): `InSection(s, i)` or `NewSection(before)`, with `NewSection(numSections())` the "empty space". (7) *Clips:* Copy gives parts in progression order (whole sections, or one section's selected chords with that section's name and colour). Pasted into a section, all their chords go in at the point; pasted as a new section, each part becomes a section. So Alt-duplicating a section between sections copies it exactly; Move uses the same path with positions as before the move (dropping into a section the move empties puts the chords in a new section in its place; into a moved section is refused). `duplicateInPlace` puts the copy right after the selection's last item. (8) *Extra edits* for later phases: `replaceChord` (Replace with ▸, Chord-from-notes Set), `setSectionColour`, `clear`. Limits: 64 sections, 64 chords per section, 256 in all, names ≤ 48 bytes of UTF-8 (trimmed, control characters become spaces). (9) *Undo is by snapshot,* not inverse commands: each command is a labelled callable run on a copy, kept as a step only if it changed something; 100 steps, oldest dropped. Every edit copies the progression (a few small allocations on the message thread, ≈ 1 MB at the limits); undo/redo don't allocate; nothing touches the audio thread. (10) *Saved text:* one ASCII line, versioned `czp1`, names percent-encoded; reading is strict (any malformed token, out-of-range value, unknown version, broken invariant or > 64 KB rejects the whole text, leaving the target as it was). (11) *State:* a `PROGRESSION` element with the text, next to `WORKSPACE`, in `getStateInformation` and `presetState`; `setStateInformation` restores it (empty if missing or unreadable) and removes it before the APVTS state; `applyPreset` replaces it only if the preset has one (as for the workspace). Kept under a lock like the workspace; no `SnapshotExchange` yet, since the audio thread doesn't use it before Phase 3. The UndoList isn't in the processor yet (Phase 4, with the timeline; a restore or preset should then `reset` it). **Checks:** `Tests/run_tests.sh`: all 15 binaries passed, including the new `test_progression` (snap; palette cells as chords; bass/inversion/octave notes; every edit, its refusals and limits; insert between sections; move in all its cases; duplicate and duplicate-in-place; multi-select delete; paste at the marker inside and between sections; queries; undo/redo of every edit, bounded history, redo dropped by a new edit, 400 random edits undone and redone exactly; the text written out by hand, 500 random progressions round-tripped, 47 malformed texts and the limits rejected, 20000 mutated/random texts with no crash and every accepted one reading back as itself). `Tools/install.sh --validate`: built with no warnings from `Source/`, installed, `pluginval` strictness 10 passed. **Processor round trip** checked with a scratch rig linked against the build's `libChordZimmer.a` (not committed; kept in the session notes): a progression with UTF-8 and XML-special names survives save → restore → save with identical XML and one `PROGRESSION`, the workspace alongside; presets carry it; a preset without one keeps the current one; a Phase 0/1 state and a state with garbage text restore an empty progression with the rest intact. **Not checked:** saving and reopening a Live set with a progression (nothing in the editor makes one yet; Phase 7); the processor rig isn't part of `run_tests.sh`. |
| 3. Playback | **Done (2026-11-24)** | **Built:** `core/engine/ProgressionPlayer.h` (replaces `PatternPlayer.h`, deleted): a `Timeline` (the progression flattened for the audio thread: up to 256 chords with start, length, notes at Chord Octave 0 incl. bass and inversion, root, section; the loop region; ~12 KB, trivially copyable) built by `makeTimeline` on the message thread and handed over through a `SnapshotExchange` whenever the progression or the loop selection changes; and the `ProgressionPlayer`, which turns it into sample-accurate triggers per Clock segment. `ChordEngine` plays the triggers as performance 128 on a new perform slot (20), and previews as performance 129 on slot 21 (`ChordFx`: `ExtraChords` = the workspace's 8 slot-preview chords + timeline + preview; `VoiceTracker` 388 sources); `PatternRender` now renders a `ChordProgression` (`renderProgression`; `renderPattern` goes through `progressionFromPattern`). Parameters appended: **Sync mode** (Sync, Preview), **Loop** (default on), **Start bar** (1–999); 26 in all; not preset parameters. Processor: `setLoopSelection`, `rewind`, `previewChord`/`endPreview` (a lock-free FIFO), `playingChord`/`playhead` atomics; the test rig's `Progression.txt`. **Decisions beyond the plan:** (1) *Sync:* the timeline is locked to the song from Start bar (bar length from the host's meter, counted from song position 0): with Loop the region repeats as `region start + (song − start) mod region length`, so it also plays before the start bar; without Loop it plays once from the start bar and is silent outside. Nothing plays while Live is stopped. (2) *Preview* keeps its own timeline position, advanced at the host's tempo per segment, whether or not Live plays; Play off pauses (on again resumes, restriking the chord under it); Loop off stops at the end and the next Play starts from the top; ⏮ is a processor call, not a parameter (Sync's rewind is Live's). Switching mode restrikes. If Live stops while Preview plays, all notes end (the stop rule) and the chord restrikes at once. (3) *Restrikes:* at every chord start (two equal chords in a row are two strikes), each wrap of the loop region, playback start, any jump, and when the chord under the playhead changes; an edit that keeps the sounding chord under the playhead only retunes it (as ChordMonkey's slot edits did). (4) *Velocity (§3.6)* is the existing **Velocity peak** parameter when the Velocity curve is Off (default 127; the old pattern played at 100); a curve still applies on top. (5) *§3.6 performance* is the existing chain applied to the timeline: Voicing, Bass, Voice leading, Chord Octave, Strum, Velocity (and Stack, Hold, which §3.6 drops, still work). **Strum stays a note-value rate** (1/64 …), not the plan's 0–100 ms: not built. (6) *Previews* start at the next block's first sample (UI events have no finer time), held or for N beats (the end sample-accurate, at the tempo when it starts); a new one replaces the last; ignored with Enable off or listening; voice-led from the last chord when Voice leading is Smooth. (7) *Interim:* the workspace's patterns no longer play (Play is the timeline's), so the inherited editor's Play does nothing until Phase 4 gives the timeline a UI; its slot preview and pattern drag-out still work (the drag-out through the new player). **Checks:** `Tests/run_tests.sh`: all 15 binaries passed. `test_player` (rewritten, G5): timeline lines and loop regions; Sync from the top, uneven lengths across sections, restrikes, mid-chord start, Start bar with and without Loop (also in 3/4), the loop region from a section and from chords across sections, Velocity peak; host loops (blocks across the wrap and split), a host loop against a longer progression, locates, tempo changes mid-play; Preview looping, play-once and Play-again, pause/resume, rewind, running while Live plays elsewhere, Sync silent while stopped; Play/Enable/Analyse, edits that retune, edits that move lines, emptying and refilling, Chord Octave, Sync → Preview, keys over the timeline; held, timed and overlapping previews; Strum and Bass on the timeline. Each sweep runs at 32–2048-sample blocks, 44.1/48/96 kHz and 97/120/174 BPM (26 configurations): worst error 1 sample. Stuck-note fuzz (G9): 60 runs × 120 random blocks with 1058 edits during play, mode/Loop/start-bar switches, rewinds, previews, host loops, locates, tempo changes, stops, resets, keys and every effect: 2416 note-ons, nothing left sounding, the timeline never sounding when it shouldn't. `test_render`: the pattern drag-out, now through the timeline, equals what a host records at 3 rates × 3 block sizes, to the tick. `Tools/install.sh --validate`: built with no warnings from `Source/`, `pluginval` strictness 10 passed. The scratch processor rig (session notes) also passed: the Phase 2 state checks, and through `processBlock` with no host playhead, Preview + Play gives C at sample 0 and G at sample 96000 (beat 4 at 48 kHz, 120 BPM) at Velocity peak 90, `playingChord`/`playhead` published, and a 1-beat `previewChord` sounds and ends. **Live 12.4.16 (checked afterwards, through the bridge; set `ChordZimmer Phase 3 Check`):** 28 parameters (26 + Program + Device On); Drift on *CZ Rec*, MIDI From *CZ Src → ChordZimmer*; `Progression.txt` with Verse `I:4 vi:4 IV:2 V:2`, Chorus `IV:4 V:4 I:8`, Bridge `ii:3 V:1` (32 beats), 120 BPM. *Sync, Loop, Start bar 1:* silent while stopped; a 50-beat take recorded 45 notes, every chord on its beat (0.000) and of its full length, wrapping from beat 32 to the Verse, the last chord cut at the stop, meters 0 afterwards; velocity 127 (Velocity peak). *Sync, Loop off, Start bar 3:* silent for bars 1-2, the progression once from beat 8 to 40 exactly, silent after. *Preview with Live stopped (the §8 risk):* it sounds, on an armed track (Monitor Auto) or an unarmed one with Monitor In (an unarmed Auto track hears nothing, as for any input; a monitoring change is heard from the next chord); Play off ends the notes. *Preview while Live records:* its own phase (+1.749 beats, from before the start) and every chord its exact length at Live's tempo across the wrap (IV, V 4, I 8, ii 3, V 1, I 4, vi 4, IV 2, V 2, IV 4, V 4, I …); starting Live doesn't restrike; stopping it keeps Preview sounding. **Not checked:** Live's own loop and jumps (only the fake-transport tests); a tempo change in Live; the ms Strum; the inherited editor's controls for the new parameters (Phase 4). |
| 4. UI: palette, navigator, timeline | **Done (2026-11-24)** | **Built:** the editor of §5. Top to bottom: the top bar (kept); a **transport bar** (⏮ rewind, Play, Loop, Sync mode, Start bar, Snap, undo/redo, the chord now with its numeral and bar.beat); the **timeline** (`ui/TimelinePanel`: sections as tinted lanes with a name tab, chord pills in their family gradient, filled in key and a ring out of it, the ruler, playhead with trail, loop band, insertion marker, drop indicator, horizontal scroll and zoom, inline rename); the **palette** (`ui/PalettePanel`: orbs by degree × row, numeral/root headers, row and tier labels, about four rows, triads in view at first, wheel scroll, click to hear, drag to the timeline, double-click or right-click to add, hover text with notes and out-of-key tones, the sounding chord pulsing); the **footer** (`ui/NavigatorBar`: Home key = Key + Scale, ⌂, ◀ key ▶ with the steps from home, mode ▾ of the family, a circle-of-fifths dial, Performance); the output keyboard and Info View (kept). Gestures as edits in `core/model/TimelineLayout.h` (layout, hit test, drop point, resize/scale to x, move/Alt-copy to x, click selection, marker, ← →, zoom, chord labels), and `Progression.h`'s inserting edits report where their chords landed (`placed`). The processor holds the **UndoList** (`editProgression`, `undoProgression`/`redoProgression`, `undoState`, `progressionVersion`). `ChordFamily` replaces `ui::familyOf` (Theme's `Family` is now an alias). The **snapshot rig** is back (`Tests/Snapshot`, `Tests/run_snapshot.sh`, 6 reference images in `Tests/fixtures/snapshot`). **Decisions beyond the plan:** (1) *Layout:* Key and Scale moved to the footer as the home key; the §3.6 popover is a panel inside the editor (not a CallOutBox), so the Info View and the snapshot reach it, and it also holds what MIDI in does (Chord Group, Progression, Hold, Key octave, Heard, Detect: the engine still plays keys, open question 3). Chord Relations and Match get their footer buttons with their phases (5, 6). (2) *Removed from the editor:* ChordMonkey's chord pads, workspace panel (with the pattern drag-out; the selection's drag-out is Phase 7), effects cards and global row. The workspace stays in the state. **Pattern** is retired: no control, exempt from the "every parameter has a control" check like the test rig; its Info View entry says so. (3) *Detect's Use result* puts the chords heard on the end of the timeline as a section "Detected" (one undo step) instead of into a workspace pattern. (4) *Undo:* the history lives in the processor under the progression's lock, so the editor can close and reopen; `setProgression` is a load and resets it (restore, preset with a progression, test rig); a preset without a progression leaves both alone; not saved. (5) *Geometry:* sections sit 14 px apart, and that gap (with the space before the first and after the last) is the drop zone for a new section; inside a section a drop goes before the first chord whose middle is right of it; a right edge grabs within 5 px either side, but at most 30 % of a narrow item, which keeps its body clickable; zoom 6–96 px a beat (default 24). (6) *Selection:* a click selects one item, Shift or Cmd toggles it; pressing on a selected item drags the whole selection (a click without a drag then selects it alone); a chord in a selected section counts as selected; a chord added as a new section selects the chord, so the next one joins it; moved and copied items stay selected. The insertion marker follows the selection (after its last item) or goes where empty space is clicked. (7) *Commit on release:* a resize shows live and becomes one undo step when let go; a move shows a drop indicator (Alt: a copy). Clicking a chord in the timeline also sounds it, as the palette does. (8) *Labels:* timeline numerals are relative to the home scale, as the palette's (`timelineChordLabel`); a bass or inversion shows as a slash (Am over G names as Am7/G, vi7). (9) *Families:* the core's rule (a major 3rd and perfect 5th is major even with a minor 3rd, so C7#9 is green) replaces the UI's, which called it other. (10) *UI state* (`uiState`): palette position (fifths from home, mode), snap, zoom, beside the keyboard and size. (11) A palette drag carries its chord as the `czp1` text of a one-chord progression (`czchord:` prefix). (12) The beat pulse runs on the editor's 30 Hz timer from the playhead's phase, not a VBlankAttachment; no *Reduce motion* option yet. **Checks:** `Tests/run_tests.sh`: all 16 binaries passed, including the new `test_timeline` (layout, hit tests incl. a 1/16 chord, drop points, resize at every snap and in 3/4, section scaling, moves and Alt-copies within, between and across sections, palette drops, clicks and Shift-clicks, marker, arrows, labels, zoom, and 3000 random gestures at random zooms: 834 edits, every result valid and round-tripping, all undone). `Tests/run_snapshot.sh`: all passed — 6 reference images; 26 parameters, none without a control but the test rig and Pattern; every Info View key reachable at the default and smallest size with the popover open and closed; scripted mouse gestures on the real panels (four palette double-clicks making one section, a palette drop on empty space, a chord edge +2 beats, a section edge to half, Cmd-Z / Shift-Cmd-Z, Am dragged into another section, a section Alt-dragged to the end, Shift-click and the loop region, Cmd-C / click-in-gap / Cmd-V, Delete, Cmd-D, ← →, Space, L, the snap menu, the menus' actions, 17 undo steps back to empty and forward again); a palette click sounds D F A through `processBlock` and letting go ends it; the navigator (◀, mode, ⌂, ▶, the dial, Key carrying an away palette, a home palette following Key and Scale, the position surviving a reopen); undo history reset by a restore and a preset with a progression, kept by one without; parameters, UI state and progression restore without reporting a change; Detect's result as "Detected[C G Am F]", one undo step; 600 blocks of Preview with edits between them, no allocation in `processBlock`. `Tools/install.sh --validate`: built (Release, universal) with no warnings from `Source/`, installed, `pluginval` strictness 10 passed. The Standalone launches and opens its editor window (1002 × 668 with the title bar); its screenshot couldn't be taken here (no screen-recording or computer-use access), so `docs/editor.png` is the snapshot rig's render of the same editor (`Tests/fixtures/snapshot/editor.png`). **Live 12.4.16 (checked afterwards, through the bridge and the plug-in window; set `ChordZimmer Phase 3 Check`):** Live had to be quit and relaunched to load the new build (reopening the set kept the Phase 3 binary loaded). The plug-in window (1000 × 659 with Live's title bar) shows the Phase 4 editor, with the test rig's `Progression.txt` on the timeline (Verse, Chorus, Bridge; 3 sections, 9 chords, 8 bars). With a real mouse in Live's window: the palette's Dm7 dragged past the end made *Section 1[Dm7]*, selected, the readout *Dm7 ii7*; Cmd-Z reached the plug-in and undid it (redo lit), and Live's own undo history stayed empty; the Bridge's Dm right edge dragged 45 px became 5 beats (34 beats in all). Play in Preview with Live stopped sounded Drift on the armed *CZ Rec* (meter 0.83), looping the selected chord (the loop region) with the playhead and readout following; Space reached the plug-in (Play off) and didn't start Live. *Sync, Loop, Start bar 51*, a take from beat 200: every chord on its beat (0.000) and of its full length, the edit included (Dm 5 beats at 228, V 1 at 233), wrapping at 234 and again at 268, velocity 127; meters 0 after the stop. *Second pass (with the Phase 5 build, a real mouse in Live's window):* a chord's right-click menu (Inversion ▸ Root/1st/2nd/3rd, Octave ▸, Bass note ▸, Duplicate, Copy, Cut, Paste after greyed, Delete, Rename section…) set Am to 1st inversion (Am/C); a section's (Rename…, Colour ▸ 8 colours, Duplicate, Copy, Cut, Paste after, Delete) set the Chorus to Amber; the palette's (Add at the insertion marker, Add as a new section) added Em7 after G in the Verse (10 chords, 9 bars, scroll bar shown); double-clicking a section tab opened the inline editor and typing renamed it (Live didn't take the keys); Cmd-Z undid each. **Not checked:** Alt-drag copy (the test tool's Alt didn't reach the drag events, so it moved; the code reads `e.mods.isAltDown()` on drag and release — try it by hand); a triad's menu offers *3rd inversion* (worth a look); no *Replace with ▸* yet (§3.5); a host's undo; *Reduce motion*. |
| 5. Match and Chord-from-notes | **Done (2026-11-24)** | **Built:** `core/engine/NoteCollector.h` (Listen's notes with their lengths in beats of listening, the last 256, in the engine like the ChordListener; `noteSetFromCollected` gives the last N beats *of playing*); `core/theory/Matcher.h` (`NoteSet`, `matchScore`, `matchPalette`); `core/model/ChordFromNotes.h` (`chordFromNotes`, `suggestedRoot`); the **Listen** parameter (appended, 27 in all); the processor publishes the collected notes (`collectedNotes`, `clearCollectedNotes`); `ui/MatchPanel` (Notes: Listen, Last 1–16 bars, Use heard, Clear, the 12-key note picker; Matches: 8 ranked chips, ±2 fifths; Chord from notes: Root (Auto), Bass, the name and numeral, Set, Add); the palette's rank badges and fade; a Match button in the footer; `TimelinePanel::replaceSelectedChord`. **Decisions beyond the plan:** (1) *Scoring* (§3.4's formula made concrete, weights normalised to sum 1): chord tones count their weight, ×1.5 the root, ×1.25 the 3rd (the major 3rd when the chord has one: a minor 3rd beside it is a #9); a note a semitone above a chord tone counts −0.75 × its weight; **every colour tone** (anything but root, 3rd, 5th) costs 0.15 whether played or not, so it pays only when held for over 15 % of the phrase (with a cost only for unplayed ones, passing notes pulled in Cadd9 and Cmaj13); −0.01 × tier. Passing notes a tone away count nothing. (2) *Ties:* in key first, then lower tier, then the nearer palette, then column and row; a chord in several palettes or rows counts once. Only positive scores, top 8. (3) *Listen* passes the input through with no chords (like Analyse and Detect), starts afresh each time it's turned on, and isn't saved or in presets. (4) *The window* is the last N bars of playing, ending at the last note's end (or now while one is held), so stopping to look doesn't let the phrase slide away; the bass is the lowest note in it. (5) *Picked notes win:* any picked note makes the picker the note set (1 each); *Use heard* copies the heard notes over to edit them. (6) *Chord from notes* voices the chord as the palette does (root at C3 + its pitch class, the rest above within the octave, up to 8 notes), so D F A on D is the palette's Dm note for note; Auto root is the namer's; a bass other than the root is the chord's bass note (a slash chord); Set keeps the chord's length, inversion and octave and is one undo step with the bass. (7) *Placement:* the Match popover lies over the transport and timeline (two rows: Notes and Chord from notes side by side, Matches under them), not over the palette, so the ranks on the palette stay visible; one popover at a time; clicks in the palette leave it open. **Checks:** `Tests/run_tests.sh`: all 17 binaries passed, including the new `test_matcher` (62 checks: 16 melodies in C major and 4 in other homes (A minor, A harmonic minor, D Dorian) with their best chord; the V7 and vi melodies in all 12 keys; each scoring term, normalisation and the #9; the Csus4/Fsus2 tie by column; borrowing Bb (bVII from F, −1) and C7 with ±2 fifths, C counted once; the collector's lengths, window, silence, held notes, 256-note limit, reset; Listen through the engine with the host stopped (the melody passes through, four 0.9-beat notes, G7); chords from notes equal to the palette's (Dm, G7), named C7#9, Fm(maj7), C(b9), Cadd9/D, C/E; 24576 sets × roots each a named chord on its root). `Tests/run_snapshot.sh`: all passed, the references updated for the footer's Match button and a new `editor-match`; 27 parameters, none without a control but the test rig and Pattern; 63 Info View keys reachable at the default size and 62 at the smallest, with each popover open; Match through `processBlock` (G B D F: passed through, no allocation, G7 first, rank 1 on the palette's G7, Set and Add as undo steps, the picker's Dm, a root of F giving F6, Bb with ±2 fifths, Clear, Listen not saved, one popover at a time). `Tools/install.sh --validate`: built (Release, universal) with no warnings from `Source/`, installed, `pluginval` strictness 10 passed. **Live 12.4.16 (checked afterwards, through the bridge and the plug-in window; set `ChordZimmer Phase 3 Check`):** after a full relaunch the restored ChordZimmer on *CZ Src* still listed its 26 saved parameters (Live keeps a restored plug-in device's parameter list from the set), so a fresh instance went on a new track, *CZ5 Src*, with 27 (29 with Program and Device On), Listen among them; *CZ Rec* (Drift) took MIDI From *CZ5 Src → ChordZimmer*. With Listen and the test rig on, the rig's `Keys.txt` played G B D F (a beat each, 0.9 long) as host MIDI while Live recorded: *CZ Rec* got exactly those four notes at 282–285, 0.9 beats, velocity 100, and no chords. The Match popover in the plug-in window then showed them heard (teal on the picker) and ranked G7, G, Bdim, G9, G7b9, G7#9, Dm6, Bm7b5, with the same badges on the palette as the snapshot's `editor-match`. With a real mouse: picking D, F and A on the strip gave Dm first and *Dm ii* in Chord from notes; with the Verse's Am selected ("Set replaces Am"), Set made it Dm (4 beats kept) and Add put another Dm after it (10 chords, 9 bars). **Not checked:** Listen from a clip on another track (the rig plays host MIDI into the device), Matcher weights against real melodies beyond the hand-made ones (open to tuning, PLAN.md §8). |
| 6. Chord Relations | **Done (2026-11-24; after Phase 7)** | **Built:** `core/model/Relations.h` (`ChordId`, `voiceLeadingDistance`, `relationTo` and `paletteRelations`, `nextChords` with our transition table, `replacementsFor` on top of the inherited `Substitutes.h`); the palette's **Relations** toggle and **Next ▸** strip in its title row (the row grew 4 px), its overlay (amber function tags, common-tone dots, the distance, fading, a ring on the focus); the focus from the palette cell last clicked or the timeline chord selected, else the home tonic; **Replace with ▸** in the timeline's chord menu (`TimelinePanel::replaceChordWith`, one undo step). **Decisions beyond the plan:** (1) *Where:* in `core/model`, not §4.1's `core/theory`, since it builds on `Substitutes.h` and the timeline's chords. (2) *Badges, not arrows* (§3.5 says arrows/badges): a tag per cell reads at the palette's size; arrows across a 7 × 4 grid crossed each other. (3) *Functions:* dominant = a major or dominant-7th chord a 5th above the focus ("V/x", "V7/x", just "V" for the home tonic); subdominant = a major chord without a b7, or a minor one, a 4th above ("IV/x", "iv/x"); relative = the relative minor of a major focus, or the relative major (without a b7) of a minor one; tritone sub = a dominant 7th a semitone above (leading to the focus), or for a dominant focus the 7th a tritone away. A dominant-7th chord is read only as a dominant or a sub (D7b9 a 4th above Am leads to G, it isn't iv/vi): the first hand check caught this, and C7b9 shown as Am's relative. Every row carries functions (Fmaj7, F6 are IV too), not just triads. (4) *Distance:* the fewest semitones moved when every note of the larger chord goes to a note of the smaller and each of those is used (exact, by a small dynamic programme). (5) *Fading:* a chord with no function and under two notes in common fades. (6) *Next ▸:* our table by degree (major homes: I → IV V vi ii iii; V → I vi IV iii ii …; minor homes: i → iv VI VII v III …), the home palette's triads, V as V7 where the palette has a dominant 7th; a secondary dominant suggests its target first, other borrowed chords I IV V vi ii. Click hears, double-click adds at the marker. (7) *Replace with ▸:* the substitutes, then the home palette's chords sharing 2+ notes, nearest first, 12 at most; it keeps the chord's length, inversion, octave and bass. **Checks:** `Tests/run_tests.sh`: all 18 binaries passed, including the new `test_relations` (33 checks, every expectation worked out by hand: distances C→C 0, Am 2, Em 1, F 3, G 3, Dm 5, G7→C 4, G7→Db7 2, C→F# 9; in C major the functions for the focus C (F IV, G V, Am rel, G7 V7, Am7 rel, G7b9/G7#9 V7), Am (C rel, Dm/Dm7 iv/vi, E7b9/E7#9 V7/vi), G7 (C IV/V, Em/Em7 rel, D7b9/D7#9 V7/V), F major's and Ab major's palettes against C (Db7b9 and Db7#9 as subs, Fm iv); in A minor for Am (C rel, Dm iv, E7b9 V7), harmonic minor's E and E7, and C as III; Next after C, G, Dm, G7, E7 (Am first), Bb, and in A minor after Am and Dm; Replace with for C: the seven substitutes in order, then the nearest chords, each once). `Tests/run_snapshot.sh`: all passed (references updated for the palette's title row, a new `editor-relations`): the toggle, the home tonic as the first focus, a palette click (Am) and a timeline selection (G) moving it, Next's double-click adding after G, Replace with turning the Verse's C into Am as one undo step, and Next and Relations reachable in the Info View sweep. `Tools/install.sh --validate`: no warnings from `Source/`, `pluginval` strictness 10 passed. **Live 12.4.16 (plug-in window, real mouse; the saved set `ChordZimmer Phase 7 Check`, after a relaunch for the new build):** the progression came back from the set; Relations on and a click on the palette's Am gave the same overlay as `editor-relations` (Am ringed, C and C6 rel, Dm, Dm6, Dm7 iv/vi, F and F6 with two notes in common, the rest faded) and Next Dm F G7 Em C; right-clicking the Verse's C moved the focus to it (F IV, G V, Next F G7 Am Dm Em) and *Replace with ▸* listed the twelve of `test_relations` in order (Am a 3rd below … Fsus2 and Caug 1 semitone apart … Am7 and Gsus4); choosing Am replaced it (1 bar kept) and Cmd-Z in the plug-in window undid it. **Not checked:** the transition table against real progressions (it is ours, and open to tuning). |
| 7. Export and Live check | **Done (2026-11-24; before Phase 6)** | Done ahead of Phase 6, since Phase 4 had left no way to drag the progression out. **Built:** `PatternRender.h`'s `exportProgression` (the selection's parts as sections, or everything), `exportName` and `renderSelection` (the render with each section's name as a marker at its start); markers in `MidiFile.h` (meta-event 6 on the tempo track, read back too); the processor's `writeSelectionExport` (in place of `writePatternExport`, which had no control since Phase 4); `ui::SelectionDragHandle`, "Drag MIDI: …", at the right of the timeline's title row (the row grew 4 px). **Decisions beyond the plan:** (1) *Where:* the handle is in the timeline's title row, next to what it exports, not the footer's bottom right (§3.7, §5): the footer has no room left at the smallest size, and the label says what the selection holds ("Drag MIDI: Chorus, 4 bars", "all, 8 bars"). (2) *What:* the selection's parts back to back in progression order (`copy`), so two separate sections make one clip without the gap between them; nothing selected (or nothing valid) is everything. A contiguous selection's export equals what plays with it as the loop region. Rendered from the clip's own start, so Smooth voice leading starts afresh. (3) *Name:* the sections in order (a repeat back to back once; four, then "+N"), or the chord when it is one, then "_" and the key with "m" for a minor-3rd scale; "/" becomes " over "; Alt adds " chords" (no strum). The clip in the host takes the same name. (4) *Listen is saved now* (not in presets): Phase 5 left it out of the state like Analyse, so a restore kept whatever value it had, and pluginval's state-restoration test failed on it (by chance in Phase 5's run it passed). Analyse and Detect stay unsaved. **Checks:** `Tests/run_tests.sh`: all 17 binaries passed; `test_render` (25 checks) adds what a drag-out holds (nothing, a section, chords from two sections, a chord and a section, an invalid selection), the names (all, one chord, A minor, seven sections, a slash chord), markers at 0/16/24 beats through the file and back, an empty progression, and four selections (the Chorus with Root + 5th; two chords strummed, Spread; everything with Smooth voice leading; Chorus and Bridge with House velocity) equal to what a host records with them as the loop region at 44.1/48 kHz and 64/512 blocks, to the tick. `Tests/run_snapshot.sh`: all passed (references updated for the title row); Drag MIDI on the real editor: an empty timeline refuses, everything writes `Verse-Chorus-Bridge_C.mid` (the render, Bass included, three markers), the Chorus `Chorus_C.mid` exactly, one chord `Bb_C chords.mid` with Alt, and the Info View says what it holds; Listen restores with the state, on and off. `Tools/install.sh --validate`: no warnings from `Source/`, `pluginval` strictness 10 passed (after the Listen fix; it had failed with "Listen not restored on setStateInformation"). **Live 12.4.16 (bridge and plug-in window; new set `ChordZimmer Phase 7 Check`: *CZ Src* ChordZimmer, *CZ Rec* Drift from it, *Clips* Drift; the rig's `Progression.txt` loaded, then Test workspace off):** *Drag-out:* the handle dragged with the mouse onto the Clips lane made an arrangement clip `Verse-Chorus-Bridge_C`, 32 beats, whose 27 notes equal the file's (pitch, start, length, velocity 127); Live asked whether to import the file's tempo and meter (the user answered). *Recorded:* a Sync take on *CZ Rec* gave every chord on the file's beat (0.000) with the file's length, wrapping at 32. *Save and reopen:* after Cmd-S the `.als` holds ChordZimmer's state with the whole `czp1` progression; after opening another set and this one again (fresh undo history, Test workspace off, 29 parameters with Listen) a Sync take played C, Am at 36, F at 40, G at 42 and the Chorus's F at 44, the saved progression. **Not checked:** dragging to the Finder; markers inside Live (Live may ignore them); a drag in Session view. |
| 8. Hardening and install | **Done (2026-11-24)** | **Built:** the **benchmark** back (`--bench` in the snapshot app, `Tests/run_bench.sh`: 8 instances run one after another as a host's audio thread runs them, six scenarios × 32/128/512-sample blocks at 44.1 kHz); ChordMonkey's **hardening checks** back in the snapshot test, made ChordZimmer's (section 14); **factory templates** (`FactoryPreset::sections`, `templateProgressionText`, a `PROGRESSION` child in `PresetManager::factoryTree`): a "Templates" category of 8 after the 11 existing presets; **CI** steps for the snapshot test (`--no-pixels`) and the benchmark, with `Tests/snapshot_build.sh` building the snapshot app from a copy of its `.jucer` pointed at another JUCE (`Builds/CI`, ignored); the **README** rewritten (features, build and tests, Live, notes); the screenshot (`docs/editor.png`) is the snapshot rig's render, current since Phase 6. **Decisions beyond the plan:** (1) *Templates are presets* with a progression (§2: "a few factory template progressions on the presets menu"), so they load like any preset (key, scale, the progression; the undo history starts again) and appear in the browser and as host programs 11–18; programs 0–10 keep their numbers. Written as numerals in the key, read with ChordSets.h's parser, with lengths up to 16 bars (a workspace slot stops at 8 beats). The progressions are common ones (four chords, axis, ii–V–I, 12-bar blues, Andalusian cadence, the Canon's ground, the fifties, a Dorian vamp), not copied from anyone's presets. (2) *Benchmark scenarios* for ChordZimmer: idle; the timeline in Sync; everything on (Thirteenths, Stack +1&2, House velocity, Hold Bar, voice leading, Wide, Root + 5th, strum, the timeline playing and a tune coming in); Preview with the host stopped; Listen and Detect with a tune coming in. (3) *Hardening* adds the timeline to ChordMonkey's checks: edits, undos, loop selections and previews between blocks; a damaged `PROGRESSION`, `WORKSPACE` and palette/snap/zoom UI state; the host's all-notes-off while Preview plays. (4) *Versioning* stays 0.0.1 and the CHANGELOG "Unreleased": a release isn't part of the plan's Phase 8. **Checks:** `Tests/run_tests.sh`: all 18 binaries passed. `Tests/run_snapshot.sh`: all passed, including the templates (19 programs; each template's key, scale and progression, e.g. Two-Five-One in Bb: A[Cm7:4 F7:4 Bbmaj7:8] Turnaround[Dm7:2 G7:2 Cm7:2 F7:2], Twelve-Bar Blues in A Mixolydian: A7:16 D7:8 A7:8 E7:4 D7:4 A7:4 E7:4; history reset; a preset without one leaves the timeline) and the hardening: 8000 blocks of two instances with everything on and the timeline playing (61 edits, 152 bypassed, up to 65 events a block), 0 allocations in `processBlock`; 300 random blobs and 9 states of junk values with a broken progression, workspace and UI: parameters in range, the progression empty, the editor opening, the instance playing; a NaN Key in a preset loads C; zero-length, 8192-sample and mono blocks, bypass, all-notes-off in Preview (the CC passed on), releaseResources and a rate change, SysEx intact, 2900 note-ons with 128 key-ups in one block; 64 instances. The same with `JUCE` pointing elsewhere (CI's path, `Builds/CI`, `--no-pixels`): all passed. `Tests/run_bench.sh 20`: every scenario under 1% (worst mean 0.196%, Listen at 32 samples; 512-sample blocks 0.005–0.014%; the worst single block 6.7% of its duration, idle at 32 samples). `Tools/install.sh --validate`: no warnings from `Source/`, `pluginval` strictness 10 passed. **Live 12.4.16 (checked afterwards; the saved set `ChordZimmer Phase 7 Check`, after a relaunch for the new build):** the device's Program parameter lists the templates by name; setting it to *Two-Five-One* (program 13) set Key Bb, Major and Smooth voice leading and loaded the progression, and a Sync take from beat 44 (the template's beat 20) recorded Cm7 at 44 and F7 at 46 (voice-led, C Eb F A), the wrap to Cm7 at 48, F7 at 52 and Bbmaj7 at 56, each on its beat and of its length. In the plug-in window the preset reads *Templates / Two-Five-One* and the timeline A[Cm7 F7 Bbmaj7] Turnaround[Dm7 G7 Cm7 F7]; the browser lists the Templates category after Vibe, and choosing *Twelve-Bar Blues* loaded A Mixolydian and the 12-bar Blues section (D7 and E7 drawn as rings: their C and G# aren't in A Mixolydian), the undo history cleared. **Not checked:** the CI workflow on GitHub (nothing is pushed; its new steps ran locally only with JUCE at another path); a benchmark in Live itself (its CPU meter). |
