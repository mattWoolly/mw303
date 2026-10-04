// The one-knob dynamics stage: more compression as the knob rises, levels pulled towards
// the nominal level, sane attack and release, and a steady tone left steady.
#include "AllocGuard.h"
#include "DistTestUtil.h"
#include "acid/fx/Dynamics.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::fx;
using namespace disttest;

namespace
{
constexpr double kFs = 48000.0;

/** Gain in dB once a steady sine of this amplitude has settled. */
double settledGainDb (double amount, double amplitude, double fs = kFs, double freq = 220.0)
{
    Dynamics dyn;
    dyn.prepare (fs);
    dyn.setAmount (amount);
    dyn.reset();
    auto x = sine (freq, amplitude, fs, static_cast<int> (fs));
    dyn.process (x.data(), nullptr, static_cast<int> (x.size()));
    return dyn.gainDb();
}

/** Per-sample gain in dB for an amplitude step at `stepAt` seconds. */
std::vector<double> gainTrace (double amount, double ampBefore, double ampAfter, double stepAt, double seconds,
                               double fs = kFs)
{
    Dynamics dyn;
    dyn.prepare (fs);
    dyn.setAmount (amount);
    dyn.reset();
    const int n = static_cast<int> (seconds * fs);
    std::vector<double> trace (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double amp = i < static_cast<int> (stepAt * fs) ? ampBefore : ampAfter;
        dyn.processGain (std::abs (amp * std::sin (kTwoPi * 440.0 * i / fs)));
        trace[static_cast<std::size_t> (i)] = dyn.gainDb();
    }
    return trace;
}
}

TEST_CASE ("dynamics: zero amount is bit-transparent", "[dist][dynamics]")
{
    Dynamics dyn;
    dyn.prepare (kFs);
    dyn.setAmount (0.0);
    dyn.reset();
    CHECK_FALSE (dyn.isActive());

    const auto in = acidLine (kFs, 1.0);
    auto l = in, r = in;
    dyn.process (l.data(), r.data(), static_cast<int> (l.size()));
    CHECK (l == in);
    CHECK (r == in);
    CHECK (dyn.gainDb() == 0.0);
}

TEST_CASE ("dynamics: gain reduction grows with the knob", "[dist][dynamics]")
{
    double previous = 0.0;
    for (const double amount : { 10.0, 25.0, 50.0, 75.0, 100.0 })
    {
        const double gain = settledGainDb (amount, 1.0);   // a full-scale tone, 12 dB over nominal
        INFO ("amount " << amount << ": " << gain << " dB");
        CHECK (gain < previous - 0.5);
        previous = gain;
    }
    CHECK (previous < -9.0);
    CHECK (previous > -13.0);   // never more reduction than the excess over the nominal level
}

TEST_CASE ("dynamics: levels are pulled towards the nominal level", "[dist][dynamics]")
{
    // The nominal level itself passes at unity at every setting: that is the make-up rule.
    for (const double amount : { 20.0, 60.0, 100.0 })
    {
        INFO ("amount " << amount);
        CHECK (std::abs (settledGainDb (amount, 0.25)) < 0.3);
    }

    // The spread between a quiet, a nominal and a loud tone shrinks as the knob rises.
    double previousSpread = 30.0;
    for (const double amount : { 25.0, 50.0, 75.0, 100.0 })
    {
        const double quiet = db (0.25 / 8.0) + settledGainDb (amount, 0.25 / 8.0);   // 18 dB under
        const double loud = db (1.0) + settledGainDb (amount, 1.0);                   // 12 dB over
        const double spread = loud - quiet;
        INFO ("amount " << amount << ": spread " << spread << " dB");
        CHECK (spread < previousSpread - 2.0);
        previousSpread = spread;
    }
    CHECK (previousSpread < 6.0);   // 30 dB in, under 6 dB out at full

    // Quiet material is lifted, loud material is lowered.
    CHECK (settledGainDb (100.0, 0.25 / 8.0) > 10.0);
    CHECK (settledGainDb (100.0, 1.0) < -9.0);
}

