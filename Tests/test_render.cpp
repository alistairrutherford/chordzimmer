// Render tests (ChordMonkey's PLAN.md §3.6, §7; Phase 6, G9; PLAN.md §3.7): a pattern's drag-out MIDI (now
// rendered as a one-section progression, Phase 3) is the notes the engine plays from the same
// timeline under a host (FakeHost.h), to the tick: chords only, with bass and
// voicing, strummed, voice-led, at other tempos and meters; the file round-trips through
// the MIDI writer and reader; an empty pattern renders nothing.
#include "FakeHost.h"
#include "core/midiio/PatternRender.h"
#include "core/model/ChordSets.h"

using namespace fakehost;
using testutil::check;

namespace
{
    Pattern progression (const std::string& t, int key = 0)
    {
        Pattern p;
        std::string error;
        parseProgression (t, key, p, error);
        return p;
    }

    // The notes a host records from the pattern over one loop, as a song (pairs on/off).
    MidiSong hosted (const Workspace& ws, int index, EngineSettings s, double bpm, double rate, int block, int num = 4, int den = 4)
    {
        Host h (rate, bpm);
        h.numerator = num;
        h.denominator = den;
        h.engine->setTimeline (makeTimeline (progressionFromPattern (ws.patterns[(size_t) index])));
        s.play = true;
        h.settings = s;
        const double beats = ws.patterns[(size_t) index].totalHalfBeats() * 0.5;
        h.run (h.at (beats), block);
        h.stop (block);
        MidiSong song;
        std::vector<std::vector<size_t>> open (128);
        const int64_t last = (int64_t) std::llround (beats * 960);
        for (const auto& hit : h.hits)
        {
            const auto tick = (int64_t) std::llround ((double) hit.sample / h.spq() * 960);
            if (hit.on && tick >= last)
                continue;                 // a step on the loop's end belongs to the next loop
            if (hit.on)
            {
                open[(size_t) hit.note].push_back (song.notes.size());
                song.notes.push_back ({ tick, -1, (uint8_t) hit.note, (uint8_t) hit.velocity, 0 });
            }
            else if (! open[(size_t) hit.note].empty())
            {
                song.notes[open[(size_t) hit.note].front()].end = std::min (tick, last);
                open[(size_t) hit.note].erase (open[(size_t) hit.note].begin());
            }
        }
        std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return song;
    }

    // The same notes within `tolerance` ticks (a sample's rounding at another rate and block).
    bool same (const MidiSong& a, const MidiSong& b, int64_t tolerance, std::string& why)
    {
        if (a.notes.size() != b.notes.size())
        {
            why = std::to_string (a.notes.size()) + " vs " + std::to_string (b.notes.size()) + " notes";
            return false;
        }
        for (size_t i = 0; i < a.notes.size(); ++i)
        {
            const auto& x = a.notes[i];
            const auto& y = b.notes[i];
            if (x.pitch != y.pitch || x.velocity != y.velocity || std::llabs (x.start - y.start) > tolerance || std::llabs (x.end - y.end) > tolerance)
            {
                why = "note " + std::to_string (i) + ": " + std::to_string (x.pitch) + "@" + std::to_string (x.start) + "-" + std::to_string (x.end)
                    + " v" + std::to_string (x.velocity) + " vs " + std::to_string (y.pitch) + "@" + std::to_string (y.start) + "-"
                    + std::to_string (y.end) + " v" + std::to_string (y.velocity);
                return false;
            }
        }
        return true;
    }
}

