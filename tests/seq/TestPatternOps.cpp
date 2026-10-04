#include "AllocGuard.h"
#include "SeqTestSupport.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid::seq;
using acid::seq::test::randomPattern;

namespace
{
Pattern fromText (const char* text)
{
    Pattern p;
    std::string err;
    INFO (text);
    REQUIRE (fromStepText (text, p, &err));
    return p;
}

std::string text (const Pattern& p)
{
    StepTextOptions o;
    o.header = false;
    return toStepText (p, o);
}

std::string textWithRests (const Pattern& p)
{
    StepTextOptions o;
    o.header = false;
    o.restPitches = true;
    return toStepText (p, o);
}
}

TEST_CASE ("rotate moves every field of every step and wraps over length only", "[ops]")
{
    for (int len : { 1, 2, 7, 16, 63, 64 })
    {
        Pattern p = randomPattern (static_cast<std::uint64_t> (len));
        p.length = static_cast<std::uint8_t> (len);
        const Pattern orig = p;

        shiftRight (p);
        for (int i = 0; i < len; ++i)
            REQUIRE (sameStep (p.steps[static_cast<std::size_t> ((i + 1) % len)], orig.steps[static_cast<std::size_t> (i)]));
        for (int i = len; i < kMaxSteps; ++i)
            REQUIRE (sameStep (p.steps[static_cast<std::size_t> (i)], orig.steps[static_cast<std::size_t> (i)]));

        shiftLeft (p);
        REQUIRE (samePattern (p, orig));

        rotate (p, 1000003);
        Pattern q = orig;
        rotate (q, 1000003 % len);
        REQUIRE (samePattern (p, q));
        rotate (p, -1000003);
        REQUIRE (samePattern (p, orig));
    }

    Pattern p = fromText ("C D:A E:S F");
    shiftLeft (p);
    REQUIRE (text (p) == "D:A E:S F C");
    shiftRight (p, 2);
    REQUIRE (text (p) == "F C D:A E:S");
}

TEST_CASE ("transpose carries note and octave and wraps at the range limits", "[ops]")
{
    Pattern p = fromText ("C- B+++ B . ~");
    p.steps[3].note = 11;   // a rest holding B
    transpose (p, 1);
    REQUIRE (textWithRests (p) == "C#- C- C+ .C+ ~");

    transpose (p, -2);
    REQUIRE (textWithRests (p) == "B+++ A#+++ A# .A# ~");

    // A full cycle of the 60-semitone window is the identity, for every pitch.
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        Pattern r = randomPattern (seed);
        const Pattern orig = r;
        transpose (r, 60);
        REQUIRE (samePattern (r, orig));
        transpose (r, 7);
        transpose (r, -7);
        REQUIRE (samePattern (r, orig));
        transpose (r, -1234567);
        transpose (r, 1234567);
        REQUIRE (samePattern (r, orig));
    }

    SECTION ("generator candidates and root move with includeGenerator")
    {
        Pattern g;
        g.steps[0].candNote = 11;
        g.steps[0].candOctave = 3;
        g.gen.root = 11;
        transpose (g, 1);
        REQUIRE (g.steps[0].candNote == 0);
        REQUIRE (g.steps[0].candOctave == -1);
        REQUIRE (g.gen.root == 0);

        transpose (g, 1, false);
        REQUIRE (g.steps[0].candNote == 0);
        REQUIRE (g.gen.root == 0);
    }

    SECTION ("a narrower window wraps inside it and folds pitches from outside")
    {
        Pattern w = fromText ("B+ C C+++");
        transpose (w, 1, true, TransposeRange { 0, 1 });
        REQUIRE (text (w) == "C C# C#+");
    }
}

TEST_CASE ("arpeggiator inversion rotates arpIndex inside 1..10", "[ops]")
{
    Pattern p;
    p.steps[0].arpIndex = 10;
    p.steps[1].arpIndex = 1;
    p.steps[2].arpIndex = 0;     // illegal, clamped to 1 first
    p.steps[3].arpIndex = 200;   // illegal, clamped to 10 first
    invertArp (p, 1);
    REQUIRE (p.steps[0].arpIndex == 1);
    REQUIRE (p.steps[1].arpIndex == 2);
    REQUIRE (p.steps[2].arpIndex == 2);
    REQUIRE (p.steps[3].arpIndex == 1);
    invertArp (p, -2);
    REQUIRE (p.steps[0].arpIndex == 9);
    REQUIRE (p.steps[1].arpIndex == 10);
    const Pattern q = p;
    invertArp (p, 10);
    REQUIRE (samePattern (p, q));
    invertArp (p, -1000);
    REQUIRE (samePattern (p, q));
}

