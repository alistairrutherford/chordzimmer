// Effect tests (Chordism's Phase 3; ChordChimp's PLAN.md §8): the Clock, the EventQueue, the effect building
// blocks (Stack, Strum, Velocity Algorithm), and the effect chain under a
// fake host transport. Every timing test runs at 32-2048-sample blocks and 44.1, 48 and 96 kHz,
// and must put each note-on and note-off within one sample of where its musical time falls,
// through loop wraps (hosts that split their blocks at the loop end and hosts that don't),
// locates and tempo changes, and while the transport is stopped.
#include "FakeHost.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

using namespace chordzimmer;
using namespace fakehost;
using testutil::check;

namespace
{
    constexpr int C = 48, D = 50, E = 52, G = 55;
    const std::vector<int> cMajor { 48, 52, 55 }, dMinor { 50, 53, 57 }, gMajor { 55, 59, 62 }, eMinor { 52, 55, 59 };
}

//==============================================================================
static void testClock()
{
    std::cout << "== clock ==\n";
    Clock c;
    Transport t { 48000.0, true, 1.0, 120.0, 4, 4 };
    c.beginBlock (t, 480);
    check (c.numSegments() == 1 && c.ppqAt (0, 240) == 1.0 + 240.0 / 24000.0 && ! c.jumpedAtStart(),
           "playing: one segment, ppq at a sample");
    t.ppq = 1.0 + 480.0 / 24000.0;
    c.beginBlock (t, 480);
    check (! c.jumpedAtStart(), "a block that carries on is no jump");
    t.ppq = 9.0;
    c.beginBlock (t, 480);
    check (c.jumpedAtStart() && std::abs (c.jumpDelta() - (9.0 - (1.0 + 960.0 / 24000.0))) < 1e-9, "a locate is a jump, with its distance");

    // A loop end inside the block: split at the first sample at or past it.
    t.looping = true;
    t.loopStart = 0.0;
    t.loopEnd = 4.0;
    t.ppq = 3.99;
    c.beginBlock (t, 1024);
    const auto& s1 = c.segment (1);
    check (c.numSegments() == 2 && c.segment (0).end == 240 && s1.start == 240 && std::abs (s1.ppq - 0.0) < 1e-9
               && std::abs (c.wrapDelta (1) + 4.0) < 1e-9,
           "loop wrap inside a block: two segments at sample 240, wrap delta -4");
    t.ppq = (1024 - 240) / 24000.0;
    c.beginBlock (t, 512);
    check (! c.jumpedAtStart(), "the host's next block after the wrap continues (no jump)");

    // Tiny loop, big block: several wraps.
    t.loopStart = 0.0;
    t.loopEnd = 0.01;   // 240 samples
    t.ppq = 0.0;
    c.beginBlock (t, 1000);
    check (c.numSegments() == 5 && c.segment (4).start == 960, "a 240-sample loop in a 1000-sample block: 5 segments");

    // Tempo change, then a stop: the internal clock runs on at the last tempo.
    t.looping = false;
    t.bpm = 90.0;
    t.ppq = 10.0;
    c.beginBlock (t, 100);
    check (c.tempoChanged() && std::abs (c.samplesPerQuarter() - 32000.0) < 1e-9, "tempo change seen; 90 BPM = 32000 samples a beat");
    t.playing = false;
    c.beginBlock (t, 100);
    const double stoppedAt = c.ppqAt (0, 0);
    check (! c.isPlaying() && std::abs (stoppedAt - (10.0 + 100.0 / 32000.0)) < 1e-9, "stopped: internal clock carries on from the host");
    c.anchor (40);
    check (std::abs (c.ppqAt (0, 40)) < 1e-12 && c.barStart (c.ppqAt (0, 99)) == 0.0, "anchor: bar 1 at the anchored sample");
    const int64_t before = c.blockStart();
    c.beginBlock (t, 100);
    check (std::abs (c.ppqAt (0, 0) - 60.0 / 32000.0) < 1e-12 && c.blockStart() == before + 100, "the internal clock runs on; absolute samples count");
    t.playing = true;
    t.ppq = 0.0;
    c.beginBlock (t, 100);
    check (c.jumpedAtStart() && std::abs (c.jumpDelta() + 160.0 / 32000.0) < 1e-12, "transport start: a jump from internal to host time");

    // Bars from the host's bar start (3/4).
    t.numerator = 3;
    t.hasBarStart = true;
    t.barStartPpq = 6.0;
    t.ppq = 7.5;
    c.beginBlock (t, 100);
    check (c.barLength() == 3.0 && c.barStart (7.5) == 6.0 && c.nextBarLine (7.5) == 9.0 && std::abs (c.barFraction (7.5) - 0.5) < 1e-12,
           "3/4 from the host's bar start: bar lines at 6, 9");
}

