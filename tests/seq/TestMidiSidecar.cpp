// The lossless sidecar must only win while it still describes the file's notes, and the
// export of odd-length shuffled patterns must loop the way the player plays them.
#include "SeqTestSupport.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace acid::seq;
using acid::seq::test::randomPattern;
using acid::seq::test::randomPlayablePattern;

namespace
{
using Bytes = std::vector<std::uint8_t>;

Pattern fromText (const char* text)
{
    Pattern p;
    REQUIRE (fromStepText (text, p));
    return p;
}

void putVlq (Bytes& out, std::uint64_t v)
{
    std::uint8_t buf[10];
    int n = 0;
    buf[n++] = static_cast<std::uint8_t> (v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<std::uint8_t> (0x80 | (v & 0x7F));
    while (n > 0) out.push_back (buf[--n]);
}

/** What a host does when it saves a clip: the same events (text events included), possibly
    at another resolution and after an edit, written back as a type-0 file. */
Bytes resave (const Bytes& original, int newPpq, const std::function<void (std::vector<SmfEvent>&)>& edit = {})
{
    SmfFile f;
    REQUIRE (parseSmf (original, f));
    std::vector<SmfEvent> ev = f.tracks[0].events;
    const double scale = static_cast<double> (newPpq) / static_cast<double> (f.ppq);
    for (SmfEvent& e : ev) e.tick = static_cast<std::int64_t> (std::llround (static_cast<double> (e.tick) * scale));
    if (edit) edit (ev);
    // Stable by tick. Not std::stable_sort: its buffer uses nothrow operator new, which the
    // test allocation guard does not replace (an ASan alloc-dealloc mismatch).
    std::vector<std::size_t> order (ev.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort (order.begin(), order.end(), [&ev] (std::size_t a, std::size_t b) {
        return ev[a].tick != ev[b].tick ? ev[a].tick < ev[b].tick : a < b;
    });
    std::vector<SmfEvent> sorted;
    for (std::size_t i : order) sorted.push_back (ev[i]);
    ev = std::move (sorted);

    Bytes trk;
    std::int64_t now = 0;
    for (const SmfEvent& e : ev)
    {
        putVlq (trk, static_cast<std::uint64_t> (e.tick - now));
        now = e.tick;
        trk.push_back (e.status);
        if (e.status == 0xFF)
        {
            trk.push_back (e.data1);
            putVlq (trk, e.payload.size());
            trk.insert (trk.end(), e.payload.begin(), e.payload.end());
        }
        else
        {
            trk.push_back (e.data1);
            const int type = e.status & 0xF0;
            if (type != 0xC0 && type != 0xD0) trk.push_back (e.data2);
        }
    }
    Bytes out { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                static_cast<std::uint8_t> (newPpq >> 8), static_cast<std::uint8_t> (newPpq & 0xFF),
                'M', 'T', 'r', 'k' };
    const auto n = static_cast<std::uint32_t> (trk.size());
    for (int s = 24; s >= 0; s -= 8) out.push_back (static_cast<std::uint8_t> ((n >> s) & 0xFF));
    out.insert (out.end(), trk.begin(), trk.end());
    return out;
}

bool isNoteOn (const SmfEvent& e) { return (e.status & 0xF0) == 0x90 && e.data2 > 0; }
bool isNoteOff (const SmfEvent& e) { return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0); }

Pattern heuristicImport (const Bytes& b)
{
    MidiImportOptions o;
    o.useEmbeddedPattern = false;
    Pattern p;
    REQUIRE (readMidiFile (b, p, o));
    return p;
}
}

TEST_CASE ("an unedited clip keeps its sidecar through a host re-save, at any resolution", "[midi][sidecar]")
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        const Pattern orig = randomPattern (seed);
        const Bytes b = writeMidiFile (orig);
        for (int ppq : { 480, 960, 96, 1000, 384 })
        {
            Pattern back;
            MidiImportReport rep;
            REQUIRE (readMidiFile (resave (b, ppq), back, {}, &rep));
            INFO ("seed " << seed << " ppq " << ppq);
            REQUIRE (rep.usedEmbedded);
            REQUIRE_FALSE (rep.sidecarStale);
            REQUIRE (samePattern (back, orig));
        }
    }
}

