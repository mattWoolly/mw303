// A generated line and the edits made on it: every edit must survive the next density knob
// move, and the knobs must keep only adding or removing events afterwards (SPEC 6.3).
#include "AllocGuard.h"
#include "SeqTestSupport.h"
#include "acid/common/Rng.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <vector>

using namespace acid::seq;

namespace
{
Pattern drawn (std::uint64_t seed, int length)
{
    acid::Rng r (seed * 0x9E3779B97F4A7C15ull + 7);
    Pattern p;
    p.length = static_cast<std::uint8_t> (length);
    p.gen.useVibrato = true;
    p.gen.octaveMask = 0x1F;
    p.gen.amountNote = static_cast<std::uint8_t> (10 + r.nextInt (91));
    p.gen.amountAccent = static_cast<std::uint8_t> (r.nextInt (101));
    p.gen.amountSlide = static_cast<std::uint8_t> (r.nextInt (101));
    p.gen.amountVibrato = static_cast<std::uint8_t> (r.nextInt (101));
    if (seed % 4 == 0) p.divMod = DivMod::Triplet;
    regenerate (p, seed);
    return p;
}

bool stableUnderKnobs (const Pattern& p)
{
    Pattern q = p;
    applyDensities (q);
    return samePattern (q, p);
}

std::string show (const Pattern& p)
{
    StepTextOptions o;
    o.header = false;
    o.restPitches = true;
    return toStepText (p, o);
}

std::uint8_t& amountOf (Pattern& p, int knob)
{
    switch (knob)
    {
        case 0: return p.gen.amountNote;
        case 1: return p.gen.amountAccent;
        case 2: return p.gen.amountSlide;
        default: return p.gen.amountVibrato;
    }
}

/** Sweeps each density knob over 0..100 from p (which applyDensities leaves alone) and checks
    that no pitch moves, that each knob only adds or removes its own events, and that the
    starting amount gives p back. */
void requireKnobsOnlyAddOrRemove (const Pattern& p)
{
    for (int knob = 0; knob < 4; ++knob)
    {
        Pattern last;
        for (int a = 0; a <= 100; ++a)
        {
            Pattern cur = p;
            amountOf (cur, knob) = static_cast<std::uint8_t> (a);
            applyDensities (cur);
            Pattern start = p;
            if (a == amountOf (start, knob)) REQUIRE (samePattern (cur, p));
            for (int i = 0; i < kMaxSteps; ++i)
            {
                const Step& c = cur.steps[static_cast<std::size_t> (i)];
                const Step& o = p.steps[static_cast<std::size_t> (i)];
                REQUIRE (c.note == o.note);
                REQUIRE (c.octave == o.octave);
                if (knob != 0) REQUIRE (c.state == o.state);
                if (a == 0) continue;
                const Step& l = last.steps[static_cast<std::size_t> (i)];
                if (knob == 0 && l.state == StepState::Note) REQUIRE (c.state == StepState::Note);
                if (knob == 1 && l.accent) REQUIRE (c.accent);
                if (knob == 2 && l.slide) REQUIRE (c.slide);
                if (knob == 3 && l.vibrato) REQUIRE (c.vibrato);
            }
            last = cur;
        }
    }
}

struct Edit
{
    const char* name;
    std::function<void (Pattern&, acid::Rng&)> run;
};

std::vector<Edit> gridEdits()
{
    using R = acid::Rng;
    std::vector<Edit> e;
    e.push_back ({ "shift right", [] (Pattern& p, R&) { shiftRight (p); } });
    e.push_back ({ "shift left 3", [] (Pattern& p, R&) { shiftLeft (p, 3); } });
    e.push_back ({ "rotate any", [] (Pattern& p, R& r) { rotate (p, r.nextInt (300) - 150); } });
    e.push_back ({ "reverse", [] (Pattern& p, R&) { reverse (p); } });
    e.push_back ({ "reverseSteps", [] (Pattern& p, R&) { reverseSteps (p); } });
    e.push_back ({ "paste own steps", [] (Pattern& p, R& r) {
        const auto clip = copySteps (p, r.nextInt (p.length), 1 + r.nextInt (p.length));
        pasteSteps (p, r.nextInt (p.length), clip, r.chance (0.5));
    } });
    e.push_back ({ "paste from another line", [] (Pattern& p, R& r) {
        const Pattern other = drawn (static_cast<std::uint64_t> (r.nextInt (100000)) + 1, 64);
        const auto clip = copySteps (other, r.nextInt (64), 1 + r.nextInt (64));
        pasteSteps (p, r.nextInt (p.length), clip, r.chance (0.5));
    } });
    e.push_back ({ "duplicate", [] (Pattern& p, R&) { duplicate (p); } });
    e.push_back ({ "setLength", [] (Pattern& p, R& r) { setLength (p, 1 + r.nextInt (kMaxSteps)); } });
    e.push_back ({ "setLength down and up", [] (Pattern& p, R& r) {
        const int len = p.length;
        setLength (p, 1 + r.nextInt (len));
        setLength (p, len);
    } });
    e.push_back ({ "resetTiming", [] (Pattern& p, R& r) { resetTiming (p, r.nextInt (64), r.nextInt (64)); } });
    e.push_back ({ "muteSteps", [] (Pattern& p, R&) { muteSteps (p); } });
    for (LaneBit lane : { kLanePitch, kLaneOctave, kLaneGate, kLaneAccent, kLaneSlide, kLaneVibrato })
        e.push_back ({ "randomiseLane", [lane] (Pattern& p, R& r) { randomiseLane (p, lane, r.nextU64(), r.nextInt (101)); } });
    e.push_back ({ "transpose", [] (Pattern& p, R& r) { transpose (p, r.nextInt (25) - 12); } });
    e.push_back ({ "transpose without generator", [] (Pattern& p, R& r) { transpose (p, r.nextInt (25) - 12, false); } });
    e.push_back ({ "invertArp", [] (Pattern& p, R& r) { invertArp (p, r.nextInt (20)); } });
    e.push_back ({ "authentic state", [] (Pattern& p, R& r) {
        std::vector<PitchEntry> spare { { 7, 1 } };
        authenticSetState (p, spare, r.nextInt (p.length), static_cast<StepState> (r.nextInt (3)));
    } });
    e.push_back ({ "authentic pitch", [] (Pattern& p, R& r) {
        std::vector<PitchEntry> spare;
        authenticWritePitch (p, spare, r.nextInt (4), r.nextInt (12), r.nextInt (3) - 1);
    } });
    e.push_back ({ "authentic flags", [] (Pattern& p, R& r) {
        std::vector<PitchEntry> spare;
        authenticSetFlags (p, spare, r.nextInt (4), r.chance (0.5), r.chance (0.5), r.chance (0.5));
    } });
    e.push_back ({ "authentic length", [] (Pattern& p, R& r) {
        std::vector<PitchEntry> spare;
        authenticSetLength (p, spare, 1 + r.nextInt (kMaxSteps));
    } });
    return e;
}

/** One direct edit of a Step field, the way a grid editor makes it. Returns the lane. */
LaneBit handEdit (Pattern& p, int step, acid::Rng& r)
{
    Step& s = p.steps[static_cast<std::size_t> (step)];
    switch (r.nextInt (6))
    {
        case 0: s.state = static_cast<StepState> ((static_cast<int> (s.state) + 1 + r.nextInt (2)) % 3); return kLaneGate;
        case 1: s.note = static_cast<std::uint8_t> ((s.note + 1 + r.nextInt (11)) % 12); return kLanePitch;
        case 2: s.octave = static_cast<std::int8_t> (s.octave == kMaxOctave ? kMinOctave : s.octave + 1); return kLaneOctave;
        case 3: s.accent = ! s.accent; return kLaneAccent;
        case 4: s.slide = ! s.slide; return kLaneSlide;
        default: s.vibrato = ! s.vibrato; return kLaneVibrato;
    }
}

bool laneValue (const Step& s, LaneBit lane, int& out)
{
    switch (lane)
    {
        case kLaneGate: out = static_cast<int> (s.state); return true;
        case kLanePitch: out = s.note; return true;
        case kLaneOctave: out = s.octave; return true;
        case kLaneAccent: out = s.accent; return true;
        case kLaneSlide: out = s.slide; return true;
        case kLaneVibrato: out = s.vibrato; return true;
    }
    return false;
}
}

