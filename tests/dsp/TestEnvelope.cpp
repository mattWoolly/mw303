// Main envelope, Env Mod and the cutoff control law.
// "03 #n" is a row of the acceptance-target table in docs/research/03, "02 Fn" a row of
// the VCF table in docs/research/02, "04 En" a test of docs/research/04 section 6.
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
constexpr double kFs = 96000.0;

/** Seconds from the trigger until the envelope first falls below fraction * peak. */
double timeToFraction (const Trace& t, double fraction)
{
    std::size_t peak = 0;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (t.taps[i].envelopeVolts > t.taps[peak].envelopeVolts) peak = i;
    for (std::size_t i = peak; i < t.size(); ++i)
        if (t.taps[i].envelopeVolts < fraction * dsp::laws::kMegPeakVolts)
            return t.seconds (i);
    return -1.0;
}

Trace noteTrace (const dsp::VoiceParams& p, bool accent, double seconds, double fs = kFs)
{
    dsp::Voice v;
    start (v, p, fs, 1);
    Trace t;
    v.noteOn (36, accent, false, false);
    run (v, t, 0.06);
    v.noteOff();
    run (v, t, seconds - 0.06);
    return t;
}
}

TEST_CASE ("envelope: decay range and attack (03 #1, #2, #4)", "[voice][envelope]")
{
    auto p = quietParams();

    p.decay = 0.0;
    const Trace shortest = noteTrace (p, false, 0.5);
    const double t10min = timeToFraction (shortest, 0.1);
    std::printf ("[envelope] decay min: 10 %% at %.1f ms (target 171 to 200)\n", t10min * 1e3);
    CHECK (t10min == Approx (0.080 * std::log (10.0)).margin (0.001));
    CHECK (t10min > 0.171 * 0.85);
    CHECK (t10min < 0.200 * 1.15);

    p.decay = 100.0;
    const Trace longest = noteTrace (p, false, 3.5);
    const double t10max = timeToFraction (longest, 0.1);
    std::printf ("[envelope] decay max: 10 %% at %.2f s (target 2.4 to 2.5)\n", t10max);
    CHECK (t10max > 2.4 * 0.8);
    CHECK (t10max < 2.5 * 1.2);
    CHECK (t10max == Approx (1.080 * std::log (10.0)).margin (0.005));

    // Attack: under 0.5 ms to 90 %, and the peak is 10 V on every trigger.
    const std::size_t i90 = firstIndex (shortest, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.envelopeVolts >= 9.0; });
    std::printf ("[envelope] attack: 90 %% after %.3f ms\n", shortest.seconds (i90) * 1e3);
    CHECK (shortest.seconds (i90) < 0.0005);
    double peak = 0.0;
    for (const auto& d : shortest.taps) peak = std::max (peak, d.envelopeVolts);
    CHECK (peak == Approx (10.0).margin (1e-12));
}

TEST_CASE ("envelope: accent forces the short decay whatever the Decay knob says (03 #3)", "[voice][envelope]")
{
    for (double decay : { 0.0, 40.0, 100.0 })
    {
        auto p = quietParams();
        p.decay = decay;
        const Trace t = noteTrace (p, true, 0.5);
        // Fit the time constant between 20 and 200 ms.
        const double v1 = t.taps[t.at (0.020)].envelopeVolts, v2 = t.taps[t.at (0.200)].envelopeVolts;
        const double tau = 0.180 / std::log (v1 / v2);
        INFO ("decay knob " << decay);
        CHECK (tau == Approx (0.080).epsilon (0.001));
        CHECK (tau >= 0.072);
        CHECK (tau <= 0.087);
        const double t10 = timeToFraction (t, 0.1);
        CHECK (t10 > 0.170);
        CHECK (t10 < 0.200);
    }

    // The latch holds through a rest and through the note's own gate-off: still the
    // short decay 150 ms later, long after the gate closed.
    auto p = quietParams();
    p.decay = 100.0;
    const Trace t = noteTrace (p, true, 0.3);
    CHECK (t.taps.back().accentLatched);
    CHECK (t.taps[t.at (0.25)].envelopeVolts == Approx (10.0 * std::exp (-0.25 / 0.080)).epsilon (0.02));

    // acc_decay lengthens the accented decay only.
    p.accDecay = 100.0;
    const Trace slow = noteTrace (p, true, 1.0);
    const double tau = 0.5 / std::log (slow.taps[slow.at (0.2)].envelopeVolts / slow.taps[slow.at (0.7)].envelopeVolts);
    CHECK (tau == Approx (1.3).epsilon (0.01));
}

