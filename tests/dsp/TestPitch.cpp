// Pitch: the note map, tuning, octave width, slide, vibrato trill, bend and drift
// (docs/research/02 targets V1 to V3 and V12; docs/research/03 targets #5 and #26 to #28;
// docs/research/04 tests E3 and E4).
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
double settledHz (const dsp::VoiceParams& p, int note, double fs = 48000.0)
{
    dsp::Voice v;
    start (v, p, fs, 1);
    v.noteOn (note, false, false, false);
    skip (v, 0.01);
    return v.debugTaps().pitchHz;
}
}

TEST_CASE ("pitch map at tune centre (02 V1)", "[voice][pitch]")
{
    struct Row { int note; double hz; };
    for (const Row& row : { Row { 24, 32.70 }, Row { 36, 65.41 }, Row { 45, 110.00 }, Row { 48, 130.81 },
                            Row { 60, 261.63 }, Row { 72, 523.25 } })
    {
        INFO ("note " << row.note);
        CHECK (std::abs (cents (settledHz (quietParams(), row.note), row.hz)) < 1.0);
        CHECK (settledHz (quietParams(), row.note) == Approx (midiNoteToHz (row.note)).epsilon (1e-12));
    }
}

TEST_CASE ("pitch of the audio output is the pitch asked for, at every sample rate (02 V1)", "[voice][pitch][rates]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int os = fs < 50000.0 ? 4 : (fs < 100000.0 ? 2 : 1);
        for (int note : { 24, 45, 60 })
        {
            auto p = quietParams();
            p.modOn = true;
            p.modAmpSustain = 100.0;        // hold the note open for the measurement
            p.resonance = 20.0;
            dsp::Voice v;
            start (v, p, fs, os);
            v.noteOn (note, false, false, false);
            skip (v, 0.3);
            Trace t;
            run (v, t, 1.0);
            const double expected = midiNoteToHz (note);
            const double measured = fundamentalHz (t.audio, 0, t.size(), fs, expected);
            INFO ("fs " << fs << " note " << note << " measured " << measured);
            CHECK (std::abs (cents (measured, expected)) < 1.0);
        }
    }
}

TEST_CASE ("tuning knob, extended tuning, octave width and bend (02 V3)", "[voice][pitch]")
{
    auto p = quietParams();
    const double a2 = settledHz (p, 45);

    p.tune = 7.0;
    CHECK (cents (settledHz (p, 45), a2) == Approx (700.0).margin (0.01));
    p.tune = -7.0;
    CHECK (cents (settledHz (p, 45), a2) == Approx (-700.0).margin (0.01));
    p.tune = 12.0;
    CHECK (cents (settledHz (p, 45), a2) == Approx (1200.0).margin (0.01));
    p.tune = 99.0;      // clamped to the knob's range
    CHECK (cents (settledHz (p, 45), a2) == Approx (1200.0).margin (0.01));

    // Extended mode: coarse semitones and fine cents replace the knob.
    p = quietParams();
    p.tuneExt = true;
    p.tune = 5.0;
    p.tuneCoarse = -3;
    p.tuneFine = 25.0;
    CHECK (cents (settledHz (p, 45), a2) == Approx (-275.0).margin (0.01));

    // TM5: octaves per volt, pivot at A2. 1.01 stretches C4 (1.25 V above A2) by 15 cents.
    p = quietParams();
    p.calTm5 = 1.01;
    CHECK (settledHz (p, 45) == Approx (110.0).epsilon (1e-12));
    CHECK (cents (settledHz (p, 60), midiNoteToHz (60)) == Approx (15.0).margin (0.01));
    CHECK (cents (settledHz (p, 24), midiNoteToHz (24)) == Approx (-21.0).margin (0.01));

    // Bend, in semitones, after the 3 ms smoothing.
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 1);
    v.noteOn (45, false, false, false);
    v.setPitchBend (2.0);
    skip (v, 0.1);
    CHECK (cents (v.debugTaps().pitchHz, 110.0) == Approx (200.0).margin (0.01));
    v.setPitchBend (0.0);
    skip (v, 0.1);
    CHECK (cents (v.debugTaps().pitchHz, 110.0) == Approx (0.0).margin (0.01));
}

