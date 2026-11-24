// Timeline playback tests (PLAN.md §3.3, §7 test_player; Phase 3, G5, G9): the Timeline's chord
// lines, and the engine playing a progression under a fake transport (FakeHost.h) at 32-2048-
// sample blocks, 44.1-96 kHz and three tempos: every chord change on its beat (within a sample),
// across host loops, locates and tempo changes; Sync from a start bar, with and without Loop;
// the loop region from the selection; Preview on its own clock (loop, play once, pause and
// resume, rewind); Play, Enable and edits while a chord sounds; previews from the editor (held
// and timed); the effects on the timeline; and a stuck-note fuzz with edits during play.
//
// Expected notes are written out by hand (MIDI 48 = C2, the chords' default C). The tests set
// Velocity peak to 100, the timeline's fixed velocity.
#include "FakeHost.h"
#include "core/model/ChordSets.h"
#include <random>

using namespace fakehost;
using testutil::check;

namespace
{
    // A section from numerals (ChordSets.h; a bar each unless ":beats").
    ProgressionSection section (const std::string& text, const std::string& name = "S", int key = 0)
    {
        Pattern p;
        std::string error;
        if (! parseProgression (text, key, p, error))
            std::cout << "  (bad progression \"" << text << "\": " << error << ")\n";
        auto prog = progressionFromPattern (p, name);
        return prog.empty() ? ProgressionSection {} : prog.sections[0];
    }

    ChordProgression prog (std::initializer_list<const char*> sections, int key = 0)
    {
        ChordProgression p;
        int i = 0;
        for (const char* s : sections)
            p.sections.push_back (section (s, "S" + std::to_string (++i), key));
        return p;
    }

    using Notes = std::vector<int>;
    const Notes C { 48, 52, 55 }, G { 55, 59, 62 }, Am { 57, 60, 64 }, F { 53, 57, 60 };
    const Notes Dm7 { 50, 53, 57, 60 }, G7 { 55, 59, 62, 65 };

    void chord (Expect& e, double from, double to, const Notes& notes, int velocity = 100)
    {
        for (int n : notes)
            e.note (from, to, n, velocity);
    }
    void chordOn (Expect& e, double at, const Notes& notes)
    {
        for (int n : notes)
            e.on (at, n);
    }
    void chordAt (Expect& e, int64_t from, int64_t to, const Notes& notes)
    {
        for (int n : notes)
        {
            e.onAt (from, n);
            e.offAt (to, n);
        }
    }

    // A host playing `p` with Play on (Sync, Loop on, bar 1), Velocity peak 100.
    std::unique_ptr<Host> playing (const Config& c, const ChordProgression& p, const Selection& loop = {})
    {
        auto h = std::make_unique<Host> (c.rate, c.bpm);
        h->engine->setTimeline (makeTimeline (p, loop));
        h->settings.play = true;
        h->settings.fx.velocity.peak = 100;
        return h;
    }

    // Stops the host and expects every note still sounding to end there.
    void stopAndEnd (Host& h, Expect& e, std::initializer_list<Notes> sounding, int block)
    {
        const int64_t at = h.pos;
        h.stop (block);
        for (const auto& notes : sounding)
            for (int n : notes)
                e.offAt (at, n);
    }

    // Ends a stopped-host scenario (Preview): Play off for a block; what sounds ends there.
    void playOffAndEnd (Host& h, Expect& e, std::initializer_list<Notes> sounding, int block)
    {
        const int64_t at = h.pos;
        h.settings.play = false;
        h.run (h.pos + block, block);
        for (const auto& notes : sounding)
            for (int n : notes)
                e.offAt (at, n);
    }
}

