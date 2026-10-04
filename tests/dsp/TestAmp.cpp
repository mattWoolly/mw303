// Amplitude envelope, gate shaping and the accent path into the amplifier
// (docs/research/03 section 6 and acceptance targets #18 to #25).
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
constexpr double kFs = 96000.0;

double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-12)); }

/** Seconds at which the tap picked by `get` first crosses `level` going up, from `from`. */
template <typename Get>
double risesAbove (const Trace& t, double from, double level, Get get)
{
    for (std::size_t i = t.at (from); i < t.size(); ++i)
        if (get (t.taps[i]) >= level) return t.seconds (i);
    return -1.0;
}

template <typename Get>
double fallsBelow (const Trace& t, double from, double level, Get get)
{
    for (std::size_t i = t.at (from); i < t.size(); ++i)
        if (get (t.taps[i]) <= level) return t.seconds (i);
    return -1.0;
}

const auto gateCurrent = [] (const dsp::VoiceDebugTaps& d) { return d.gateCurrent; };
const auto accentCurrent = [] (const dsp::VoiceDebugTaps& d) { return d.accentCurrent; };
const auto vcaGain = [] (const dsp::VoiceDebugTaps& d) { return d.vcaGain; };
}

TEST_CASE ("amplitude envelope: 1.5 s decay to silence at 3.4 s, no sustain (03 #18, #19)", "[voice][amp]")
{
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 4.2);

    // Closed form: 1.118 exp (-t / 1.5) - 0.118, measured from the gate's arrival.
    for (double s : { 0.06, 0.12, 0.25, 0.5, 1.0, 2.0 })
    {
        INFO ("t = " << s);
        CHECK (t.taps[t.at (s)].ampEnvelope == Approx (1.118 * std::exp (-s / 1.5) - 0.118).margin (1e-4));
        CHECK (t.taps[t.at (s)].gateCurrent == Approx (t.taps[t.at (s)].ampEnvelope).margin (1e-9));
    }
    const double at1s = db (t.taps[t.at (1.0)].gateCurrent);
    const double silent = fallsBelow (t, 0.1, 0.0, gateCurrent);
    std::printf ("[amp] held plain note: %.2f dB at 1 s (target -6.8), silent at %.2f s (target 3.0 to 4.0)\n", at1s, silent);
    CHECK (at1s == Approx (-6.8).margin (1.0));
    CHECK (silent > 3.0);
    CHECK (silent < 4.0);
    CHECK (silent == Approx (3.37).margin (0.02));
    // The audio is silent from there on as well.
    CHECK (peakAbs (t.audio, t.at (silent + 0.3)) < 1e-5);
}

TEST_CASE ("gate: turn-on delay depends on the preceding off time (03 #23)", "[voice][amp][gate]")
{
    // First note after a long pause.
    dsp::Voice v;
    start (v, quietParams(), kFs, 1);
    Trace first;
    v.noteOn (36, false, false, false);
    run (v, first, 0.02);
    const double longPause = risesAbove (first, 0.0, 0.1, gateCurrent);
    std::printf ("[gate] turn-on delay after a long pause: %.2f ms (target 5.4)\n", longPause * 1e3);
    CHECK (longPause == Approx (0.0054).margin (0.001));
    CHECK (first.taps.back().turnOnDelaySeconds == Approx (0.0054).epsilon (1e-6));
    // The filter envelope is not delayed: it has already peaked while the amplifier is shut.
    CHECK (first.taps[first.at (0.001)].envelopeVolts > 9.0);
    CHECK (first.taps[first.at (0.004)].gateCurrent == 0.0);

    // 16ths at 40, 125 and 300 BPM: the third note's delay.
    struct Row { double bpm, expected; };
    for (const Row& row : { Row { 40.0, 0.0045 }, Row { 125.0, 0.0019 }, Row { 300.0, 0.0004 } })
    {
        dsp::Voice u;
        start (u, quietParams(), kFs, 1);
        const std::vector<SeqStep> pattern (4, SeqStep {});
        const double step = stepSeconds (row.bpm);
        const Trace t = renderTraced (u, makeEvents (pattern, row.bpm, kFs, 1), static_cast<long long> (4 * step * kFs));
        const double onset = 2.0 * step;
        const double delay = risesAbove (t, onset, 0.1, gateCurrent) - onset;
        std::printf ("[gate] 16ths at %.0f BPM: turn-on delay %.2f ms (target %.1f)\n", row.bpm, delay * 1e3, row.expected * 1e3);
        INFO ("bpm " << row.bpm);
        CHECK (delay == Approx (row.expected).margin (0.001));
    }

    // cal_gate_delay 0: the note starts within half a millisecond.
    auto p = quietParams();
    p.calGateDelay = 0.0;
    start (v, p, kFs, 1);
    Trace quick;
    v.noteOn (36, false, false, false);
    run (v, quick, 0.01);
    CHECK (risesAbove (quick, 0.0, 0.5, gateCurrent) < 0.0005);
}

