// The accent sweep network: single-accent shapes, accumulation over runs of accents, the
// knob threshold and the discharge time constants (docs/research/03 sections 5.3, 5.4 and
// acceptance targets #10 to #17; docs/research/04 test E2).
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
/** The network driven as the research document's simulation drove it: envelope time
    constant 72 ms, 1.19 octaves per volt, no CV scale trim. */
struct ReferenceRig
{
    dsp::MainEnvelope env;
    dsp::AccentSweep sweep;
    double fs;
    double accent;
    bool latch = false;

    ReferenceRig (double sampleRate, double accentKnob, double tap, double c13 = 1.0e-6)
        : fs (sampleRate), accent (accentKnob)
    {
        env.prepare (fs);
        env.setTimes (dsp::laws::kMegAttackSeconds, 0.25, 0.072);
        sweep.prepare (fs);
        sweep.configure (accentKnob, tap, c13);
    }

    void noteOn (bool accented) { latch = accented; env.trigger(); }

    /** One tick; returns the sweep's cutoff offset in octaves. */
    double tick()
    {
        const double meg = env.tick (latch);
        return dsp::laws::kKAcc * sweep.tick (latch ? meg * dsp::laws::accentWiperGain (accent) : 0.0, latch);
    }

    /** Runs one step and returns the largest offset in it. */
    double stepPeak (double seconds, double* peakTime = nullptr)
    {
        double pk = -1.0;
        const int n = static_cast<int> (seconds * fs + 0.5);
        for (int i = 0; i < n; ++i)
        {
            const double o = tick();
            if (o > pk) { pk = o; if (peakTime != nullptr) *peakTime = static_cast<double> (i + 1) / fs; }
        }
        return pk;
    }
};

bool near (double measured, double table, double relative = 0.04, double absolute = 0.02)
{
    return std::abs (measured - table) <= relative * table + absolute;
}
}

TEST_CASE ("accent sweep: single-accent shapes at three resonance settings (03 section 5.3)", "[voice][accent]")
{
    const double times[] = { 0.0002, 0.005, 0.010, 0.020, 0.030, 0.040, 0.060, 0.080, 0.120, 0.200, 0.300, 0.400 };
    const double table[3][12] = {
        { 3.04, 2.84, 2.62, 2.20, 1.81, 1.44, 0.81, 0.67, 0.52, 0.30, 0.16, 0.08 },     // r = 0
        { 1.60, 1.61, 1.60, 1.53, 1.40, 1.24, 0.88, 0.75, 0.54, 0.29, 0.13, 0.06 },     // r = 0.5
        { 0.01, 0.34, 0.61, 0.98, 1.16, 1.22, 1.08, 0.89, 0.60, 0.27, 0.10, 0.04 } };   // r = 1
    const double taps[3] = { 0.0, 0.5, 1.0 };

    for (double fs : { 48000.0, 192000.0 })
    {
        for (int k = 0; k < 3; ++k)
        {
            ReferenceRig rig (fs, 1.0, taps[k]);
            rig.noteOn (true);
            std::vector<double> curve;
            for (int i = 0; i < static_cast<int> (0.45 * fs); ++i)
                curve.push_back (rig.tick());
            std::printf ("[accent] fs %.0f r %.1f:", fs, taps[k]);
            for (int j = 0; j < 12; ++j)
            {
                const double v = curve[static_cast<std::size_t> (times[j] * fs + 0.5)];
                std::printf (" %.2f", v);
                INFO ("fs " << fs << " r " << taps[k] << " t " << times[j] << " measured " << v << " table " << table[k][j]);
                // The first point of r = 0 sits on the 0.1 ms attack; the others are well clear of it.
                CHECK (near (v, table[k][j], j == 0 ? 0.08 : 0.05, 0.03));
            }
            std::printf ("\n");
        }
    }

    // Peak and time of peak (03 #10, #11, #12 at the schematic's own scale).
    struct Row { double tap, peak, time; };
    for (const Row& row : { Row { 0.0, 3.05, 0.0 }, Row { 0.25, 2.31, 0.0 }, Row { 0.5, 1.61, 0.005 },
                            Row { 0.75, 1.26, 0.027 }, Row { 1.0, 1.22, 0.040 } })
    {
        ReferenceRig rig (96000.0, 1.0, row.tap);
        rig.noteOn (true);
        double when = 0.0;
        const double pk = rig.stepPeak (0.4, &when);
        std::printf ("[accent] r %.2f: peak %.3f oct at %.1f ms (table %.2f at %.0f ms)\n", row.tap, pk, when * 1e3, row.peak, row.time * 1e3);
        INFO ("r " << row.tap);
        CHECK (near (pk, row.peak, 0.03, 0.02));
        CHECK (when == Approx (row.time).margin (row.tap >= 0.75 ? 0.005 : 0.004));
    }

    // r = 1: half of the peak at 10 ms on the way up and at 119 ms on the way down.
    {
        ReferenceRig rig (96000.0, 1.0, 1.0);
        rig.noteOn (true);
        std::vector<double> c;
        for (int i = 0; i < 28800; ++i) c.push_back (rig.tick());
        const double pk = *std::max_element (c.begin(), c.end());
        std::size_t up = 0, down = 0;
        while (c[up] < 0.5 * pk) ++up;
        down = static_cast<std::size_t> (0.04 * 96000.0);
        while (c[down] > 0.5 * pk) ++down;
        std::printf ("[accent] r 1: half peak at %.1f ms rising, %.1f ms falling (table 10 and 119)\n", up / 96.0, down / 96.0);
        CHECK (up / 96000.0 == Approx (0.010).epsilon (0.2));
        CHECK (down / 96000.0 == Approx (0.119).epsilon (0.2));
    }
}