static void testTimeline()
{
    std::cout << "== the timeline ==\n";
    const auto p = prog ({ "I:2 V:1 vi:0.5", "IV:4.5" });   // 8 beats, lines at 2, 3, 3.5
    const auto t = makeTimeline (p);
    check (t.count == 4 && t.total == 8 * ticksPerBeat && t.loopStart == 0 && t.loopEnd == t.total, "4 chords, 8 beats, the loop region is everything");
    check (t.chordAt (0.0) == 0 && t.chordAt (1.99) == 0 && t.chordAt (2.0) == 1 && t.chordAt (3.0) == 2 && t.chordAt (3.5) == 3 && t.chordAt (7.99) == 3,
           "chordAt: lines at 2, 3, 3.5 beats");
    check (t.chordAt (2.0 - 1.0e-9) == 1 && t.chordAt (8.0) == -1 && t.chordAt (-0.5) == -1, "a hair before a line counts as on it; outside, none");
    check (t.nextLine (0.0) == 2.0 && t.nextLine (2.0) == 3.0 && t.nextLine (3.6) == 8.0 && t.nextLine (8.0) == HUGE_VAL && t.nextLine (-1.0) == 0.0,
           "nextLine: the next line strictly after; the end; nothing past it");
    check (t.onLine (3.5) && ! t.onLine (3.6) && t.chords[3].section == 1 && t.chords[0].notes.size == 3, "onLine; chords know their section and notes");
    const auto r = makeTimeline (p, Selection::section (1));
    check (r.loopStart == 336 && r.loopEnd == 8 * ticksPerBeat, "the loop region from a selected section (IV): beats 3.5-8");
    Selection chords;
    chords.chords = { { 0, 1 }, { 0, 2 } };
    const auto r2 = makeTimeline (p, chords);
    check (r2.loopStart == 2 * ticksPerBeat && r2.loopEnd == 336, "… from two selected chords: beats 2-3.5");
    check (makeTimeline (ChordProgression {}).empty() && makeTimeline (ChordProgression {}).chordAt (0.0) == -1, "an empty progression: an empty timeline");
    auto withBass = p;
    withBass.setBass ({ 0, 0 }, 4);
    withBass.setInversion ({ 0, 1 }, 1);
    const auto b = makeTimeline (withBass);
    check (b.chords[0].notes.size == 4 && b.chords[0].notes.notes[0] == 40 && b.chords[1].notes.notes[0] == 59,
           "the notes carry the bass and inversion (C/E: E 40 under; G first inversion: B 59 lowest)");
}

