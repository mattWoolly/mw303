#include "AllocGuard.h"
#include "SeqTestSupport.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include "acid/seq/Scales.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>

using namespace acid::seq;
using acid::seq::test::randomPattern;

namespace
{
std::uint64_t fnv1a (const std::string& s)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (char c : s)
    {
        h ^= static_cast<unsigned char> (c);
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string steps16 (const Pattern& p)
{
    StepTextOptions o;
    o.header = false;
    return toStepText (p, o);
}

Pattern drawn (std::uint64_t seed, int length = 16)
{
    Pattern p;
    p.length = static_cast<std::uint8_t> (length);
    regenerate (p, seed);
    return p;
}

/** Everything except the generator-owned fields (candidates, magnitudes, seed). */
void requireOnlyGeneratorFieldsChanged (const Pattern& a, const Pattern& b)
{
    Pattern x = a, y = b;
    for (Pattern* p : { &x, &y })
    {
        p->gen.seed = 0;
        for (Step& s : p->steps)
        {
            s.candNote = 0;
            s.candOctave = 0;
            s.magGate = s.magAccent = s.magSlide = s.magVibrato = 0;
        }
    }
    REQUIRE (samePattern (x, y));
}
}

TEST_CASE ("generator is deterministic with stable golden output", "[gen]")
{
    REQUIRE (encodePattern (drawn (1)) == encodePattern (drawn (1)));
    REQUIRE_FALSE (encodePattern (drawn (1)) == encodePattern (drawn (2)));

    // Golden values: integer-only algorithm, so these hold on every compiler and platform.
    // If one changes, the generator's output changed for every saved seed: that is a breaking
    // change and needs a deliberate decision, not a test update.
    //
    // Re-pinned once, deliberately, when the stored generator memory changed from raw
    // magnitudes to position-independent turn-on amounts (review fix: edits that move steps
    // must survive knob moves). The hash covers the stored memory, so it moved; the step
    // strings did not. Measured against the previous implementation over 180000 patterns
    // (3000 seeds, ten lengths, six amount settings): no visible step differs; the only change
    // is the slide flag of a hidden step 63, which no longer wraps to step 0.
    struct Golden { std::uint64_t seed; const char* steps; std::uint64_t hash; };
    const Golden golden[] {
        { 1, "G#:A . C:S C . . D:A C G# C D#+ . . . C .", 0x2e2b9539c6098080ull },
        { 2, "C:A . C# . . C# C# . C+:A . C# C#+ . B+ . C:A", 0x5f5437b2753cfb23ull },
        { 0xDEADBEEFull, "C# . . A D# . G#+ . . F# C C . D+ D:S D#:AS", 0xef09a20929e99e1cull },
    };
    for (const Golden& g : golden)
    {
        const Pattern p = drawn (g.seed);
        INFO ("seed " << g.seed << "  " << steps16 (p) << "  hash 0x" << std::hex << fnv1a (encodePattern (p)));
        CHECK (steps16 (p) == g.steps);
        CHECK (fnv1a (encodePattern (p)) == g.hash);
    }
}

TEST_CASE ("generate writes only candidates, magnitudes and the seed", "[gen]")
{
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        Pattern p = randomPattern (seed);
        const Pattern orig = p;
        generate (p, seed * 7);
        requireOnlyGeneratorFieldsChanged (p, orig);
        REQUIRE (p.gen.seed == static_cast<std::uint32_t> (seed * 7));
    }

    Pattern p;
    generate (p, 0x123456789ABCDEFull);
    REQUIRE (p.gen.seed == 0x89ABCDEFu);
    Pattern q;
    q.gen.seed = 0x89ABCDEFu;
    generate (q);
    Pattern r;
    generate (r, 0x89ABCDEFull);
    REQUIRE (encodePattern (q) == encodePattern (r));

    // Only the stored 32 bits seed the streams, so the stored seed reproduces the line.
    Pattern big;
    generate (big, 0xABCD00000005ull);
    Pattern small;
    generate (small, 5);
    REQUIRE (samePattern (big, small));
}

TEST_CASE ("applyDensities is a no-op on a pattern that was never generated", "[gen]")
{
    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        Pattern p = randomPattern (seed);
        flatten (p);
        const Pattern orig = p;
        applyDensities (p);
        REQUIRE (samePattern (p, orig));
    }
}