static void testEventQueue()
{
    std::cout << "== event queue ==\n";
    Clock c;
    Transport t { 48000.0, true, 0.0, 120.0, 4, 4 };
    c.beginBlock (t, 512);
    EventQueue<8> q;
    q.push (TimedEvent::musical (100.0 / 24000.0, 1, 2, 0, 0, 1));    // start at 100
    q.push (TimedEvent::musical (100.0 / 24000.0, 2, 0, 0, 0, 2));    // end at 100
    q.push (TimedEvent::atSample (50, 3, 2, 0, 0, 3));                // samples: 50
    q.push (TimedEvent::musical (1.0, 4, 0, 1, 0, 4));                // beat 1: not in this block
    q.push (TimedEvent::musical (100.0 / 24000.0, 5, 0, 0, 0, 5));    // end at 100, pushed later

    std::vector<uint32_t> order;
    int at = 0, i;
    while ((i = q.earliestDue (c, 0, 0, 512, at)) >= 0)
        order.push_back (q.take (i).arg);
    check (order == std::vector<uint32_t> { 3, 2, 5, 1 } && q.size() == 1, "by sample, then ends before starts, then push order");

    q.forEach ([] (TimedEvent& e) { e.ppq -= 0.99; });
    check (q.earliestDue (c, 0, 0, 512, at) == 0 && at == 240, "moved musical events find their new sample");
    q.removeIf ([] (const TimedEvent& e) { return e.owner == 1; });
    check (q.isEmpty(), "removeIf");
    for (int n = 0; n < 8; ++n)
        q.push (TimedEvent::musical (0.0, 0, 0, 0, 0, 0));
    check (! q.push (TimedEvent::musical (0.0, 0, 0, 0, 0, 0)) && q.size() == 8, "fixed capacity: a full queue refuses");
    q.clear();
    q.push (TimedEvent::musical (-1.0, 0, 0, 0, 0, 7));
    check (q.earliestDue (c, 0, 30, 512, at) == 0 && at == 30, "an event in the past comes out at the floor");
}