TEST_CASE ("every grid edit on a generated line survives the next density knob move", "[gen][edits]")
{
    const auto edits = gridEdits();
    for (std::uint64_t seed = 1; seed <= 120; ++seed)
        for (int length : { 1, 5, 12, 16, 17, 32, 40, 64 })
        {
            const Pattern base = drawn (seed, length);
            REQUIRE (stableUnderKnobs (base));
            for (std::size_t k = 0; k < edits.size(); ++k)
            {
                acid::Rng r (seed * 1000 + k);
                Pattern p = base;
                edits[k].run (p, r);
                INFO ("seed " << seed << " length " << length << " edit " << edits[k].name << "\n  before " << show (base) << "\n  after  " << show (p));
                REQUIRE (stableUnderKnobs (p));
            }
        }
}

TEST_CASE ("chains of edits stay stable, and the knobs still only add or remove", "[gen][edits]")
{
    const auto edits = gridEdits();
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
    {
        acid::Rng r (seed + 99);
        Pattern p = drawn (seed, 1 + r.nextInt (kMaxSteps));
        for (int n = 0; n < 6; ++n)
        {
            const Edit& e = edits[static_cast<std::size_t> (r.nextInt (static_cast<int> (edits.size())))];
            e.run (p, r);
            INFO ("seed " << seed << " after " << e.name << ": " << show (p));
            REQUIRE (stableUnderKnobs (p));
        }
        requireKnobsOnlyAddOrRemove (p);
    }
}