TEST_CASE ("envelope: exact exponential at every rate (04 E1)", "[voice][envelope]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        dsp::MainEnvelope env;
        env.prepare (fs);
        env.setTimes (dsp::laws::kMegAttackSeconds, 0.25, 0.08);
        env.trigger();
        int peakIndex = -1;
        double worst = 0.0;
        for (int n = 0; n < static_cast<int> (0.25 * fs); ++n)
        {
            const double v = env.tick (false);
            if (peakIndex < 0 && ! env.isAttacking()) peakIndex = n;
            if (peakIndex >= 0)
            {
                const double expected = 10.0 * std::exp (-static_cast<double> (n - peakIndex) / (0.25 * fs));
                worst = std::max (worst, std::abs (v / expected - 1.0));
            }
        }
        INFO ("fs " << fs);
        CHECK (worst < 1.0e-9);
        // The peak arrives 1.6 attack time constants after the trigger, to within a sample.
        CHECK (static_cast<double> (peakIndex) / fs == Approx (0.000161).margin (1.5 / fs));
    }
}

TEST_CASE ("envelope: the same times at 44.1, 48, 96 and 192 kHz", "[voice][envelope][rates]")
{
    double reference = 0.0;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto p = quietParams();
        p.decay = 50.0;
        const Trace t = noteTrace (p, false, 1.0, fs);
        const double t10 = timeToFraction (t, 0.1);
        if (reference == 0.0) reference = t10;
        INFO ("fs " << fs);
        CHECK (t10 == Approx (reference).margin (2.0 / 44100.0));
        CHECK (t10 == Approx (dsp::laws::decaySeconds (0.5) * std::log (10.0)).margin (0.0005));
    }
}

