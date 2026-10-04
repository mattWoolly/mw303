// Output headroom: what the stage after the voice has to take. The spec constrains the
// plain note (-12 dBFS at default settings, TestAudio.cpp); accented notes rise above
// 0 dBFS until the output amplifier's rails round them off (Voice.h, outputStage). Voice.h
// publishes the measured worst cases as kWorstCaseStockPeak and kWorstCaseModsPeak; this
// test holds a grid of the loudest settings under them so the constants stay honest, and
// checks the rails' curve itself.
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
struct Worst { double peak = 0.0; std::string where; };

/** Six accented 16ths at 125 BPM (so C13 accumulates) over a grid of the controls that
    set the level, at 44.1 and 48 kHz x4. */
Worst worstPeak (bool mods, bool fm = true)
{
    Worst worst;
    for (double fs : { 44100.0, 48000.0 })
    for (int wave : { 0, 1 })
    for (double cutoff : { 0.0, 65.0, 100.0 })
    for (double envMod : { 45.0, 100.0 })
    for (double res : { 0.0, 100.0 })
    for (int note : { 36, 48 })
    {
        auto p = quietParams();
        p.wave = wave; p.cutoff = cutoff; p.envMod = envMod; p.resonance = res; p.decay = 0.0; p.accent = 100.0;
        if (mods) { p.modOn = true; p.modOverdrive = 100.0; p.modFilterFm = fm ? 100.0 : 0.0; }
        dsp::Voice v;
        start (v, p, fs, 4);
        const std::vector<SeqStep> pattern (6, SeqStep { note, true, false, false, false });
        const auto audio = render (v, makeEvents (pattern, 125.0, fs, 1), static_cast<long long> (0.9 * fs), 256);
        REQUIRE (allFinite (audio));
        const double pk = peakAbs (audio);
        if (pk > worst.peak)
        {
            worst.peak = pk;
            char text[160];
            std::snprintf (text, sizeof (text), "%.0f Hz, %s, cutoff %.0f, env mod %.0f, resonance %.0f, note %d",
                           fs, wave == 0 ? "saw" : "square", cutoff, envMod, res, note);
            worst.where = text;
        }
    }
    return worst;
}
}

TEST_CASE ("headroom: accented notes stay under the published worst-case peaks", "[voice][audio][headroom]")
{
    // Stock controls, Accent 100. Measured +6.5 dBFS (2.10) at 44.1 kHz, saw, cutoff 100,
    // Env Mod 100, resonance 0, C3, on a wider grid than the one here (cutoff 0, 35, 65,
    // 100; Env Mod 0, 45, 100; resonance 0, 50, 75, 100; Decay 0 and 100; C1 to C4). Before
    // the output amplifier's rails were modelled: +9.5 dBFS (2.97).
    const Worst stock = worstPeak (false);
    std::printf ("[headroom] stock, Accent 100: worst peak %.3f (%+.1f dBFS) at %s; published kWorstCaseStockPeak %.2f\n",
                 stock.peak, 20.0 * std::log10 (stock.peak), stock.where.c_str(), dsp::Voice::kWorstCaseStockPeak);
    CHECK (stock.peak > 2.0);                                   // the constant is not slack
    CHECK (stock.peak < dsp::Voice::kWorstCaseStockPeak);

    // Mods: Overdrive and Filter FM at 100 on top. The rails and the guard behind the
    // decimator bound it at +6.8 dBFS (2.2); the same wider grid presses against that bound
    // at many settings. Before the rails: +16.0 dBFS (6.31).
    const Worst loud = worstPeak (true);
    std::printf ("[headroom] Mods Overdrive and FM 100, Accent 100: worst peak %.3f (%+.1f dBFS) at %s; published kWorstCaseModsPeak %.2f\n",
                 loud.peak, 20.0 * std::log10 (loud.peak), loud.where.c_str(), dsp::Voice::kWorstCaseModsPeak);
    const Worst od = worstPeak (true, false);
    std::printf ("[headroom] Mods Overdrive 100 only: worst peak %.3f (%+.1f dBFS) at %s\n", od.peak, 20.0 * std::log10 (od.peak), od.where.c_str());
    CHECK (loud.peak > 2.1);
    CHECK (od.peak < dsp::Voice::kWorstCaseModsPeak);
    CHECK (loud.peak < dsp::Voice::kWorstCaseModsPeak);

    // The defaults with Accent at its default 70 and at 100, for the record.
    for (double accent : { 70.0, 100.0 })
    {
        auto p = quietParams();
        p.accent = accent;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        const std::vector<SeqStep> pattern (6, SeqStep { 36, true, false, false, false });
        const auto audio = render (v, makeEvents (pattern, 125.0, 48000.0, 1), 43200, 256);
        const double pk = peakAbs (audio);
        std::printf ("[headroom] default knobs, Accent %.0f, six accented 16ths: peak %.3f (%+.1f dBFS)\n", accent, pk, 20.0 * std::log10 (pk));
        CHECK (pk < dsp::Voice::kWorstCaseStockPeak);
    }
}

TEST_CASE ("headroom: the output amplifier's rails are clean below full scale and bounded above it", "[voice][audio][headroom]")
{
    using dsp::Voice;
    // Exactly linear up to the knee: no distortion at all on anything under 0 dBFS.
    for (double x = -Voice::kOutputClipKnee; x <= Voice::kOutputClipKnee; x += 1.0e-3)
        CHECK (Voice::outputStage (x) == x);

    // Odd, monotonic, bounded, and continuous in slope at the knee.
    double last = Voice::outputStage (-100.0);
    for (double x = -100.0; x <= 100.0; x += 1.0e-3)
    {
        const double y = Voice::outputStage (x);
        REQUIRE (y >= last);
        REQUIRE (std::abs (y) <= Voice::kOutputClipCeiling);
        REQUIRE (Voice::outputStage (-x) == -y);
        last = y;
    }
    const double h = 1.0e-6;
    const double slopeAbove = (Voice::outputStage (Voice::kOutputClipKnee + h) - Voice::outputStage (Voice::kOutputClipKnee)) / h;
    CHECK (slopeAbove == Approx (1.0).margin (1.0e-4));
    CHECK (Voice::outputStage (Voice::kOutputClipKnee + 1.0) > 1.7);      // rounds off, does not flatten early
    CHECK (std::isnan (Voice::outputStage (std::nan (""))));

    // The guard behind the decimator: exactly x up to the ceiling, bounded above it.
    for (double x = -Voice::kOutputClipCeiling; x <= Voice::kOutputClipCeiling; x += 1.0e-3)
        CHECK (Voice::outputGuard (x) == x);
    CHECK (std::abs (Voice::outputGuard (1.0e6)) <= Voice::kOutputGuardCeiling);

    // A plain note at the default settings peaks near -12 dBFS, far under the knee, so the
    // rails add nothing to it (0 % distortion from the output stage).
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 4);
    const std::vector<SeqStep> pattern (6, SeqStep { 36, false, false, false, false });
    const auto audio = render (v, makeEvents (pattern, 125.0, 48000.0, 1), 43200, 256);
    CHECK (peakAbs (audio) < 0.5 * Voice::kOutputClipKnee);
}