static void testBuildingBlocks()
{
    std::cout << "== stack, strum, velocity ==\n";
    ChordNotes triad;
    triad.notes = { 48, 52, 55 };
    triad.size = 3;
    auto list = [] (const VoicedNotes& v) { return std::vector<int> (v.begin(), v.end()); };
    check (list (stackNotes (triad, StackMode::Off)) == std::vector<int> { 48, 52, 55 }, "Stack Off");
    check (list (stackNotes (triad, StackMode::PlusOne)) == std::vector<int> { 48, 52, 55, 60, 64, 67 }, "Stack +1");
    check (list (stackNotes (triad, StackMode::PlusTwo)) == std::vector<int> { 48, 52, 55, 72, 76, 79 }, "Stack +2");
    check (list (stackNotes (triad, StackMode::PlusOneAndTwo)) == std::vector<int> { 48, 52, 55, 60, 64, 67, 72, 76, 79 }, "Stack +1&2");
    ChordNotes high;
    high.notes = { 100, 112, 124 };
    high.size = 3;
    check (list (stackNotes (high, StackMode::PlusOneAndTwo)) == std::vector<int> { 100, 112, 124 }, "Stack drops pitches above 127 and duplicates");

    Strummer s;
    uint8_t o[4];
    auto ord = [&] (StrumMode m) { s.order (m, 4, o); return std::vector<int> (o, o + 4); };
    check (ord (StrumMode::Up) == std::vector<int> { 0, 1, 2, 3 } && ord (StrumMode::Down) == std::vector<int> { 3, 2, 1, 0 }, "Strum Up, Down");
    s.reset();
    check (ord (StrumMode::Alternating) == std::vector<int> { 0, 1, 2, 3 } && ord (StrumMode::Alternating) == std::vector<int> { 3, 2, 1, 0 }
               && ord (StrumMode::Alternating) == std::vector<int> { 0, 1, 2, 3 },
           "Strum Alternating: up, down, up");
    s.reset();
    std::vector<std::vector<int>> first, second;
    for (int n = 0; n < 6; ++n) first.push_back (ord (StrumMode::Random));
    s.reset();
    for (int n = 0; n < 6; ++n) second.push_back (ord (StrumMode::Random));
    bool perms = true, varied = false;
    for (auto v : first)
    {
        varied |= v != first[0];
        std::sort (v.begin(), v.end());
        perms &= v == std::vector<int> { 0, 1, 2, 3 };
    }
    check (perms && varied && first == second, "Strum Random: permutations, varied, the same after a reseed");

    VelocitySettings v;
    check (v.apply (77, 0.3) == 77, "Velocity Off keeps the key's velocity");
    v.curve = VelocityCurve::Up;
    v.peak = 100;
    v.range = 1.0;
    check (v.apply (77, 0.0) == 1 && v.apply (77, 0.999) == 100 && v.apply (77, 0.5) == 53, "Up, Range 100%: 1 .. Peak across the bar");
    v.range = 0.0;
    check (v.apply (77, 0.0) == 100, "Range 0%: always Peak");
    v.curve = VelocityCurve::DownUp;
    v.range = 0.5;
    check (v.apply (10, 0.5) == 50 && v.apply (10, 0.0) == 100, "Down/Up: a dip mid-bar");
    bool curvesOk = true;
    for (int c = 1; c < numVelocityCurves; ++c)
        for (int i = 0; i < 16; ++i)
        {
            const double x = velocityCurveValue ((VelocityCurve) c, i);
            curvesOk &= x >= 0.0 && x <= 1.0;
        }
    check (curvesOk, "all 13 curves stay within 0..1");
}

//==============================================================================
// Timing sweeps: each runs in every configuration (configs()).