TEST_CASE ("reverse keeps note-plus-tie groups and re-attaches slides", "[ops]")
{
    // C slides into D, D is held by a tie, then E, then a rest.
    Pattern p = fromText ("C:S D:A ~ E .");
    reverse (p);
    // Mirrored: rest, E, D (with its tie after it), C. The glide joined C and D; reversed it
    // joins D and C, so the flag is now on D.
    REQUIRE (text (p) == ". E D:AS ~ C");
    reverse (p);
    REQUIRE (text (p) == "C:S D:A ~ E .");

    // The loop glide (E into C across the wrap) is kept: reversed, C glides into E across the wrap.
    Pattern w = fromText ("C D E:S");
    reverse (w);
    REQUIRE (text (w) == "E D C:S");

    Pattern one = fromText ("C:SA");
    reverse (one);
    REQUIRE (text (one) == "C:AS");

    // Twice is the identity for patterns without orphan ties (ties after a rest).
    for (std::uint64_t seed = 1; seed <= 500; ++seed)
    {
        Pattern r = randomPattern (seed);
        bool sounding = false;
        bool orphan = false;
        for (int i = 0; i < r.length; ++i)
        {
            const StepState st = r.steps[static_cast<std::size_t> (i)].state;
            if (st == StepState::Tie && ! sounding) orphan = true;
            sounding = st != StepState::Rest;
        }
        if (orphan) continue;
        const Pattern orig = r;
        reverse (r);
        reverse (r);
        REQUIRE (samePattern (r, orig));
    }
}

TEST_CASE ("reverseSteps is a plain mirror over length", "[ops]")
{
    // On steps without generator memory (keepEdits only touches generated steps).
    Pattern p = randomPattern (77);
    flatten (p);
    p.length = 5;
    const Pattern orig = p;
    reverseSteps (p);
    for (int i = 0; i < 5; ++i)
        REQUIRE (sameStep (p.steps[static_cast<std::size_t> (i)], orig.steps[static_cast<std::size_t> (4 - i)]));
    REQUIRE (sameStep (p.steps[5], orig.steps[5]));
    reverseSteps (p);
    REQUIRE (samePattern (p, orig));
}

TEST_CASE ("clear resets steps; mute keeps pitches", "[ops]")
{
    Pattern p = randomPattern (5);
    const Pattern orig = p;
    muteSteps (p);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        const Step& o = orig.steps[static_cast<std::size_t> (i)];
        REQUIRE (s.state == StepState::Rest);
        REQUIRE_FALSE (s.accent);
        REQUIRE_FALSE (s.slide);
        REQUIRE_FALSE (s.vibrato);
        REQUIRE (s.note == o.note);
        REQUIRE (s.octave == o.octave);
        REQUIRE (s.probability == o.probability);
        REQUIRE (s.ratchet == o.ratchet);
        REQUIRE (s.gate == o.gate);
        REQUIRE (s.locks == o.locks);
    }
    // The generator memory was made to agree: the next knob move changes nothing.
    {
        Pattern q = p;
        applyDensities (q);
        REQUIRE (samePattern (q, p));
    }
    // On a generated line no knob setting brings the muted notes back (steps that were
    // hidden may still appear).
    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        Pattern g;
        regenerate (g, seed);
        const Pattern line = g;
        muteSteps (g);
        g.gen.amountNote = 100;
        applyDensities (g);
        for (int i = 0; i < kMaxSteps; ++i)
            if (line.steps[static_cast<std::size_t> (i)].state == StepState::Note)
                REQUIRE (g.steps[static_cast<std::size_t> (i)].state == StepState::Rest);
    }
    clearSteps (p);
    for (const Step& s : p.steps) REQUIRE (sameStep (s, Step {}));
    REQUIRE (p.length == orig.length);
    REQUIRE (p.gen.seed == orig.gen.seed);
}