static void testChordsOnly()
{
    std::cout << "== chords only ==\n";
    Workspace ws;
    ws.patterns[1] = progression ("I V vi:2 IV:6");
    EngineSettings s;
    s.fx.strum = StrumMode::Up;                     // left out
    const auto song = renderPattern (ws, 1, s, 120.0, 4, 4, true);
    struct N { int64_t start, end; int pitch; };
    const std::vector<N> want {
        { 0, 3840, 48 }, { 0, 3840, 52 }, { 0, 3840, 55 },
        { 3840, 7680, 55 }, { 3840, 7680, 59 }, { 3840, 7680, 62 },
        { 7680, 9600, 57 }, { 7680, 9600, 60 }, { 7680, 9600, 64 },
        { 9600, 15360, 53 }, { 9600, 15360, 57 }, { 9600, 15360, 60 },
    };
    bool ok = song.notes.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); ++i)
        ok = song.notes[i].start == want[i].start && song.notes[i].end == want[i].end && song.notes[i].pitch == want[i].pitch && song.notes[i].velocity == s.fx.velocity.peak;
    check (ok, "B = I V vi:2 IV:6, chords only: C G Am F held for 4, 4, 2 and 6 beats, ticks exact, at Velocity peak (" + std::to_string (song.notes.size()) + " notes)");
    check (song.bpm == 120.0 && song.ppq == 960 && song.name == "ChordZimmer", "the song's tempo, resolution and name");
    check (renderPattern (ws, 0, s, 120.0).notes.empty() && renderPattern (ws, 7, s, 120.0).notes.empty(), "an empty pattern, or no such pattern, renders nothing");
}

