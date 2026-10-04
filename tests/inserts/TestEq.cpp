#include "InsertTestUtil.h"
#include "acid/fx/Eq.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Matchers::WithinAbs;

namespace
{
fx::EqParams flat (bool on = true)
{
    fx::EqParams p;
    p.on = on;
    return p;
}

std::vector<float> eqImpulseResponse (const fx::EqParams& p, double sr, std::size_t n)
{
    fx::Eq eq;
    eq.prepare (sr, 512);
    eq.setParams (p);
    eq.reset();   // start fully on, no fade
    auto buf = impulse (n);
    renderConst (eq, buf, p, 512);
    return buf.l;
}
}

TEST_CASE ("EQ: each band matches the analytic peaking response", "[inserts][eq]")
{
    struct Case { int band; double freq, width, gain; };
    const Case cases[] = {
        { 0, 100.0, 2.0, 12.0 }, { 0, 20.0, 0.5, -24.0 }, { 1, 550.0, 1.5, -9.0 }, { 1, 1000.0, 4.0, 24.0 },
        { 2, 3500.0, 0.5, 18.0 }, { 2, 6000.0, 1.0, -24.0 }, { 3, 10000.0, 2.0, 6.0 }, { 3, 16000.0, 3.0, -12.0 },
    };

    for (const double sr : kRates)
        for (const auto& c : cases)
        {
            auto p = flat();
            float* fields[4][3] = { { &p.lowFreqHz, &p.lowWidthOct, &p.lowGainDb },
                                    { &p.loMidFreqHz, &p.loMidWidthOct, &p.loMidGainDb },
                                    { &p.hiMidFreqHz, &p.hiMidWidthOct, &p.hiMidGainDb },
                                    { &p.highFreqHz, &p.highWidthOct, &p.highGainDb } };
            *fields[c.band][0] = static_cast<float> (c.freq);
            *fields[c.band][1] = static_cast<float> (c.width);
            *fields[c.band][2] = static_cast<float> (c.gain);

            const auto ir = eqImpulseResponse (p, sr, 1u << 16);
            for (double hz = 15.0; hz < 0.45 * sr; hz *= 1.37)
            {
                const double measured = gainToDb (dftMagnitude (ir, hz, sr));
                const double expected = gainToDb (fx::Eq::bandMagnitude (c.freq, c.width, c.gain, hz, sr));
                INFO ("sr " << sr << " band " << c.band << " f0 " << c.freq << " at " << hz << " Hz");
                REQUIRE_THAT (measured, WithinAbs (expected, 0.02));
            }
            // Exact gain at the centre.
            REQUIRE_THAT (gainToDb (dftMagnitude (ir, c.freq, sr)), WithinAbs (c.gain, 0.02));
        }
}

TEST_CASE ("EQ: four bands combine as the product of their responses", "[inserts][eq]")
{
    auto p = flat();
    p.lowGainDb = 9.0f;   p.lowFreqHz = 80.0f;    p.lowWidthOct = 1.0f;
    p.loMidGainDb = -6.0f; p.loMidFreqHz = 400.0f; p.loMidWidthOct = 2.5f;
    p.hiMidGainDb = 4.0f; p.hiMidFreqHz = 2500.0f; p.hiMidWidthOct = 0.7f;
    p.highGainDb = -15.0f; p.highFreqHz = 12000.0f; p.highWidthOct = 3.5f;
    const double sr = 48000.0;
    const auto ir = eqImpulseResponse (p, sr, 1u << 16);
    for (double hz = 20.0; hz < 20000.0; hz *= 1.21)
    {
        const double expected = gainToDb (fx::Eq::bandMagnitude (80, 1, 9, hz, sr) * fx::Eq::bandMagnitude (400, 2.5, -6, hz, sr)
                                          * fx::Eq::bandMagnitude (2500, 0.7, 4, hz, sr) * fx::Eq::bandMagnitude (12000, 3.5, -15, hz, sr));
        REQUIRE_THAT (gainToDb (dftMagnitude (ir, hz, sr)), WithinAbs (expected, 0.02));
    }
}

TEST_CASE ("EQ: width is the octave span between half-gain points", "[inserts][eq]")
{
    for (const double w : { 0.5, 1.0, 2.0, 4.0 })
    {
        auto p = flat();
        p.loMidFreqHz = 300.0f;
        p.loMidWidthOct = static_cast<float> (w);
        p.loMidGainDb = 12.0f;
        const auto ir = eqImpulseResponse (p, 96000.0, 1u << 17);
        const double lo = 300.0 * std::exp2 (-w / 2.0), hi = 300.0 * std::exp2 (w / 2.0);
        REQUIRE_THAT (gainToDb (dftMagnitude (ir, lo, 96000.0)), WithinAbs (6.0, 0.1));
        REQUIRE_THAT (gainToDb (dftMagnitude (ir, hi, 96000.0)), WithinAbs (6.0, 0.1));
    }
}

TEST_CASE ("EQ: gain and frequency automation does not zipper", "[inserts][eq]")
{
    const double sr = 48000.0;
    const std::size_t n = 96000;
    auto buf = sine (n, 200.0, sr, 0.25);
    fx::Eq eq;
    eq.prepare (sr, 512);
    auto p = flat();
    p.lowFreqHz = 200.0f;
    p.lowWidthOct = 1.0f;
    eq.setParams (p);
    eq.reset();

    // Worst case: the gain jumps between the extremes every block and the frequency hops.
    renderBlocks (eq, buf, 512, [&p] (fx::Eq& e, int pos) {
        const int blk = pos / 512;
        p.lowGainDb = (blk % 2 == 0) ? 24.0f : -24.0f;
        p.lowFreqHz = (blk % 3 == 0) ? 150.0f : 260.0f;
        e.setParams (p);
    });

    // Second difference of a 200 Hz sine with gain up to 16 is at most 0.25 * 16 * w^2.
    const double w = kTwoPi * 200.0 / sr;
    const double bound = 0.25 * 16.0 * w * w;
    double worst = 0.0;
    for (std::size_t i = 2; i < n; ++i)
        worst = std::max (worst, static_cast<double> (std::abs (buf.l[i] - 2.0f * buf.l[i - 1] + buf.l[i - 2])));
    INFO ("second difference " << worst << " vs steady-state bound " << bound);
    REQUIRE (worst < 2.0 * bound);
    REQUIRE (allFinite (buf));
}

TEST_CASE ("EQ: a band at 0 dB is exactly transparent", "[inserts][eq]")
{
    fx::Eq eq;
    eq.prepare (48000.0, 256);
    const auto p = flat();
    eq.setParams (p);
    eq.reset();
    auto buf = noise (4096, 7);
    const auto ref = buf;
    renderConst (eq, buf, p);
    REQUIRE (buf.l == ref.l);
    REQUIRE (buf.r == ref.r);
}