TEST_CASE ("copy and paste a step range", "[ops]")
{
    Pattern p = fromText ("C D E F G A B C+");
    auto clip = copySteps (p, 2, 3);
    REQUIRE (clip.size() == 3);
    REQUIRE (pasteSteps (p, 0, clip) == 3);
    REQUIRE (text (p) == "E F G F G A B C+");

    // Clamped to the length: the part of [first, first + count) inside [0, length).
    REQUIRE (copySteps (p, 6, 10).size() == 2);
    REQUIRE (copySteps (p, -5, 2).empty());
    REQUIRE (copySteps (p, -5, 5).empty());
    const auto head = copySteps (p, -3, 5);
    REQUIRE (head.size() == 2);
    REQUIRE (sameStep (head[0], p.steps[0]));
    REQUIRE (sameStep (head[1], p.steps[1]));
    REQUIRE (copySteps (p, -2147483647 - 1, 2147483647).size() == 0);
    REQUIRE (copySteps (p, 0, 2147483647).size() == 8);
    REQUIRE (copySteps (p, 8, 2).empty());
    REQUIRE (copySteps (p, 3, 0).empty());
    REQUIRE (copySteps (p, 3, -4).empty());

    // Without wrap, the tail falls off; with wrap it continues at step 0.
    Pattern q = fromText ("C C C C");
    auto abc = copySteps (fromText ("D E F"), 0, 3);
    REQUIRE (pasteSteps (q, 2, abc) == 2);
    REQUIRE (text (q) == "C C D E");
    REQUIRE (pasteSteps (q, 3, abc, true) == 3);
    REQUIRE (text (q) == "E F D D");
    REQUIRE (pasteSteps (q, 4, abc) == 0);
    REQUIRE (pasteSteps (q, -1, abc) == 0);

    // A clip longer than the pattern never writes more than length steps.
    Pattern one = fromText ("C");
    REQUIRE (pasteSteps (one, 0, abc, true) == 1);
    REQUIRE (text (one) == "D");
}

TEST_CASE ("duplicate doubles up to 64", "[ops]")
{
    Pattern p = fromText ("C D:S");
    REQUIRE (duplicate (p));
    REQUIRE (text (p) == "C D:S C D:S");

    Pattern one = fromText ("G");
    REQUIRE (duplicate (one));
    REQUIRE (one.length == 2);

    Pattern big = randomPattern (9);
    big.length = 40;
    const Pattern orig = big;
    REQUIRE (duplicate (big));
    REQUIRE (big.length == 64);
    for (int i = 40; i < 64; ++i)
        REQUIRE (sameStep (big.steps[static_cast<std::size_t> (i)], orig.steps[static_cast<std::size_t> (i - 40)]));
    REQUIRE_FALSE (duplicate (big));
    REQUIRE (big.length == 64);
}

TEST_CASE ("setLength keeps every step; resetTiming clears a range", "[ops]")
{
    Pattern p = randomPattern (11);
    flatten (p);   // generated slides are covered by the generator tests
    p.length = 8;
    const Pattern orig = p;
    setLength (p, 12);
    REQUIRE (p.length == 12);
    for (int i = 0; i < kMaxSteps; ++i)
        REQUIRE (sameStep (p.steps[static_cast<std::size_t> (i)], orig.steps[static_cast<std::size_t> (i)]));
    setLength (p, 3);
    REQUIRE (p.length == 3);
    setLength (p, 8);
    REQUIRE (samePattern (p, orig));   // down and back up restores the line
    setLength (p, 0);
    REQUIRE (p.length == 1);
    setLength (p, 999);
    REQUIRE (p.length == 64);
    p.length = 0;          // out of range from outside: still written back clamped
    setLength (p, 1);
    REQUIRE (p.length == 1);

    // The original unit's behaviour for steps a longer length reveals, on request.
    Pattern q = orig;
    resetTiming (q, 8, 4);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const Step& s = q.steps[static_cast<std::size_t> (i)];
        const Step& o = orig.steps[static_cast<std::size_t> (i)];
        if (i < 8 || i >= 12)
        {
            REQUIRE (sameStep (s, o));
            continue;
        }
        REQUIRE (s.state == StepState::Rest);
        REQUIRE_FALSE (s.accent);
        REQUIRE_FALSE (s.slide);
        REQUIRE_FALSE (s.vibrato);
        REQUIRE (s.probability == 100);
        REQUIRE (s.ratchet == 1);
        REQUIRE (s.gate == 0);
        REQUIRE (s.note == o.note);        // pitch kept
        REQUIRE (s.locks == o.locks);      // locks kept
    }
    Pattern r = orig;
    resetTiming (r, -5, 3);
    resetTiming (r, 70, 3);
    resetTiming (r, 4, -1);
    REQUIRE (samePattern (r, orig));
}

