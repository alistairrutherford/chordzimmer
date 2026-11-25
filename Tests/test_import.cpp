// MIDI import tests (PLAN.md §6 Phase 9: "import tests on fixtures"). The fixtures in
// Tests/fixtures/import are written byte by byte by Tools/gen_import_fixtures.py (type 0 and 1
// files, 96/480/960 ppq, running status, note-on velocity 0, markers), not by ChordZimmer's own
// writer. Expected chords and lengths are written out by hand. Also: a Drag MIDI export read
// back (sections from its markers), the limits, and files that aren't MIDI.
#include "TestUtil.h"
#include "core/midiio/MidiImport.h"
#include "core/midiio/PatternRender.h"
#include "core/model/ChordSets.h"
#include <fstream>
#include <iterator>

using namespace chordzimmer;
using testutil::check;

namespace
{
    std::string fixtures()
    {
        const char* dir = std::getenv ("CHORDZIMMER_FIXTURES");
        return std::string (dir != nullptr ? dir : "Tests/fixtures") + "/import/";
    }

    MidiImport load (const char* file, int key = 0, Scale scale = Scale::Major)
    {
        std::ifstream in (fixtures() + file, std::ios::binary);
        const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
        return importMidiFile (bytes.data(), bytes.size(), key, scale, "Imported");
    }

    // "Verse[C:4 G:4]": names in C major, lengths in beats.
    std::string layout (const ChordProgression& p, int key = 0, Scale scale = Scale::Major)
    {
        std::string s;
        for (const auto& sec : p.sections)
        {
            s += (s.empty() ? "" : " ") + sec.name + "[";
            for (size_t i = 0; i < sec.chords.size(); ++i)
            {
                std::string beats = std::to_string (sec.chords[i].beats());
                beats.erase (beats.find_last_not_of ('0') + 1);
                if (! beats.empty() && beats.back() == '.')
                    beats.pop_back();
                s += (i ? " " : "") + timelineChordLabel (sec.chords[i], key, scale).name + ":" + beats;
            }
            s += "]";
        }
        return s;
    }

    std::string best (const NoteSet& n, int key, Scale scale)
    {
        PaletteContext c;
        c.paletteKey = c.homeKey = key;
        c.paletteScale = c.homeScale = scale;
        const auto m = matchPalette (n, c);
        return m.empty() ? "-" : m.front().cell.name;
    }
}

static void testFixtures()
{
    std::cout << "== fixtures ==\n";
    auto r = load ("block_chords.mid");
    check (r.ok() && layout (r.chords) == "Imported[C:4 G:4 Am:4 F:4]" && r.numNotes == 12,
           "block chords (type 1, 480 ppq): C G Am F, 4 beats each, one section named after the file (" + layout (r.chords) + ")");
    check (r.melodyNotes == 12 && r.melody.weight[0] > 0.0, "no notes outside the chords: the melody is all of them");

    r = load ("chords_melody.mid");
    check (r.ok() && layout (r.chords) == "Imported[C:4 Am:4 F:4 G:4]", "chords with a melody on another track: the chords only (" + layout (r.chords) + ")");
    check (r.melodyNotes == 11 && r.melody.weight[0] == 5.0 && r.melody.weight[9] == 4.0 && r.melody.bass == 7,
           "the melody: all 11 notes, those struck with a chord (over an octave above it) included (C 5 beats, A 4, the bass G)");
    check (best (r.melody, 0, Scale::Major) == "Am", "and Match finds Am for it (" + best (r.melody, 0, Scale::Major) + ")");

    r = load ("rolled.mid", 0, Scale::Major);
    check (r.ok() && layout (r.chords) == "Imported[Dm7:4 G7:4 Cmaj7:8]", "rolled chords (type 0, 96 ppq, 90 BPM, running status): Dm7 G7 Cmaj7 (" + layout (r.chords) + ")");

    r = load ("rhythm.mid");
    check (r.ok() && layout (r.chords) == "Imported[C:4 F:4]", "C comped in eighths, F in quarters: one chord each, the repeats merged (" + layout (r.chords) + ")");

    r = load ("inversions.mid");
    const auto inv = layout (r.chords);
    check (r.ok() && inv.rfind ("Imported[C/E:2 ", 0) == 0 && inv.find ("/G:2") != std::string::npos && inv.find ("F/C:2]") != std::string::npos,
           "inversions and a bass: C/E, A minor over G, F/C (" + inv + ")");
    const auto& c = r.chords.sections[0].chords[0];
    check (progressionChordNotes (c).notes[0] == 52, "C/E keeps its register: E3 lowest");

    r = load ("melody_only.mid", 7, Scale::Major);
    check (r.ok() && r.chords.empty() && r.melodyNotes == 10 && r.melody.weight[7] == 6.0 && r.melody.bass == 7,
           "a melody alone: no chords; its notes for Match (G 6 beats, the bass G)");
    // The tune dwells on A (3 of its 15 beats, over the 15% a colour tone needs): Gadd9.
    check (best (r.melody, 7, Scale::Major) == "Gadd9", "Match finds Gadd9 for it in G major: its A is long enough (" + best (r.melody, 7, Scale::Major) + ")");

    r = load ("sections.mid");
    check (r.ok() && layout (r.chords) == "Verse[C:4 G:4] Chorus[F:4 G:4]", "markers start sections named after them (" + layout (r.chords) + ")");

    r = load ("rests.mid");
    check (r.ok() && layout (r.chords) == "Imported[C:4 G:4]", "a lead-in is dropped; a rest belongs to the chord before (" + layout (r.chords) + ")");

    r = load ("band_drums.mid");
    check (r.ok() && layout (r.chords) == "Imported[Am:4 F:4 C:4 G:4]" && r.drumNotes == 48 && r.numNotes == 12,
           "a piano and drums on channel 10: the piano's Am F C G, the 48 drum notes left out (" + layout (r.chords) + ")");

    r = load ("not_midi.mid");
    check (! r.ok() && r.chords.empty() && r.melody.empty() && r.error == "not a MIDI file", "a text file: an error, nothing imported");
    check (! importMidiFile (nullptr, 0, 0, Scale::Major, "x").ok(), "no data: an error");
}