TEST_CASE ("env mod: depth, ratio, offset and resting cutoff (03 #6 to #9)", "[voice][envelope][cutoff]")
{
    struct Sweep { double peak, rest, nominal; };
    const auto measure = [] (double cutoff, double envMod, double cvScale)
    {
        auto p = quietParams();
        p.cutoff = cutoff;
        p.envMod = envMod;
        p.decay = 0.0;
        p.calCvScale = cvScale;
        const Trace t = noteTrace (p, false, 2.5);
        Sweep s { 0.0, t.taps.back().cutoffHz, t.taps.back().nominalCutoffHz };
        for (const auto& d : t.taps) s.peak = std::max (s.peak, d.cutoffHz);
        return s;
    };

    const Sweep lo = measure (50.0, 0.0, 0.88), hi = measure (50.0, 100.0, 0.88);
    const double sweepLo = std::log2 (lo.peak / lo.rest), sweepHi = std::log2 (hi.peak / hi.rest);
    const double belowLo = std::log2 (lo.nominal / lo.rest) / sweepLo, belowHi = std::log2 (hi.nominal / hi.rest) / sweepHi;
    const double restShift = std::log2 (hi.rest / lo.rest);
    std::printf ("[envmod] sweep %.3f oct at min, %.3f oct at max, ratio %.2f; below nominal %.3f / %.3f; rest shift %.3f oct\n",
                 sweepLo, sweepHi, sweepHi / sweepLo, belowLo, belowHi, restShift);

    CHECK (sweepLo >= 0.74);
    CHECK (sweepLo <= 0.90);
    CHECK (sweepHi >= 4.5);
    CHECK (sweepHi <= 5.4);
    CHECK (sweepHi / sweepLo == Approx (6.0).epsilon (0.05));
    CHECK (belowLo == Approx (0.33).margin (0.04));
    CHECK (belowHi == Approx (0.33).margin (0.04));
    CHECK (restShift <= -1.25);
    CHECK (restShift >= -1.5);

    // The schematic's own depth with the CV scale at 1: 0.90 and 5.41 octaves, less the
    // wiper loading (3.6 % at minimum, none at maximum), times the rise of the depth with
    // the cutoff knob (laws::depthRise: 1 at the bottom of the knob, 1.063 at the top,
    // 1.021 at the 50 % used here). The rise is the measured unit's: 02 section 3.8 item 6
    // and 03 section 4.2 both quote its total sweep as 4.51 octaves at low cutoff and 5.06
    // at high with Env Mod at maximum (0.74 and 0.86 at minimum); the schematic alone has
    // one depth at every cutoff setting.
    const Sweep lo1 = measure (50.0, 0.0, 1.0), hi1 = measure (50.0, 100.0, 1.0);
    const double rise = dsp::laws::depthRise (hi1.nominal);
    CHECK (rise == Approx (1.0213).margin (0.001));
    CHECK (std::log2 (lo1.peak / lo1.rest) / rise == Approx (0.90 * 0.964).epsilon (0.01));
    CHECK (std::log2 (hi1.peak / hi1.rest) / rise == Approx (5.40).epsilon (0.01));

    // At the bottom of the knob the rise is 1 and the schematic's numbers come out as they are.
    const Sweep lo0 = measure (0.0, 0.0, 1.0), hi0 = measure (0.0, 100.0, 1.0);
    CHECK (std::log2 (lo0.peak / lo0.rest) == Approx (0.90 * 0.964).epsilon (0.01));
    CHECK (std::log2 (hi0.peak / hi0.rest) == Approx (5.40).epsilon (0.01));
    CHECK (dsp::laws::depthRise (dsp::laws::kCutoffMinHz) == 1.0);
    CHECK (dsp::laws::depthRise (dsp::laws::kCutoffMaxHz) == Approx (dsp::laws::kDepthRiseTop));
    CHECK (dsp::laws::kDepthRiseTop == Approx (1.0627).margin (0.001));

    // With the CV scale at its default the ends of the knob bracket the measured unit's
    // 4.51 to 5.06 octaves (F16: 4.5 to 5.1). The top reading is a little compressed by the
    // ceiling, which bends the peak from 25.0 to 24.6 kHz.
    const Sweep botMax = measure (0.0, 100.0, 0.88), topMax = measure (100.0, 100.0, 0.88);
    const double sweepBot = std::log2 (botMax.peak / botMax.rest), sweepTop = std::log2 (topMax.peak / topMax.rest);
    std::printf ("[envmod] env mod max: sweep %.3f oct at the bottom of the cutoff knob, %.3f oct at the top (measured unit 4.51, 5.06)\n",
                 sweepBot, sweepTop);
    CHECK (sweepBot >= 4.5);
    CHECK (sweepBot <= 4.9);
    CHECK (sweepTop >= 4.9);
    CHECK (sweepTop <= 5.1);
    CHECK (sweepTop > sweepBot);
}