TEST_CASE ("locked lanes are bit-identical after generate and applyDensities", "[gen]")
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        Pattern p = randomPattern (seed);
        p.gen.useAccent = p.gen.useSlide = p.gen.useVibrato = true;
        p.gen.noteMask = 0x0FFF;
        p.gen.octaveMask = 0x1F;
        const Pattern orig = p;
        regenerate (p, seed + 1000);
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const Step& s = p.steps[static_cast<std::size_t> (i)];
            const Step& o = orig.steps[static_cast<std::size_t> (i)];
            if (o.locks & kLanePitch) { REQUIRE (s.note == o.note); REQUIRE (s.candNote == o.candNote); }
            if (o.locks & kLaneOctave) { REQUIRE (s.octave == o.octave); REQUIRE (s.candOctave == o.candOctave); }
            if (o.locks & kLaneGate) { REQUIRE (s.state == o.state); REQUIRE (s.magGate == o.magGate); }
            if (o.locks & kLaneAccent) { REQUIRE (s.accent == o.accent); REQUIRE (s.magAccent == o.magAccent); }
            if (o.locks & kLaneSlide) { REQUIRE (s.slide == o.slide); REQUIRE (s.magSlide == o.magSlide); }
            if (o.locks & kLaneVibrato) { REQUIRE (s.vibrato == o.vibrato); REQUIRE (s.magVibrato == o.magVibrato); }
            REQUIRE (s.probability == o.probability);
            REQUIRE (s.ratchet == o.ratchet);
            REQUIRE (s.gate == o.gate);
            REQUIRE (s.locks == o.locks);
        }
    }
}

TEST_CASE ("locking one step never changes what other steps get", "[gen]")
{
    Pattern a;
    a.length = 64;
    Pattern b = a;
    b.steps[5].locks = 63;
    b.steps[6].locks = kLanePitch;
    generate (a, 42);
    generate (b, 42);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        // Step 4's slide leap is to step 5's (locked) pitch; step 7 may repeat step 6's pitch.
        if (i >= 4 && i <= 7) continue;
        REQUIRE (sameStep (a.steps[static_cast<std::size_t> (i)], b.steps[static_cast<std::size_t> (i)]));
    }
    REQUIRE (a.steps[4].magGate == b.steps[4].magGate);
    REQUIRE (a.steps[4].magAccent == b.steps[4].magAccent);
    REQUIRE (a.steps[6].magGate == b.steps[6].magGate);
}

TEST_CASE ("switched-off attribute lanes are left untouched", "[gen]")
{
    Pattern p = randomPattern (4);
    for (Step& s : p.steps) s.locks = 0;
    p.gen.useAccent = false;
    p.gen.useSlide = false;
    p.gen.useVibrato = false;
    const Pattern orig = p;
    regenerate (p, 9);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        const Step& o = orig.steps[static_cast<std::size_t> (i)];
        REQUIRE (s.accent == o.accent);
        REQUIRE (s.slide == o.slide);
        REQUIRE (s.vibrato == o.vibrato);
        REQUIRE (s.magAccent == o.magAccent);
        REQUIRE (s.magSlide == o.magSlide);
        REQUIRE (s.magVibrato == o.magVibrato);
    }
}

TEST_CASE ("density knobs only add or remove events, never move them", "[gen][density]")
{
    for (std::uint64_t seed = 1; seed <= 250; ++seed)
    {
        Pattern base;
        base.length = static_cast<std::uint8_t> (1 + seed % 64);
        base.gen.useVibrato = true;
        base.gen.octaveMask = 0x1F;
        generate (base, seed);

        // Gate: the sounding set grows with amountNote, and every revealed note plays its candidate.
        Pattern prev = base;
        prev.gen.amountNote = 0;
        applyDensities (prev);
        for (const Step& s : prev.steps) REQUIRE (s.state == StepState::Rest);
        for (int a = 1; a <= 100; ++a)
        {
            Pattern cur = base;
            cur.gen.amountNote = static_cast<std::uint8_t> (a);
            applyDensities (cur);
            for (int i = 0; i < kMaxSteps; ++i)
            {
                const Step& c = cur.steps[static_cast<std::size_t> (i)];
                const Step& q = prev.steps[static_cast<std::size_t> (i)];
                if (q.state == StepState::Note) REQUIRE (c.state == StepState::Note);
                REQUIRE (c.note == c.candNote);
                REQUIRE (c.octave == c.candOctave);
                REQUIRE (c.note == q.note);
                REQUIRE (c.octave == q.octave);
                // A step that sounds at both settings keeps its accent and vibrato.
                if (q.state == StepState::Note && c.state == StepState::Note)
                {
                    REQUIRE (c.accent == q.accent);
                    REQUIRE (c.vibrato == q.vibrato);
                }
            }
            prev = cur;
        }
        for (const Step& s : prev.steps) REQUIRE (s.state == StepState::Note);

        // Accent, slide and vibrato grow with their own knob and never touch states or pitches.
        Pattern mid = base;
        applyDensities (mid);
        for (int lane = 0; lane < 3; ++lane)
        {
            Pattern last = mid;
            for (int a = 0; a <= 100; ++a)
            {
                Pattern cur = mid;
                if (lane == 0) cur.gen.amountAccent = static_cast<std::uint8_t> (a);
                if (lane == 1) cur.gen.amountSlide = static_cast<std::uint8_t> (a);
                if (lane == 2) cur.gen.amountVibrato = static_cast<std::uint8_t> (a);
                applyDensities (cur);
                for (int i = 0; i < kMaxSteps; ++i)
                {
                    const Step& c = cur.steps[static_cast<std::size_t> (i)];
                    const Step& m = mid.steps[static_cast<std::size_t> (i)];
                    const Step& l = last.steps[static_cast<std::size_t> (i)];
                    REQUIRE (c.state == m.state);
                    REQUIRE (c.note == m.note);
                    REQUIRE (c.octave == m.octave);
                    if (lane != 0) REQUIRE (c.accent == m.accent);
                    if (lane != 1) REQUIRE (c.slide == m.slide);
                    if (lane != 2) REQUIRE (c.vibrato == m.vibrato);
                    if (a > 0)
                    {
                        if (lane == 0 && l.accent) REQUIRE (c.accent);
                        if (lane == 1 && l.slide) REQUIRE (c.slide);
                        if (lane == 2 && l.vibrato) REQUIRE (c.vibrato);
                    }
                    if (a == 0) REQUIRE_FALSE ((lane == 0 ? c.accent : lane == 1 ? c.slide : c.vibrato));
                }
                last = cur;
            }
        }
    }
}

