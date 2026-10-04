// The closed-form step grid: divisions, shuffle, gate lengths, the position-to-step mapping
// and the musical-time to sample mapping.
#include "Harness.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ptest;
using Catch::Approx;

TEST_CASE ("StepClock: step lengths for every division and modifier", "[stepclock]")
{
    const StepDiv divs[] = { StepDiv::d1_64, StepDiv::d1_32, StepDiv::d1_16, StepDiv::d1_8, StepDiv::d1_4, StepDiv::d1_2, StepDiv::d1_1 };
    const double full[] = { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0 };

    for (int i = 0; i < 7; ++i)
    {
        REQUIRE (stepLengthQ (divs[i], DivMod::Full) == full[i]);
        REQUIRE (stepLengthQ (divs[i], DivMod::Dotted) == Approx (full[i] * 1.5).epsilon (1e-15));
        REQUIRE (stepLengthQ (divs[i], DivMod::Triplet) == Approx (full[i] * 2.0 / 3.0).epsilon (1e-15));
    }

    // Three triplet steps span two full ones.
    REQUIRE (3.0 * stepLengthQ (StepDiv::d1_8, DivMod::Triplet) == Approx (1.0).epsilon (1e-15));
}

TEST_CASE ("StepClock: shuffle delays every second step, 100 % is a 75:25 pair", "[stepclock]")
{
    Pattern p;
    p.shuffle = 100;
    const StepClock c = StepClock::forPattern (p, 0.0, 0);

    REQUIRE (c.onsetQ (0) == 0.0);
    REQUIRE (c.onsetQ (1) == Approx (0.375));      // 75 % of the half-beat pair
    REQUIRE (c.onsetQ (2) == 0.5);
    REQUIRE (c.onsetQ (3) == Approx (0.875));
    REQUIRE (c.stepDurationQ (0) / (c.stepDurationQ (0) + c.stepDurationQ (1)) == Approx (0.75));
    REQUIRE (c.stepDurationQ (1) / (c.stepDurationQ (0) + c.stepDurationQ (1)) == Approx (0.25));

    p.shuffle = 50;
    const StepClock half = StepClock::forPattern (p, 0.0, 0);
    REQUIRE (half.onsetQ (1) == Approx (0.25 + 0.0625));   // 62.5 : 37.5
    REQUIRE (half.onsetQ (-1) == Approx (-0.25 + 0.0625)); // odd steps before the origin too

    p.shuffle = 0;
    REQUIRE (StepClock::forPattern (p, 0.0, 0).onsetQ (1) == 0.25);

    // Dotted and triplet divisions ignore shuffle.
    p.shuffle = 100;
    p.divMod = DivMod::Triplet;
    REQUIRE (shuffleDelayQ (p) == 0.0);
    REQUIRE (StepClock::forPattern (p, 0.0, 0).onsetQ (1) == Approx (1.0 / 6.0));
    p.divMod = DivMod::Dotted;
    REQUIRE (shuffleDelayQ (p) == 0.0);
    REQUIRE (StepClock::forPattern (p, 0.0, 0).onsetQ (1) == Approx (0.375));

    // Every full division shuffles, by half its own step at most.
    p.divMod = DivMod::Full;
    p.div = StepDiv::d1_8;
    REQUIRE (shuffleDelayQ (p) == Approx (0.25));
}

TEST_CASE ("StepClock: shuffle parity follows the absolute grid, not the pattern step", "[stepclock]")
{
    Pattern p;
    p.shuffle = 60;

    // A clock anchored on an odd grid step delays its even local steps.
    const StepClock c = StepClock::forPattern (p, 3.75, 15);
    REQUIRE (c.isDelayed (0));
    REQUIRE_FALSE (c.isDelayed (1));
    REQUIRE (c.onsetQ (0) == Approx (3.75 + 0.3 * 0.25));
    REQUIRE (c.onsetQ (1) == Approx (4.0));

    REQUIRE (gridIndexFor (3.75, 0.25) == 15);
    REQUIRE (gridIndexFor (3.80, 0.25) == 0);        // off the grid: parity starts at the anchor
    REQUIRE (gridIndexFor (-1.0, 0.25) == -4);
}

TEST_CASE ("StepClock: position to step mapping is the inverse of the onset formula", "[stepclock]")
{
    Pattern p;

    for (int shuffle : { 0, 37, 100 })
    {
        p.shuffle = static_cast<std::uint8_t> (shuffle);
        const StepClock c = StepClock::forPattern (p, 1.3, 0);

        for (std::int64_t g = -40; g <= 40; ++g)
        {
            const double t = c.onsetQ (g);
            REQUIRE (c.firstStepAtOrAfter (t) == g);
            REQUIRE (c.firstStepAtOrAfter (t + 1.0e-9) == g + 1);
            REQUIRE (c.firstStepAtOrAfter (t - 1.0e-9) == g);
            REQUIRE (c.stepAt (t) == g);
            REQUIRE (c.stepAt (t + 0.01) == g);
            REQUIRE (c.stepAt (t - 1.0e-9) == g - 1);
            REQUIRE (c.onsetQ (g + 1) > c.onsetQ (g));
        }
    }
}

