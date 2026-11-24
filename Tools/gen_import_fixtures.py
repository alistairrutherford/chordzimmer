#!/usr/bin/env python3
"""Writes the MIDI import fixtures (PLAN.md §6 Phase 9) to Tests/fixtures/import.

Plain Python, no packages: the files are written byte by byte here, independently of
ChordZimmer's own MIDI writer, so the import tests don't only read back what we write.
Run from anywhere:  python3 Tools/gen_import_fixtures.py

  block_chords.mid    type 1, 480 ppq, 120 BPM: C G Am F, 4 beats each, held
  chords_melody.mid   type 1, 960 ppq, a chords track (C Am F G) and a melody track over it
  rolled.mid          type 0, 96 ppq, 90 BPM, running status: Dm7 G7 Cmaj7 (4, 4, 8 beats),
                      each strummed upwards over about 30 ms
  rhythm.mid          type 1, 480 ppq: C comped in eighths for a bar, F in quarters for a bar
  inversions.mid      type 1, 480 ppq: C/E, Am/G, F/C, 2 beats each
  melody_only.mid     type 1, 480 ppq: a tune in G major, no chords
  sections.mid        type 1, 960 ppq, markers "Verse" at 0 and "Chorus" at beat 8 (as ChordZimmer
                      exports them): Verse C G, Chorus F G (4 beats each)
  rests.mid           type 1, 480 ppq: 2 beats of silence, C for 2 beats, 2 beats' rest, G for 4
  not_midi.mid        text, not a MIDI file
"""
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Tests", "fixtures", "import")


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def track(events, running_status=False):
    """events: (tick, bytes) in any order; written sorted, offs before ons at one tick."""
    events = sorted(events, key=lambda e: (e[0], 0 if (e[1][0] & 0xF0) == 0x80 or (len(e[1]) > 2 and (e[1][0] & 0xF0) == 0x90 and e[1][2] == 0) else 1))
    data, now, last_status = b"", 0, None
    for tick, msg in events:
        data += varlen(tick - now)
        now = tick
        if running_status and msg[0] < 0xF0 and msg[0] == last_status:
            data += msg[1:]
        else:
            data += msg
            last_status = msg[0] if msg[0] < 0xF0 else None
    data += varlen(0) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + data


def tempo(bpm):
    us = round(60_000_000 / bpm)
    return b"\xff\x51\x03" + us.to_bytes(3, "big")


def meta(kind, text):
    raw = text.encode()
    return bytes([0xFF, kind]) + varlen(len(raw)) + raw


def notes(ppq, spec, channel=0, velocity=90, off_as_zero=False):
    """spec: (start beats, length beats, [pitches], spread ticks per note)."""
    ev = []
    for start, length, pitches, spread in spec:
        for i, p in enumerate(pitches):
            on = round(start * ppq) + i * spread
            off = round((start + length) * ppq)
            ev.append((on, bytes([0x90 | channel, p, velocity])))
            ev.append((off, bytes([0x90 | channel, p, 0]) if off_as_zero else bytes([0x80 | channel, p, 0])))
    return ev


def write(name, fmt, ppq, tracks):
    head = b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), ppq)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(head + b"".join(tracks))


C3, E3, G3 = 48, 52, 55
os.makedirs(OUT, exist_ok=True)

# Block chords: C G Am F.
ppq = 480
write("block_chords.mid", 1, ppq, [
    track([(0, tempo(120)), (0, meta(0x58 - 0x55, ""))][:1] + [(0, b"\xff\x58\x04\x04\x02\x18\x08")]),
    track(notes(ppq, [(0, 4, [48, 52, 55], 0), (4, 4, [43, 47, 50], 0), (8, 4, [45, 48, 52], 0), (12, 4, [41, 45, 48], 0)])),
])

# Chords with a melody on top.
ppq = 960
melody = [(0, 1, [76], 0), (1, 1, [74], 0), (2, 2, [72], 0), (4, 1, [72], 0), (5, 1, [71], 0), (6, 2, [69], 0),
          (8, 2, [69], 0), (10, 2, [72], 0), (12, 1, [74], 0), (13, 1, [71], 0), (14, 2, [67], 0)]
write("chords_melody.mid", 1, ppq, [
    track([(0, tempo(100))]),
    track([(0, meta(0x03, "Chords"))] + notes(ppq, [(0, 4, [48, 52, 55], 0), (4, 4, [45, 48, 52], 0), (8, 4, [41, 45, 48], 0), (12, 4, [43, 47, 50], 0)])),
    track([(0, meta(0x03, "Melody"))] + notes(ppq, melody, channel=1, velocity=100)),
])

# Rolled chords, type 0, running status, note-on velocity 0 as note-off.
ppq = 96
write("rolled.mid", 0, ppq, [
    track([(0, tempo(90))] + notes(ppq, [(0, 4, [50, 53, 57, 60], 1), (4, 4, [43, 47, 50, 53], 1), (8, 8, [48, 52, 55, 59], 1)],
                                   off_as_zero=True), running_status=True),
])

# A rhythm: C in eighths (each 0.4 long), F in quarters (0.8 long).
ppq = 480
spec = [(i * 0.5, 0.4, [48, 52, 55], 0) for i in range(8)] + [(4 + i, 0.8, [41, 45, 48], 0) for i in range(4)]
write("rhythm.mid", 1, ppq, [track([(0, tempo(120))]), track(notes(ppq, spec))])

# Inversions and a bass under the chord.
write("inversions.mid", 1, ppq, [
    track([(0, tempo(120))]),
    track(notes(ppq, [(0, 2, [52, 55, 60], 0), (2, 2, [43, 57, 60, 64], 0), (4, 2, [48, 53, 57, 60], 0)])),
])

# A melody, no chords: in G major, the G, B and D long.
tune = [(0, 2, [67], 0), (2, 1, [69], 0), (3, 1, [71], 0), (4, 2, [74], 0), (6, 1, [72], 0), (7, 1, [71], 0),
        (8, 2, [69], 0), (10, 1, [71], 0), (11, 1, [74], 0), (12, 4, [67], 0)]
write("melody_only.mid", 1, ppq, [track([(0, tempo(120))]), track(notes(ppq, tune))])

# Sections as markers (as ChordZimmer's Drag MIDI writes them).
ppq = 960
write("sections.mid", 1, ppq, [
    track([(0, tempo(120)), (0, meta(0x06, "Verse")), (8 * ppq, meta(0x06, "Chorus"))]),
    track(notes(ppq, [(0, 4, [48, 52, 55], 0), (4, 4, [43, 47, 50], 0), (8, 4, [41, 45, 48], 0), (12, 4, [43, 47, 50], 0)])),
])

# Rests: a lead-in, then a gap between two chords.
ppq = 480
write("rests.mid", 1, ppq, [track([(0, tempo(120))]), track(notes(ppq, [(2, 2, [48, 52, 55], 0), (6, 4, [43, 47, 50], 0)]))])

with open(os.path.join(OUT, "not_midi.mid"), "wb") as f:
    f.write(b"This is not a MIDI file.\n")
print("wrote", sorted(os.listdir(OUT)))