static void testRoundTrip()
{
    std::cout << "== Drag MIDI, read back ==\n";
    // A progression exported as Drag MIDI does it (markers included) comes back the same:
    // sections, chords, lengths.
    ChordProgression p;
    const auto section = [] (const char* name, const char* numerals)
    {
        Pattern pattern;
        std::string error;
        parseProgression (numerals, 0, pattern, error);
        return progressionFromPattern (pattern, name).sections[0];
    };
    p.sections = { section ("Verse", "I vi:2 IV:2 V7"), section ("Chorus", "IV:4 V:4 iii7:2 vi:6"), section ("Bridge", "ii7:3 V7:1") };
    EngineSettings s;
    const auto bytes = writeMidiFile (renderSelection (p, {}, s, 120.0));
    const auto r = importMidiFile (bytes.data(), bytes.size(), 0, Scale::Major, "Imported");
    check (r.ok() && layout (r.chords) == layout (p), "Verse, Chorus and Bridge come back as exported (" + layout (r.chords) + ")");
    check (r.chords.sections[1].chords[3].pitchClasses() == p.sections[1].chords[3].pitchClasses()
               && r.chords.sections[0].chords[3].root() == p.sections[0].chords[3].root(),
           "with the same notes");

    // With a bass and an inversion in the export, the import names them the same.
    auto q = p;
    q.sections[0].chords[0].inversion = 1;
    q.sections[0].chords[1].bass = 7;
    const auto bytes2 = writeMidiFile (renderSelection (q, {}, s, 120.0));
    const auto r2 = importMidiFile (bytes2.data(), bytes2.size(), 0, Scale::Major, "Imported");
    check (r2.ok() && layout (r2.chords).rfind ("Verse[C/E:4 Am7/G:2 ", 0) == 0, "an inversion and a bass come back as C/E and Am7/G (" + layout (r2.chords).substr (0, 30) + ")");
}

static void testLimits()
{
    std::cout << "== limits ==\n";
    // 300 chords of alternating C and G, a beat each: 256 come in, in sections of 64.
    MidiSong song;
    song.ppq = 480;
    for (int i = 0; i < 300; ++i)
    {
        const int root = i % 2 == 0 ? 48 : 43;
        for (int k : { 0, 4, 7 })
            song.notes.push_back ({ (int64_t) i * 480, (int64_t) i * 480 + 470, (uint8_t) (root + k), 90, 0 });
    }
    const auto r = importMidi (song, 0, Scale::Major, "Long");
    check (r.chords.numChords() == maxProgressionChords && r.droppedChords == 300 - maxProgressionChords && r.chords.numSections() == 4
               && r.chords.sections[1].name == "Long 2" && r.chords.sections[0].chords.size() == (size_t) maxSectionChords,
           "300 chords: 256 in four sections of 64 (Long, Long 2 …), 44 dropped");

    // A chord held for 40 bars: cut at 16.
    MidiSong held;
    held.ppq = 96;
    for (int k : { 0, 4, 7 })
        held.notes.push_back ({ 0, 160 * 96, (uint8_t) (48 + k), 90, 0 });
    const auto h = importMidi (held, 0, Scale::Major, "Held");
    check (h.chords.numChords() == 1 && h.chords.sections[0].chords[0].ticks == maxChordTicks, "a chord 40 bars long: 16 bars");

    // Two notes a fifth apart count (a power chord); a third doesn't.
    MidiSong two;
    two.ppq = 96;
    two.notes = { { 0, 384, 48, 90, 0 }, { 0, 384, 55, 90, 0 }, { 384, 768, 48, 90, 0 }, { 384, 768, 52, 90, 0 } };
    const auto t = importMidi (two, 0, Scale::Major, "Two");
    check (layout (t.chords) == "Two[C5:4]" && t.melodyNotes == 2, "C and G: a power chord, the last, to its own release; C and E after it: melody (" + layout (t.chords) + ")");

    // Drums on channel 10 (kick, snare, hats struck together on every beat) are left out:
    // the chords are the piano's C and G, not a drum "chord", and the melody has no drums in it.
    MidiSong band;
    band.ppq = 96;
    for (int beat = 0; beat < 8; ++beat)
    {
        for (int k : { 36, 38, 42, 46 })
            band.notes.push_back ({ (int64_t) beat * 96, (int64_t) beat * 96 + 20, (uint8_t) k, 100, 9 });
        if (beat % 4 == 0)
            for (int k : { 0, 4, 7 })
                band.notes.push_back ({ (int64_t) beat * 96, (int64_t) beat * 96 + 380, (uint8_t) ((beat == 0 ? 48 : 43) + k), 90, 0 });
    }
    std::stable_sort (band.notes.begin(), band.notes.end(), [] (const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
    const auto b = importMidi (band, 0, Scale::Major, "Band");
    check (layout (b.chords) == "Band[C:4 G:4]" && b.drumNotes == 32 && b.numNotes == 6 && b.melody.empty() == false,
           "drums on channel 10 are left out: Band[C:4 G:4], 32 drum notes counted (" + layout (b.chords) + ", drums " + std::to_string (b.drumNotes) + ")");
}

int main()
{
    testFixtures();
    testRoundTrip();
    testLimits();
    return testutil::finish ("test_import");
}