TEST_CASE ("cutoff: knob range, calibration point and corner cases (02 F14 to F18)", "[voice][cutoff]")
{
    const auto rest = [] (double cutoff, double envMod)
    {
        auto p = quietParams();
        p.cutoff = cutoff;
        p.envMod = envMod;
        p.decay = 0.0;
        return noteTrace (p, false, 2.5);
    };
    const auto peakOf = [] (const Trace& t)
    {
        double pk = 0.0;
        for (const auto& d : t.taps) pk = std::max (pk, d.cutoffHz);
        return pk;
    };

    // F14: knob range with the envelope's contribution removed.
    const Trace minMin = rest (0.0, 0.0), maxMin = rest (100.0, 0.0), minMax = rest (0.0, 100.0), maxMax = rest (100.0, 100.0);
    CHECK (minMin.taps.back().nominalCutoffHz == Approx (314.0).epsilon (1e-9));
    CHECK (maxMin.taps.back().nominalCutoffHz == Approx (2394.0).epsilon (1e-9));

    // F15: knob at 50 %, Env Mod at minimum: the filter rings with a 2 ms period at rest.
    const Trace mid = rest (50.0, 0.0);
    const double period = 1.0 / mid.taps.back().cutoffHz;
    std::printf ("[cutoff] knob 50 %%, env mod min: rest %.1f Hz, ring period %.2f ms (target 2 +- 0.5)\n",
                 mid.taps.back().cutoffHz, period * 1e3);
    CHECK (period == Approx (0.002).margin (0.0005));

    // F16: the four corners, peak at note-on and resting, against the measured unit's table.
    struct Corner { const Trace* t; double peakHz, restHz; const char* name; };
    const Corner corners[] = { { &minMin, 450.0, 270.0, "min/min" }, { &maxMin, 3550.0, 1950.0, "max/min" },
                               { &minMax, 2850.0, 125.0, "min/max" }, { &maxMax, 23900.0, 717.0, "max/max" } };
    for (const Corner& c : corners)
    {
        const double pk = peakOf (*c.t), rs = c.t->taps.back().cutoffHz;
        std::printf ("[cutoff] %s: peak %.0f Hz (table %.0f, %+.2f oct), rest %.0f Hz (table %.0f, %+.2f oct)\n",
                     c.name, pk, c.peakHz, std::log2 (pk / c.peakHz), rs, c.restHz, std::log2 (rs / c.restHz));
        INFO (c.name);
        CHECK (std::abs (std::log2 (pk / c.peakHz)) < 0.2);
        CHECK (std::abs (std::log2 (rs / c.restHz)) < 0.3);
    }

    // F17: the ceiling, at the documented condition: a 44.1 kHz host, a plain note, Cutoff
    // and Env Mod at maximum, no accent. The resonant frequency reaches past 24 kHz at 2x
    // and above (24.6 kHz: the measured unit's 23.9 kHz plus 0.04 octave, from the depth
    // rise) and stays under the 30 kHz saturation, with the solver stable throughout. At
    // 1x the filter's 0.45 fs clamp holds it at 19.8 kHz: a resonant frequency above the
    // Nyquist of the rate the ladder runs at has no meaning, so F17 needs oversampling.
    for (int os : { 1, 2, 4 })
    {
        auto p = quietParams();
        p.cutoff = 100.0; p.envMod = 100.0; p.resonance = 0.0;
        dsp::Voice v;
        start (v, p, 44100.0, os);
        Trace t;
        v.noteOn (36, false, false, false);
        run (v, t, 0.1);
        const double pk = peakOf (t);
        std::printf ("[cutoff] 44.1 kHz x%d, cutoff and env mod at maximum, plain note: peak %.0f Hz\n", os, pk);
        INFO ("x" << os);
        if (os == 1)
        {
            CHECK (pk == Approx (dsp::DiodeLadder::kMaxFresOverFs * 44100.0).epsilon (1e-3));
        }
        else
        {
            CHECK (pk > 24000.0);
            CHECK (pk < 30000.0);
        }
        CHECK (allFinite (t.audio));
        CHECK (v.debugFilter().stats().stateResets == 0);
        CHECK (v.debugFilter().stats().nonConverged == 0);
    }
    // A full accent on top runs into the saturation.
    {
        auto p = quietParams();
        p.cutoff = 100.0; p.envMod = 100.0; p.accent = 100.0; p.resonance = 0.0;
        dsp::Voice v;
        start (v, p, 44100.0, 4);
        Trace t;
        v.noteOn (36, true, false, false);
        run (v, t, 0.1);
        const double pk = peakOf (t);
        std::printf ("[cutoff] everything at maximum, accented: %.0f Hz\n", pk);
        CHECK (pk > 28000.0);
        CHECK (pk <= 30000.0);
        CHECK (allFinite (t.audio));
        CHECK (v.debugFilter().stats().stateResets == 0);
    }

    // F18: no keyboard tracking.
    {
        dsp::Voice v;
        start (v, quietParams(), kFs, 1);
        Trace t;
        v.noteOn (24, false, false, false);
        run (v, t, 0.02);
        const double low = t.taps.back().cutoffHz;
        v.noteOff();
        skip (v, 3.0);
        Trace u;
        v.noteOn (72, false, false, false);
        run (v, u, 0.02);
        CHECK (u.taps.back().cutoffHz == Approx (low).epsilon (1e-12));
    }

    // TM3: 500 mV is one octave; the CV scale trim scales envelope depth, not the knob.
    {
        auto p = quietParams();
        p.calTm3 = 500.0;
        dsp::Voice v;
        start (v, p, kFs, 1);
        skip (v, 0.01);
        CHECK (v.debugTaps().nominalCutoffHz == Approx (2.0 * dsp::laws::cutoffNominalHz (0.35)).epsilon (1e-9));
        p.calTm3 = -250.0;
        start (v, p, kFs, 1);
        skip (v, 0.01);
        CHECK (v.debugTaps().nominalCutoffHz == Approx (dsp::laws::cutoffNominalHz (0.35) / std::sqrt (2.0)).epsilon (1e-9));
    }
}