TEST_CASE ("tracking stays within the factory tolerance with drift on (02 V2)", "[voice][pitch]")
{
    dsp::VoiceParams p;         // default drift, 10 %
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    skip (v, 20.0);             // let the drift wander
    double worst = 0.0, sum = 0.0;
    int count = 0;
    for (int k = 0; k < 400; ++k)
    {
        v.noteOn (36, false, false, false);
        skip (v, 0.05);
        const double low = v.debugTaps().pitchHz;
        worst = std::max (worst, std::abs (v.debugTaps().driftCents));
        sum += v.debugTaps().driftCents * v.debugTaps().driftCents;
        ++count;
        v.noteOn (60, false, false, false);
        skip (v, 0.05);
        const double ratio = v.debugTaps().pitchHz / low;
        REQUIRE (ratio == Approx (4.0).epsilon (0.005));
    }
    std::printf ("[pitch] drift at the default 10 %%: %.2f cents rms, %.2f cents worst over 40 s\n", std::sqrt (sum / count), worst);
    CHECK (worst > 0.05);       // it is on
    CHECK (worst < 8.0);

    // Drift off: exactly none. Full: about 15 cents rms, never wild.
    p.calDrift = 0.0;
    start (v, p, 48000.0, 1);
    skip (v, 1.0);
    CHECK (v.debugTaps().driftCents == 0.0);
    p.calDrift = 100.0;
    start (v, p, 48000.0, 1);
    double sq = 0.0;
    int n = 0;
    for (int k = 0; k < 1200; ++k)
    {
        skip (v, 0.1);
        sq += v.debugTaps().driftCents * v.debugTaps().driftCents;
        ++n;
        REQUIRE (std::abs (v.debugTaps().driftCents) < 80.0);
    }
    const double rmsCents = std::sqrt (sq / n);
    std::printf ("[pitch] drift at 100 %%: %.1f cents rms over 120 s\n", rmsCents);
    CHECK (rmsCents > 5.0);
    CHECK (rmsCents < 30.0);
}

TEST_CASE ("slide: 22 ms lag in the pitch CV, the same for any interval (03 #26, #27, 04 E3)", "[voice][pitch][slide]")
{
    const auto slide = [] (int from, int to, double fs)
    {
        dsp::Voice v;
        start (v, quietParams(), fs, 1);
        v.noteOn (from, false, false, false);
        skip (v, 0.1);
        Trace t;
        v.noteOn (to, false, true, false);
        run (v, t, 0.2);
        std::vector<double> progress;
        const double a = dsp::laws::pitchVolts (from), b = dsp::laws::pitchVolts (to);
        for (const auto& d : t.taps) progress.push_back ((d.pitchVolts - a) / (b - a));
        return progress;
    };

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto octave = slide (36, 48, fs);
        const auto when = [&] (double level)
        {
            std::size_t i = 0;
            while (i < octave.size() && octave[i] < level) ++i;
            return static_cast<double> (i + 1) / fs;
        };
        if (fs == 48000.0)
            std::printf ("[slide] octave slide: 90 %% at %.1f ms, 95 %% at %.1f ms, 99 %% at %.1f ms (targets 51, 66, 101)\n",
                         when (0.90) * 1e3, when (0.95) * 1e3, when (0.99) * 1e3);
        INFO ("fs " << fs);
        CHECK (when (0.90) == Approx (0.0507).margin (0.001));
        CHECK (when (0.95) == Approx (0.0659).margin (0.001));
        CHECK (when (0.99) == Approx (0.1013).margin (0.001));

        // Exact exponential: 1 - exp (-n T / 22 ms) at 10, 50 and 200 ms (E1).
        for (double s : { 0.010, 0.050, 0.199 })
        {
            const auto n = static_cast<std::size_t> (s * fs);
            const double expected = 1.0 - std::exp (-static_cast<double> (n + 1) / (0.022 * fs));
            CHECK (std::abs (octave[n] / expected - 1.0) < 1.0e-9);
        }
    }

    // One semitone, two octaves, downward: the same curve.
    const auto semitone = slide (36, 37, 48000.0), twoOctaves = slide (36, 60, 48000.0), down = slide (60, 36, 48000.0);
    for (std::size_t i = 0; i < semitone.size(); i += 16)
    {
        REQUIRE (semitone[i] == Approx (twoOctaves[i]).margin (1e-9));
        REQUIRE (down[i] == Approx (twoOctaves[i]).margin (1e-9));
    }

    // Exponential in semitones means the frequency is not an exponential approach in Hz:
    // halfway in volts is the geometric mean.
    {
        dsp::Voice v;
        start (v, quietParams(), 48000.0, 1);
        v.noteOn (36, false, false, false);
        skip (v, 0.1);
        v.noteOn (48, false, true, false);
        Trace t;
        run (v, t, 0.1);
        const std::size_t half = firstIndex (t, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.pitchVolts >= 2.5; });
        CHECK (t.taps[half].pitchHz == Approx (std::sqrt (65.406 * 130.813)).epsilon (0.002));
    }
}

