#include "InsertTestUtil.h"
#include "acid/fx/Chorus.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
fx::ChorusParams wetSingle()
{
    fx::ChorusParams p;
    p.on = true;
    p.mixPct = 100.0f;
    p.fatnessPct = 0.0f;
    p.hpPct = 0.0f;
    return p;
}

Stereo run (const fx::ChorusParams& p, Stereo buf, double sr, int block = 256)
{
    fx::Chorus ch;
    ch.prepare (sr, block);
    ch.setParams (p);
    ch.reset();
    renderConst (ch, buf, p, block);
    return buf;
}

/** Largest relative deviation of the instantaneous frequency from f0, from the spacing of
    interpolated upward zero crossings. */
double maxPitchDeviation (const std::vector<float>& v, double f0, double sr, std::size_t from)
{
    double last = -1.0, worst = 0.0;
    for (std::size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (v[i - 1] / (v[i - 1] - v[i]));
            if (last >= 0.0) worst = std::max (worst, std::abs (sr / (t - last) / f0 - 1.0));
            last = t;
        }
    return worst;
}
}

TEST_CASE ("Chorus: pitch modulation depth matches the sweep and stays within range", "[inserts][chorus]")
{
    const double sr = 48000.0;
    for (const float rate : { 10.0f, 35.0f, 70.0f, 100.0f })
        for (const float depth : { 20.0f, 60.0f, 100.0f })
        {
            auto p = wetSingle();
            p.ratePct = rate;
            p.depthPct = depth;
            const double hz = fx::Chorus::rateHz (rate);
            const double seconds = std::max (2.5 / hz, 1.0);
            const auto out = run (p, sine (static_cast<std::size_t> (seconds * sr), 1000.0, sr, 0.5), sr);
            const double measured = maxPitchDeviation (out.l, 1000.0, sr, static_cast<std::size_t> (0.06 * sr));
            const double predicted = kPi * hz * fx::Chorus::sweepSeconds (depth, rate);
            INFO ("rate " << rate << " % (" << hz << " Hz), depth " << depth << " %: deviation " << measured << ", predicted " << predicted);
            REQUIRE_THAT (measured, WithinRel (predicted, 0.08));
            REQUIRE (measured <= fx::Chorus::kMaxPitchDeviation * 1.05);
        }
}

TEST_CASE ("Chorus: mix is a linear crossfade of dry and wet", "[inserts][chorus]")
{
    const double sr = 48000.0;
    auto p = wetSingle();
    p.fatnessPct = 70.0f;
    p.depthPct = 60.0f;
    const auto in = noise (24000, 5);
    p.mixPct = 100.0f;
    const auto wet = run (p, in, sr);
    for (const float mix : { 0.0f, 25.0f, 50.0f, 80.0f })
    {
        p.mixPct = mix;
        const auto out = run (p, in, sr);
        const double m = static_cast<double> (mix) * 0.01;
        double worst = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (out.l[i]) - ((1.0 - m) * static_cast<double> (in.l[i]) + m * static_cast<double> (wet.l[i]))));
        REQUIRE (worst < 1e-6);
    }
}

TEST_CASE ("Chorus: fatness adds the second line at 1.5 times the base delay", "[inserts][chorus]")
{
    const double sr = 48000.0;
    auto p = wetSingle();
    p.depthPct = 0.0f;
    p.offsetPct = 0.0f;   // 5 ms = 240 samples
    p.fatnessPct = 100.0f;
    const auto out = run (p, impulse (1000, 1.0f), sr);
    REQUIRE_THAT (out.l[240], WithinAbs (std::sqrt (0.5), 0.005));
    REQUIRE_THAT (out.l[360], WithinAbs (std::sqrt (0.5), 0.005));

    p.fatnessPct = 0.0f;
    const auto single = run (p, impulse (1000, 1.0f), sr);
    REQUIRE_THAT (single.l[240], WithinAbs (1.0, 0.005));
    REQUIRE (std::abs (single.l[360]) < 0.005f);
}

TEST_CASE ("Chorus: spatial offsets the right LFO, volume and high-pass shape the wet signal", "[inserts][chorus]")
{
    const double sr = 48000.0;
    auto p = wetSingle();
    p.depthPct = 80.0f;
    p.spatialPct = 0.0f;
    const auto in = sine (48000, 500.0, sr, 0.5);
    const auto mono = run (p, in, sr);
    REQUIRE (mono.l == mono.r);
    p.spatialPct = 100.0f;
    const auto wide = run (p, in, sr);
    REQUIRE (correlation (wide.l, wide.r, 4800, 48000) < 0.99);

    // Volume trims the wet path in dB.
    p = wetSingle();
    p.depthPct = 0.0f;
    p.volumeDb = -12.0f;
    const auto quiet = run (p, impulse (1000, 1.0f), sr);
    REQUIRE_THAT (quiet.l[240], WithinRel (dbToGain (-12.0), 0.01));

    // The wet high-pass at 100 % (2 kHz) removes a bass fundamental.
    p = wetSingle();
    p.hpPct = 100.0f;
    const auto bass = run (p, sine (48000, 55.0, sr, 0.5), sr);
    REQUIRE (rms (bass.l, 24000, 48000) < 0.5 / std::sqrt (2.0) * dbToGain (-60.0));
}

TEST_CASE ("Chorus: knob laws cover the documented ranges", "[inserts][chorus]")
{
    REQUIRE_THAT (fx::Chorus::baseDelaySeconds (0.0), WithinAbs (0.005, 1e-12));
    REQUIRE_THAT (fx::Chorus::baseDelaySeconds (100.0), WithinAbs (0.025, 1e-12));
    REQUIRE_THAT (fx::Chorus::rateHz (0.0), WithinRel (0.05, 1e-9));
    REQUIRE_THAT (fx::Chorus::rateHz (100.0), WithinRel (8.0, 1e-9));
    REQUIRE_THAT (fx::Chorus::highPassHz (0.0), WithinRel (10.0, 1e-9));
    REQUIRE_THAT (fx::Chorus::highPassHz (100.0), WithinRel (2000.0, 1e-9));
    REQUIRE_THAT (fx::Chorus::sweepSeconds (100.0, 0.0), WithinAbs (0.010, 1e-12));
    for (double r = 0.0; r <= 100.0; r += 5.0)
        REQUIRE (kPi * fx::Chorus::rateHz (r) * fx::Chorus::sweepSeconds (100.0, r) <= fx::Chorus::kMaxPitchDeviation + 1e-12);
}
