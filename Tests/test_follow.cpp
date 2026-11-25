// Follow tests (1.2, Follow.h): where the palette shows the chord that's playing. Every expected
// key and cell was worked out by hand from the palettes of C major and its neighbours.
#include "TestUtil.h"
#include "core/theory/Follow.h"
#include <iostream>

using namespace chordzimmer;
using testutil::check;

namespace
{
    unsigned mask (std::initializer_list<int> pcs)
    {
        unsigned m = 0;
        for (int pc : pcs)
            m |= 1u << (pc % 12);
        return m;
    }

    CircleOfFifths at (int fifths, int home = 0, Scale scale = Scale::Major)
    {
        CircleOfFifths nav;
        nav.homeKey = home;
        nav.homeScale = nav.paletteScale = scale;
        nav.fifths = fifths;
        return nav;
    }

    // The row a variant is in, by its label ("triad", "7" …).
    std::string row (int variant) { return variant >= 0 ? std::string (tierVariants[variant].label) : "-"; }
}

int main()
{
    std::cout << "== the search order ==\n";
    const auto o = followOrder (0);
    check (o.size() == 13 && o[0] == 0 && o[1] == -1 && o[2] == 1 && o[11] == -6 && o[12] == 6, "from home: 0, -1, +1, -2, +2 … -6, +6");
    const auto edge = followOrder (6);
    check (edge.size() == 13 && edge[0] == 6 && edge[1] == 5 && edge[12] == -6, "from +6: 6, 5, 4 … -6 (only what the navigator reaches)");

    std::cout << "== exact ==\n";
    auto s = followSpot (mask ({ 0, 4, 7 }), 0, at (0));
    check (s.found && s.exact && s.fifths == 0 && s.degree == 0 && row (s.variant) == "triad", "C in C major: here, the I column, the triad row (" + row (s.variant) + ")");
    s = followSpot (mask ({ 2, 5, 9, 0 }), 2, at (0));
    check (s.exact && s.fifths == 0 && s.degree == 1 && row (s.variant) == "7", "Dm7 in C major: here, ii, the 7th row");
    s = followSpot (mask ({ 10, 2, 5 }), 10, at (0));
    check (s.exact && s.fifths == -1 && s.degree == 3, "Bb from C: a fifth down (F major), its IV");
    s = followSpot (mask ({ 2, 6, 9 }), 2, at (0));
    check (s.exact && s.fifths == 1 && s.degree == 4, "D from C: a fifth up (G major), its V");
    s = followSpot (mask ({ 5, 9, 0 }), 5, at (2));
    check (s.exact && s.fifths == 0, "F with the palette at D major (+2): the nearest that holds it is C (0), not F (-1) or Bb (-2)");
    s = followSpot (mask ({ 7, 11, 2 }), 7, at (0));
    check (s.exact && s.fifths == 0, "G in C major stays here, though G major (+1) and D major (+2) hold it too");
    s = followSpot (mask ({ 1, 5, 8 }), 1, at (0));
    check (s.exact && s.fifths == -4, "Db from C: four fifths down (Ab major), its IV");

    std::cout << "== close ==\n";
    // C E G with Db and F# (no palette has it): here, the cell on C with a major 3rd sharing most:
    // C7b9 (C E G Bb Db, the 7b9 row) shares four, adds one and misses one.
    s = followSpot (mask ({ 0, 4, 7, 1, 6 }), 0, at (0));
    check (s.found && ! s.exact && s.fifths == 0 && s.root == 0 && row (s.variant) == "7b9" && s.pitchClasses == mask ({ 0, 4, 7, 10, 1 }),
           "C E G + Db F#: no palette has it; C7b9, here (" + row (s.variant) + ")");
    // A near likeness doesn't move the key: Bm7b5 with an added E, from C, stays in this palette, not
    // in one further round.
    s = followSpot (mask ({ 11, 2, 5, 9, 4 }), 11, at (0));
    check (s.found && ! s.exact && s.fifths == 0 && s.root == 11, "Bm7b5 with an E: no key move for a likeness (" + row (s.variant) + ")");
    // A minor never lands on A major, though it shares two notes.
    s = followSpot (mask ({ 9, 0, 4, 3 }), 9, at (0));
    check (s.found && follow_detail::third (s.pitchClasses, 9) == 1, "Am with an Eb in it: a minor chord on A, never A major");
    // An Em11 as an import names it (E G B D A F#): exact somewhere, or close on E minor.
    s = followSpot (mask ({ 4, 7, 11, 2, 9, 6 }), 4, at (0));
    check (s.found && s.root == 4 && follow_detail::third (s.pitchClasses, 4) == 1, "E G B D F# A: a minor chord on E");

    std::cout << "== nothing ==\n";
    check (! followSpot (0, -1, at (0)).found, "no notes: nowhere");
    check (! followSpot (mask ({ 0, 4, 7 }), 12, at (0)).found, "a root out of range: nowhere");

    return testutil::finish ("test_follow");
}