TEST_CASE ("knob laws put the stock values where the spec says", "[voice][laws]")
{
    using namespace dsp::laws;
    CHECK (slideSeconds (0.5) == Approx (0.022));
    CHECK (slideSeconds (0.0) == Approx (0.022 / 6.0));
    CHECK (slideSeconds (1.0) == Approx (0.132));
    CHECK (accentDecaySeconds (0.0) == Approx (0.080));
    CHECK (accentDecaySeconds (1.0) == Approx (1.3));
    CHECK (decaySeconds (0.0) == Approx (0.080));
    CHECK (decaySeconds (1.0) == Approx (1.080));
    CHECK (decaySeconds (0.5) == Approx (0.080 + 1.0 / (std::sqrt (32.0) + 1.0)));
    CHECK (envAttackSeconds (0.0) == Approx (0.0001));
    CHECK (cutoffNominalHz (0.0) == Approx (314.0));
    CHECK (cutoffNominalHz (1.0) == Approx (2394.0));
    CHECK (cutoffNominalHz (0.5) / cutoffNominalHz (1.0) > 0.20);      // 02 section 3.8: centre at 0.20 to 0.29 of maximum
    CHECK (cutoffNominalHz (0.5) / cutoffNominalHz (1.0) < 0.29);
    CHECK (cutoffNominalHz (1.0, true) == Approx (5000.0));
    CHECK (envModOctavesPerVolt (1.0) == Approx (0.54));
    CHECK (envModOctavesPerVolt (0.0) == Approx (0.54 / 6.0 * 220.0 / (220.0 + 50.0 / 6.0)));
    CHECK (envModOctavesPerVolt (0.0, true) == Approx (0.0).margin (1e-12));
    CHECK (envModOctavesPerVolt (1.0, true) / envModOctavesPerVolt (1.0) == Approx (3.13).epsilon (0.01));
    CHECK (overdriveGain (0.0) == Approx (1.0));
    CHECK (overdriveGain (1.0) == Approx (66.6));
    CHECK (oscillatorHz (pitchVolts (45.0), 1.0, 0.0) == Approx (110.0));
    CHECK (pitchVolts (24.0) == Approx (1.0));
    CHECK (pitchVolts (60.0) == Approx (4.0));
    CHECK (gateTurnOnDelay (10.0, 0.0054) == Approx (0.0054).epsilon (1e-6));
    CHECK (gateTurnOnDelay (0.059, 0.0054) == Approx (0.0019).margin (0.0001));
    CHECK (gateTurnOnDelay (0.186, 0.0054) == Approx (0.0045).margin (0.0002));
    CHECK (gateTurnOnDelay (0.024, 0.0054) == Approx (0.0004).margin (0.0001));
    CHECK (gateTurnOnDelay (10.0, 0.0) == 0.0);
    CHECK (cutoffCeiling (5000.0) == 5000.0);
    CHECK (cutoffCeiling (1.0e6) == Approx (30000.0));
    CHECK (vibratoHalfPeriodSeconds (0.5) == Approx (0.050));

    // The parameter table's defaults and the struct's defaults are the same thing.
    const dsp::VoiceParams fromTable = dsp::voiceParamsFrom (defaultParamValues());
    const dsp::VoiceParams plain;
    CHECK (fromTable.wave == plain.wave);
    CHECK (fromTable.cutoff == plain.cutoff);
    CHECK (fromTable.resonance == plain.resonance);
    CHECK (fromTable.envMod == plain.envMod);
    CHECK (fromTable.decay == plain.decay);
    CHECK (fromTable.accent == plain.accent);
    CHECK (fromTable.slideTime == plain.slideTime);
    CHECK (fromTable.sweepCouple == plain.sweepCouple);
    CHECK (fromTable.sweepAmt == plain.sweepAmt);
    CHECK (fromTable.accVolCouple == plain.accVolCouple);
    CHECK (fromTable.accVol == plain.accVol);
    CHECK (fromTable.vibSpeed == plain.vibSpeed);
    CHECK (fromTable.vibDepth == plain.vibDepth);
    CHECK (fromTable.calC21 == Approx (plain.calC21));
    CHECK (fromTable.calAccEnv == plain.calAccEnv);
    CHECK (fromTable.calAccAmp == Approx (plain.calAccAmp));
    CHECK (fromTable.calC13 == plain.calC13);
    CHECK (fromTable.calTm5 == plain.calTm5);
    CHECK (fromTable.calVcaClicks == plain.calVcaClicks);
    CHECK (fromTable.calRb == Approx (plain.calRb));
    CHECK (fromTable.calKmax == Approx (plain.calKmax));
    CHECK (fromTable.calCvScale == Approx (plain.calCvScale));
    CHECK (fromTable.calGateDelay == Approx (plain.calGateDelay));
    CHECK (fromTable.calDrift == plain.calDrift);
    CHECK (fromTable.calCpuJitter == plain.calCpuJitter);
    CHECK (fromTable.modOn == plain.modOn);
    CHECK (fromTable.modAmpDecay == plain.modAmpDecay);
    CHECK (fromTable.modSoftAttack == plain.modSoftAttack);
    CHECK (fromTable.modSweepSpeed == plain.modSweepSpeed);
    CHECK (fromTable.modSubOct == plain.modSubOct);

    ParamValues values = defaultParamValues();
    values[kWave] = 1.0f; values[kTuneCoarse] = -5.0f; values[kModMuffler] = 2.0f; values[kModOn] = 1.0f;
    values[kCalTm3] = -372.0f; values[kModSubOct] = 1.0f;
    const dsp::VoiceParams edited = dsp::voiceParamsFrom (values);
    CHECK (edited.wave == 1);
    CHECK (edited.tuneCoarse == -5);
    CHECK (edited.modMuffler == 2);
    CHECK (edited.modOn);
    CHECK (edited.calTm3 == -372.0);
    CHECK (edited.modSubOct == 1);
}