static void testStrumTiming()
{
    std::cout << "== strum ==\n";
    sweep ("Strum Up 1/32 and Down 1/16, cut short by the key-up", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 4;   // 1/32
        h.key (1.0, C, true);
        h.key (2.0, C, false);
        h.run (h.at (2.5), c.block);
        h.settings.fx.strum = StrumMode::Down;
        h.settings.fx.strumRate = 6;   // 1/16
        h.key (3.0, D, true);
        h.key (4.0, D, false);
        h.key (5.0, G, true);
        h.key (5.3, G, false);         // before the third tone
        h.run (h.at (6.0), c.block);

        Expect e (h.spq());
        for (int i = 0; i < 3; ++i)
            e.note (1.0 + 0.125 * i, 2.0, cMajor[(size_t) i]);
        for (int i = 0; i < 3; ++i)
            e.note (3.0 + 0.25 * i, 4.0, dMinor[(size_t) (2 - i)]);
        e.note (5.0, 5.3, gMajor[2]);
        e.note (5.25, 5.3, gMajor[1]);
        return finish (h, e, worst);
    });

    sweep ("Strum Alternating 1/64: up, down, up on three chords", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.strum = StrumMode::Alternating;
        h.settings.fx.strumRate = 2;   // 1/64
        for (int strike = 0; strike < 3; ++strike)
        {
            h.key (strike, C, true);
            h.key (strike + 0.5, C, false);
        }
        h.run (h.at (3.5), c.block);

        Expect e (h.spq());
        for (int strike = 0; strike < 3; ++strike)
            for (int i = 0; i < 3; ++i)
                e.note (strike + 0.0625 * i, strike + 0.5, cMajor[(size_t) (strike % 2 == 0 ? i : 2 - i)]);
        return finish (h, e, worst);
    });

    // Stopped: Strum Rate at the last tempo, from the key press.
    sweep ("Strum Up 1/32 while stopped: at the last tempo, from the key press", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 4;   // 1/32
        const int64_t press = 1000, up = 1000 + (int64_t) (0.5 * c.rate);
        h.keyAt (press, C, true);
        h.keyAt (up, C, false);
        h.run (up + 100, c.block);

        Expect e (h.spq());
        for (int i = 0; i < 3; ++i)
        {
            e.onAt (press + (int64_t) std::llround (i * 0.125 * h.spq()), cMajor[(size_t) i]);
            e.offAt (up, cMajor[(size_t) i]);
        }
        return finish (h, e, worst);
    });
}

static void testHoldAndStack()
{
    std::cout << "== hold, stack, restrike ==\n";
    sweep ("Hold Latch: until the next chord key; ends on stop", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.hold = HoldMode::Latch;
        h.key (0.0, C, true);
        h.key (0.5, C, false);
        h.key (2.0, D, true);
        h.key (2.5, D, false);
        h.key (3.0, G, true);
        h.key (3.1, G, false);
        h.run (h.at (4.0), c.block);
        const int64_t stopAt = h.pos;
        h.stop();

        Expect e (h.spq());
        for (int n : cMajor) e.note (0.0, 2.0, n);
        for (int n : dMinor) e.note (2.0, 3.0, n);
        for (int n : gMajor) { e.on (3.0, n); e.offAt (stopAt, n); }
        return finish (h, e, worst);
    });

    sweep ("Hold Bar: to the bar line after the release, or the next chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.hold = HoldMode::Bar;
        h.key (1.0, C, true);
        h.key (1.5, C, false);
        h.key (4.5, D, true);
        h.key (5.0, D, false);
        h.key (6.0, E, true);
        h.key (7.5, E, false);
        h.run (h.at (8.5), c.block);

        Expect e (h.spq());
        for (int n : cMajor) e.note (1.0, 4.0, n);    // to the bar line after the release
        for (int n : dMinor) e.note (4.5, 6.0, n);    // to the next chord
        for (int n : eMinor) e.note (6.0, 8.0, n);
        return finish (h, e, worst);
    });

    sweep ("Hold while stopped: the same key again ends it", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.playing = false;
        h.settings.fx.hold = HoldMode::Bar;
        h.keyAt (1000, C, true);
        h.keyAt (2000, C, false);
        h.keyAt (50000, C, true);      // ends the held C; starts nothing
        h.keyAt (51000, C, false);
        h.keyAt (60000, D, true);
        h.keyAt (61000, D, false);
        h.keyAt (70000, D, true);
        h.run (80000, c.block);

        Expect e (h.spq());
        for (int n : cMajor) { e.onAt (1000, n); e.offAt (50000, n); }
        for (int n : dMinor) { e.onAt (60000, n); e.offAt (70000, n); }
        std::string why = compare (h.hits, e.hits, worst);
        if (h.engine->getPerformer().aliveCount() != 0)
            why += " a performance is still alive";
        return why;
    });

    sweep ("Stack +1&2, a Stack change and a Chord Group change restrike a strummed chord", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.stack = StackMode::PlusOneAndTwo;
        h.key (0.0, C, true);
        h.key (1.0, C, false);
        h.run (h.at (2.0), c.block);
        h.settings.fx.stack = StackMode::Off;
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 6;   // 1/16
        h.key (2.0, C, true);
        h.key (4.5, C, false);
        h.run (h.at (3.0), c.block);
        h.settings.group = ChordGroup::Sevenths;   // restrums from beat 3
        h.run (h.at (4.0), c.block);
        h.settings.fx.stack = StackMode::PlusOne;   // at the block that starts at beat 4
        const double change = h.ppq;
        h.run (h.at (5.0), c.block);

        Expect e (h.spq());
        for (int n : { 48, 52, 55, 60, 64, 67, 72, 76, 79 })
            e.note (0.0, 1.0, n);
        for (int i = 0; i < 3; ++i)
            e.note (2.0 + 0.25 * i, 3.0, cMajor[(size_t) i]);
        const int c7[] = { 48, 52, 55, 59 }, c7up[] = { 48, 52, 55, 59, 60, 64, 67, 71 };
        for (int i = 0; i < 4; ++i)
            e.note (3.0 + 0.25 * i, change, c7[i]);
        for (int i = 0; i < 8; ++i)
            if (change + 0.25 * i < 4.5 - 1e-9)
                e.note (change + 0.25 * i, 4.5, c7up[i]);
        return finish (h, e, worst);
    });
}

