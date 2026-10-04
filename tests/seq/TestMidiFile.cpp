#include "SeqTestSupport.h"
#include "acid/common/Rng.h"
#include "acid/seq/Generator.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace acid::seq;
using acid::seq::test::audibleView;
using acid::seq::test::randomPattern;
using acid::seq::test::randomPlayablePattern;

namespace
{
using Bytes = std::vector<std::uint8_t>;

Pattern fromText (const char* text)
{
    Pattern p;
    std::string err;
    INFO (text);
    REQUIRE (fromStepText (text, p, &err));
    return p;
}

std::uint32_t be32 (const Bytes& b, std::size_t at)
{
    return (std::uint32_t (b[at]) << 24) | (std::uint32_t (b[at + 1]) << 16) | (std::uint32_t (b[at + 2]) << 8) | b[at + 3];
}

/** Independent structural check, written against the SMF spec rather than our parser:
    header chunk, one track chunk whose length matches the file, every delta a VLQ of at most
    four bytes, channel events with data bytes below 0x80, the track closed by FF 2F 00 at
    exactly the chunk end. Returns the absolute tick of End of Track. */
std::int64_t checkStructure (const Bytes& b)
{
    REQUIRE (b.size() >= 22);
    REQUIRE (std::memcmp (b.data(), "MThd", 4) == 0);
    REQUIRE (be32 (b, 4) == 6);
    REQUIRE (b[8] == 0);
    REQUIRE (b[9] == 0);     // format 0
    REQUIRE (b[10] == 0);
    REQUIRE (b[11] == 1);    // one track
    REQUIRE ((b[12] & 0x80) == 0);
    REQUIRE (std::memcmp (b.data() + 14, "MTrk", 4) == 0);
    const std::size_t len = be32 (b, 18);
    REQUIRE (22 + len == b.size());

    std::size_t i = 22;
    std::int64_t tick = 0;
    bool ended = false;
    while (i < b.size())
    {
        REQUIRE_FALSE (ended);
        std::uint32_t delta = 0;
        int n = 0;
        while (true)
        {
            REQUIRE (i < b.size());
            const std::uint8_t c = b[i++];
            delta = (delta << 7) | (c & 0x7F);
            REQUIRE (++n <= 4);
            if ((c & 0x80) == 0) break;
        }
        tick += delta;
        REQUIRE (i < b.size());
        const std::uint8_t status = b[i++];
        REQUIRE (status >= 0x80);    // we never write running status
        if (status == 0xFF)
        {
            const std::uint8_t type = b[i++];
            std::uint32_t mlen = 0;
            while (true)
            {
                const std::uint8_t c = b[i++];
                mlen = (mlen << 7) | (c & 0x7F);
                if ((c & 0x80) == 0) break;
            }
            i += mlen;
            if (type == 0x2F)
            {
                REQUIRE (mlen == 0);
                ended = true;
            }
        }
        else
        {
            REQUIRE (status < 0xF0);
            const int dataBytes = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
            for (int k = 0; k < dataBytes; ++k) REQUIRE (b[i++] < 0x80);
        }
    }
    REQUIRE (i == b.size());
    REQUIRE (ended);
    return tick;
}

struct Note
{
    std::int64_t on, off;
    int key, vel;
};

/** Notes of the first track, checking that every Note On has its Note Off and that a key
    never overlaps itself. */
std::vector<Note> notesOf (const Bytes& b, std::vector<SmfEvent>* ccs = nullptr)
{
    SmfFile f;
    std::string err;
    REQUIRE (parseSmf (b, f, &err));
    std::vector<Note> out;
    int open[128];
    for (int& o : open) o = -1;
    for (const SmfEvent& e : f.tracks[0].events)
    {
        const int type = e.status & 0xF0;
        if (type == 0xB0 && ccs != nullptr) ccs->push_back (e);
        if (type == 0x90 && e.data2 > 0)
        {
            REQUIRE (open[e.data1] < 0);
            open[e.data1] = static_cast<int> (out.size());
            out.push_back (Note { e.tick, -1, e.data1, e.data2 });
        }
        else if (type == 0x80 || (type == 0x90 && e.data2 == 0))
        {
            REQUIRE (open[e.data1] >= 0);
            out[static_cast<std::size_t> (open[e.data1])].off = e.tick;
            open[e.data1] = -1;
        }
    }
    for (int o : open) REQUIRE (o < 0);
    for (const Note& n : out) REQUIRE (n.off > n.on);
    return out;
}

MidiExportOptions plain()
{
    MidiExportOptions o;
    o.embedPattern = false;
    return o;
}

void saveForInspection (const Bytes& b, const char* name)
{
    std::ofstream f (std::string (ACID_TEST_OUTPUT_DIR) + "/" + name, std::ios::binary);
    f.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
}
}

