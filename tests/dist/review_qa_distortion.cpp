// QA review (sound), distortion block. Fails until fixed.
//
// 1. The Console clipper's amplifier-loop solver can lock into a two-cycle
// between its bracket ends and return an unconverged value. Heard as a broadband noise floor
// 70 to 90 dB under the note on Console voicings with Knee below 100 at high drive (factory
// voicing "Desk Red" at Drive 75 to 100 on the realtime Normal tier).
// 2. Tilt On / Tilt Amount are applied as an instant coefficient step (a click when a voicing
//    with a different tilt is picked during playback).
#include "DistTestUtil.h"
#include "acid/fx/Distortion.h"
#include "acid/fx/DistortionClippers.h"
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace acid::fx;
using namespace acid::fx::detail;

TEST_CASE ("review_qa: console loop solver converges from its warm start", "[dist][review_qa]")
{
    // Console, knee 0 (loop gain 40), preamp gain 178 (drive 100 on a voicing whose Preamp Max
    // is 100), a 400 Hz sine at the nominal level, at 192 kHz (48 kHz x4).
    const double fs = 192000.0, f0 = 400.0;
    ClipperControls ctl;
    ctl.family = ClipperFamily::Console;
    ctl.gain = 178.0;
    ctl.knee = 0.0;
    ClipperCoeffs k;
    k.update (ctl, fs);
    const ConsoleCoeffs& c = k.console;

    SolverState st;
    double worst = 0.0;
    for (int i = 0; i < static_cast<int> (fs / 4); ++i)
    {
        const double rhs = kNominalLevel * std::sin (2.0 * M_PI * f0 * i / fs) * c.inGain + c.offset;
        const double e = solveOffsetTanhLoop (c.loopC, c.loopK, c.pairB, c.pairTanh, rhs, st);
        const double residual = e + c.loopC * (std::tanh (c.loopK * e + c.pairB) - c.pairTanh) - rhs;
        worst = std::max (worst, std::abs (residual));
    }
    // Observed before the fix: 0.975 (the iterate alternates between lo and hi for all
    // 40 iterations because Newton lands exactly on the other bracket end, which the
    // inclusive in-bracket test accepts, and the bracket never shrinks).
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("review_qa: console clipper output is periodic on a periodic input", "[dist][review_qa]")
{
    // 192 kHz / 400 Hz = 480 samples per period exactly: once settled, every period of the
    // output must equal the one before it. Glitches at the rail transition break that.
    const double fs = 192000.0, f0 = 400.0;
    const int period = 480;
    ClipperControls ctl;
    ctl.family = ClipperFamily::Console;
    ctl.gain = 178.0;
    ctl.knee = 0.0;
    ClipperCoeffs k;
    k.update (ctl, fs);
    ClipperState s;
    s.reset();
    s.settle (k);
    const int n = static_cast<int> (fs);
    std::vector<double> b (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        b[static_cast<std::size_t> (i)] = kNominalLevel * std::sin (2.0 * M_PI * f0 * i / fs);
    processClipper (k, s, b.data(), n);

    double worst = 0.0, peak = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        worst = std::max (worst, std::abs (b[static_cast<std::size_t> (i)] - b[static_cast<std::size_t> (i - period)]));
        peak = std::max (peak, std::abs (b[static_cast<std::size_t> (i)]));
    }
    // Observed before the fix: worst 0.31 against a peak of 0.37 (single-sample glitches).
    // The transformer core's 5 Hz corner settles long before n / 2.
    CHECK (worst < 1.0e-3 * peak);
}

TEST_CASE ("review_qa: switching the distortion tilt mid-note does not step the output", "[dist][review_qa]")
{
    // Tilt On (and Tilt Amount) change tiltDirect and tiltMix in one sample, so a 100 Hz
    // note through the block drops by the tilt's bass cut in a single step: a click when a
    // voicing with a different tilt is picked during playback. Observed before the fix: a
    // step about 8 times the largest sample-to-sample change of the steady signal.
    const double fs = 48000.0;
    acid::fx::Distortion d;
    d.prepare (fs, 256);
    d.setOversampling (4);
    acid::fx::DistortionParams p;
    p.on = true;
    p.drive = 30.0f;
    p.tiltAmt = 12.0f;
    p.tiltOn = false;
    d.setParams (p);
    d.reset();

    const auto in = disttest::sine (100.0, 0.25, fs, static_cast<int> (fs));
    std::vector<float> out = in;
    const int half = static_cast<int> (fs / 2);
    for (int i = 0; i < static_cast<int> (fs); i += 256)
    {
        if (i == half - half % 256)
        {
            p.tiltOn = true;
            d.setParams (p);
        }
        d.process (out.data() + i, nullptr, std::min (256, static_cast<int> (fs) - i));
    }

    const int sw = half - half % 256;
    double steady = 0.0, around = 0.0;
    for (int i = sw - 9600; i < sw - 1; ++i)
        steady = std::max (steady, static_cast<double> (std::abs (out[static_cast<std::size_t> (i + 1)] - out[static_cast<std::size_t> (i)])));
    for (int i = sw - 8; i < sw + 480; ++i)
        around = std::max (around, static_cast<double> (std::abs (out[static_cast<std::size_t> (i + 1)] - out[static_cast<std::size_t> (i)])));
    INFO ("largest step around the switch " << around << ", steady " << steady);
    CHECK (around <= 2.0 * steady);
}