//==============================================================================
// Loop wraps, tempo changes, transport start.

static void testLoops()
{
    std::cout << "== loop wraps ==\n";
    for (auto style : { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot })
    {
        const std::string styleName = style == LoopStyle::Wrap ? "host doesn't split" : style == LoopStyle::Split ? "host splits" : "host splits, drops the overshoot";
        for (double loopLen : { 4.0, 3.5 })
            sweep ("Strum Up 1/16 across a " + std::to_string (loopLen).substr (0, 3) + "-beat loop (" + styleName + ")",
                   [style, loopLen] (const Config& c, int64_t& worst)
            {
                Host h (c.rate, c.bpm);
                h.looping = true;
                h.loopStart = 0.0;
                h.loopEnd = loopLen;
                h.style = style;
                h.settings.fx.strum = StrumMode::Up;
                h.settings.fx.strumRate = 6;   // 1/16
                h.key (loopLen - 0.25, C, true);   // the strum crosses the wrap
                h.key (loopLen + 1.0, C, false);
                h.run (h.at (loopLen + 1.5), c.block);

                // In samples the strum goes on as if there were no wrap: the tones move with the music.
                Expect e (h.spq());
                for (int i = 0; i < 3; ++i)
                    e.note (loopLen - 0.25 + 0.25 * i, loopLen + 1.0, cMajor[(size_t) i]);
                // A host that drops the overshoot loses up to a sample of song time per wrap.
                return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 3 : 1);
            });

        sweep ("A held chord across three loops (" + styleName + "): no restrike", [style] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            h.looping = true;
            h.loopStart = 0.0;
            h.loopEnd = 2.0;
            h.style = style;
            h.key (0.5, D, true);
            h.key (5.75, D, false);        // absolute beats
            h.run (h.at (6.0), c.block);

            Expect e (h.spq());
            for (int n : dMinor)
                e.note (0.5, 5.75, n);
            return finish (h, e, worst, style == LoopStyle::SplitDropOvershoot ? 6 : 1);
        });
    }
}