TEST_CASE ("gate: rise, hold and fall (03 #24, #25)", "[voice][amp][gate]")
{
    dsp::Voice v;
    start (v, quietParams(), kFs, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.0614);
    v.noteOff();
    run (v, t, 0.05);

    const double t10 = risesAbove (t, 0.0, 0.1, gateCurrent), t90 = risesAbove (t, 0.0, 0.9, gateCurrent);
    std::printf ("[gate] rise 10 to 90 %%: %.3f ms (target 0.4 to 0.8)\n", (t90 - t10) * 1e3);
    CHECK (t90 - t10 >= 0.0004);
    CHECK (t90 - t10 <= 0.0008);

    const double off = 0.0614;
    const double level = t.taps[t.at (off)].gateCurrent;
    const double holdEnd = fallsBelow (t, off, 0.98 * level, gateCurrent) - off;
    const double silent = fallsBelow (t, off, 0.0, gateCurrent) - off;
    const double f90 = fallsBelow (t, off, 0.9 * level, gateCurrent), f10 = fallsBelow (t, off, 0.1 * level, gateCurrent);
    std::printf ("[gate] release: full level for %.2f ms, 90 to 10 %% in %.2f ms, silent after %.2f ms (targets 8, 6 to 8, 16 to 20)\n",
                 holdEnd * 1e3, (f10 - f90) * 1e3, silent * 1e3);
    CHECK (holdEnd == Approx (0.008).margin (0.003));
    CHECK (f10 - f90 > 0.005);
    CHECK (f10 - f90 < 0.009);
    CHECK (silent > 0.016 - 0.003);
    CHECK (silent < 0.020 + 0.003);

    // A plain 16th at 125 BPM sounds for about 62 % of the step.
    const double audible = (off + silent) - t10;
    std::printf ("[gate] plain 16th at 125 BPM: audible for %.1f ms of the 120 ms step\n", audible * 1e3);
    CHECK (audible / 0.120 == Approx (0.62).margin (0.05));

    // A gate that returns inside the 8 ms hold never drops.
    start (v, quietParams(), kFs, 1);
    Trace legato;
    v.noteOn (36, false, false, false);
    run (v, legato, 0.05);
    v.noteOff();
    run (v, legato, 0.004);
    v.noteOn (38, false, false, false);
    run (v, legato, 0.02);
    for (std::size_t i = legato.at (0.05); i < legato.size(); ++i)
        REQUIRE (legato.taps[i].gateLevel == 1.0);
}