TEST_CASE ("export: notes, gates, ties, slides and clip length", "[midi]")
{
    // 120 BPM, 480 ppq: a 1/16 step is 120 ticks; the authentic gate is 60 ticks plus 1.35 ms (1 tick).
    const Pattern p = fromText ("C C+:S D ~ . E:A");
    const Bytes b = writeMidiFile (p, plain());
    saveForInspection (b, "seq_export_basic.mid");
    REQUIRE (checkStructure (b) == 720);
    const auto n = notesOf (b);
    REQUIRE (n.size() == 4);
    CHECK ((n[0].on == 0 && n[0].off == 61 && n[0].key == 36 && n[0].vel == 32));
    CHECK ((n[1].on == 120 && n[1].key == 48));
    CHECK ((n[2].on == 240 && n[2].key == 38));
    CHECK (n[1].off == 241);                       // overlaps the slide target by one tick
    CHECK (n[2].off == 360 + 61);                  // tie: gate end of the tied step
    CHECK ((n[3].on == 600 && n[3].off == 661 && n[3].key == 40 && n[3].vel == 96));

    SmfFile f;
    REQUIRE (parseSmf (b, f));
    REQUIRE (f.format == 0);
    REQUIRE (f.ppq == 480);
    bool tempo = false;
    for (const SmfEvent& e : f.tracks[0].events)
        if (e.status == 0xFF && e.data1 == 0x51)
        {
            tempo = true;
            REQUIRE (e.payload == std::vector<std::uint8_t> { 0x07, 0xA1, 0x20 });   // 500000 us
        }
    REQUIRE (tempo);
}

TEST_CASE ("export: gate options and the next-onset clamp", "[midi]")
{
    Pattern p = fromText ("C C");
    p.gateLength = 100;
    auto n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[0].off == 119);   // a full-step gate still ends before the next onset
    p.gateLength = 25;
    n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[0].off == 30);
    p.steps[0].gate = 127;
    n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[0].off == 119);
    p.steps[0].gate = 0;
    p.div = StepDiv::d1_8;
    p.divMod = DivMod::Triplet;
    n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[1].on == 160);
    CHECK (n[0].off == 40);
}

TEST_CASE ("export: shuffle delays odd steps of Full divisions only", "[midi]")
{
    Pattern p = fromText ("C C C C");
    p.shuffle = 100;
    auto n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[1].on == 180);          // 75:25 pair at 100 %
    CHECK (n[0].off <= 179);
    CHECK (n[2].on == 240);
    CHECK (n[3].on == 420);
    CHECK (n[3].off <= 479);
    p.shuffle = 50;
    n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[1].on == 150);
    p.divMod = DivMod::Triplet;
    n = notesOf (writeMidiFile (p, plain()));
    CHECK (n[1].on == 80);           // triplet ignores shuffle
    MidiExportOptions o = plain();
    o.applyShuffle = false;
    p.divMod = DivMod::Full;
    n = notesOf (writeMidiFile (p, o));
    CHECK (n[1].on == 120);
}