TEST_CASE ("accent sweep: depth against the Accent knob and its threshold (03 section 5.3, #17)", "[voice][accent]")
{
    const double knobs[] = { 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 };
    const double table[3][7] = { { 0.0, 0.05, 0.17, 0.34, 0.56, 0.84, 1.22 },      // r = 1
                                 { 0.0, 0.15, 0.35, 0.58, 0.84, 1.17, 1.61 },      // r = 0.5
                                 { 0.0, 0.28, 0.64, 1.05, 1.55, 2.17, 3.05 } };    // r = 0
    const double taps[3] = { 1.0, 0.5, 0.0 };
    for (int k = 0; k < 3; ++k)
    {
        std::printf ("[accent] depth against knob, r %.1f:", taps[k]);
        for (int j = 0; j < 7; ++j)
        {
            ReferenceRig rig (96000.0, knobs[j], taps[k]);
            rig.noteOn (true);
            const double pk = std::max (rig.stepPeak (0.4), 0.0);
            std::printf (" %.2f", pk);
            INFO ("r " << taps[k] << " knob " << knobs[j] << " measured " << pk << " table " << table[k][j]);
            CHECK (std::abs (pk - table[k][j]) <= 0.05 + 0.03 * table[k][j]);     // the document's own bound: 0.05 octave
        }
        std::printf ("\n");
    }

    // Threshold: the smallest knob position that moves the filter by a hundredth of an
    // octave (the diode's knee is soft, so "at all" needs a number).
    double threshold = 1.0;
    for (double knob = 0.30; knob <= 0.60; knob += 0.0025)
    {
        ReferenceRig rig (96000.0, knob, 0.0);
        rig.noteOn (true);
        if (rig.stepPeak (0.2) > 0.01) { threshold = knob; break; }
    }
    std::printf ("[accent] filter accent starts at %.1f %% of the Accent knob (target 40 to 48)\n", threshold * 100.0);
    CHECK (threshold >= 0.40);
    CHECK (threshold <= 0.48);
}