static void testTempoAndStart()
{
    std::cout << "== tempo changes, transport start ==\n";
    sweep ("Strum Up 1/16 after a tempo change (120 → 90 BPM, and 120 → 160): the new tempo's spacing",
           [] (const Config& c, int64_t& worst)
    {
        std::string why;
        for (double to : { 90.0, 160.0 })
        {
            Host h (c.rate, 120.0);
            h.settings.fx.strum = StrumMode::Up;
            h.settings.fx.strumRate = 6;   // 1/16
            const double spq1 = h.spq(), spq2 = c.rate * 60.0 / to;
            h.tempo.push_back ({ (int64_t) (2.1 * spq1), to });
            const int64_t press = (int64_t) (2.1 * spq1) + 8192, up = press + (int64_t) std::llround (2.0 * spq2);
            h.keyAt (press, C, true);
            h.keyAt (up, C, false);
            h.run (up + 4096, c.block);

            Expect e (h.spq());
            for (int i = 0; i < 3; ++i)
            {
                e.onAt (press + (int64_t) std::llround (0.25 * i * spq2), cMajor[(size_t) i]);
                e.offAt (up, cMajor[(size_t) i]);
            }
            int64_t w = 0;
            why += finish (h, e, w);
            worst = std::max (worst, w);
        }
        return why;
    });

    sweep ("Locate while a strum plays: its remaining tones move with the music", [] (const Config& c, int64_t& worst)
    {
        Host h (c.rate, c.bpm);
        h.settings.fx.strum = StrumMode::Up;
        h.settings.fx.strumRate = 6;   // 1/16
        h.key (1.0, C, true);
        h.run (h.at (1.05), c.block);  // the first tone is sounding; the others are queued
        const int64_t jumpAt = h.pos;
        const double from = h.ppq;
        h.ppq = 10.6;                  // locate forward
        const int64_t up = jumpAt + h.at (1.0);
        h.keyAt (up, C, false);
        h.run (up + h.at (0.5), c.block);

        Expect e (h.spq());
        e.on (1.0, cMajor[0]);
        for (int i = 1; i < 3; ++i)
            e.onAt (jumpAt + h.at (1.0 + 0.25 * i - from), cMajor[(size_t) i]);
        for (int n : cMajor)
            e.offAt (up, n);
        return finish (h, e, worst);
    });
}

//==============================================================================
// Determinism: the same playing gives the same output, including Random strum.

static void testDeterminism()
{
    std::cout << "== determinism ==\n";
    auto play = [] (int block)
    {
        Host h (44100.0, 120.0);
        h.settings.fx.strum = StrumMode::Random;
        h.settings.fx.strumRate = 0;   // 1/128
        for (int beat = 0; beat < 8; ++beat)
        {
            h.key (beat, C, true);
            h.key (beat + 0.75, C, false);
        }
        h.run (h.at (8.5), block);
        return h.hits;
    };
    const auto a = play (512), b = play (512), d = play (77);
    int64_t worst = 0;
    check (compare (a, b, worst, 0).empty() && compare (a, d, worst, 1).empty() && a.size() == 8 * 3 * 2,
           "Random strum on eight chords: identical runs, and the same at another block size");

    // A Random strum is reseeded by a transport start: two takes from the top are the same.
    auto take = [] (Host& h, int64_t base)
    {
        for (int beat = 0; beat < 4; ++beat)
        {
            h.keyAt (base + h.at (beat), D, true);
            h.keyAt (base + h.at (beat + 0.5), D, false);
        }
        h.run (base + h.at (4.5), 256);
    };
    Host h (48000.0, 120.0);
    h.settings.fx.strum = StrumMode::Random;
    h.settings.fx.strumRate = 4;
    take (h, 0);
    const auto take1 = h.hits;
    h.hits.clear();
    h.stop();
    h.hits.clear();
    h.playing = true;
    h.ppq = 0.0;
    const int64_t base = h.pos;
    take (h, base);
    std::vector<Hit> take2;
    for (auto x : h.hits) { x.sample -= base; take2.push_back (x); }
    check (compare (take1, take2, worst, 1).empty(), "Random strum: the same strums on every take from the top");
}

int main()
{
    testClock();
    testEventQueue();
    testBuildingBlocks();
    testStrumTiming();
    testHoldAndStack();
    testLoops();
    testTempoAndStart();
    testDeterminism();
    return testutil::finish ("test_fx");
}