TEST_CASE ("accent into the amplifier: boost, bump decay, tail and early onset (03 #20 to #22)", "[voice][amp][accent]")
{
    auto p = quietParams();
    p.accent = 100.0;
    dsp::Voice v;
    start (v, p, kFs, 1);
    Trace t;
    v.noteOn (36, true, false, false);
    run (v, t, 0.0614);
    v.noteOff();
    run (v, t, 0.25);

    double peak = 0.0, peakAccent = 0.0;
    std::size_t at = 0;
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        if (t.taps[i].vcaGain > peak) { peak = t.taps[i].vcaGain; at = i; }
        peakAccent = std::max (peakAccent, t.taps[i].accentCurrent);
    }
    std::printf ("[amp accent] peak gain %.2f (%.1f dB over a plain note) at %.1f ms; accent current %.0f uA peak\n",
                 peak, db (peak), t.seconds (at) * 1e3, peakAccent * 47.7);
    CHECK (db (peak) == Approx (11.0).margin (3.0));
    CHECK (peakAccent * 47.7 == Approx (122.0).epsilon (0.08));

    // The research's simulated current at a few times, in microamps (envelope 72 ms there, 80 ms here).
    CHECK (t.taps[t.at (0.0005)].accentCurrent * 47.7 == Approx (60.0).margin (12.0));
    CHECK (t.taps[t.at (0.0025)].accentCurrent * 47.7 == Approx (121.0).margin (8.0));
    CHECK (t.taps[t.at (0.010)].accentCurrent * 47.7 == Approx (109.0).margin (8.0));

    // Bump decay: fitted between 10 and 60 ms.
    const double tau = 0.050 / std::log (t.taps[t.at (0.010)].accentCurrent / t.taps[t.at (0.060)].accentCurrent);
    // "Zero" is a hundredth of a plain note's level: the 0.7 ms lag's own tail is not the point.
    const double zero = fallsBelow (t, 0.02, 0.01, accentCurrent);
    std::printf ("[amp accent] bump decay tau %.1f ms (target 58 to 65), tail ends %.0f ms after the note started (target 150 to 160)\n",
                 tau * 1e3, zero * 1e3);
    CHECK (tau > 0.058 * 0.85);
    CHECK (tau < 0.065 * 1.15);
    CHECK (zero > 0.150 - 0.020);
    CHECK (zero < 0.160 + 0.020);

    // The tail outlasts the gate: still sounding 40 ms after gate-off, when a plain note is silent.
    CHECK (t.taps[t.at (0.0614 + 0.040)].vcaGain > 0.1);
    CHECK (t.taps[t.at (0.0614 + 0.040)].gateCurrent == 0.0);

    // Accented notes speak earlier: the accent path does not wait for the gate.
    const double accentedOnset = risesAbove (t, 0.0, 0.5, vcaGain);
    CHECK (accentedOnset < 0.0015);

    // Boost against the knob (03 section 6.3 table), within 1.5 dB.
    struct Row { double knob, boostDb; };
    for (const Row& row : { Row { 13.0, 0.0 }, Row { 30.0, 3.3 }, Row { 50.0, 5.7 }, Row { 75.0, 8.1 }, Row { 90.0, 9.8 }, Row { 100.0, 11.0 } })
    {
        p.accent = row.knob;
        start (v, p, kFs, 1);
        Trace u;
        v.noteOn (36, true, false, false);
        run (v, u, 0.03);
        double pk = 0.0;
        for (const auto& d : u.taps) pk = std::max (pk, d.vcaGain);
        std::printf ("[amp accent] knob %.0f %%: +%.1f dB (table %.1f)\n", row.knob, db (pk), row.boostDb);
        INFO ("knob " << row.knob);
        CHECK (db (pk) == Approx (row.boostDb).margin (1.5));
    }

    // cal_accamp scales the current; uncoupled, Acc Vol sets it and Accent no longer does.
    p = quietParams();
    p.accent = 100.0;
    p.calAccAmp = 2.2;
    start (v, p, kFs, 1);
    Trace doubled;
    v.noteOn (36, true, false, false);
    run (v, doubled, 0.03);
    double pk2 = 0.0;
    for (const auto& d : doubled.taps) pk2 = std::max (pk2, d.accentCurrent);
    CHECK (pk2 == Approx (2.0 * peakAccent).epsilon (0.01));

    p = quietParams();
    p.accent = 100.0;
    p.accVolCouple = false;
    p.accVol = 0.0;
    start (v, p, kFs, 1);
    Trace none;
    v.noteOn (36, true, false, false);
    run (v, none, 0.03);
    for (const auto& d : none.taps) REQUIRE (d.accentCurrent == 0.0);
    CHECK (none.taps[none.at (0.02)].accentOctaves > 0.3);     // the filter accent is still there
}

TEST_CASE ("accent after a slide works on what is left of the envelope (03 section 10.2)", "[voice][amp][accent]")
{
    // One plain 16th sliding into an accented one at 125 BPM, Accent and Resonance at maximum.
    struct Row { double decay, megVolts, currentUa; };
    for (const Row& row : { Row { 0.0, 1.9, 11.0 }, Row { 100.0, 9.0, 107.0 } })
    {
        auto p = quietParams();
        p.accent = 100.0; p.resonance = 100.0; p.decay = row.decay;
        dsp::Voice v;
        start (v, p, kFs, 1);
        Trace t;
        v.noteOn (36, false, false, false);
        run (v, t, 0.120);
        v.noteOn (36, true, true, false);
        run (v, t, 0.060);
        const double meg = t.taps[t.at (0.120)].envelopeVolts;
        double current = 0.0;
        for (std::size_t i = t.at (0.120); i < t.size(); ++i) current = std::max (current, t.taps[i].accentCurrent);
        std::printf ("[amp accent] slide into accent, decay %.0f %%: envelope %.1f V at the boundary, accent current %.0f uA (table %.1f V, %.0f uA)\n",
                     row.decay, meg, current * 47.7, row.megVolts, row.currentUa);
        INFO ("decay " << row.decay);
        CHECK (meg == Approx (row.megVolts).margin (0.4));
        CHECK (current * 47.7 == Approx (row.currentUa).margin (10.0));
        // No retrigger on the slid-to note.
        for (std::size_t i = t.at (0.119); i < t.size(); ++i) REQUIRE (! t.taps[i].envelopeTriggered);
    }
}