TEST_CASE ("accent sweep: consecutive accents climb (03 section 5.4, #13 to #16, 04 E2)", "[voice][accent]")
{
    // Six accented 16ths then four plain ones at 125 BPM.
    const double table[4][10] = {
        { 1.22, 1.51, 1.56, 1.57, 1.57, 1.57, 0.73, 0.23, 0.07, 0.02 },     // r = 1
        { 1.26, 1.60, 1.68, 1.70, 1.71, 1.71, 0.74, 0.26, 0.09, 0.03 },     // r = 0.75
        { 1.61, 2.03, 2.14, 2.17, 2.18, 2.18, 0.74, 0.29, 0.11, 0.04 },     // r = 0.5
        { 3.05, 3.36, 3.46, 3.49, 3.50, 3.50, 0.77, 0.35, 0.16, 0.07 } };   // r = 0
    const double taps[4] = { 1.0, 0.75, 0.5, 0.0 };
    for (int k = 0; k < 4; ++k)
    {
        ReferenceRig rig (96000.0, 1.0, taps[k]);
        std::printf ("[accent] 125 BPM, r %.2f:", taps[k]);
        for (int s = 0; s < 10; ++s)
        {
            rig.noteOn (s < 6);
            const double pk = rig.stepPeak (0.120);
            std::printf (" %.2f", pk);
            INFO ("r " << taps[k] << " step " << s + 1 << " measured " << pk << " table " << table[k][s]);
            CHECK (near (pk, table[k][s], 0.03, 0.025));
        }
        std::printf ("\n");
    }

    // Tempo dependence at r = 1: first, second, third and sixth peaks.
    struct Tempo { double bpm, p1, p2, p3, p6; };
    for (const Tempo& t : { Tempo { 100, 1.22, 1.43, 1.46, 1.46 }, Tempo { 120, 1.22, 1.49, 1.54, 1.55 },
                            Tempo { 130, 1.22, 1.52, 1.58, 1.59 }, Tempo { 140, 1.22, 1.55, 1.62, 1.64 },
                            Tempo { 150, 1.22, 1.57, 1.66, 1.68 }, Tempo { 170, 1.22, 1.62, 1.73, 1.76 } })
    {
        ReferenceRig rig (96000.0, 1.0, 1.0);
        double pk[6];
        for (int s = 0; s < 6; ++s)
        {
            rig.noteOn (true);
            pk[s] = rig.stepPeak (stepSeconds (t.bpm));
        }
        std::printf ("[accent] %.0f BPM, r 1: %.2f %.2f %.2f .. %.2f (table %.2f %.2f %.2f .. %.2f)\n",
                     t.bpm, pk[0], pk[1], pk[2], pk[5], t.p1, t.p2, t.p3, t.p6);
        INFO ("bpm " << t.bpm);
        CHECK (near (pk[0], t.p1, 0.02, 0.02));
        CHECK (near (pk[1], t.p2, 0.02, 0.02));
        CHECK (near (pk[2], t.p3, 0.02, 0.02));
        CHECK (near (pk[5], t.p6, 0.02, 0.02));
        // Targets #13 and #14: the ratios.
        if (t.bpm == 140.0)
        {
            CHECK (pk[1] / pk[0] == Approx (1.27).margin (0.05));
            CHECK (pk[2] / pk[0] == Approx (1.33).margin (0.05));
            CHECK (pk[3] / pk[0] == Approx (1.34).margin (0.05));
        }
    }

    // Accent on every other step: accumulation needs adjacent accents.
    {
        ReferenceRig rig (96000.0, 1.0, 1.0);
        double pk[3];
        for (int s = 0; s < 6; ++s)
        {
            rig.noteOn (s % 2 == 0);
            const double p = rig.stepPeak (0.120);
            if (s % 2 == 0) pk[s / 2] = p;
        }
        std::printf ("[accent] A N A N A N at 125 BPM: %.2f %.2f %.2f (table 1.22 1.30 1.31)\n", pk[0], pk[1], pk[2]);
        CHECK (near (pk[0], 1.22, 0.02, 0.02));
        CHECK (near (pk[1], 1.30, 0.02, 0.02));
        CHECK (near (pk[2], 1.31, 0.02, 0.02));
    }

    // Accent knob at 0.75, r = 1: 0.44, 0.55, 0.58, 0.58.
    {
        ReferenceRig rig (96000.0, 0.75, 1.0);
        const double expected[4] = { 0.44, 0.55, 0.58, 0.58 };
        for (int s = 0; s < 4; ++s)
        {
            rig.noteOn (true);
            CHECK (std::abs (rig.stepPeak (0.120) - expected[s]) < 0.05);
        }
    }
}