TEST_CASE ("dynamics: attack and release are sane", "[dist][dynamics]")
{
    for (const double fs : { 44100.0, 48000.0, 192000.0 })
    {
        const auto at = [fs] (double seconds) { return static_cast<std::size_t> (seconds * fs); };
        INFO ("fs " << fs);

        // Quiet to loud at 1 s.
        {
            const auto trace = gainTrace (100.0, 0.05, 1.0, 1.0, 2.0, fs);
            const double before = trace[at (0.999)];
            const double settled = trace.back();
            const double depth = before - settled;
            CHECK (depth > 15.0);
            // Not instantaneous: a tenth of a millisecond in, most of the change is still to come.
            CHECK (trace[at (1.0001)] > settled + 0.9 * depth);
            // Half way in 1 to 5 ms, done in 25 ms.
            CHECK (trace[at (1.001)] > settled + 0.5 * depth);
            CHECK (trace[at (1.005)] < settled + 0.5 * depth);
            CHECK (std::abs (trace[at (1.025)] - settled) < 1.0);
            // It never overshoots below the settled gain.
            double lowest = 0.0;
            for (std::size_t i = at (1.0); i < trace.size(); ++i) lowest = std::min (lowest, trace[i]);
            CHECK (lowest > settled - 0.1);
        }

        // Loud to quiet at 1 s.
        {
            const auto trace = gainTrace (100.0, 1.0, 0.05, 1.0, 3.0, fs);
            const double before = trace[at (0.999)];
            const double settled = trace.back();
            const double depth = settled - before;
            CHECK (depth > 15.0);
            // The hold keeps the gain where it was for the first 20 ms.
            CHECK (std::abs (trace[at (1.02)] - before) < 0.5);
            // Half way somewhere between 60 and 300 ms, done in 0.8 s.
            CHECK (trace[at (1.06)] < before + 0.5 * depth);
            CHECK (trace[at (1.3)] > before + 0.5 * depth);
            CHECK (std::abs (trace[at (1.8)] - settled) < 1.0);
        }
    }
}

TEST_CASE ("dynamics: a steady tone is not pumped or distorted", "[dist][dynamics]")
{
    for (const double freq : { 41.2, 55.0, 110.0, 880.0 })
    {
        Dynamics dyn;
        dyn.prepare (kFs);
        dyn.setAmount (100.0);
        dyn.reset();

        const int n = static_cast<int> (2.0 * kFs);
        const auto in = sine (freq, 0.6, kFs, n);
        std::vector<double> out (static_cast<std::size_t> (n));
        double lo = 1.0e9, hi = -1.0e9;
        for (int i = 0; i < n; ++i)
        {
            const double g = dyn.processGain (std::abs (static_cast<double> (in[static_cast<std::size_t> (i)])));
            out[static_cast<std::size_t> (i)] = g * static_cast<double> (in[static_cast<std::size_t> (i)]);
            if (i > n / 2)
            {
                lo = std::min (lo, dyn.gainDb());
                hi = std::max (hi, dyn.gainDb());
            }
        }

        INFO (freq << " Hz: gain ripple " << (hi - lo) << " dB");
        CHECK (hi - lo < 0.05);

        // No harmonics added: the gain is constant, so the tone stays a sine.
        const std::size_t from = static_cast<std::size_t> (n / 2);
        const double fundamental = tone (out, freq, kFs, from);
        for (int h = 2; h <= 5; ++h)
            CHECK (db (tone (out, freq * h, kFs, from) / fundamental) < -70.0);
    }
}

TEST_CASE ("dynamics: silence is not lifted and a note after a rest starts at unity", "[dist][dynamics]")
{
    Dynamics dyn;
    dyn.prepare (kFs);
    dyn.setAmount (100.0);
    dyn.reset();

    // A quiet tail gets the full lift...
    for (int i = 0; i < 48000; ++i)
        dyn.processGain (0.01);
    CHECK (dyn.gainDb() > 12.0);

    // ...true silence lets go of it.
    std::vector<float> silence (48000, 0.0f);
    dyn.process (silence.data(), nullptr, 48000);
    CHECK (std::abs (dyn.gainDb()) < 0.2);
    CHECK (peak (silence) == 0.0);

    // The first sample of the next note is therefore not boosted.
    CHECK (dyn.processGain (0.25) < 1.03);
}

TEST_CASE ("dynamics: channels are linked", "[dist][dynamics]")
{
    Dynamics dyn;
    dyn.prepare (kFs);
    dyn.setAmount (80.0);
    dyn.reset();

    auto l = sine (220.0, 1.0, kFs, 48000);
    auto r = sine (220.0, 0.1, kFs, 48000);
    dyn.process (l.data(), r.data(), 48000);

    // Both channels got the same gain, so their ratio is unchanged.
    const double ratio = rms (l, 24000) / rms (r, 24000);
    CHECK (std::abs (db (ratio) - 20.0) < 0.01);
    CHECK (dyn.gainDb() < -6.0);
}

TEST_CASE ("dynamics: survives bad input and never allocates", "[dist][dynamics]")
{
    Dynamics dyn;
    dyn.prepare (kFs);
    dyn.setAmount (std::nan (""));
    CHECK_FALSE (dyn.isActive());
    dyn.setAmount (250.0);

    auto x = sine (220.0, 0.5, kFs, 4800);
    x[100] = std::nanf ("");
    x[200] = std::numeric_limits<float>::infinity();

    acid::test::ScopedAllocGuard guard;
    dyn.process (x.data(), nullptr, static_cast<int> (x.size()));
    dyn.setAmount (30.0);
    dyn.reset();
    REQUIRE (guard.count() == 0);

    CHECK (std::isfinite (dyn.gainDb()));
    for (std::size_t i = 300; i < x.size(); ++i)
        REQUIRE (std::isfinite (x[i]));
}
