// Remaining controls: knob smoothing, the waveform switch, amplifier gain law and noise,
// Env Attack, the pulse-width trim, ladder trims and the meters.
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-12)); }
}

TEST_CASE ("knobs are smoothed with a 3 ms lag; envelopes are not", "[voice][controls]")
{
    auto p = quietParams();
    p.cutoff = 20.0;
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    skip (v, 0.05);
    const double before = v.debugTaps().nominalCutoffHz;
    p.cutoff = 80.0;
    v.setParams (p);
    Trace t;
    run (v, t, 0.1);
    const double after = t.taps.back().nominalCutoffHz;
    CHECK (before == Approx (dsp::laws::cutoffNominalHz (0.2)).epsilon (1e-9));
    CHECK (after == Approx (dsp::laws::cutoffNominalHz (0.8)).epsilon (1e-6));
    // The knob value moves as a one-pole: 63 % of the travel after 3 ms, and no jump.
    const double knobAt3ms = 0.2 + 0.6 * (1.0 - std::exp (-1.0));
    CHECK (t.taps[t.at (0.003) - 1].nominalCutoffHz == Approx (dsp::laws::cutoffNominalHz (knobAt3ms)).epsilon (0.01));
    for (std::size_t i = 1; i < t.size(); ++i)
        REQUIRE (t.taps[i].nominalCutoffHz - t.taps[i - 1].nominalCutoffHz < 0.02 * (after - before));

    // The first block after prepare or reset jumps straight to the knob positions.
    start (v, p, 48000.0, 1);
    float s;
    v.process (&s, 1);
    CHECK (v.debugTaps().nominalCutoffHz == Approx (after).epsilon (1e-9));

    // The envelope is not smoothed: full height within a quarter of a millisecond.
    v.noteOn (36, false, false, false);
    Trace e;
    run (v, e, 0.001);
    CHECK (e.taps[e.at (0.00025)].envelopeVolts > 9.9);

    // Resonance is smoothed too (a jump in loop gain would ring).
    p.resonance = 0.0;
    start (v, p, 48000.0, 1);
    skip (v, 0.01);
    p.resonance = 100.0;
    v.setParams (p);
    Trace r;
    run (v, r, 0.05);
    CHECK (r.taps.front().resonance < 0.02);
    CHECK (r.taps.back().resonance == Approx (1.0).epsilon (1e-6));
}

TEST_CASE ("resonance knob: linear rotation with the loading bow", "[voice][controls]")
{
    const auto r = [] (double percent)
    {
        auto p = quietParams();
        p.resonance = percent;
        dsp::Voice v;
        start (v, p, 48000.0, 1);
        skip (v, 0.001);
        return v.debugTaps().resonance;
    };
    CHECK (r (0.0) == 0.0);
    CHECK (r (100.0) == Approx (1.0));
    // Sags most at mid rotation, by about 20 % (02 F24).
    CHECK (r (50.0) == Approx (0.5 / (1.0 + 0.25 * 50.0 / 51.0)).epsilon (1e-9));
    CHECK (r (50.0) / 0.5 > 0.78);
    CHECK (r (50.0) / 0.5 < 0.85);
    CHECK (r (25.0) < 0.25);
    CHECK (r (75.0) < 0.75);
}

TEST_CASE ("waveform switch crossfades without a thump and lets the square shaper idle on saw", "[voice][controls]")
{
    auto p = quietParams();
    p.modOn = true;
    p.modAmpSustain = 100.0;
    p.cutoff = 0.0; p.envMod = 0.0; p.resonance = 0.0;        // a dull tone: any step would stand out
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    v.noteOn (36, false, false, false);
    skip (v, 1.0);
    CHECK_FALSE (v.debugOscillator().getParams().squareInUse);
    Trace t;
    run (v, t, 0.1);
    double steady = 0.0;
    for (std::size_t i = 1; i < t.size(); ++i) steady = std::max (steady, static_cast<double> (std::abs (t.audio[i] - t.audio[i - 1])));

    p.wave = 1;
    v.setParams (p);
    Trace sw;
    run (v, sw, 0.3);
    CHECK (v.debugOscillator().getParams().squareInUse);
    double worst = 0.0;
    for (std::size_t i = 1; i < sw.size(); ++i) worst = std::max (worst, static_cast<double> (std::abs (sw.audio[i] - sw.audio[i - 1])));
    // No sample-to-sample jump beyond what the waveforms themselves do, and no DC lurch.
    CHECK (worst < 3.0 * steady);
    double mean = 0.0;
    for (std::size_t i = sw.at (0.01); i < sw.at (0.11); ++i) mean += static_cast<double> (sw.audio[i]);
    mean /= static_cast<double> (sw.at (0.11) - sw.at (0.01));
    CHECK (std::abs (mean) < 0.2 * rms (sw.audio, sw.at (0.01), sw.at (0.11)));

    p.wave = 0;
    v.setParams (p);
    skip (v, 0.01);
    CHECK_FALSE (v.debugOscillator().getParams().squareInUse);

    // The pulse-width trim reaches the oscillator as its saw top level.
    p.calPw = 100.0;
    v.setParams (p);
    skip (v, 0.1);
    CHECK (v.debugOscillator().getParams().sawTopVolts == Approx (10.3).epsilon (1e-6));
}