TEST_CASE ("Draw, Shift, ride the Note knob: shifting commutes with the knobs", "[gen][edits]")
{
    for (std::uint64_t seed = 1; seed <= 150; ++seed)
        for (int length : { 3, 16, 17, 64 })
        {
            Pattern gen;
            gen.length = static_cast<std::uint8_t> (length);
            gen.gen.useVibrato = true;
            if (seed % 3 == 0) gen.divMod = DivMod::Triplet;
            generate (gen, seed);
            const int n = static_cast<int> (seed % 7) - 3;
            for (int knob = 0; knob < 4; ++knob)
                for (int a = 0; a <= 100; a += 5)
                {
                    Pattern shiftedThenKnob = gen;
                    amountOf (shiftedThenKnob, knob) = static_cast<std::uint8_t> (a);
                    applyDensities (shiftedThenKnob);
                    rotate (shiftedThenKnob, n);
                    applyDensities (shiftedThenKnob);   // the knob move after the shift

                    Pattern knobThenShifted = gen;
                    amountOf (knobThenShifted, knob) = static_cast<std::uint8_t> (a);
                    applyDensities (knobThenShifted);
                    rotate (knobThenShifted, n);
                    REQUIRE (samePattern (shiftedThenKnob, knobThenShifted));
                }
        }
}

TEST_CASE ("duplicate commutes with the knobs", "[gen][edits]")
{
    for (std::uint64_t seed = 1; seed <= 150; ++seed)
        for (int length : { 1, 7, 16, 32, 40 })
        {
            Pattern gen;
            gen.length = static_cast<std::uint8_t> (length);
            generate (gen, seed);
            for (int a = 0; a <= 100; a += 10)
            {
                Pattern x = gen;
                x.gen.amountNote = x.gen.amountSlide = static_cast<std::uint8_t> (a);
                Pattern y = x;
                applyDensities (x);
                duplicate (x);
                duplicate (y);
                applyDensities (y);
                INFO ("seed " << seed << " length " << length << " amount " << a);
                REQUIRE (samePattern (x, y));
            }
        }
}

TEST_CASE ("hand edits on a generated line survive every later knob move after keepEdits", "[gen][edits]")
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        acid::Rng r (seed * 31 + 5);
        Pattern p = drawn (seed, 1 + r.nextInt (kMaxSteps));
        struct Done { int step; LaneBit lane; int value; };
        std::vector<Done> done;
        for (int n = 0; n < 1 + r.nextInt (6); ++n)
        {
            const int step = r.nextInt (kMaxSteps);
            const LaneBit lane = handEdit (p, step, r);
            keepEdits (p);
            done.erase (std::remove_if (done.begin(), done.end(), [&] (const Done& d) { return d.step == step && d.lane == lane; }), done.end());
            int v = 0;
            laneValue (p.steps[static_cast<std::size_t> (step)], lane, v);
            done.push_back ({ step, lane, v });
        }
        REQUIRE (stableUnderKnobs (p));
        const Pattern edited = p;
        for (int knob = 0; knob < 4; ++knob)
            for (int a = 0; a <= 100; a += 4)
            {
                Pattern cur = edited;
                amountOf (cur, knob) = static_cast<std::uint8_t> (a);
                applyDensities (cur);
                for (const Done& d : done)
                {
                    // A slide edited by hand stays as set. Gate and pitch edits never move.
                    int v = 0;
                    laneValue (cur.steps[static_cast<std::size_t> (d.step)], d.lane, v);
                    INFO ("seed " << seed << " step " << d.step << " lane " << int (d.lane) << " knob " << knob << " amount " << a);
                    REQUIRE (v == d.value);
                }
            }
        // The locks are never touched, so the next Draw regenerates the edited lanes as usual.
        for (const Step& s : p.steps) REQUIRE (s.locks == 0);
    }
}