TEST_CASE ("accent sweep: discharge time constants and exactness (03 #16, 04 E1)", "[voice][accent]")
{
    for (double fs : { 44100.0, 192000.0 })
    {
        for (double tap : { 0.0, 0.5, 1.0 })
        {
            dsp::AccentSweep sweep;
            sweep.prepare (fs);
            sweep.configure (1.0, tap, 1.0e-6);
            sweep.setCapacitorVolts (1.0);
            const double tau = 1.0e-6 * ((1.0 - tap) * 50.0e3 + dsp::laws::kR72);
            double worst = 0.0;
            for (int n = 1; n <= static_cast<int> (0.2 * fs); ++n)
            {
                const double w = sweep.tick (0.0, false);
                const double expected = std::exp (-static_cast<double> (n) / (tau * fs));
                worst = std::max (worst, std::abs (sweep.capacitorVolts() / expected - 1.0));
                // The filter sees the capacitor through the divider of the track and R72.
                REQUIRE (w == Approx (sweep.capacitorVolts() * dsp::laws::kR72 / (dsp::laws::kR72 + (1.0 - tap) * 50.0e3)).epsilon (1e-12));
            }
            INFO ("fs " << fs << " tap " << tap);
            CHECK (worst < 1.0e-9);
        }
    }
    // 102 ms at full resonance, 152 ms at none.
    CHECK (1.0e-6 * dsp::laws::kR72 == Approx (0.102).epsilon (0.01));
    CHECK (1.0e-6 * (50.0e3 + dsp::laws::kR72) == Approx (0.152).epsilon (0.01));

    // Diode on: the diode's drop moves with its current, so there is no closed form.
    // The charge must not depend on the tick rate: against a 1.536 MHz run of the same
    // network, constant drive, at 10, 50 and 200 ms.
    const auto charge = [] (double fs, double seconds, double tap)
    {
        dsp::AccentSweep sweep;
        sweep.prepare (fs);
        sweep.configure (1.0, tap, 1.0e-6);
        for (int n = 0; n < static_cast<int> (seconds * fs + 0.5); ++n)
            sweep.tick (9.0, true);
        return sweep.capacitorVolts();
    };
    for (double tap : { 0.0, 1.0 })
        for (double seconds : { 0.010, 0.050, 0.200 })
        {
            const double reference = charge (1536000.0, seconds, tap);
            INFO ("tap " << tap << " t " << seconds);
            CHECK (charge (44100.0, seconds, tap) == Approx (reference).epsilon (2.0e-3));
            CHECK (charge (192000.0, seconds, tap) == Approx (reference).epsilon (5.0e-4));
        }
    // Charging time constant at r = 1: 97 k in parallel with R72 on 1 uF, 50 ms. After
    // five of them the capacitor sits at the divider's share of the drive less the diode.
    {
        const double settled = charge (96000.0, 0.5, 1.0);
        const double share = dsp::laws::kR72 / (97.0e3 + dsp::laws::kR72);
        CHECK (settled > share * (9.0 - 3.3 - 0.50));
        CHECK (settled < share * (9.0 - 3.3 - 0.35));
        const double oneTau = charge (96000.0, 0.0497, 1.0);
        CHECK (oneTau / settled == Approx (1.0 - std::exp (-1.0)).margin (0.02));
    }
}

//==============================================================================
// The same behaviour through the whole voice at its default calibration (accent decay
// 80 ms, CV scale 0.88).