TEST_CASE ("export: same-pitch slides merge, accent changes use CC64", "[midi]")
{
    auto n = notesOf (writeMidiFile (fromText ("C:S C C"), plain()));
    REQUIRE (n.size() == 2);
    CHECK ((n[0].on == 0 && n[0].off == 181));   // merged through step 1's gate
    CHECK (n[1].on == 240);

    std::vector<SmfEvent> cc;
    n = notesOf (writeMidiFile (fromText ("C:S C:A ."), plain()), &cc);
    REQUIRE (n.size() == 2);
    CHECK ((n[0].off == 120 && n[1].on == 120 && n[1].vel == 96));
    REQUIRE (cc.size() == 2);
    CHECK ((cc[0].data1 == 64 && cc[0].data2 == 127 && cc[0].tick == 120));
    CHECK ((cc[1].data1 == 64 && cc[1].data2 == 0 && cc[1].tick == n[1].off));
}

TEST_CASE ("export: slide into a rest follows the policy; loop glide", "[midi]")
{
    const Pattern p = fromText ("C:S . D .");
    MidiExportOptions o = plain();
    o.slideIntoRest = SlideIntoRest::Release;
    auto n = notesOf (writeMidiFile (p, o));
    CHECK (n[0].off == 61);
    o.slideIntoRest = SlideIntoRest::HoldThroughRest;
    n = notesOf (writeMidiFile (p, o));
    CHECK (n[0].off == 120 + 61);
    o.slideIntoRest = SlideIntoRest::SustainToNextNote;
    n = notesOf (writeMidiFile (p, o));
    CHECK (n[0].off == 241);

    // The last note slides into step 0: it is held to the end of the clip.
    n = notesOf (writeMidiFile (fromText ("C . D:S"), plain()));
    CHECK (n[1].off == 360);
    // One-step drone.
    n = notesOf (writeMidiFile (fromText ("C:S"), plain()));
    CHECK ((n.size() == 1 && n[0].off == 120));
}

TEST_CASE ("export: vibrato CC1, ratchets, transpose, probability zero", "[midi]")
{
    std::vector<SmfEvent> cc;
    auto n = notesOf (writeMidiFile (fromText ("C:V D E:V"), plain()), &cc);
    REQUIRE (cc.size() == 4);
    CHECK ((cc[0].data1 == 1 && cc[0].data2 == 127 && cc[0].tick == 0));
    CHECK ((cc[1].data2 == 0 && cc[1].tick == n[0].off));
    CHECK ((cc[2].data2 == 127 && cc[2].tick == 240));
    CHECK ((cc[3].data2 == 0 && cc[3].tick == n[2].off));

    n = notesOf (writeMidiFile (fromText ("C:R4 D"), plain()));
    REQUIRE (n.size() == 5);
    CHECK ((n[0].on == 0 && n[1].on == 30 && n[2].on == 60 && n[3].on == 90 && n[4].on == 120));
    for (int k = 0; k < 4; ++k) CHECK (n[static_cast<std::size_t> (k)].off < n[static_cast<std::size_t> (k + 1)].on);

    Pattern t = fromText ("C D:P0 E");
    t.transpose = 12;
    n = notesOf (writeMidiFile (t, plain()));
    REQUIRE (n.size() == 2);
    CHECK (n[0].key == 48);
    CHECK (n[1].key == 52);
    MidiExportOptions o = plain();
    o.applyTranspose = false;
    CHECK (notesOf (writeMidiFile (t, o))[0].key == 36);
    o.padToBar = true;
    CHECK (checkStructure (writeMidiFile (t, o)) == 1920);
}