TEST_CASE ("StepClock: gate lengths", "[stepclock]")
{
    Pattern p;
    Step s;

    // Authentic: half a step plus 1.35 ms, whatever the tempo.
    for (double bpm : { 40.0, 120.0, 147.0, 300.0 })
    {
        const double seconds = gateLengthQ (p, s, bpm) * 60.0 / bpm;
        const double stepSeconds = 0.25 * 60.0 / bpm;
        REQUIRE (seconds == Approx (0.5 * stepSeconds + 1.35e-3).epsilon (1e-12));
    }

    // 147 BPM: a 102 ms step gives a 52.4 ms gate, as measured on the original unit.
    REQUIRE (gateLengthQ (p, s, 147.0) * 60.0 / 147.0 == Approx (0.05237).margin (0.0003));

    p.div = StepDiv::d1_8;
    p.divMod = DivMod::Triplet;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.5 / 3.0 + 1.35e-3 * 2.0).epsilon (1e-12));

    // A percentage of the step.
    p.div = StepDiv::d1_16;
    p.divMod = DivMod::Full;
    p.gateLength = 80;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.2));
    p.gateLength = 100;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.25));

    // The step's own gate wins.
    s.gate = 127;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.25));
    s.gate = 1;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.25 / 127.0));
    p.gateLength = 0;
    s.gate = 64;
    REQUIRE (gateLengthQ (p, s, 120.0) == Approx (0.25 * 64.0 / 127.0));
}

TEST_CASE ("StepClock: an event belongs to the first sample at or after its time", "[stepclock]")
{
    const double qps = 120.0 / (60.0 * 44100.0);     // a 1/16 step is 5512.5 samples

    REQUIRE (sampleIndexAtOrAfter (0.0, 0.0, qps) == 0);
    REQUIRE (sampleIndexAtOrAfter (0.25, 0.0, qps) == 5513);
    REQUIRE (sampleIndexAtOrAfter (0.5, 0.0, qps) == 11025);
    REQUIRE (sampleIndexAtOrAfter (0.5, 0.25, qps) == 5513);
    REQUIRE (sampleIndexAtOrAfter (0.0, 0.25, qps) == -5512);

    // The absolute sample does not depend on where the block starts.
    for (std::int64_t blockStart : { 0, 1, 17, 5000, 5512, 5513, 11024 })
    {
        const double startQ = static_cast<double> (blockStart) * qps;
        REQUIRE (blockStart + sampleIndexAtOrAfter (0.5, startQ, qps) == 11025);
        REQUIRE (blockStart + sampleIndexAtOrAfter (0.75, startQ, qps) == 16538);
    }

    REQUIRE (sampleIndexAtOrAfter (std::numeric_limits<double>::infinity(), 0.0, qps) == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE ("StepClock: directions and cycle lengths", "[stepclock]")
{
    REQUIRE (cycleLength (16, Direction::Forward) == 16);
    REQUIRE (cycleLength (16, Direction::Reverse) == 16);
    REQUIRE (cycleLength (16, Direction::Random) == 16);
    REQUIRE (cycleLength (4, Direction::PingPong) == 6);
    REQUIRE (cycleLength (2, Direction::PingPong) == 2);
    REQUIRE (cycleLength (1, Direction::PingPong) == 1);

    const int pingPong[] = { 0, 1, 2, 3, 2, 1 };
    for (int i = 0; i < 6; ++i)
    {
        REQUIRE (stepPosition (Direction::PingPong, 4, i, 1, i) == pingPong[i]);
        if (i < 4)
        {
            REQUIRE (stepPosition (Direction::Forward, 4, i, 1, i) == i);
            REQUIRE (stepPosition (Direction::Reverse, 4, i, 1, i) == 3 - i);
        }
    }

    // Random: in range, a function of (seed, absolute index) only, and it does visit every step.
    bool seen[8] = {};
    for (std::int64_t g = -200; g < 200; ++g)
    {
        const int a = stepPosition (Direction::Random, 8, 0, 77, g);
        REQUIRE (a >= 0);
        REQUIRE (a < 8);
        REQUIRE (a == stepPosition (Direction::Random, 8, 5, 77, g));
        seen[a] = true;
    }
    for (bool b : seen) REQUIRE (b);

    REQUIRE (floorMod (-1, 16) == 15);
    REQUIRE (floorMod (-16, 16) == 0);
    REQUIRE (floorDiv (-1, 16) == -1);
    REQUIRE (floorDiv (-16, 16) == -1);
    REQUIRE (floorDiv (31, 16) == 1);
}

TEST_CASE ("StepClock: the probability roll is a fair, repeatable function of seed and step", "[stepclock]")
{
    int passes = 0;

    for (std::int64_t g = 0; g < 20000; ++g)
    {
        const bool a = stepRollPasses (1234, g, 30);
        REQUIRE (a == stepRollPasses (1234, g, 30));
        if (a) ++passes;

        REQUIRE (stepRollPasses (1234, g, 100));
        REQUIRE_FALSE (stepRollPasses (1234, g, 0));

        // Raising the probability never removes a hit.
        if (a) REQUIRE (stepRollPasses (1234, g, 60));
    }

    REQUIRE (passes > 5700);
    REQUIRE (passes < 6300);

    // A different seed gives a different sequence.
    int same = 0;
    for (std::int64_t g = 0; g < 1000; ++g)
        if (stepRollPasses (1, g, 50) == stepRollPasses (2, g, 50)) ++same;
    REQUIRE (same > 400);
    REQUIRE (same < 600);
}