TEST_CASE ("randomiseLane changes only its lane, respects locks, is deterministic", "[ops]")
{
    for (LaneBit lane : { kLanePitch, kLaneOctave, kLaneGate, kLaneAccent, kLaneSlide, kLaneVibrato })
    {
        // Raw semantics on steps without generator memory; on a generated line the changed
        // lanes are also released from the knobs (see the generator edit tests).
        Pattern p = randomPattern (21);
        flatten (p);
        p.length = 64;
        p.gen.noteMask = 0x0FFF;
        p.gen.octaveMask = 0x1F;
        const Pattern orig = p;
        randomiseLane (p, lane, 1234, 50);

        Pattern again = orig;
        randomiseLane (again, lane, 1234, 50);
        REQUIRE (samePattern (p, again));

        int changed = 0;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            Step a = p.steps[static_cast<std::size_t> (i)];
            const Step& o = orig.steps[static_cast<std::size_t> (i)];
            if (o.locks & lane) REQUIRE (sameStep (a, o));
            // Put the lane back and everything else must be identical.
            switch (lane)
            {
                case kLanePitch: changed += a.note != o.note; a.note = o.note; break;
                case kLaneOctave: changed += a.octave != o.octave; a.octave = o.octave; break;
                case kLaneGate: changed += a.state != o.state; a.state = o.state; break;
                case kLaneAccent: changed += a.accent != o.accent; a.accent = o.accent; break;
                case kLaneSlide: changed += a.slide != o.slide; a.slide = o.slide; break;
                case kLaneVibrato: changed += a.vibrato != o.vibrato; a.vibrato = o.vibrato; break;
            }
            REQUIRE (sameStep (a, o));
        }
        REQUIRE (changed > 0);
    }

    Pattern p;
    p.length = 64;
    randomiseLane (p, kLaneGate, 5, 100);
    for (const Step& s : p.steps) REQUIRE (s.state == StepState::Note);
    randomiseLane (p, kLaneGate, 5, 0);
    for (const Step& s : p.steps) REQUIRE (s.state == StepState::Rest);

    // Pitch draws stay inside the rotated mask.
    p.gen.noteMask = 0b10000001;   // root and fifth
    p.gen.root = 2;                // D and A
    randomiseLane (p, kLanePitch, 99);
    for (const Step& s : p.steps) REQUIRE ((s.note == 2 || s.note == 9));
}

TEST_CASE ("sanitize clamps everything into range and leaves valid patterns alone", "[ops]")
{
    Pattern p = randomPattern (3);
    REQUIRE (sanitizePattern (p) == 0);

    Pattern bad;
    bad.length = 0;
    bad.shuffle = 200;
    bad.transpose = 120;
    bad.steps[0].note = 40;
    bad.steps[0].octave = 9;
    bad.steps[0].arpIndex = 0;
    bad.steps[0].ratchet = 9;
    bad.steps[0].gate = 200;
    bad.steps[0].magGate = 200;
    bad.steps[0].state = static_cast<StepState> (7);
    bad.div = static_cast<StepDiv> (99);
    bad.gen.root = 30;
    bad.gen.noteMask = 0xFFFF;
    for (char& c : bad.name) c = 'x';
    REQUIRE (sanitizePattern (bad) == 14);
    REQUIRE (bad.length == 1);
    REQUIRE (bad.shuffle == 100);
    REQUIRE (bad.transpose == 48);
    REQUIRE (bad.steps[0].note == 11);
    REQUIRE (bad.steps[0].octave == kMaxOctave);
    REQUIRE (bad.steps[0].arpIndex == 1);
    REQUIRE (bad.steps[0].ratchet == 4);
    REQUIRE (bad.steps[0].gate == 127);
    REQUIRE (bad.steps[0].magGate == 127);
    REQUIRE (bad.steps[0].state == StepState::Tie);
    REQUIRE (bad.div == StepDiv::d1_1);
    REQUIRE (bad.gen.root == 11);
    REQUIRE (bad.gen.noteMask == 0x0FFF);
    REQUIRE (bad.name[kPatternNameLength - 1] == '\0');
    REQUIRE (sanitizePattern (bad) == 0);
}

TEST_CASE ("pitch/time view round-trips losslessly, spares included", "[ops][authentic]")
{
    for (std::uint64_t seed = 1; seed <= 2000; ++seed)
    {
        const Pattern orig = randomPattern (seed);
        std::vector<PitchEntry> spare;
        for (int k = 0; k < static_cast<int> (seed % 9); ++k)
            spare.push_back (PitchEntry { static_cast<std::uint8_t> ((seed + static_cast<std::uint64_t> (k)) % 12),
                                          static_cast<std::int8_t> (k % 3), k % 2 == 0, k % 3 == 0, false });

        const PitchTimeView v = toPitchTime (orig, spare);
        REQUIRE (static_cast<int> (v.pitches.size()) == v.consumedCount() + static_cast<int> (spare.size()));

        Pattern back = randomPattern (seed + 100000);   // everything overwritten except settings
        back.name[0] = 0;
        std::vector<PitchEntry> spareBack;
        fromPitchTime (v, back, &spareBack);
        for (int i = 0; i < kMaxSteps; ++i)
            REQUIRE (sameStep (back.steps[static_cast<std::size_t> (i)], orig.steps[static_cast<std::size_t> (i)]));
        REQUIRE (back.length == orig.length);
        REQUIRE (spareBack.size() == spare.size());
        for (std::size_t k = 0; k < spare.size(); ++k) REQUIRE (samePitchEntry (spareBack[k], spare[k]));
    }
}