namespace
{
std::vector<double> voiceStepPeaks (double bpm, double resonance, double accent, int accents, int plains, double fs = 96000.0)
{
    auto p = quietParams();
    p.resonance = resonance;
    p.accent = accent;
    dsp::Voice v;
    start (v, p, fs, 1);
    std::vector<SeqStep> pattern;
    for (int i = 0; i < accents + plains; ++i)
        pattern.push_back (SeqStep { 36, i < accents, false, false, false });
    const double step = stepSeconds (bpm);
    const Trace t = renderTraced (v, makeEvents (pattern, bpm, fs, 1), static_cast<long long> ((accents + plains) * step * fs));
    std::vector<double> peaks;
    for (int i = 0; i < accents + plains; ++i)
    {
        double pk = 0.0;
        for (std::size_t n = t.at (i * step); n < std::min (t.at ((i + 1) * step), t.size()); ++n)
            pk = std::max (pk, t.taps[n].accentOctaves);
        peaks.push_back (pk);
    }
    return peaks;
}
}

TEST_CASE ("voice: accent sweep depth, timing and accumulation at the default calibration (03 #10, #12 to #15)", "[voice][accent]")
{
    // Single accent, Accent and Resonance at maximum.
    {
        auto p = quietParams();
        p.resonance = 100.0;
        p.accent = 100.0;
        dsp::Voice v;
        start (v, p, 96000.0, 1);
        Trace t;
        v.noteOn (36, true, false, false);
        run (v, t, 0.3);
        std::size_t at = 0;
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t.taps[i].accentOctaves > t.taps[at].accentOctaves) at = i;
        std::printf ("[voice accent] res max: peak %.3f oct at %.1f ms (target 1.0 to 1.25 at 40)\n", t.taps[at].accentOctaves, t.seconds (at) * 1e3);
        CHECK (t.taps[at].accentOctaves > 1.0 * 0.75);
        CHECK (t.taps[at].accentOctaves < 1.25 * 1.25);
        CHECK (t.seconds (at) == Approx (0.040).margin (0.008));
        // Meter: the capacitor's charge.
        CHECK (v.meters().accentCharge > 0.0f);
    }
    // Resonance at minimum: a spike at the start, under an octave by 60 ms.
    {
        auto p = quietParams();
        p.resonance = 0.0;
        p.accent = 100.0;
        dsp::Voice v;
        start (v, p, 96000.0, 1);
        Trace t;
        v.noteOn (36, true, false, false);
        run (v, t, 0.3);
        double pk = 0.0;
        std::size_t at = 0;
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t.taps[i].accentOctaves > pk) { pk = t.taps[i].accentOctaves; at = i; }
        std::printf ("[voice accent] res min: peak %.3f oct at %.2f ms, %.2f oct at 60 ms (target 2.6 to 3.05 at 0, under 1 by 60 ms)\n",
                     pk, t.seconds (at) * 1e3, t.taps[t.at (0.060)].accentOctaves);
        CHECK (pk > 2.6 * 0.75);
        CHECK (pk < 3.05 * 1.25);
        CHECK (t.seconds (at) < 0.001);
        CHECK (t.taps[t.at (0.060)].accentOctaves < 1.0);
    }

    // Runs of accents: ratios of the peaks, then the afterglow on the plain notes.
    const auto p125 = voiceStepPeaks (125.0, 100.0, 100.0, 6, 4);
    std::printf ("[voice accent] 125 BPM:");
    for (double v : p125) std::printf (" %.3f", v);
    std::printf ("  ratios %.3f %.3f %.3f (target 1.24 1.28 1.29)\n", p125[1] / p125[0], p125[2] / p125[0], p125[3] / p125[0]);
    CHECK (p125[1] / p125[0] == Approx (1.24).margin (0.05));
    CHECK (p125[2] / p125[0] == Approx (1.28).margin (0.05));
    CHECK (p125[3] / p125[0] == Approx (1.29).margin (0.05));
    CHECK (p125[1] > p125[0]);
    CHECK (p125[2] > p125[1]);
    CHECK (p125[5] == Approx (p125[4]).epsilon (0.01));        // flat after the fourth
    CHECK (p125[6] == Approx (0.73).epsilon (0.30));
    CHECK (p125[7] == Approx (0.23).epsilon (0.30));
    CHECK (p125[8] == Approx (0.07).epsilon (0.30));

    const auto p140 = voiceStepPeaks (140.0, 100.0, 100.0, 6, 0);
    std::printf ("[voice accent] 140 BPM ratios %.3f %.3f %.3f (target 1.27 1.33 1.34)\n", p140[1] / p140[0], p140[2] / p140[0], p140[3] / p140[0]);
    CHECK (p140[1] / p140[0] == Approx (1.27).margin (0.05));
    CHECK (p140[2] / p140[0] == Approx (1.33).margin (0.05));
    CHECK (p140[3] / p140[0] == Approx (1.34).margin (0.05));

    // Below the threshold the filter does not move; above it, it does.
    CHECK (voiceStepPeaks (125.0, 100.0, 35.0, 3, 0)[2] < 0.005);
    CHECK (voiceStepPeaks (125.0, 100.0, 50.0, 3, 0)[2] > 0.02);

    // reset() is a panic: it discharges the capacitor. Notes do not.
    {
        auto p = quietParams();
        p.resonance = 100.0; p.accent = 100.0;
        dsp::Voice v;
        start (v, p, 48000.0, 1);
        v.noteOn (36, true, false, false);
        skip (v, 0.06);
        v.noteOff();
        skip (v, 0.06);
        const double charged = v.debugTaps().accentCapVolts;
        CHECK (charged > 0.3);
        v.noteOn (36, false, false, false);
        skip (v, 0.001);
        CHECK (v.debugTaps().accentCapVolts == Approx (charged).epsilon (0.02));
        v.reset();
        CHECK (v.debugTaps().accentCapVolts == 0.0);
        CHECK (v.meters().accentCharge == 0.0f);
    }
}

