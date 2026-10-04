// Reviewer's tests. Each one encodes a defect found in review and fails until it is fixed.
#include "SeqTestSupport.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid::seq;

namespace
{
Pattern drawnLine (std::uint64_t seed)
{
    Pattern p;
    regenerate (p, seed);
    return p;
}

int changedSteps (const Pattern& a, const Pattern& b)
{
    int n = 0;
    for (int i = 0; i < a.length; ++i)
        if (! sameStep (a.steps[static_cast<std::size_t> (i)], b.steps[static_cast<std::size_t> (i)])) ++n;
    return n;
}
}

TEST_CASE ("review: a shifted generated line survives the next density knob move", "[review][gen]")
{
    // Draw, Shift right once, then touch any density knob (every knob move calls
    // applyDensities). SPEC 6.3: a density change only adds or removes events, it never moves
    // the ones already showing. The thresholds depend on the absolute step index (metric
    // weight i % 16, accent weight i % 4, bar anchors, neighbour anti-clustering), so the
    // moved magnitudes are re-thresholded at their new positions and the rhythm changes.
    for (std::uint64_t seed : { 1ull, 2ull, 3ull })
    {
        Pattern shifted = drawnLine (seed);
        rotate (shifted, 1);
        Pattern afterKnob = shifted;
        applyDensities (afterKnob);   // same amounts: only a re-derive
        INFO ("seed " << seed << ": " << changedSteps (shifted, afterKnob) << " steps changed");
        CHECK (samePattern (afterKnob, shifted));
    }
}

TEST_CASE ("review: a reversed generated line survives the next density knob move", "[review][gen]")
{
    Pattern rev = drawnLine (7);
    reverse (rev);
    Pattern afterKnob = rev;
    applyDensities (afterKnob);
    CHECK (samePattern (afterKnob, rev));
}

TEST_CASE ("review: a hand edit on a generated line survives a density knob move", "[review][gen]")
{
    // The parity target lets the user edit the grid while the generator is active. Without a
    // lock (and nothing sets one for a plain grid edit), applyDensities writes the candidate
    // pitch and the thresholded state back over the edit.
    //
    // Fix: the edit entry point is keepEdits (Generator.h), which an editor calls after any
    // direct Step write; applyDensities alone cannot tell an edit from a stale derive.
    Pattern p = drawnLine (1);
    Step& s = p.steps[1];
    s.state = s.state == StepState::Note ? StepState::Rest : StepState::Note;
    s.note = static_cast<std::uint8_t> ((s.note + 5) % 12);
    keepEdits (p);
    const Pattern edited = p;

    p.gen.amountAccent = static_cast<std::uint8_t> (p.gen.amountAccent + 10);
    applyDensities (p);
    CHECK (p.steps[1].state == edited.steps[1].state);
    CHECK (p.steps[1].note == edited.steps[1].note);
}

TEST_CASE ("review: generate keeps only 32 bits of the seed but draws from all 64", "[review][gen]")
{
    // GenParams::seed is uint32. generate (p, s) stores s & 0xFFFFFFFF but seeds the streams
    // with all 64 bits, so re-generating from the stored seed (after a mask change, or
    // generate (p) after a reload) gives a different line from the one that was drawn.
    const std::uint64_t big = 0x100000005ull;
    Pattern a;
    generate (a, big);
    Pattern b;
    generate (b, a.gen.seed);
    CHECK (samePattern (a, b));
}
