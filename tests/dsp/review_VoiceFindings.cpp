// Review tests (independent reviewer): voice-level defects that the module's own tests did
// not catch. Each one failed on the code as first delivered; they now pin the fixes.
//   - aliasing: the oscillator is band-limited at 24 kHz ahead of the ladder's input pair
//     (Voice.h), which takes C6 from -74 dBc to -115 dBc at 48 kHz x4. The Overdrive mod
//     is reported here, not held to -90: with the input pair clipping hard the ladder
//     itself makes harmonics above the oversampled Nyquist, and only more oversampling
//     helps (the engine's tier choice).
//   - CPU under automation: the knob laws are split into groups re-derived only when their
//     control moves, with the expensive ones on a control-rate grid. The timing test is
//     hidden by default, as every timing test is: run it with the [benchmark] tag.
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace acid;
using namespace acid::test;

namespace
{
double midiHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

/** Largest component that is a harmonic of f0 folded about a multiple of the oversampled
    rate into 20 Hz..20 kHz (and not on a harmonic), in dB below the fundamental. */
double worstFoldedDbc (const std::vector<float>& x, double fs, int os, double f0)
{
    const double fsOver = fs * os;
    const double fund = std::abs (hannBin (x, 0, x.size(), f0, fs));
    double worst = 0.0;
    for (int k = 2; k * f0 < 2.0 * fsOver; ++k)
        for (int m = 1; m <= 2; ++m)
        {
            const double hz = std::abs (k * f0 - m * fsOver);
            if (hz < 20.0 || hz > 20000.0) continue;
            const double h = hz / f0;
            if (std::abs (h - std::round (h)) * f0 < 10.0) continue;    // lands on a harmonic
            worst = std::max (worst, std::abs (hannBin (x, 0, x.size(), hz, fs)));
        }
    return 20.0 * std::log10 (worst / fund);
}
}

TEST_CASE ("review: aliasing at the voice output stays at the oscillator's 04 O2 level", "[voice][review]")
{
    // The oscillator alone measures -118 dBc at 1046.5 Hz (osc tests). Through the voice the
    // ladder's input pair (a tanh at the oversampled rate) folds the oscillator's ultrasonic
    // harmonics back into the audio band.
    for (int note : { 72, 84, 96 })
    {
        auto p = quietParams();
        p.cutoff = 100.0; p.envMod = 0.0; p.resonance = 0.0;
        p.modOn = true; p.modAmpSustain = 100.0; p.modExtRange = true;      // a held, open, static tone
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (note, false, false, false);
        skip (v, 0.5);
        Trace t;
        run (v, t, 1.5);
        const double dbc = worstFoldedDbc (t.audio, 48000.0, 4, midiHz (note));
        std::printf ("[review] MIDI %d saw, 48 kHz x4: worst folded component %.1f dBc\n", note, dbc);
        INFO ("MIDI " << note << ": " << dbc << " dBc");
        CHECK (dbc < -90.0);
    }

    // At 2x the band limit still runs (96 kHz is above its 80 kHz floor) and C6 reads
    // -89.5 dBc, C5 -102 dBc: pinned at -85 so a regression shows.
    {
        auto p = quietParams();
        p.cutoff = 100.0; p.envMod = 0.0; p.resonance = 0.0;
        p.modOn = true; p.modAmpSustain = 100.0; p.modExtRange = true;
        dsp::Voice v;
        start (v, p, 48000.0, 2);
        v.noteOn (96, false, false, false);
        skip (v, 0.5);
        Trace t;
        run (v, t, 1.5);
        const double dbc = worstFoldedDbc (t.audio, 48000.0, 2, midiHz (96));
        std::printf ("[review] MIDI 96 saw, 48 kHz x2: worst folded component %.1f dBc\n", dbc);
        CHECK (dbc < -85.0);
    }

    // Overdrive (Mods) at 100 %: the input pair clips the band-limited saw hard, and the
    // ladder's own harmonics fold. Measured at 48 kHz x4: -58 dBc at C4, -48 at C5, -36 at
    // C6; at x8 it is the next tier's job. Reported, and pinned loosely.
    for (int note : { 60, 72 })
    {
        auto p = quietParams();
        p.cutoff = 100.0; p.envMod = 0.0; p.resonance = 0.0;
        p.modOn = true; p.modAmpSustain = 100.0; p.modExtRange = true; p.modOverdrive = 100.0;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (note, false, false, false);
        skip (v, 0.5);
        Trace t;
        run (v, t, 1.5);
        const double dbc = worstFoldedDbc (t.audio, 48000.0, 4, midiHz (note));
        std::printf ("[review] MIDI %d saw, Overdrive 100 %%, 48 kHz x4: worst folded component %.1f dBc (not held to -90: see the file comment)\n", note, dbc);
        INFO ("MIDI " << note << " overdrive: " << dbc << " dBc");
        CHECK (dbc < -40.0);
    }
}

TEST_CASE ("review: moving a knob does not make the voice much more expensive", "[voice][review][.benchmark]")
{
    // SPEC 6.1 asks for better than 15x real time at 48 kHz x4. With any smoothed control in
    // motion (cutoff automation, pitch bend, mod wheel) every coefficient is re-derived on
    // every base-rate sample, which costs about 60 % on top of the static case.
    const auto timeIt = [] (bool automate)
    {
        double best = 1.0e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            auto p = quietParams();
            dsp::Voice v;
            start (v, p, 48000.0, 4);
            std::vector<float> block (128);
            v.noteOn (36, false, false, false);
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 1500; ++b)          // 4 s
            {
                if (automate) { p.cutoff = 30.0 + 20.0 * std::sin (b * 0.05); v.setParams (p); }
                if (b % 60 == 0) v.noteOn (36 + (b / 60) % 12, false, false, false);
                v.process (block.data(), 128);
            }
            best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        return best;
    };
    const double still = timeIt (false), moving = timeIt (true);
    std::printf ("[review] 48 kHz x4: %.1f x real time with static knobs, %.1f x with the cutoff automated\n",
                 4.0 / still, 4.0 / moving);
    CHECK (moving < 1.25 * still);
}