static void testMatchesTheHost()
{
    std::cout << "== the render is what the host plays ==\n";
    Workspace ws;
    ws.patterns[0] = progression ("I V vi IV");
    ws.patterns[2] = progression ("ii7:2 V7:2 Imaj7:6 VI7:2", 3);
    struct Case { const char* what; int pattern; double bpm; int num, den; EngineSettings s; };
    std::vector<Case> cases;
    {
        EngineSettings s;
        s.fx.bass = BassMode::RootFifth;
        s.fx.voicing = VoicingMode::Open;
        cases.push_back ({ "Root + 5th, Open", 0, 120.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.fx.strum = StrumMode::Up;
        s.fx.strumRate = 4;
        s.fx.velocity.curve = VelocityCurve::House;
        cases.push_back ({ "Strummed, House velocity", 2, 97.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.fx.voiceLeading = VoiceLeadingMode::Smooth;
        s.fx.stack = StackMode::PlusOne;
        cases.push_back ({ "Smooth voice leading, Stack +1", 0, 140.0, 4, 4, s });
    }
    {
        EngineSettings s;
        s.chordOctave = 1;
        cases.push_back ({ "3/4, Chord Octave +1", 0, 120.0, 3, 4, s });
    }
    for (const auto& c : cases)
    {
        const auto rendered = renderPattern (ws, c.pattern, c.s, c.bpm, c.num, c.den);
        bool all = true;
        std::string why;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 333, 2048 })
            {
                const MidiSong host = hosted (ws, c.pattern, c.s, c.bpm, rate, block, c.num, c.den);
                // A sample at 44.1 kHz is under half a tick at these tempos; allow one tick for rounding.
                if (! same (rendered, host, 1, why))
                {
                    all = false;
                    why = std::to_string ((int) rate) + "/" + std::to_string (block) + ": " + why;
                    break;
                }
            }
        check (all && ! rendered.notes.empty(), std::string (c.what) + ": " + std::to_string (rendered.notes.size())
                                                    + " notes, the same as a host records at 44.1/48/96 kHz and 64/333/2048-sample blocks, to the tick"
                                                    + (all ? "" : " (" + why + ")"));
    }

    const auto song = renderPattern (ws, 2, cases[1].s, 97.0);
    const auto bytes = writeMidiFile (song);
    MidiSong back;
    std::string error;
    const bool read = readMidiFile (bytes.data(), bytes.size(), back, error);
    std::string why;
    check (read && same (song, back, 0, why) && std::abs (back.bpm - 97.0) < 0.01, "the .mid reads back: the same notes and tempo" + (why.empty() ? "" : " (" + why + ")"));

    // Both tracks carry the name (a host names the dropped clip after the notes track).
    auto named = song;
    named.name = "ChordZimmer C Eb 4 bars";
    const auto file = writeMidiFile (named);
    const std::string meta = std::string ("\xff\x03", 2) + (char) named.name.size() + named.name;
    const std::string text (file.begin(), file.end());
    const auto first = text.find (meta);
    const auto second = first == std::string::npos ? first : text.find (meta, first + 1);
    const auto notesTrack = text.find ("MTrk", text.find ("MTrk") + 4);
    check (first != std::string::npos && second != std::string::npos && notesTrack != std::string::npos && first < notesTrack && second > notesTrack,
           "the name is in both tracks: the tempo track and the notes track");
}

//==============================================================================
// Phase 7: the selection's drag-out (PLAN.md §3.7, G8).

namespace
{
    ProgressionSection sectionOf (const char* name, const char* numerals, int key = 0)
    {
        auto p = progressionFromPattern (progression (numerals, key), name);
        return p.sections[0];
    }

    // Verse I V vi IV · Chorus IV:2 V:2 I:4 · Bridge ii7:3 V7:1 (16 + 8 + 4 = 28 beats)
    ChordProgression song3()
    {
        ChordProgression p;
        p.sections = { sectionOf ("Verse", "I V vi IV"), sectionOf ("Chorus", "IV:2 V:2 I:4"), sectionOf ("Bridge", "ii7:3 V7:1") };
        return p;
    }

    // What a host records over one pass of the loop region (the selection) in Sync with Loop,
    // from song position 0 (where the region starts).
    MidiSong hostedSelection (const ChordProgression& p, const Selection& sel, EngineSettings s, double bpm, double rate, int block)
    {
        Host h (rate, bpm);
        h.engine->setTimeline (makeTimeline (p, sel));
        s.play = true;
        s.loop = true;
        h.settings = s;
        const double beats = p.range (sel).length() / (double) ticksPerBeat;
        h.run (h.at (beats), block);
        h.stop (block);
        MidiSong song;
        std::vector<std::vector<size_t>> open (128);
        const int64_t last = (int64_t) std::llround (beats * 960);
        for (const auto& hit : h.hits)
        {
            const auto tick = (int64_t) std::llround ((double) hit.sample / h.spq() * 960);
            if (hit.on && tick >= last)
                continue;
            if (hit.on)
            {
                open[(size_t) hit.note].push_back (song.notes.size());
                song.notes.push_back ({ tick, -1, (uint8_t) hit.note, (uint8_t) hit.velocity, 0 });
            }
            else if (! open[(size_t) hit.note].empty())
            {
                song.notes[open[(size_t) hit.note].front()].end = std::min (tick, last);
                open[(size_t) hit.note].erase (open[(size_t) hit.note].begin());
            }
        }
        std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return song;
    }

    std::string sectionsOf (const ChordProgression& p)
    {
        std::string out;
        for (const auto& sec : p.sections)
            out += (out.empty() ? "" : " ") + sec.name + ":" + std::to_string (sec.chords.size());
        return out;
    }
}

static void testSelectionExport()
{
    std::cout << "== the selection's drag-out ==\n";
    const auto p = song3();
    Selection two;
    two.chords = { { 0, 3 }, { 2, 0 } };
    Selection mixed;
    mixed.sections = { 1 };
    mixed.chords = { { 0, 0 } };
    check (exportProgression (p, {}) == p && sectionsOf (exportProgression (p, Selection::section (1))) == "Chorus:3",
           "nothing selected: everything; the Chorus selected: the Chorus");
    check (sectionsOf (exportProgression (p, two)) == "Verse:1 Bridge:1" && sectionsOf (exportProgression (p, mixed)) == "Verse:1 Chorus:3",
           "chords from two sections: a part each, in order; a chord and a section: both");
    check (exportProgression (p, Selection::chord (7, 0)) == p, "a selection of nothing that exists: everything");

    EngineSettings s;
    check (exportName (p, 0, Scale::Major) == "Verse-Chorus-Bridge_C", "the name: the sections and the key (" + exportName (p, 0, Scale::Major) + ")");
    check (exportName (exportProgression (p, Selection::chord (2, 0)), 0, Scale::Major) == "Dm7_C", "one chord: the chord's name (Dm7_C)");
    check (exportName (exportProgression (p, Selection::chord (2, 0)), 9, Scale::NaturalMinor) == "Dm7_Am", "in A minor: _Am");
    ChordProgression many;
    for (const char* n : { "Intro", "Verse", "Verse", "Chorus", "Bridge", "Chorus", "Outro" })
        many.sections.push_back (sectionOf (n, "I"));
    check (exportName (many, 7, Scale::Major) == "Intro-Verse-Chorus-Bridge+2_G", "a name repeated back to back once; at most four, then how many more (" + exportName (many, 7, Scale::Major) + ")");
    auto slash = p;
    slash.sections[0].chords[0].inversion = 1;
    check (exportName (exportProgression (slash, Selection::chord (0, 0)), 0, Scale::Major) == "C over E_C", "a slash chord: no '/' in the name (C over E_C)");

    // Markers at each section's start, through the file and back.
    const auto song = renderSelection (p, {}, s, 120.0);
    const std::vector<MidiMarker> want { { 0, "Verse" }, { 16 * 960, "Chorus" }, { 24 * 960, "Bridge" } };
    check (song.markers == want && song.name == "Verse-Chorus-Bridge_C", "everything: markers Verse at 0, Chorus at beat 16, Bridge at 24; named after them");
    const auto bytes = writeMidiFile (song);
    MidiSong back;
    std::string error, why;
    check (readMidiFile (bytes.data(), bytes.size(), back, error) && back.markers == want && back.name == song.name && same (song, back, 0, why),
           "the .mid reads back with its markers, name and notes");
    const auto chorus = renderSelection (p, Selection::section (1), s, 120.0);
    check (chorus.markers == std::vector<MidiMarker> { { 0, "Chorus" } } && ! chorus.notes.empty() && chorus.notes.back().end == 8 * 960,
           "the Chorus alone: one marker at 0, 8 beats long");
    check (renderSelection ({}, {}, s, 120.0).notes.empty() && renderSelection ({}, {}, s, 120.0).markers.empty(), "an empty progression renders nothing");

    // G8: a selection's export is what plays when it's the loop region (Sync, Loop), to the tick.
    struct Case { const char* what; Selection sel; EngineSettings s; double bpm; };
    std::vector<Case> cases;
    {
        EngineSettings e;
        e.fx.bass = BassMode::RootFifth;
        cases.push_back ({ "the Chorus, Root + 5th", Selection::section (1), e, 120.0 });
    }
    {
        EngineSettings e;
        e.fx.strum = StrumMode::Down;
        e.fx.strumRate = 4;
        e.fx.voicing = VoicingMode::Spread;
        cases.push_back ({ "Verse's vi and IV, strummed, Spread", [] { Selection x; x.chords = { { 0, 2 }, { 0, 3 } }; return x; }(), e, 97.0 });
    }
    {
        EngineSettings e;
        e.fx.voiceLeading = VoiceLeadingMode::Smooth;
        cases.push_back ({ "everything, smooth voice leading", Selection {}, e, 140.0 });
    }
    {
        EngineSettings e;
        e.fx.velocity.curve = VelocityCurve::House;
        cases.push_back ({ "Chorus and Bridge, House velocity", [] { Selection x; x.sections = { 1, 2 }; return x; }(), e, 120.0 });
    }
    for (const auto& c : cases)
    {
        const auto rendered = renderSelection (p, c.sel, c.s, c.bpm);
        bool all = true;
        for (double rate : { 44100.0, 48000.0 })
            for (int block : { 64, 512 })
                if (! same (rendered, hostedSelection (p, c.sel, c.s, c.bpm, rate, block), 1, why))
                {
                    all = false;
                    break;
                }
        check (all && ! rendered.notes.empty(), std::string (c.what) + ": " + std::to_string (rendered.notes.size())
                                                    + " notes, the same as a host records with it as the loop region" + (all ? "" : " (" + why + ")"));
    }
}

int main()
{
    testChordsOnly();
    testMatchesTheHost();
    testSelectionExport();
    return testutil::finish ("test_render");
}