TEST_CASE ("voice: sweep couple, accent calibration trims and C13", "[voice][accent]")
{
    const auto peak = [] (const dsp::VoiceParams& p)
    {
        dsp::Voice v;
        start (v, p, 48000.0, 1);
        Trace t;
        v.noteOn (36, true, false, false);
        run (v, t, 0.4);
        double pk = 0.0;
        std::size_t at = 0;
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t.taps[i].accentOctaves > pk) { pk = t.taps[i].accentOctaves; at = i; }
        return std::pair<double, double> (pk, t.seconds (at));
    };

    auto p = quietParams();
    p.accent = 100.0;
    p.resonance = 100.0;
    const auto coupled = peak (p);

    // Uncoupled: Sweep Amount sets the tap, resonance no longer does.
    p.sweepCouple = false;
    p.sweepAmt = 100.0;
    p.resonance = 0.0;
    const auto smooth = peak (p);
    CHECK (smooth.first == Approx (coupled.first).epsilon (1e-9));
    p.sweepAmt = 0.0;
    p.resonance = 100.0;
    const auto spiky = peak (p);
    CHECK (spiky.first > 2.0 * coupled.first);
    CHECK (spiky.second < 0.001);

    // cal_accenv scales the depth; 0 removes the filter accent.
    p = quietParams();
    p.accent = 100.0; p.resonance = 100.0;
    p.calAccEnv = 1951.0 * 1.5;
    CHECK (peak (p).first == Approx (1.5 * coupled.first).epsilon (1e-6));
    p.calAccEnv = 0.0;
    CHECK (peak (p).first == 0.0);

    // cal_cv_scale scales it too (and the envelope depth with it).
    p.calAccEnv = 1951.0;
    p.calCvScale = 1.0;
    CHECK (peak (p).first == Approx (coupled.first / 0.88).epsilon (1e-6));

    // A larger C13 is slower and lower on a single accent.
    p = quietParams();
    p.accent = 100.0; p.resonance = 100.0;
    p.calC13 = 2.2;
    const auto big = peak (p);
    CHECK (big.second > coupled.second + 0.005);
    CHECK (big.first < coupled.first);
}