static void testSync()
{
    std::cout << "== Sync: locked to the song ==\n";
    sweep ("I V vi IV, a bar each, from the top: every chord on its bar line, ends at the stop", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->run (h->at (19.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        chord (e, 8, 12, Am);
        chord (e, 12, 16, F);
        chordOn (e, 16, C);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("two sections, uneven lengths (I:2 V:1 vi:0.5 | IV:4.5), twice round", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:2 V:1 vi:0.5", "IV:4.5" }));
        h->run (h->at (15.75), c.block);
        Expect e (h->spq());
        for (double base : { 0.0, 8.0 })
        {
            chord (e, base, base + 2, C);
            chord (e, base + 2, base + 3, G);
            chord (e, base + 3, base + 3.5, Am);
        }
        chord (e, 3.5, 8, F);
        chordOn (e, 11.5, F);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("one chord of one beat: restruck every beat; two equal chords in a row: two strikes", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:1", "I:2" }));
        h->run (h->at (5.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 1, C);
        chord (e, 1, 3, C);
        chord (e, 3, 4, C);
        chordOn (e, 4, C);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("starting mid-chord (ppq 6): the chord under the playhead plays at once", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->ppq = 6.0;
        h->run (h->at (6.5), c.block);   // to ppq 12.5
        Expect e ([&] (double beats) { return (beats - 6.0) * h->spq(); });
        chord (e, 6, 8, G);
        chord (e, 8, 12, Am);
        chordOn (e, 12, F);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Start bar 3, Loop off: silent for two bars, I V once from beat 8, then silent", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->settings.loop = false;
        h->settings.startBar = 3;
        h->run (h->at (20.0), c.block);
        Expect e (h->spq());
        chord (e, 8, 12, C);
        chord (e, 12, 16, G);
        stopAndEnd (*h, e, {}, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Start bar 2, Loop on: the loop runs before its start bar too (G at 0, C at 4)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->settings.startBar = 2;
        h->run (h->at (10.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, G);
        chord (e, 4, 8, C);
        chordOn (e, 8, G);
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Start bar 2 in 3/4: the timeline starts on beat 3", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:3 V:3" }));
        h->numerator = 3;
        h->settings.loop = false;
        h->settings.startBar = 2;
        h->run (h->at (10.0), c.block);
        Expect e (h->spq());
        chord (e, 3, 6, C);
        chord (e, 6, 9, G);
        stopAndEnd (*h, e, {}, c.block);
        return finish (*h, e, worst);
    });

    sweep ("the loop region is the selection: section 2 (vi IV) repeats, locked to the song", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V", "vi IV" }), Selection::section (1));
        h->run (h->at (17.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, Am);
        chord (e, 4, 8, F);
        chord (e, 8, 12, Am);
        chord (e, 12, 16, F);
        chordOn (e, 16, Am);
        stopAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("the loop region from two chords across sections (V | vi): V vi V vi", [] (const Config& c, int64_t& worst)
    {
        Selection sel;
        sel.chords = { { 0, 1 }, { 1, 0 } };
        auto h = playing (c, prog ({ "I V", "vi IV" }), sel);
        h->run (h->at (14.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, G);
        chord (e, 4, 8, Am);
        chord (e, 8, 12, G);
        chordOn (e, 12, Am);
        stopAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Velocity peak 90 is the timeline's velocity", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I" }));
        h->settings.fx.velocity.peak = 90;
        h->run (h->at (2.0), c.block);
        Expect e (h->spq());
        for (int n : C)
            e.on (0, n, 90);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });
}

static void testLoopsLocatesTempo()
{
    std::cout << "== host loops, locates and tempo changes ==\n";
    for (auto style : { LoopStyle::Wrap, LoopStyle::Split })
        sweep (std::string ("a host loop of beats 2-6 over I V vi IV (")
                   + (style == LoopStyle::Wrap ? "blocks across the wrap" : "split at the loop end")
                   + "): the chord at the loop start restrikes on every wrap",
               [style] (const Config& c, int64_t& worst)
        {
            auto h = playing (c, prog ({ "I V vi IV" }));
            h->looping = true;
            h->loopStart = 2.0;
            h->loopEnd = 6.0;
            h->style = style;
            h->run (h->at (13.5), c.block);   // song time: 0-6, then 2-6, then 2-5.5
            Expect e (h->spq());
            chord (e, 0, 4, C);
            chord (e, 4, 6, G);
            chord (e, 6, 8, C);      // wrap to 2
            chord (e, 8, 10, G);
            chord (e, 10, 12, C);    // wrap to 2
            chordOn (e, 12, G);
            stopAndEnd (*h, e, { G }, c.block);
            return finish (*h, e, worst);
        });

    sweep ("a host loop of beats 4-12 over a 6-beat progression (I:4 V:2): the two loops beat against each other", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:4 V:2" }));
        h->looping = true;
        h->loopStart = 4.0;
        h->loopEnd = 12.0;
        h->ppq = 4.0;
        h->run (h->at (16.5), c.block);   // song 4-12, 4-12, 4-4.5
        Expect e ([&] (double beats) { return (beats - 4.0) * h->spq(); });
        // song 4 = timeline 4 (V), 6 = 0 (I), 10 = 4 (V); wrap at 12 → song 4 (V again)
        chord (e, 4, 6, G);
        chord (e, 6, 10, C);
        chord (e, 10, 12, G);
        chord (e, 12, 14, G);
        chord (e, 14, 18, C);
        chord (e, 18, 20, G);
        chordOn (e, 20, G);
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("a locate from beat 6 to 13: IV restrikes at the jump, then I at 16", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->run (h->at (6.0), c.block);
        const int64_t jump = h->pos;
        h->ppq = 13.0;
        h->run (jump + h->at (5.0), c.block);   // to ppq 18
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (jump, n);
        }
        const double j = (double) jump / h->spq();
        for (int n : F)
        {
            e.onAt (jump, n);
            e.off (j + 3.0, n);
        }
        chordOn (e, j + 3.0, C);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("a locate into the same chord restrikes it", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:8" }));
        h->run (h->at (3.0), c.block);
        const int64_t jump = h->pos;
        h->ppq = 1.0;
        h->run (jump + h->at (2.0), c.block);
        Expect e (h->spq());
        chordAt (e, 0, jump, C);
        for (int n : C)
            e.onAt (jump, n);
        stopAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("tempo changes (120 → 90 → 150 at block boundaries): each chord on its beat", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:2 V:2 vi:2 IV:2" }));
        // Record where each block starts and its tempo, to map beats to samples afterwards.
        std::vector<std::tuple<int64_t, double, double>> blocks;   // (sample, ppq, spq)
        h->observe = [&] (const Transport& t, int, const BlockEvents&) { blocks.push_back ({ h->pos, t.ppq, t.samplesPerBeat() }); };
        h->tempo = { { (int64_t) (c.rate * 1.7), 90.0 }, { (int64_t) (c.rate * 4.3), 150.0 } };
        h->run ((int64_t) (c.rate * 7.0), c.block);
        const auto toSample = [&] (double beats)
        {
            for (size_t i = blocks.size(); i-- > 0;)
                if (std::get<1> (blocks[i]) <= beats + 1.0e-9)
                    return (double) std::get<0> (blocks[i]) + (beats - std::get<1> (blocks[i])) * std::get<2> (blocks[i]);
            return 0.0;
        };
        Expect e (toSample);
        const double end = h->ppq;
        const Notes order[4] = { C, G, Am, F };
        int k = 0;
        double b = 0.0;
        for (; b + 2.0 <= end + 1.0e-9; b += 2.0, ++k)
            chord (e, b, b + 2.0, order[k % 4]);
        if (b < end - 1.0e-9)
            chordOn (e, b, order[k % 4]);
        const Notes& last = order[k % 4];
        h->observe = nullptr;
        stopAndEnd (*h, e, { b < end - 1.0e-9 ? last : Notes {} }, c.block);
        return finish (*h, e, worst);
    });
}

static void testPreview()
{
    std::cout << "== Preview: its own clock ==\n";
    sweep ("host stopped, Play on: I V vi IV from the top at the host's tempo, looping", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->playing = false;
        h->settings.sync = SyncMode::Preview;
        h->run (h->at (18.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 4, 8, G);
        chord (e, 8, 12, Am);
        chord (e, 12, 16, F);
        chordOn (e, 16, C);
        playOffAndEnd (*h, e, { C }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Loop off: plays once and stops at the end; Play again starts from the top", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:2 V:2" }));
        h->playing = false;
        h->settings.sync = SyncMode::Preview;
        h->settings.loop = false;
        h->run (h->at (6.0), c.block);
        h->settings.play = false;
        h->run (h->at (7.0), c.block);
        const int64_t again = h->pos;
        h->settings.play = true;
        h->run (again + h->at (3.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 2, C);
        chord (e, 2, 4, G);
        const double a = (double) again / h->spq();
        chord (e, a, a + 2, C);
        chordOn (e, a + 2, G);
        playOffAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("pause and resume: Play off at 5, on at 9 resumes in V, vi 3 beats later", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->playing = false;
        h->settings.sync = SyncMode::Preview;
        h->run (h->at (5.0), c.block);
        const int64_t off = h->pos;
        h->settings.play = false;
        h->run (h->at (9.0), c.block);
        const int64_t on = h->pos;
        h->settings.play = true;
        const double resumed = (double) off / h->spq();          // the position where it paused
        h->run (on + h->at (8.0 - resumed + 1.0), c.block);    // into vi
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (off, n);
        }
        const int64_t next = on + h->at (8.0 - resumed);
        chordAt (e, on, next, G);
        for (int n : Am)
            e.onAt (next, n);
        playOffAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("rewind: back to the loop region's start (section 2)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V", "vi IV" }), Selection::section (1));
        h->playing = false;
        h->settings.sync = SyncMode::Preview;
        h->run (h->at (6.0), c.block);
        const int64_t rew = h->pos;
        h->engine->requestRewind();
        h->run (rew + h->at (5.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, Am);
        chordAt (e, h->at (4.0), rew, F);
        const int64_t next = rew + h->at (4.0);
        chordAt (e, rew, next, Am);
        for (int n : F)
            e.onAt (next, n);
        playOffAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Preview plays on its own while the host plays elsewhere (song at beat 30)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->ppq = 30.0;
        h->settings.sync = SyncMode::Preview;
        h->run (h->at (6.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chordOn (e, 4, G);
        playOffAndEnd (*h, e, { G }, c.block);
        h->stop (c.block);
        return finish (*h, e, worst);
    });

    sweep ("Sync never plays while the host is stopped", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->playing = false;
        h->run (h->at (6.0), c.block);
        Expect e (h->spq());
        return finish (*h, e, worst);
    });
}

static void testOnOffAndEdits()
{
    std::cout << "== Play, Enable and edits ==\n";
    sweep ("Play off mid-chord ends it; on again plays the chord under the playhead (Sync)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->run (h->at (5.0), c.block);
        const int64_t off = h->pos;
        h->settings.play = false;
        h->run (h->at (9.0), c.block);
        const int64_t on = h->pos;
        h->settings.play = true;
        h->run (h->at (12.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (off, n);
        }
        for (int n : Am)
        {
            e.onAt (on, n);
            e.off (12, n);
        }
        chordOn (e, 12, F);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Enable off and Analyse stop the timeline", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:8" }));
        h->run (h->at (2.0), c.block);
        const int64_t off = h->pos;
        h->settings.enabled = false;
        h->run (h->at (3.0), c.block);
        h->settings.enabled = true;
        h->settings.analysing = true;
        h->run (h->at (4.0), c.block);
        Expect e (h->spq());
        chordAt (e, 0, off, C);
        stopAndEnd (*h, e, {}, c.block);
        return finish (*h, e, worst);
    });

    sweep ("an edit to the sounding chord retunes it at the next block; an edit later changes nothing yet", [] (const Config& c, int64_t& worst)
    {
        auto p = prog ({ "I V vi IV" });
        auto h = playing (c, p);
        h->run (h->at (5.0), c.block);
        const int64_t edit = h->pos;
        p.replaceChord ({ 0, 1 }, section ("V7").chords[0].chord);   // G → G7 while G sounds
        p.setOctave ({ 0, 3 }, 1);                                    // F an octave up, later
        h->engine->setTimeline (makeTimeline (p));
        h->run (h->at (14.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        for (int n : G)
        {
            e.on (4, n);
            e.offAt (edit, n);
        }
        for (int n : G7)
        {
            e.onAt (edit, n);
            e.off (8, n);
        }
        chord (e, 8, 12, Am);
        for (int n : F)
            e.on (12, n + 12);
        stopAndEnd (*h, e, { { 65, 69, 72 } }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("an edit that moves the chord lines (V resized from 4 to 2 beats at beat 5): vi restrikes at once", [] (const Config& c, int64_t& worst)
    {
        auto p = prog ({ "I V vi IV" });
        auto h = playing (c, p);
        h->run (h->at (5.0), c.block);
        const int64_t edit = h->pos;
        p.resizeChord ({ 0, 1 }, 2 * ticksPerBeat);   // V now 4-6, vi 6-10: song 5 is still V…
        p.resizeChord ({ 0, 0 }, 2 * ticksPerBeat);   // …and I 0-2: V 2-4, vi 4-8 under the playhead
        h->engine->setTimeline (makeTimeline (p));
        h->run (h->at (9.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chordAt (e, h->at (4.0), edit, G);
        for (int n : Am)
        {
            e.onAt (edit, n);
            e.off (8, n);
        }
        chordOn (e, 8, F);
        stopAndEnd (*h, e, { F }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("deleting every chord while one plays ends it; adding one plays again", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V vi IV" }));
        h->run (h->at (2.0), c.block);
        const int64_t emptied = h->pos;
        h->engine->setTimeline (makeTimeline (ChordProgression {}));
        h->run (h->at (6.0), c.block);
        const int64_t refilled = h->pos;
        h->engine->setTimeline (makeTimeline (prog ({ "vi:8" })));
        h->run (h->at (7.75), c.block);
        Expect e (h->spq());
        chordAt (e, 0, emptied, C);
        for (int n : Am)
            e.onAt (refilled, n);
        stopAndEnd (*h, e, { Am }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Chord Octave +1 while a timeline chord sounds moves it", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I:8" }));
        h->run (h->at (3.0), c.block);
        const int64_t moved = h->pos;
        h->settings.chordOctave = 1;
        h->run (h->at (6.0), c.block);
        Expect e (h->spq());
        for (int n : C)
        {
            e.on (0, n);
            e.offAt (moved, n);
            e.onAt (moved, n + 12);
        }
        stopAndEnd (*h, e, { { 60, 64, 67 } }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("switching Sync → Preview mid-chord restrikes from the Preview position (the top)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->run (h->at (5.0), c.block);
        const int64_t sw = h->pos;
        h->settings.sync = SyncMode::Preview;
        h->run (sw + h->at (5.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chordAt (e, h->at (4.0), sw, G);
        chordAt (e, sw, sw + h->at (4.0), C);
        for (int n : G)
            e.onAt (sw + h->at (4.0), n);
        playOffAndEnd (*h, e, { G }, c.block);
        h->stop (c.block);
        return finish (*h, e, worst);
    });

    sweep ("a key played over the timeline sounds with it; the timeline goes on", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->key (1.0, 69, true);    // A: vi in C (Am, 57 60 64)
        h->key (2.0, 69, false);
        h->run (h->at (7.5), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chord (e, 1, 2, Am);
        chordOn (e, 4, G);
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });
}

static void testPreviewRequests()
{
    std::cout << "== previews from the editor ==\n";
    const auto request = [] (const ChordProgression& p, bool on, float beats = 0.0f)
    {
        PreviewRequest r;
        r.notes = progressionChordNotes (p.sections[0].chords[0]);
        r.root = (int8_t) p.sections[0].chords[0].root();
        r.on = on;
        r.beats = beats;
        return r;
    };
    sweep ("a held preview while stopped: from the next block until released", [&] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.velocity.peak = 100;
        h.run (h.at (1.0), c.block);
        const int64_t down = h.pos;
        h.engine->queuePreview (request (prog ({ "vi" }), true));
        h.run (h.at (2.5), c.block);
        const int64_t up = h.pos;
        h.engine->queuePreview (request (prog ({ "vi" }), false));
        h.run (h.at (3.5), c.block);
        Expect e (h.spq());
        chordAt (e, down, up, Am);
        return finish (h, e, worst);
    });

    sweep ("a 1-beat preview ends a beat later, to the sample, across blocks", [&] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.velocity.peak = 100;
        h.run (h.at (0.5), c.block);
        const int64_t down = h.pos;
        h.engine->queuePreview (request (prog ({ "IV" }), true, 1.0f));
        h.run (h.at (3.0), c.block);
        Expect e (h.spq());
        chordAt (e, down, down + h.at (1.0), F);
        return finish (h, e, worst);
    });

    sweep ("a preview over the playing timeline: both sound; the timeline goes on", [&] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->run (h->at (1.0), c.block);
        const int64_t down = h->pos;
        h->engine->queuePreview (request (prog ({ "vi" }), true, 2.0f));
        h->run (h->at (6.0), c.block);
        Expect e (h->spq());
        chord (e, 0, 4, C);
        chordAt (e, down, down + h->at (2.0), Am);
        chordOn (e, 4, G);
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("previews are ignored with Enable off; a new preview replaces the last", [&] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.velocity.peak = 100;
        h.settings.enabled = false;
        h.engine->queuePreview (request (prog ({ "I" }), true));
        h.run (h.at (1.0), c.block);
        h.settings.enabled = true;
        h.run (h.at (1.5), c.block);
        const int64_t a = h.pos;
        h.engine->queuePreview (request (prog ({ "I" }), true));
        h.run (h.at (2.5), c.block);
        const int64_t b = h.pos;
        h.engine->queuePreview (request (prog ({ "V" }), true));
        h.run (h.at (3.5), c.block);
        const int64_t end = h.pos;
        h.engine->queuePreview (request (prog ({ "V" }), false));
        h.run (h.at (4.0), c.block);
        Expect e (h.spq());
        chordAt (e, a, b, C);
        chordAt (e, b, end, G);
        return finish (h, e, worst);
    });
}

static void testEffects()
{
    std::cout << "== effects on the timeline ==\n";
    sweep ("Strum Up 1/32 strums each timeline chord", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I V" }));
        h->settings.fx.strum = StrumMode::Up;
        h->settings.fx.strumRate = 4;   // 1/32
        h->run (h->at (7.5), c.block);
        Expect e (h->spq());
        for (int i = 0; i < 3; ++i)
        {
            e.note (0.0 + 0.125 * i, 4.0, C[(size_t) i]);
            e.on (4.0 + 0.125 * i, G[(size_t) i]);
        }
        stopAndEnd (*h, e, { G }, c.block);
        return finish (*h, e, worst);
    });

    sweep ("Bass Root + Voicing Open on the timeline (as the render does)", [] (const Config& c, int64_t& worst)
    {
        auto h = playing (c, prog ({ "I" }));
        h->settings.fx.bass = BassMode::Root;
        h->run (h->at (2.0), c.block);
        Expect e (h->spq());
        for (int n : { 36, 48, 52, 55 })
            e.on (0, n);
        stopAndEnd (*h, e, { { 36, 48, 52, 55 } }, c.block);
        return finish (*h, e, worst);
    });
}

static void testFuzz()
{
    std::cout << "== stuck-note fuzz, with edits during play ==\n";
    std::mt19937 rng (2026);
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    const char* secs[] = { "I V vi IV", "ii7:2 V7:2 Imaj7", "I:0.5 IV:0.5 V:1", "i bVI bIII bVII", "vi:8", "I", "V(0.7.16):1.5 ii9:2.5" };
    const auto randomChord = [&] (int key)
    {
        return section (secs[pick (0, (int) std::size (secs) - 1)], "x", key).chords[0];
    };

    int runs = 0, dirty = 0, played = 0, soundingWhenItShouldnt = 0, edits = 0;
    std::string first;
    for (int r = 0; r < 60; ++r)
    {
        Host h (std::array<double, 3> { 44100.0, 48000.0, 96000.0 }[(size_t) pick (0, 2)], 60.0 + pick (0, 140));
        ChordProgression p;
        for (int s = pick (0, 3); s > 0; --s)
            p.sections.push_back (section (secs[pick (0, (int) std::size (secs) - 1)], "S", pick (0, 11)));
        Selection loop;
        h.engine->setTimeline (makeTimeline (p));
        h.settings.play = true;
        h.looping = chance (0.4);
        h.loopStart = pick (0, 4);
        h.loopEnd = h.loopStart + pick (1, 8) * 0.5;
        h.style = (LoopStyle) pick (0, 2);

        for (int step = 0; step < 120; ++step)
        {
            const int block = chance (0.3) ? pick (1, 4096) : 1 << pick (5, 11);
            if (chance (0.1)) h.settings.play = ! h.settings.play;
            if (chance (0.05)) h.settings.sync = (SyncMode) pick (0, numSyncModes - 1);
            if (chance (0.05)) h.settings.loop = ! h.settings.loop;
            if (chance (0.04)) h.settings.startBar = pick (1, 4);
            if (chance (0.05)) h.settings.enabled = ! h.settings.enabled;
            if (chance (0.03)) h.settings.analysing = ! h.settings.analysing;
            if (chance (0.05)) h.settings.chordOctave = pick (-3, 3);
            if (chance (0.05)) h.settings.fx.strum = (StrumMode) pick (0, numStrumModes - 1);
            if (chance (0.05)) h.settings.fx.hold = (HoldMode) pick (0, numHoldModes - 1);
            if (chance (0.05)) h.settings.fx.voiceLeading = (VoiceLeadingMode) pick (0, numVoiceLeadingModes - 1);
            if (chance (0.05)) h.settings.fx.bass = (BassMode) pick (0, numBassModes - 1);
            if (chance (0.03)) h.settings.testPattern = ! h.settings.testPattern;
            if (chance (0.15))
            {
                // An edit while playing, as the editor makes them.
                ++edits;
                const int sec = p.empty() ? 0 : pick (0, p.numSections() - 1);
                const int idx = p.empty() ? 0 : pick (0, (int) p.sections[(size_t) sec].chords.size() - 1);
                switch (pick (0, 8))
                {
                    case 0: p.insertChord (p.empty() ? InsertPoint::newSection (0) : InsertPoint::inSection (sec, idx), randomChord (pick (0, 11))); break;
                    case 1: p.resizeChord ({ sec, idx }, pick (1, 32) * 48, (Snap) pick (0, 5)); break;
                    case 2: p.resizeSection (sec, pick (1, 64) * 48); break;
                    case 3: p.move (Selection::chord (sec, idx), InsertPoint::newSection (pick (0, p.numSections()))); break;
                    case 4: p.duplicateInPlace (Selection::section (sec)); break;
                    case 5: if (p.numChords() > 1 || chance (0.3)) p.remove (Selection::chord (sec, idx)); break;
                    case 6: p.setInversion ({ sec, idx }, pick (0, 3)); p.setBass ({ sec, idx }, pick (-1, 11)); break;
                    case 7: p.replaceChord ({ sec, idx }, randomChord (pick (0, 11)).chord); break;
                    default: loop = chance (0.5) ? Selection {} : Selection::chord (sec, idx); break;
                }
                h.engine->setTimeline (makeTimeline (p, loop));
            }
            if (chance (0.03)) h.engine->requestRewind();
            if (chance (0.06) && ! p.empty())
            {
                PreviewRequest q;
                q.notes = progressionChordNotes (randomChord (pick (0, 11)));
                q.on = chance (0.7);
                q.beats = chance (0.5) ? (float) pick (0, 4) * 0.5f : 0.0f;
                h.engine->queuePreview (q);
            }
            if (chance (0.04)) h.ppq = pick (0, 64) * 0.25;                // a locate
            if (chance (0.03)) h.bpm = 40 + pick (0, 200);
            if (chance (0.04)) h.playing = ! h.playing;
            if (chance (0.02)) h.engine->requestReset();
            if (chance (0.1))
            {
                const int note = pick (36, 84);
                h.keyAt (h.pos + pick (0, block - 1), note, true, pick (1, 127));
                h.keyAt (h.pos + pick (0, 3 * block), note, false);
            }
            h.run (h.pos + block, block);
            const bool mayPlay = h.settings.play && h.settings.enabled && ! h.settings.analysing && ! p.empty()
                              && (h.playing || h.settings.sync == SyncMode::Preview);
            if (! mayPlay && h.engine->getPerformer().isAlive (ChordPerformer::timelineKey))
                ++soundingWhenItShouldnt;
        }
        // End any preview, deliver the key-ups still waiting, then stop from playing in Sync:
        // everything must end.
        PreviewRequest off;
        h.engine->queuePreview (off);
        h.settings.sync = SyncMode::Sync;
        h.playing = true;
        h.run (h.pos + 3 * 4096 + 1, 512);
        h.stop (256);
        ++runs;
        played += h.rx.noteOns;
        if (! h.clean() && dirty++ == 0)
            first = " (first: run " + std::to_string (r) + ": " + h.rx.describe() + ")";
    }
    check (soundingWhenItShouldnt == 0, "the timeline never sounds with Play off, Enable off, analysing, an empty progression, or (Sync) the host stopped ("
                                            + std::to_string (soundingWhenItShouldnt) + " blocks)");
    check (dirty == 0 && played > 1000, std::to_string (runs) + " runs of 120 random blocks (" + std::to_string (edits)
                                            + " edits during play, Sync/Preview, Loop, start bar, rewinds, previews, host loops, locates, tempo, stops, resets, keys, every effect): "
                                            + std::to_string (played) + " note-ons, nothing left sounding" + first);
}

int main()
{
    testTimeline();
    testSync();
    testLoopsLocatesTempo();
    testPreview();
    testOnOffAndEdits();
    testPreviewRequests();
    testEffects();
    testFuzz();
    return testutil::finish ("test_player");
}