TEST_CASE ("vca clicks off: click-free ramps replace the gate shape", "[voice][amp][gate]")
{
    auto p = quietParams();
    p.calVcaClicks = false;
    dsp::Voice v;
    start (v, p, kFs, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.0614);
    v.noteOff();
    run (v, t, 0.05);

    const double t10 = risesAbove (t, 0.0, 0.1, gateCurrent), t90 = risesAbove (t, 0.0, 0.9, gateCurrent);
    CHECK (t90 - t10 > 0.0015);
    // The steepest step of the control current is far below the stock gate's.
    double steepest = 0.0;
    for (std::size_t i = 1; i < t.size(); ++i)
        steepest = std::max (steepest, std::abs (t.taps[i].gateCurrent - t.taps[i - 1].gateCurrent));
    CHECK (steepest * kFs < 600.0);         // full scale in no less than 1.7 ms
    // The fall starts at gate-off and takes 16 ms.
    const double silent = fallsBelow (t, 0.0614, 0.0, gateCurrent) - 0.0614;
    CHECK (silent == Approx (0.016).margin (0.001));

    // The stock shape for comparison is more than four times steeper.
    start (v, quietParams(), kFs, 1);
    Trace stock;
    v.noteOn (36, false, false, false);
    run (v, stock, 0.03);
    double stockSteepest = 0.0;
    for (std::size_t i = 1; i < stock.size(); ++i)
        stockSteepest = std::max (stockSteepest, std::abs (stock.taps[i].gateCurrent - stock.taps[i - 1].gateCurrent));
    CHECK (stockSteepest > 4.0 * steepest);
}

TEST_CASE ("cpu gate jitter: edges snap to a free-running 1.8 ms grid", "[voice][amp][gate]")
{
    auto p = quietParams();
    p.calCpuJitter = true;
    const double fs = 48000.0;
    dsp::Voice v;
    start (v, p, fs, 1);

    // 16ths at 147 BPM (102 ms step): nominal gate 52.4 ms.
    const std::vector<SeqStep> pattern (16, SeqStep {});
    const double bpm = 147.0, step = stepSeconds (bpm);
    const Trace t = renderTraced (v, makeEvents (pattern, bpm, fs, 4), static_cast<long long> (64 * step * fs));

    std::vector<double> lengths, lateness;
    double onAt = -1.0;
    int note = 0;
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        const bool before = i > 0 && t.taps[i - 1].gate;
        if (t.taps[i].gate && ! before)
        {
            onAt = t.seconds (i);
            lateness.push_back (onAt - note * step);
            ++note;
        }
        if (! t.taps[i].gate && before && onAt >= 0.0)
            lengths.push_back (t.seconds (i) - onAt);
    }
    REQUIRE (lengths.size() >= 60);
    const double nominal = 0.5 * step + 1.35e-3;
    double lo = 1.0, hi = 0.0, latest = 0.0;
    int nearNominal = 0;
    for (double l : lengths)
    {
        lo = std::min (lo, l); hi = std::max (hi, l);
        if (std::abs (l - nominal) < 0.0011) ++nearNominal;
        // Every gate is a whole number of interrupts plus the 1.35 ms.
        const double ticks = (l - 1.35e-3) / 0.0018;
        CHECK (std::abs (ticks - std::round (ticks)) < 0.03);
    }
    for (double d : lateness) { CHECK (d >= -1.0 / fs); CHECK (d < 0.0018 + 1.0 / fs); latest = std::max (latest, d); }
    std::printf ("[gate] jitter mode at 147 BPM: gate %.2f to %.2f ms (nominal %.2f), onsets up to %.2f ms late\n",
                 lo * 1e3, hi * 1e3, nominal * 1e3, latest * 1e3);
    CHECK (hi - lo < 0.0019);           // one interrupt of spread
    CHECK (hi - lo > 0.0010);           // and there is some
    CHECK (nearNominal >= static_cast<int> (lengths.size()) / 2);

    // Off: edges are exact.
    start (v, quietParams(), fs, 1);
    const Trace exact = renderTraced (v, makeEvents (pattern, bpm, fs, 1), static_cast<long long> (16 * step * fs));
    for (int k = 0; k < 16; ++k)
    {
        const std::size_t i = static_cast<std::size_t> (std::llround (k * step * fs));
        CHECK (exact.taps[i].gate);
        if (i > 0) CHECK (! exact.taps[i - 1].gate);
    }
}