TEST_CASE ("MIDI round trip through the embedded pattern is exact", "[midi]")
{
    for (std::uint64_t seed = 1; seed <= 500; ++seed)
    {
        const Pattern orig = randomPattern (seed);
        const Bytes b = writeMidiFile (orig);
        checkStructure (b);
        Pattern back;
        MidiImportReport rep;
        REQUIRE (readMidiFile (b, back, {}, &rep));
        REQUIRE (rep.usedEmbedded);
        REQUIRE (samePattern (back, orig));
    }
}

TEST_CASE ("MIDI round trip without the sidecar keeps what is heard", "[midi]")
{
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 1500; ++seed)
    {
        const int len = 1 + static_cast<int> (seed % 64);
        Pattern p = randomPlayablePattern (seed, len);
        p.shuffle = static_cast<std::uint8_t> ((seed % 3 == 0) ? 0 : (seed % 3 == 1) ? 40 : 100);
        p.gateLength = static_cast<std::uint8_t> ((seed % 4 == 0) ? 0 : 20 + seed % 70);
        if (seed % 5 == 0)
        {
            p.div = StepDiv::d1_8;
            p.divMod = DivMod::Triplet;
            p.shuffle = 0;
        }
        // One pass: the heuristics then see the pattern's own length (two-pass export of odd
        // shuffled lengths is covered separately).
        MidiExportOptions onePass = plain();
        onePass.twoPassesForOddShuffle = false;
        const Bytes b = writeMidiFile (p, onePass);
        checkStructure (b);
        Pattern back;
        MidiImportReport rep;
        int notes = 0;
        for (int i = 0; i < len; ++i) notes += p.steps[static_cast<std::size_t> (i)].state == StepState::Note;
        INFO (rep.error);
        if (notes == 0)
        {
            REQUIRE_FALSE (readMidiFile (b, back, {}, &rep));   // nothing to import
            continue;
        }
        REQUIRE (readMidiFile (b, back, {}, &rep));
        REQUIRE_FALSE (rep.usedEmbedded);

        // Grid recovery needs evidence: enough notes for the division and shuffle detectors.
        if (notes < 4) continue;
        // 1/8 triplets that never use the third triplet of a beat are the same times as 1/16
        // with a 2:1 shuffle; only the clip end can tell them apart.
        if (p.divMod == DivMod::Triplet && back.divMod != DivMod::Triplet)
        {
            bool third = false;
            for (int i = 2; i < len; i += 3) third = third || p.steps[static_cast<std::size_t> (i)].state == StepState::Note;
            if (! third && len % 4 == 0) continue;
        }

        INFO ("seed " << seed << "\n  orig " << toStepText (p) << "\n  back " << toStepText (back));
        REQUIRE (back.div == p.div);
        REQUIRE (back.divMod == p.divMod);
        // Shuffle is only visible when at least two odd steps hold notes.
        int oddNotes = 0;
        for (int i = 1; i < len; i += 2) oddNotes += p.steps[static_cast<std::size_t> (i)].state == StepState::Note;
        if (oddNotes < 2 && p.shuffle > 0) continue;
        REQUIRE (back.shuffle == p.shuffle);
        // The end of a gate is only visible on notes that are not tied, slid or clamped.
        Pattern lhs = p, rhs = back;
        lhs.gateLength = rhs.gateLength = 0;
        REQUIRE (audibleView (rhs) == audibleView (lhs));
        ++checked;
    }
    REQUIRE (checked > 1000);
}

TEST_CASE ("MIDI round trip of generated lines", "[midi]")
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        Pattern p;
        p.gen.useVibrato = true;
        regenerate (p, seed);
        Pattern back;
        REQUIRE (readMidiFile (writeMidiFile (p, plain()), back));
        INFO ("seed " << seed << "\n  orig " << toStepText (p) << "\n  back " << toStepText (back));
        REQUIRE (audibleView (back) == audibleView (p));
        REQUIRE (back.gateLength == 0);   // authentic gate recognised
    }
}