TEST_CASE ("keepEdits is a no-op on a line that only the generator touched", "[gen][edits]")
{
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        Pattern p = drawn (seed, 1 + static_cast<int> (seed % 64));
        const Pattern orig = p;
        keepEdits (p);
        REQUIRE (samePattern (p, orig));
    }
    Pattern plain = acid::seq::test::randomPattern (3);
    flatten (plain);
    const Pattern orig = plain;
    keepEdits (plain);
    REQUIRE (samePattern (plain, orig));
}

TEST_CASE ("stored memory is the turn-on amount of each lane", "[gen]")
{
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        Pattern p = drawn (seed, 64);
        for (const Step& s : p.steps)
        {
            const int gate = laneTurnOnAmount (s, kLaneGate);
            REQUIRE (gate >= 1);
            REQUIRE (gate <= 100);
            REQUIRE ((s.state == StepState::Note) == (p.gen.amountNote >= gate));
            const int acc = laneTurnOnAmount (s, kLaneAccent);
            REQUIRE (s.accent == (s.state == StepState::Note && p.gen.amountAccent > 0 && p.gen.amountAccent >= acc));
            REQUIRE (laneTurnOnAmount (s, kLanePitch) == -1);
            REQUIRE (laneTurnOnAmount (s, kLaneOctave) == -1);
        }
    }
    REQUIRE (laneTurnOnAmount (Step {}, kLaneGate) == -1);
}

TEST_CASE ("triplet patterns use a three-per-beat table and 12-step bars", "[gen]")
{
    long onBeat = 0, weak = 0, pushing = 0;
    for (std::uint64_t seed = 1; seed <= 2000; ++seed)
    {
        Pattern t;
        t.length = 24;
        t.div = StepDiv::d1_8;
        t.divMod = DivMod::Triplet;
        generate (t, seed);
        // Bar anchors: the first step of every 12-step bar sounds from amount 1.
        for (int b = 0; b < kMaxSteps; b += 12) REQUIRE (t.steps[static_cast<std::size_t> (b)].magGate == 1);
        t.gen.amountNote = 50;
        applyDensities (t);
        for (int i = 0; i < 24; ++i)
        {
            if (t.steps[static_cast<std::size_t> (i)].state != StepState::Note || i % 12 == 0) continue;
            (i % 3 == 0 ? onBeat : i % 3 == 1 ? weak : pushing) += 1;
        }

        Pattern f;
        f.length = 32;
        generate (f, seed);
        for (int b = 0; b < kMaxSteps; b += 16) REQUIRE (f.steps[static_cast<std::size_t> (b)].magGate == 1);
    }
    // Per position: six on-beat steps (bar anchors excluded), eight of each other kind.
    const double beatRate = static_cast<double> (onBeat) / 6.0;
    const double weakRate = static_cast<double> (weak) / 8.0;
    const double pushRate = static_cast<double> (pushing) / 8.0;
    INFO ("per position: on the beat " << beatRate << ", second triplet " << weakRate << ", third triplet " << pushRate);
    CHECK (beatRate > pushRate);
    CHECK (pushRate > weakRate);
}

TEST_CASE ("keepEdits and the generator-aware edits never allocate", "[gen][edits][rt]")
{
    Pattern p = drawn (9, 24);
    const std::vector<Step> clip = copySteps (p, 2, 5);
    acid::test::ScopedAllocGuard guard;
    keepEdits (p);
    setLength (p, 13);
    setLength (p, 40);
    duplicate (p);
    pasteSteps (p, 3, clip, true);
    resetTiming (p, 30, 10);
    reverse (p);
    reverseSteps (p);
    (void) laneTurnOnAmount (p.steps[0], kLaneSlide);
    REQUIRE (guard.count() == 0);
}