TEST_CASE ("changing the length never changes earlier steps", "[gen][density]")
{
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        Pattern p;
        p.length = 64;
        generate (p, seed);
        for (int oldLen : { 7, 16, 33 })
            for (int newLen : { 5, 12, 16, 48 })
            {
                Pattern a = p, b = p;
                a.length = static_cast<std::uint8_t> (oldLen);
                b.length = static_cast<std::uint8_t> (newLen);
                applyDensities (a);
                applyDensities (b);
                const int m = std::min (oldLen, newLen);
                for (int i = 0; i < m; ++i)
                {
                    Step x = a.steps[static_cast<std::size_t> (i)];
                    Step y = b.steps[static_cast<std::size_t> (i)];
                    if (i == m - 1) x.slide = y.slide = false;   // the documented exception
                    REQUIRE (sameStep (x, y));
                }
            }
    }
}

TEST_CASE ("candidates respect the masks and root", "[gen]")
{
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        Pattern p;
        const auto t = scaleTemplates()[static_cast<std::size_t> (seed % scaleTemplates().size())];
        applyScaleTemplate (t, p.gen);
        p.gen.root = static_cast<std::uint8_t> (seed % 12);
        generate (p, seed);
        int numOct = 0;
        for (int o = 0; o < 5; ++o) numOct += (p.gen.octaveMask >> o) & 1;
        for (const Step& s : p.steps)
        {
            const int rel = (s.candNote - p.gen.root + 12) % 12;
            REQUIRE (((p.gen.noteMask >> rel) & 1) == 1);
            if (numOct > 0) REQUIRE (((p.gen.octaveMask >> (s.candOctave - kMinOctave)) & 1) == 1);
        }
    }

    // Empty masks fall back to the root and octave 0.
    Pattern e;
    e.gen.noteMask = 0;
    e.gen.octaveMask = 0;
    e.gen.root = 5;
    generate (e, 3);
    for (const Step& s : e.steps)
    {
        REQUIRE (s.candNote == 5);
        REQUIRE (s.candOctave == 0);
    }
}