TEST_CASE ("import heuristics: mono reduction, accents, octave fit, length", "[midi]")
{
    // A chord on step 0 (the latest start wins, then the loudest), an off-grid wobble, a loud
    // note, notes far above the range, and an End of Track that sets the length.
    std::vector<MidiNote> notes {
        { 0.0, 0.1, 100, false, false, 60 },
        { 0.0, 0.1, 103, false, false, 90 },
        { 0.01, 0.1, 98, false, false, 50 },     // latest start in step 0
        { 0.27, 0.35, 101, false, false, 127 },  // slightly late, still step 1
        { 0.5, 0.6, 105, false, true, 30 },
    };
    const Bytes b = writeMidiFile (notes, 2.0);
    Pattern p;
    MidiImportReport rep;
    REQUIRE (readMidiFile (b, p, {}, &rep));
    REQUIRE (rep.notesRead == 5);
    REQUIRE (rep.notesDropped == 2);
    REQUIRE (p.length == 8);
    REQUIRE (rep.octaveShift == -24);   // the smallest move that fits every note
    REQUIRE (p.transpose == 24);
    REQUIRE (stepMidiNote (p.steps[0]) + p.transpose == 98);
    REQUIRE (stepMidiNote (p.steps[1]) + p.transpose == 101);
    REQUIRE (p.steps[1].accent);
    REQUIRE_FALSE (p.steps[0].accent);
    REQUIRE (p.steps[2].vibrato);
    REQUIRE (p.steps[3].state == StepState::Rest);

    MidiImportOptions o;
    o.keepPitch = false;
    REQUIRE (readMidiFile (b, p, o));
    REQUIRE (p.transpose == 0);

    // Overlap means slide; a long note means ties.
    std::vector<MidiNote> legato {
        { 0.0, 0.30, 40 }, { 0.25, 0.70, 45 }, { 1.0, 1.1, 40 },
    };
    REQUIRE (readMidiFile (writeMidiFile (legato, 1.25), p));
    StepTextOptions so;
    so.header = false;
    REQUIRE (toStepText (p, so) == "E:S A ~ . E");
}

TEST_CASE ("import picks the triplet grid when the onsets need it", "[midi]")
{
    std::vector<MidiNote> trip;
    for (int i = 0; i < 12; ++i)
        trip.push_back (MidiNote { i / 3.0, i / 3.0 + 0.15, static_cast<std::uint8_t> (48 + i % 3) });
    Pattern p;
    MidiImportReport rep;
    REQUIRE (readMidiFile (writeMidiFile (trip, 4.0), p, {}, &rep));
    REQUIRE (rep.suggestTriplet);
    REQUIRE (p.div == StepDiv::d1_8);
    REQUIRE (p.divMod == DivMod::Triplet);
    REQUIRE (p.length == 12);

    MidiImportOptions forced;
    forced.autoDivision = false;
    forced.detectShuffle = false;
    REQUIRE (readMidiFile (writeMidiFile (trip, 4.0), p, forced, &rep));
    REQUIRE (p.div == StepDiv::d1_16);
    REQUIRE (rep.offGridFraction > 0.2);
}

TEST_CASE ("parser handles running status, format 1 and note-on velocity zero", "[midi]")
{
    // Hand-written format 1 file: a conductor track with tempo, then a track using running
    // status and Note On velocity 0 as Note Off, at 96 ppq.
    const Bytes b {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 96,
        'M', 'T', 'r', 'k', 0, 0, 0, 11,
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0xFF, 0x2F, 0x00,
        'M', 'T', 'r', 'k', 0, 0, 0, 17,
        0x00, 0x90, 48, 50,
        0x0C, 48, 0,          // running status, velocity 0 = off
        0x0C, 51, 110,
        0x0C, 51, 0,
        0x3C, 0xFF, 0x2F, 0x00,
    };
    SmfFile f;
    std::string err;
    REQUIRE (parseSmf (b, f, &err));
    REQUIRE (f.format == 1);
    REQUIRE (f.tracks.size() == 2);
    REQUIRE (f.tracks[1].endTick == 96);
    Pattern p;
    MidiImportReport rep;
    REQUIRE (readMidiFile (b, p, {}, &rep));
    REQUIRE (rep.bpm == 120.0);
    StepTextOptions so;
    so.header = false;
    REQUIRE (toStepText (p, so) == "C+ D#+:A . .");
}