TEST_CASE ("pitch/time zip: only Notes consume pitch entries", "[ops][authentic]")
{
    // Document 06 section 1.2: pitches C D E F with Note Note Rest Note play C D - E.
    PitchTimeView v;
    v.length = 4;
    v.pitches = { { 0, 0 }, { 2, 0 }, { 4, 0, true }, { 5, 0, false, true } };
    v.time[0].state = StepState::Note;
    v.time[1].state = StepState::Note;
    v.time[2].state = StepState::Rest;
    v.time[3].state = StepState::Note;
    Pattern p;
    std::vector<PitchEntry> spare;
    fromPitchTime (v, p, &spare);
    REQUIRE (text (p) == "C D . E:A");
    REQUIRE (spare.size() == 1);
    REQUIRE (spare[0].note == 5);
    REQUIRE (spare[0].slide);

    // Ties consume nothing either.
    v.time[2].state = StepState::Tie;
    fromPitchTime (v, p, &spare);
    REQUIRE (text (p) == "C D ~ E:A");

    // A pitch list that runs out leaves the Note on its own (shadow) pitch.
    v.pitches.resize (2);
    v.time[3].note = 9;
    fromPitchTime (v, p, &spare);
    REQUIRE (text (p) == "C D ~ A");
    REQUIRE (spare.empty());
}

TEST_CASE ("authentic edits re-zip later pitches like the original unit", "[ops][authentic]")
{
    Pattern p = fromText ("C D:A . E:S");
    std::vector<PitchEntry> spare { { 7, 1 } };   // G+ waiting in the pitch list

    // Rest to Note: the new Note takes E (the next entry), the last Note picks up the spare.
    authenticSetState (p, spare, 2, StepState::Note);
    REQUIRE (text (p) == "C D:A E:S G+");
    REQUIRE (spare.empty());
    REQUIRE (pitchEntryIndexForStep (p, 3) == 3);
    REQUIRE (pitchEntryIndexForStep (p, 2) == 2);

    // Note to Rest: everything after shifts back, the last entry returns to the spares, and
    // the rest step keeps the pitch it was playing.
    authenticSetState (p, spare, 1, StepState::Rest);
    REQUIRE (textWithRests (p) == "C .D D:A E:S");
    REQUIRE (spare.size() == 1);
    REQUIRE (spare[0].note == 7);
    REQUIRE (pitchEntryIndexForStep (p, 1) == -1);

    // Re-entering a pitch clears its accent and slide, as the hardware does.
    REQUIRE (authenticWritePitch (p, spare, 1, 9, 0));
    REQUIRE (text (p) == "C . A E:S");
    REQUIRE (authenticSetFlags (p, spare, 1, true, true, false));
    REQUIRE (text (p) == "C . A:AS E:S");
    REQUIRE_FALSE (authenticWritePitch (p, spare, 99, 0, 0));
    REQUIRE (authenticWritePitch (p, spare, 4, 0, 1));   // appends a spare
    REQUIRE (spare.size() == 2);

    // Shortening pushes the dropped Notes' entries to the front of the spares.
    authenticSetLength (p, spare, 2);
    REQUIRE (text (p) == "C .");
    REQUIRE (spare.size() == 4);
    REQUIRE (spare[0].note == 9);
    REQUIRE (spare[1].note == 4);
    authenticSetLength (p, spare, 4);
    REQUIRE (text (p) == "C . . .");
    authenticSetState (p, spare, 3, StepState::Note);
    REQUIRE (text (p) == "C . . A:AS");
}

TEST_CASE ("in-place edits never allocate", "[ops][rt]")
{
    Pattern p = randomPattern (8);
    acid::test::ScopedAllocGuard guard;
    rotate (p, 3);
    transpose (p, 5);
    invertArp (p, 2);
    reverse (p);
    reverseSteps (p);
    muteSteps (p);
    duplicate (p);
    setLength (p, 17);
    randomiseLane (p, kLaneAccent, 3);
    sanitizePattern (p);
    (void) samePattern (p, p);
    clearSteps (p);
    REQUIRE (guard.count() == 0);
}