TEST_CASE ("slide: starts at the boundary, never retriggers, and tracks the sounding pitch (03 #5, #28)", "[voice][pitch][slide]")
{
    auto p = quietParams();
    p.decay = 60.0;
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.120);
    const std::size_t boundary = t.size();
    v.noteOn (48, true, true, false);
    run (v, t, 0.120);
    v.noteOn (43, false, true, false);
    run (v, t, 0.060);
    v.noteOff();
    run (v, t, 0.060);

    // Nothing moves before the boundary.
    for (std::size_t i = 100; i < boundary; ++i)
        REQUIRE (t.taps[i].pitchVolts == 2.0);
    CHECK (t.taps[boundary].pitchVolts > 2.0);
    CHECK (t.taps[boundary].pitchVolts < 2.05);
    CHECK (t.taps[boundary].slideOn);

    // No retrigger anywhere after the first note: the envelopes run on, the gate stays up.
    int triggers = 0;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (t.taps[i].envelopeTriggered) ++triggers;
    CHECK (triggers == 1);
    for (std::size_t i = boundary - 10; i < t.at (0.300); ++i)
    {
        REQUIRE (t.taps[i].envelopeVolts < t.taps[i - 1].envelopeVolts);
        REQUIRE (t.taps[i].ampEnvelope < t.taps[i - 1].ampEnvelope);
        REQUIRE (t.taps[i].gateLevel == 1.0);
        REQUIRE (t.taps[i].gate);
    }
    // The accent latch follows each latched note, and switches the decay rate mid-flight.
    CHECK (! t.taps[boundary - 1].accentLatched);
    CHECK (t.taps[boundary].accentLatched);
    CHECK (! t.taps[t.at (0.25)].accentLatched);
    const auto tauAt = [&] (double s)
    {
        const std::size_t i = t.at (s);
        return -1.0 / (48000.0 * std::log (t.taps[i + 1].envelopeVolts / t.taps[i].envelopeVolts));
    };
    CHECK (tauAt (0.06) == Approx (dsp::laws::decaySeconds (0.6)).epsilon (1e-6));
    CHECK (tauAt (0.18) == Approx (0.080).epsilon (1e-6));
    CHECK (tauAt (0.27) == Approx (dsp::laws::decaySeconds (0.6)).epsilon (1e-6));

    // The second slide starts from wherever the CV was (a chain is continuous).
    const std::size_t second = t.at (0.240);
    CHECK (t.taps[second].pitchVolts == Approx (t.taps[second - 1].pitchVolts).margin (0.01));

    // A plain note after all that jumps straight to pitch, and a later slide starts from it.
    v.noteOn (40, false, false, false);
    run (v, t, 0.010);
    CHECK (t.taps.back().pitchVolts == dsp::laws::pitchVolts (40.0));
    CHECK (! t.taps.back().slideOn);
    v.noteOn (52, false, true, false);
    Trace u;
    run (v, u, 0.001);
    CHECK (u.taps.front().pitchVolts == Approx (dsp::laws::pitchVolts (40.0)).margin (0.003));

    // The Slide Time knob: 0 %, 50 % and 100 % give 3.7, 22 and 132 ms.
    for (double knob : { 0.0, 50.0, 100.0 })
    {
        auto q = quietParams();
        q.slideTime = knob;
        dsp::Voice w;
        start (w, q, 48000.0, 1);
        w.noteOn (36, false, false, false);
        skip (w, 0.05);
        w.noteOn (48, false, true, false);
        Trace s;
        run (w, s, 0.6);
        const std::size_t i63 = firstIndex (s, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.pitchVolts >= 2.0 + (1.0 - std::exp (-1.0)); });
        INFO ("slide knob " << knob);
        CHECK (s.seconds (i63 + 1) == Approx (dsp::laws::slideSeconds (knob * 0.01)).epsilon (0.01));
    }
}