TEST_CASE ("the sidecar is used whichever export options wrote the notes", "[midi][sidecar]")
{
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        Pattern orig = randomPattern (seed);
        if (seed % 2 == 0) orig.shuffle = 60;
        MidiExportOptions o;
        o.applyShuffle = seed % 3 != 0;
        o.applyTranspose = seed % 5 != 0;
        o.slideIntoRest = static_cast<SlideIntoRest> (seed % 3);
        o.twoPassesForOddShuffle = seed % 7 != 0;
        o.vibratoCC = seed % 2 == 0;
        o.padToBar = seed % 4 == 0;
        o.ppq = seed % 2 == 0 ? 960 : 480;
        o.bpm = 60.0 + static_cast<double> (seed % 140);
        if (seed % 6 == 0) o.accentVelocity = o.normalVelocity = 100;
        Pattern back;
        MidiImportReport rep;
        REQUIRE (readMidiFile (writeMidiFile (orig, o), back, {}, &rep));
        INFO ("seed " << seed);
        REQUIRE (rep.usedEmbedded);
        REQUIRE (samePattern (back, orig));
    }
}

// QA finding 25: the sidecar path returned before notesRead was set, so importing a file this
// product wrote reported 0 notes. The report describes the file whichever path built the pattern.
TEST_CASE ("an import that uses the sidecar still reports the notes in the file", "[midi][sidecar]")
{
    Pattern back;
    MidiImportReport rep;
    REQUIRE (readMidiFile (writeMidiFile (fromText ("C D# G A# C+ . G F")), back, {}, &rep));
    REQUIRE (rep.usedEmbedded);
    CHECK (rep.notesRead == 7);

    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        const Bytes b = writeMidiFile (randomPattern (seed));
        MidiImportReport embedded, heuristic;
        MidiImportOptions o;
        o.useEmbeddedPattern = false;
        Pattern p1, p2;
        const bool okEmbedded = readMidiFile (b, p1, {}, &embedded);
        const bool okHeuristic = readMidiFile (b, p2, o, &heuristic);
        INFO ("seed " << seed);
        REQUIRE (okEmbedded);
        // A pattern of rests has no notes for the heuristic path to read; the sidecar still
        // restores it, and the report then says 0 notes.
        REQUIRE (embedded.notesRead == (okHeuristic ? heuristic.notesRead : 0));
    }
}

TEST_CASE ("a sidecar whose notes were edited in the host is stale and does not win", "[midi][sidecar]")
{
    const Pattern line = fromText ("C:A E G C+:S D+ . C . C:A ~ . . C . G- .");
    const Bytes b = writeMidiFile (line);
    {
        Pattern back;
        MidiImportReport rep;
        REQUIRE (readMidiFile (resave (b, 480), back, {}, &rep));
        REQUIRE (rep.usedEmbedded);
    }

    struct HostEdit
    {
        const char* name;
        std::function<void (std::vector<SmfEvent>&)> run;
    };
    const HostEdit edits[] {
        { "pitch", [] (std::vector<SmfEvent>& ev) {
            for (SmfEvent& e : ev)
                if ((isNoteOn (e) || isNoteOff (e)) && e.data1 == 40) e.data1 = 41;
        } },
        { "velocity to accent", [] (std::vector<SmfEvent>& ev) {
            for (SmfEvent& e : ev)
                if (isNoteOn (e) && e.data1 == 43) e.data2 = 110;
        } },
        { "velocity, same class", [] (std::vector<SmfEvent>& ev) {
            for (SmfEvent& e : ev)
                if (isNoteOn (e) && e.data1 == 43) e.data2 = 33;
        } },
        { "moved later", [] (std::vector<SmfEvent>& ev) {
            for (SmfEvent& e : ev)
                if ((isNoteOn (e) || isNoteOff (e)) && e.data1 == 31) e.tick += 120;
        } },
        { "longer", [] (std::vector<SmfEvent>& ev) {
            for (SmfEvent& e : ev)
                if (isNoteOff (e) && e.data1 == 31) e.tick += 50;
        } },
        { "deleted", [] (std::vector<SmfEvent>& ev) {
            ev.erase (std::remove_if (ev.begin(), ev.end(), [] (const SmfEvent& e) { return (isNoteOn (e) || isNoteOff (e)) && e.data1 == 31; }), ev.end());
        } },
        { "added", [] (std::vector<SmfEvent>& ev) {
            SmfEvent on;
            on.tick = 5 * 120;
            on.status = 0x90;
            on.data1 = 45;
            on.data2 = 32;
            SmfEvent off = on;
            off.tick += 61;
            off.status = 0x80;
            off.data2 = 0;
            ev.push_back (on);
            ev.push_back (off);
        } },
    };
    for (const HostEdit& e : edits)
    {
        INFO (e.name);
        const Bytes edited = resave (b, 480, e.run);
        Pattern back;
        MidiImportReport rep;
        REQUIRE (readMidiFile (edited, back, {}, &rep));
        REQUIRE_FALSE (rep.usedEmbedded);
        REQUIRE (rep.sidecarStale);
        REQUIRE (samePattern (back, heuristicImport (edited)));
    }
}