TEST_CASE ("amplifier: gain is linear in the control current over 40 dB (04 V2)", "[voice][controls][amp]")
{
    const auto fundamental = [] (double sustainPercent, double& gain)
    {
        auto p = quietParams();
        p.modOn = true;
        p.modAmpSustain = sustainPercent;
        p.modAmpDecay = 20.0;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (45, false, false, false);
        skip (v, 2.5);
        Trace t;
        run (v, t, 1.0);
        gain = t.taps.back().vcaGain;
        return std::abs (hannBin (t.audio, 0, t.size(), 110.0, 48000.0));
    };
    double g1 = 0.0, g2 = 0.0, g3 = 0.0;
    const double full = fundamental (100.0, g1);
    const double tenth = fundamental (100.0 * (0.1 + 0.118) / 1.118, g2);
    const double hundredth = fundamental (100.0 * (0.01 + 0.118) / 1.118, g3);
    CHECK (g1 == Approx (1.0).margin (1e-6));
    CHECK (g2 == Approx (0.1).margin (1e-6));
    CHECK (g3 == Approx (0.01).margin (1e-6));
    std::printf ("[vca] level at gain 0.1: %.3f dB, at gain 0.01: %.3f dB (re gain 1)\n", db (tenth / full), db (hundredth / full));
    CHECK (db (tenth / full) == Approx (-20.0).margin (0.1));
    CHECK (db (hundredth / full) == Approx (-40.0).margin (0.1));
}

TEST_CASE ("amplifier noise is off by default, gated by the amplifier, and deterministic", "[voice][controls][amp]")
{
    const auto renderNote = [] (double noise)
    {
        dsp::VoiceParams p = quietParams();
        p.calVcaNoise = noise;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        std::vector<float> out (48000);
        v.process (out.data(), 12000);              // silence first
        v.noteOn (36, false, false, false);
        v.process (out.data() + 12000, 3000);
        v.noteOff();
        v.process (out.data() + 15000, 33000);
        return out;
    };
    const auto clean = renderNote (0.0), noisy = renderNote (100.0), again = renderNote (100.0);
    CHECK (std::memcmp (noisy.data(), again.data(), noisy.size() * sizeof (float)) == 0);
    // Nothing while the amplifier is shut, before the note and after its release.
    CHECK (peakAbs (noisy, 0, 12000) < 1e-6);
    // During the note the difference from the clean render is the noise.
    std::vector<float> diff (clean.size());
    for (std::size_t i = 0; i < clean.size(); ++i) diff[i] = noisy[i] - clean[i];
    const double noiseDb = db (rms (diff, 12600, 14800));
    std::printf ("[vca] noise at 100 %%: %.1f dBFS rms during a plain note\n", noiseDb);
    CHECK (noiseDb > -70.0);
    CHECK (noiseDb < -40.0);
    CHECK (peakAbs (diff, 0, 12000) == 0.0);
    CHECK (peakAbs (diff, 24000) < 1e-5);
}

TEST_CASE ("env attack slows the envelope's rise; ladder trims reach the filter", "[voice][controls]")
{
    auto p = quietParams();
    p.envAttack = 100.0;
    dsp::Voice v;
    start (v, p, 96000.0, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.1);
    const std::size_t peak = firstIndex (t, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.envelopeVolts >= 10.0; });
    CHECK (t.seconds (peak) == Approx (0.030 * std::log (5.0)).epsilon (0.02));

    p = quietParams();
    p.calRb = 28.0;
    p.calKmax = 19.4;
    start (v, p, 48000.0, 4);
    skip (v, 0.01);
    CHECK (v.debugFilter().getTrims().rbOhms == Approx (28.0e3));
    CHECK (v.debugFilter().getTrims().kMax == Approx (19.4));
    // A change arrives smoothly, on the slow trim grid, and settles.
    p.calRb = 13.6;
    v.setParams (p);
    skip (v, 0.002);
    const double midway = v.debugFilter().getTrims().rbOhms;
    CHECK (midway < 28.0e3);
    CHECK (midway > 13.6e3);
    skip (v, 0.2);
    CHECK (v.debugFilter().getTrims().rbOhms == Approx (13.6e3));
}

TEST_CASE ("meters report the control path", "[voice][controls]")
{
    auto p = quietParams();
    p.accent = 100.0; p.resonance = 100.0;
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    dsp::VoiceMeters m = v.meters();
    CHECK_FALSE (m.gate);
    CHECK (m.envelope == 0.0f);
    CHECK (m.accentCharge == 0.0f);
    CHECK (m.amplitude == 0.0f);

    v.noteOn (45, true, false, false);
    skip (v, 0.03);
    m = v.meters();
    CHECK (m.gate);
    CHECK (m.accent);
    CHECK_FALSE (m.slide);
    CHECK (m.envelope == Approx (std::exp (-0.03 / 0.08)).margin (0.01));
    CHECK (m.accentCharge > 0.7f);
    CHECK (m.amplitude > 2.0f);
    CHECK (m.pitchHz == Approx (110.0f).epsilon (1e-5));
    CHECK (m.cutoffHz > 1000.0f);

    v.noteOn (57, false, true, false);
    skip (v, 0.001);
    CHECK (v.meters().slide);
    CHECK_FALSE (v.meters().accent);
    v.noteOff();
    skip (v, 0.001);
    CHECK_FALSE (v.meters().gate);
}