TEST_CASE ("hostile MIDI never crashes and never yields out-of-range fields", "[midi][hostile]")
{
    const Bytes good = writeMidiFile (randomPlayablePattern (5, 16), plain());

    for (std::size_t n = 0; n < good.size(); ++n)
    {
        const Bytes cut (good.begin(), good.begin() + static_cast<std::ptrdiff_t> (n));
        Pattern p;
        if (readMidiFile (cut, p)) REQUIRE (sanitizePattern (p) == 0);
    }

    acid::Rng rng (31337);
    for (int iter = 0; iter < 20000; ++iter)
    {
        Bytes b = good;
        const int flips = 1 + rng.nextInt (8);
        for (int k = 0; k < flips; ++k)
            b[static_cast<std::size_t> (rng.nextInt (static_cast<int> (b.size())))] = static_cast<std::uint8_t> (rng.nextInt (256));
        Pattern p;
        if (readMidiFile (b, p)) REQUIRE (sanitizePattern (p) == 0);
    }
    for (int iter = 0; iter < 5000; ++iter)
    {
        Bytes b { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k' };
        const int len = rng.nextInt (200);
        b.push_back (0);
        b.push_back (0);
        b.push_back (0);
        b.push_back (static_cast<std::uint8_t> (len));
        for (int i = 0; i < len; ++i) b.push_back (static_cast<std::uint8_t> (rng.nextInt (256)));
        Pattern p;
        if (readMidiFile (b, p)) REQUIRE (sanitizePattern (p) == 0);
    }

    // Specific malformations are rejected with a message and leave the pattern alone.
    const Pattern keep = randomPattern (2);
    auto rejects = [&keep] (const Bytes& b) {
        Pattern p = keep;
        MidiImportReport rep;
        REQUIRE_FALSE (readMidiFile (b, p, {}, &rep));
        REQUIRE_FALSE (rep.error.empty());
        REQUIRE (samePattern (p, keep));
    };
    rejects ({});
    rejects ({ 'R', 'I', 'F', 'F', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0 });
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE7, 0x28 });   // SMPTE
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 0 });         // zero ppq
    rejects ({ 'M', 'T', 'h', 'd', 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 1, 1, 0xE0 });
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k', 0x7F, 0xFF, 0xFF, 0xFF });
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 6,
               0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x90 });                          // five-byte delta
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 3,
               0x00, 48, 100 });                                               // running status with no status
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 6,
               0x00, 0xFF, 0x01, 0x7F, 'a', 'b' });                            // meta longer than the chunk
    rejects ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 4,
               0x00, 0xFF, 0x2F, 0x00 });                                      // no notes
}

TEST_CASE ("timed-note export for recorded takes", "[midi]")
{
    std::vector<MidiNote> take {
        { 0.5, 0.75, 60, true }, { 0.0, 0.6, 60 }, { 0.25, 0.3, 64, false, true },
    };
    std::vector<SmfEvent> cc;
    const Bytes b = writeMidiFile (take, 1.0, plain(), "take");
    REQUIRE (checkStructure (b) == 480);
    const auto n = notesOf (b, &cc);
    REQUIRE (n.size() == 3);
    CHECK ((n[0].on == 0 && n[0].off == 240));     // cut where the same key restarts
    CHECK ((n[1].on == 120 && n[1].key == 64));
    CHECK ((n[2].on == 240 && n[2].vel == 96));
    REQUIRE (cc.size() == 2);
}