TEST_CASE ("default output is an idiomatic acid line", "[gen][stats]")
{
    // Document 05 B.1 and document 06 section 5.8: anchored downbeat, notes on 7..13 of 16 with
    // rests of at most four in a row, sparse accents (1..5 of 16) that avoid clustering, a few
    // slides (0.5..4 of 16) that prefer octave and fifth leaps and never lead into a rest,
    // a root-centred pitch set, and the middle octave favoured.
    constexpr int kSeeds = 4000;
    double notes = 0, accents = 0, slides = 0;
    long accAfterAcc = 0, pairsAfterAcc = 0, accTotalNotes = 0;
    long slideLeap = 0, slideCount = 0, leapPairs = 0, allPairs = 0;
    long pcCount[12] {}, octCount[5] {};
    int maxRestRun = 0;
    long distinctPcTotal = 0;
    for (int seed = 1; seed <= kSeeds; ++seed)
    {
        const Pattern p = drawn (static_cast<std::uint64_t> (seed));
        REQUIRE (p.steps[0].state == StepState::Note);
        int run = 0;
        bool seen[12] {};
        for (int i = 0; i < 16; ++i)
        {
            const Step& s = p.steps[static_cast<std::size_t> (i)];
            if (s.state != StepState::Note) { maxRestRun = std::max (maxRestRun, ++run); continue; }
            run = 0;
            notes += 1;
            accents += s.accent;
            pcCount[s.note]++;
            octCount[s.octave - kMinOctave]++;
            seen[s.note] = true;
            const Step& n = p.steps[static_cast<std::size_t> ((i + 1) % 16)];
            if (s.slide)
            {
                ++slideCount;
                REQUIRE (n.state == StepState::Note);
                const int leap = std::abs (stepMidiNote (n) - stepMidiNote (s));
                slideLeap += leap >= 7;
            }
            if (n.state == StepState::Note)
            {
                ++allPairs;
                leapPairs += std::abs (stepMidiNote (n) - stepMidiNote (s)) >= 7;
                if (i + 1 < 16)
                {
                    ++accTotalNotes;
                    if (s.accent)
                    {
                        ++pairsAfterAcc;
                        accAfterAcc += n.accent;
                    }
                }
            }
            slides += s.slide;
        }
        for (bool b : seen) distinctPcTotal += b;
    }
    const double perPattern = 1.0 / kSeeds;
    const double accRate = accents / notes;
    const double accAfterRate = static_cast<double> (accAfterAcc) / static_cast<double> (pairsAfterAcc);
    const double slideLeapRate = static_cast<double> (slideLeap) / static_cast<double> (slideCount);
    const double pairLeapRate = static_cast<double> (leapPairs) / static_cast<double> (allPairs);

    std::printf ("generator stats over %d seeds: notes %.2f, accents %.2f, slides %.2f per 16; "
                 "P(acc) %.3f, P(acc | prev acc) %.3f; P(leap>=7 | slide) %.3f vs %.3f; "
                 "distinct pitch classes %.2f; max rest run %d\n",
                 kSeeds, notes * perPattern, accents * perPattern, slides * perPattern, accRate, accAfterRate,
                 slideLeapRate, pairLeapRate, static_cast<double> (distinctPcTotal) * perPattern, maxRestRun);
    std::printf ("pitch classes:");
    for (long c : pcCount) std::printf (" %ld", c);
    std::printf ("\noctaves:");
    for (long c : octCount) std::printf (" %ld", c);
    std::printf ("\n");

    CHECK (notes * perPattern >= 7.0);
    CHECK (notes * perPattern <= 13.0);
    CHECK (maxRestRun <= 4);
    CHECK (accents * perPattern >= 1.0);
    CHECK (accents * perPattern <= 5.0);
    CHECK (slides * perPattern >= 0.5);
    CHECK (slides * perPattern <= 4.0);
    CHECK (accAfterRate < accRate);
    CHECK (slideLeapRate > pairLeapRate);
    // Root most common, fifth second (the default mask is chromatic, root 0).
    for (int pc = 1; pc < 12; ++pc) CHECK (pcCount[0] > pcCount[pc]);
    for (int pc = 1; pc < 12; ++pc)
        if (pc != 7) CHECK (pcCount[7] > pcCount[pc]);
    // Default octave mask {0, +1}: the lower (home) octave dominates; nothing outside the mask.
    CHECK (octCount[1] > octCount[2]);
    CHECK (octCount[0] == 0);
    CHECK (octCount[3] == 0);
    CHECK (octCount[4] == 0);
    CHECK (static_cast<double> (distinctPcTotal) * perPattern < 7.0);
}

TEST_CASE ("flatten keeps the sound and drops the generator link", "[gen]")
{
    Pattern p = drawn (77);
    const Pattern sounding = p;
    flatten (p);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        const Step& o = sounding.steps[static_cast<std::size_t> (i)];
        REQUIRE (s.state == o.state);
        REQUIRE (s.note == o.note);
        REQUIRE (s.accent == o.accent);
        REQUIRE (s.slide == o.slide);
        REQUIRE (s.magGate == 255);
    }
    p.gen.amountNote = 100;
    const Pattern flat = p;
    applyDensities (p);
    REQUIRE (samePattern (p, flat));
}

TEST_CASE ("generate and applyDensities never allocate", "[gen][rt]")
{
    Pattern p;
    p.length = 64;
    acid::test::ScopedAllocGuard guard;
    generate (p, 5);
    applyDensities (p);
    p.gen.amountNote = 90;
    applyDensities (p);
    flatten (p);
    REQUIRE (guard.count() == 0);
}
