// What ChordZimmer's MIDI import makes of real files (PLAN.md §6 Phase 9, tuned after 1.0): for
// each file, the chords and sections it would put on the timeline, Match's top three for the
// melody, and the file's own note onsets (pitches struck together) to judge the names against.
// Not a test: a report to read.
//
//   clang++ -std=c++17 -O2 -I Source Tools/import_report.cpp -o /tmp/import_report
//   /tmp/import_report [--key N] [--onsets N] file.mid …
//
// --key: the home key (0 = C … 11); --minor: natural minor (else major). They spell the names and
// set the palette Match ranks.
// --played / --by-beat: read the chords as played, or a beat at a time (default: the import's own
// choice; the report says when it read by the beat).
// --onsets: how many of the file's onset groups to list (default 12; 0 for none).
#include "core/midiio/MidiImport.h"
#include "core/model/TimelineLayout.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace chordzimmer;

namespace
{
    std::string pitchName (int p)
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return std::string (names[p % 12]) + std::to_string (p / 12 - 2);   // Live's octaves: C3 = 60
    }

    std::string beatsText (double b)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "%g", b);
        return buf;
    }

    void report (const std::string& path, int key, Scale scale, int onsets, ImportReading reading)
    {
        std::ifstream in (path, std::ios::binary);
        const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
        const auto slash = path.find_last_of ('/');
        const std::string file = slash == std::string::npos ? path : path.substr (slash + 1);
        std::printf ("== %s\n", file.c_str());

        const auto r = importMidiFile (bytes.data(), bytes.size(), key, scale, "Imported", reading);
        if (! r.ok())
        {
            std::printf ("   error: %s\n\n", r.error.c_str());
            return;
        }
        MidiSong song;
        std::string error;
        readMidiFile (bytes.data(), bytes.size(), song, error);
        std::printf ("   %d notes (+%d drums), %g BPM, %d/%d, %d ppq; %d chords, %d sections, %d dropped; melody %d notes%s\n",
                     r.numNotes, r.drumNotes, song.bpm, song.numerator, song.denominator, song.ppq,
                     r.chords.numChords(), r.chords.numSections(), r.droppedChords, r.melodyNotes, r.byBeat ? "; read by the beat" : "");

        for (const auto& sec : r.chords.sections)
        {
            std::string line = "   " + sec.name + ":";
            for (const auto& c : sec.chords)
                line += " " + timelineChordLabel (c, key, scale).name + ":" + beatsText (c.beats());
            std::printf ("%s\n", line.c_str());
        }

        if (! r.melody.empty())
        {
            PaletteContext ctx;
            ctx.paletteKey = ctx.homeKey = key;
            ctx.paletteScale = ctx.homeScale = scale;
            const auto m = matchPalette (r.melody, ctx);
            std::string top;
            for (size_t i = 0; i < m.size() && i < 3; ++i)
                top += (i ? ", " : "") + m[i].cell.name;
            std::printf ("   Match: %s\n", top.c_str());
        }

        // The file's onsets: notes (not drums) starting within 1/32 of a beat of each other.
        if (onsets > 0)
        {
            const double ppq = song.ppq > 0 ? song.ppq : 960;
            std::vector<MidiNote> notes;
            for (const auto& n : song.notes)
                if ((n.channel & 15) != 9)
                    notes.push_back (n);
            int shown = 0;
            for (size_t i = 0; i < notes.size() && shown < onsets;)
            {
                size_t j = i;
                std::string group;
                while (j < notes.size() && (double) (notes[j].start - notes[i].start) / ppq <= 1.0 / 32.0)
                {
                    group += (j > i ? " " : "") + pitchName (notes[j].pitch);
                    ++j;
                }
                std::printf ("     beat %-7s %s\n", beatsText ((double) notes[i].start / ppq).c_str(), group.c_str());
                i = j;
                ++shown;
            }
        }
        std::printf ("\n");
    }
}

int main (int argc, char** argv)
{
    int key = 0, onsets = 12;
    Scale scale = Scale::Major;
    ImportReading reading = ImportReading::Auto;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--key") == 0 && i + 1 < argc)
            key = std::atoi (argv[++i]);
        else if (std::strcmp (argv[i], "--played") == 0)
            reading = ImportReading::AsPlayed;
        else if (std::strcmp (argv[i], "--by-beat") == 0)
            reading = ImportReading::ByBeat;
        else if (std::strcmp (argv[i], "--minor") == 0)
            scale = Scale::NaturalMinor;
        else if (std::strcmp (argv[i], "--onsets") == 0 && i + 1 < argc)
            onsets = std::atoi (argv[++i]);
        else
            report (argv[i], key, scale, onsets, reading);
    }
    return 0;
}