TEST_CASE ("vibrato: a trill between two targets, smoothed by the slide lag", "[voice][pitch][vibrato]")
{
    auto p = quietParams();
    p.vibSpeed = 50.0;      // 50 ms per note
    p.vibDepth = 100.0;     // 2 semitones apart
    p.slideTime = 0.0;      // 3.7 ms lag: nearly square
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    Trace t;
    v.noteOn (45, false, false, true);
    run (v, t, 1.0);

    const double centre = dsp::laws::pitchVolts (45.0);
    double lo = 10.0, hi = 0.0, mean = 0.0;
    int flips = 0;
    for (std::size_t i = 1; i < t.size(); ++i)
    {
        lo = std::min (lo, t.taps[i].pitchVolts);
        hi = std::max (hi, t.taps[i].pitchVolts);
        mean += t.taps[i].pitchVolts;
        if ((t.taps[i].pitchVolts - centre) * (t.taps[i - 1].pitchVolts - centre) < 0.0) ++flips;
    }
    mean /= static_cast<double> (t.size() - 1);
    std::printf ("[vibrato] spread %.2f semitones, %d crossings in 1 s, mean offset %.2f cents\n", (hi - lo) * 12.0, flips, (mean - centre) * 1200.0);
    CHECK ((hi - lo) * 12.0 == Approx (2.0).margin (0.02));
    CHECK (flips == 19);                                    // 10 Hz: a flip every 50 ms
    CHECK ((mean - centre) * 1200.0 == Approx (0.0).margin (6.0));
    CHECK (t.taps[0].pitchVolts > centre);                  // starts from the note itself, moving up
    CHECK (t.taps[0].pitchVolts < centre + 0.01);

    // The stock 22 ms lag rounds the trill off: less than the full spread arrives.
    p.slideTime = 50.0;
    start (v, p, 48000.0, 1);
    Trace soft;
    v.noteOn (45, false, false, true);
    run (v, soft, 1.0);
    lo = 10.0; hi = 0.0;
    for (std::size_t i = soft.at (0.3); i < soft.size(); ++i) { lo = std::min (lo, soft.taps[i].pitchVolts); hi = std::max (hi, soft.taps[i].pitchVolts); }
    CHECK ((hi - lo) * 12.0 == Approx (2.0 * std::tanh (0.050 / (2.0 * 0.022))).margin (0.03));

    // A note without the flag is steady; the mod wheel brings the trill in on any note.
    start (v, p, 48000.0, 1);
    Trace plain;
    v.noteOn (45, false, false, false);
    run (v, plain, 0.2);
    for (const auto& d : plain.taps) REQUIRE (d.pitchVolts == centre);
    v.setVibratoWheel (0.5);
    run (v, plain, 0.6);
    lo = 10.0; hi = 0.0;
    for (std::size_t i = plain.at (0.5); i < plain.size(); ++i) { lo = std::min (lo, plain.taps[i].pitchVolts); hi = std::max (hi, plain.taps[i].pitchVolts); }
    CHECK ((hi - lo) * 12.0 == Approx (1.0 * std::tanh (0.050 / (2.0 * 0.022))).margin (0.03));
    v.setVibratoWheel (0.0);
    run (v, plain, 0.1);
    CHECK (plain.taps.back().pitchVolts == centre);
}

TEST_CASE ("the oscillator free-runs: a note-on does not touch its phase (02 V12)", "[voice][pitch]")
{
    dsp::Voice a, b;
    start (a, quietParams(), 48000.0, 4);
    start (b, quietParams(), 48000.0, 4);
    a.noteOn (36, false, false, false);
    b.noteOn (36, false, false, false);
    std::vector<float> buffer (4800);
    a.process (buffer.data(), 4800);
    b.process (buffer.data(), 4800);
    a.noteOff();
    a.process (buffer.data(), 2400);
    b.process (buffer.data(), 2400);
    a.noteOn (36, true, false, false);      // a second, accented note on voice a only
    for (int i = 0; i < 2400; ++i)
    {
        float s;
        a.process (&s, 1);
        b.process (&s, 1);
        REQUIRE (a.debugOscillator().phase() == b.debugOscillator().phase());
    }
}

TEST_CASE ("event timing: moving a note-on by k samples moves the envelopes by k samples (04 E4)", "[voice][timing]")
{
    const auto gains = [] (int offset)
    {
        dsp::Voice v;
        start (v, quietParams(), 48000.0, 1);
        std::vector<float> block (1024);
        v.process (block.data(), offset);
        v.noteOn (36, true, false, false);
        Trace t;
        run (v, t, 0.05);
        return t;
    };
    const Trace base = gains (64);
    for (int offset : { 65, 101, 64 + 511 })
    {
        const Trace moved = gains (offset);
        for (std::size_t i = 0; i < base.size(); i += 7)
        {
            REQUIRE (moved.taps[i].vcaGain == base.taps[i].vcaGain);
            REQUIRE (moved.taps[i].envelopeVolts == base.taps[i].envelopeVolts);
            REQUIRE (moved.taps[i].cutoffHz == base.taps[i].cutoffHz);
        }
    }
}