TEST_CASE ("export: odd-length shuffled patterns are written as two passes", "[midi]")
{
    // The player delays odd steps of the absolute grid, so step 0 of every second pass of a
    // three-step pattern is late. Two passes loop the same way.
    Pattern p = fromText ("C D E");
    p.shuffle = 100;
    MidiExportOptions o;
    o.embedPattern = false;
    SmfFile f;
    REQUIRE (parseSmf (writeMidiFile (p, o), f));
    std::vector<std::int64_t> ons;
    for (const SmfEvent& e : f.tracks[0].events)
        if (isNoteOn (e)) ons.push_back (e.tick);
    REQUIRE (ons == std::vector<std::int64_t> { 0, 180, 240, 420, 480, 660 });
    REQUIRE (f.tracks[0].endTick == 720);
    const Pattern back = heuristicImport (writeMidiFile (p, o));
    REQUIRE (back.length == 6);
    REQUIRE (back.shuffle == 100);

    o.twoPassesForOddShuffle = false;
    REQUIRE (parseSmf (writeMidiFile (p, o), f));
    REQUIRE (f.tracks[0].endTick == 360);

    // Even lengths, or no shuffle, are one pass.
    o.twoPassesForOddShuffle = true;
    p.length = 4;
    REQUIRE (parseSmf (writeMidiFile (p, o), f));
    REQUIRE (f.tracks[0].endTick == 480);
    p.length = 3;
    p.shuffle = 0;
    REQUIRE (parseSmf (writeMidiFile (p, o), f));
    REQUIRE (f.tracks[0].endTick == 360);

    // With the sidecar the round trip is still the original three steps.
    p.shuffle = 100;
    Pattern exact;
    MidiImportReport rep;
    REQUIRE (readMidiFile (writeMidiFile (p), exact, {}, &rep));
    REQUIRE (rep.usedEmbedded);
    REQUIRE (samePattern (exact, p));
}

TEST_CASE ("MIDI round trip of odd shuffled lengths through two passes keeps what is heard", "[midi]")
{
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        const int len = 1 + 2 * static_cast<int> (seed % 16);   // odd, up to 31: two passes fit in 64
        Pattern p = randomPlayablePattern (seed, len);
        p.shuffle = 100;
        MidiExportOptions o;
        o.embedPattern = false;
        int oddNotes = 0, notes = 0;
        for (int i = 0; i < len; ++i) notes += p.steps[static_cast<std::size_t> (i)].state == StepState::Note;
        for (int i = 0; i < 2 * len; ++i) oddNotes += (i & 1) && p.steps[static_cast<std::size_t> (i % len)].state == StepState::Note;
        if (notes < 2 || oddNotes < 2) continue;

        Pattern twice = p;
        twice.length = static_cast<std::uint8_t> (2 * len);
        for (int i = len; i < 2 * len; ++i) twice.steps[static_cast<std::size_t> (i)] = p.steps[static_cast<std::size_t> (i - len)];
        Pattern back;
        REQUIRE (readMidiFile (writeMidiFile (p, o), back, {}));
        INFO ("seed " << seed << "\n  orig " << toStepText (twice) << "\n  back " << toStepText (back));
        REQUIRE (back.shuffle == 100);
        Pattern lhs = twice, rhs = back;
        lhs.gateLength = rhs.gateLength = 0;
        REQUIRE (acid::seq::test::audibleView (rhs) == acid::seq::test::audibleView (lhs));
    }
}
