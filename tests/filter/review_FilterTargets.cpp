// Acceptance targets of the filter that an independent review found unasserted, or asserted
// against a loosened tolerance. "02 Fn" is the VCF table of docs/research/02 (Measurable
// acceptance targets), "04 ..." is docs/research/04.
//
// Calibration. The build spec's section 7 table lists the default trims as a ladder load
// of 13.6 k and a loop gain of 18.7, the loaded model of 04 section 1.4. Document 02 wrote
// F12 from its own loaded model with an output amplifier gain of 110 (loop gain 19.4) and
// F8 from the unloaded block model, so at 13.6 k three of F12's points and both of F8's
// fall 0.5 to 1.9 dB outside their bands. The architect's decision after review: the
// parameter table's cal_rb default is 20 k, which meets every target (a scan of 16 to
// 25 k at loop gain 18.7 meets F12 and F9 from 17 k up and F8 from 16 k up; 20 k centres
// them, and at every legal loop gain self-oscillation stays above 1.6 kHz). Three test
// cases keep that honest:
//   - the acceptance reference calibration (20 k, 18.7) meets every target, asserted;
//   - the 13.6 k calibration is the 04 section 1.4 model, asserted against that table,
//     with its F8 and F12 shortfall pinned at the measured size;
//   - the shipped defaults of the parameter table meet 02 F8, F9 and F12.
#include "FilterTestSupport.h"

#include "acid/engine/ParamTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace acid;
using namespace acid::test::filter;
using dsp::DiodeLadder;
using dsp::DiodeLadderTrims;

namespace
{
/** Peak of the full-resonance response near fRes, in dB above the response at fRes / 5. */
double peakOverPlateau (DiodeLadder& f, double fRes)
{
    const auto ir = impulseResponse (f, fRes, 1.0, 2.0);
    const Peak pk = findPeak (ir, 0.8 * fRes, 1.3 * fRes);
    return pk.db - ir.db (fRes / 5.0);
}

struct Heights { double p150, p250, p500, p975, p1k; };

Heights peakHeights (DiodeLadder& f, const char* name)
{
    const Heights h { peakOverPlateau (f, 150.0), peakOverPlateau (f, 250.0), peakOverPlateau (f, 500.0),
                      peakOverPlateau (f, 975.0), peakOverPlateau (f, 1000.0) };
    std::printf ("[targets] %s: peak over plateau, r 1: 150 Hz %+.1f, 250 Hz %+.1f, 500 Hz %+.1f, 975 Hz %+.1f, 1 kHz %+.1f dB\n",
                 name, h.p150, h.p250, h.p500, h.p975, h.p1k);
    return h;
}

/** 02 F12: under 6 dB at 150 Hz, 5 to 10 at 250 Hz, 11 to 18 at 500 Hz, 19 to 31 at 1 kHz.
    02 F9 and open question 3: the default build should read +19 +- 3 dB at 975 Hz. */
void checkPeakTargets (const Heights& h)
{
    CHECK (h.p150 < 6.0);
    CHECK (h.p250 >= 5.0);
    CHECK (h.p250 <= 10.0);
    CHECK (h.p500 >= 11.0);
    CHECK (h.p500 <= 18.0);
    CHECK (h.p1k >= 19.0);
    CHECK (h.p1k <= 31.0);
    CHECK (h.p975 == Catch::Approx (19.0).margin (3.0));
}

struct Levels { double at110, at200; };

/** Zero-resonance level, oscillator to test point, resonant frequency 975 Hz. */
Levels zeroResonanceLevels (DiodeLadder& f, const char* name)
{
    const double scale = DiodeLadder::kInputDivider * DiodeLadder::kOutputAmpGain;
    const auto ir = impulseResponse (f, 975.0, 0.0, 3.0);
    const Levels l { magDb (scale * ir.response (110.0)), magDb (scale * ir.response (200.0)) };
    std::printf ("[targets] %s: r 0, fRes 975: %.2f dB at 110 Hz (F8 -5 +- 1.5), %.2f dB at 200 Hz (F8 -7.3 +- 1.5)\n",
                 name, l.at110, l.at200);
    return l;
}

void checkZeroResonanceTargets (const Levels& l)
{
    CHECK (l.at110 == Catch::Approx (-5.0).margin (1.5));
    CHECK (l.at200 == Catch::Approx (-7.3).margin (1.5));
}

/** Kicks the filter and lets it run with no input: the peak of the last 0.3 s of 1.5 s. */
double freeRunningLevel (DiodeLadder& f, double fRes, double r)
{
    f.reset();
    const double fs = f.getSampleRate();
    const auto n = static_cast<std::size_t> (1.5 * fs), from = static_cast<std::size_t> (1.2 * fs);
    double level = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double y = f.process (i < 64 ? 0.5 : 0.0, fRes, r);
        if (i >= from) level = std::max (level, std::abs (y));
    }
    return level;
}
}

TEST_CASE ("targets: the acceptance reference calibration meets 02 F8, F9, F12 and F13", "[filter][review]")
{
    DiodeLadder f;
    f.prepare (192000.0);           // the voice's internal rate at 48 kHz x4
    f.setTrims (DiodeLadderTrims::acceptanceReference());
    REQUIRE (f.getTrims().rbOhms == 20.0e3);
    REQUIRE (f.getTrims().kMax == 18.7);
    REQUIRE (f.subAudioBypass (1.0) == 0.0);        // the stock circuit, guard not engaged

    checkPeakTargets (peakHeights (f, "reference 20 k, 18.7"));
    checkZeroResonanceTargets (zeroResonanceLevels (f, "reference 20 k, 18.7"));

    // 02 F13: no self-oscillation below 1.2 kHz at any resonance setting.
    for (double fRes : { 100.0, 300.0, 600.0, 1000.0, 1200.0 })
        for (double r : { 0.5, 1.0 })
        {
            INFO ("fRes " << fRes << " r " << r);
            CHECK (freeRunningLevel (f, fRes, r) < 1.0e-4);
        }
    // It does sing near the top of the sweep, as a stock unit does (02 section 3.4).
    CHECK (freeRunningLevel (f, 2400.0, 1.0) < 1.0e-4);
    CHECK (freeRunningLevel (f, 3200.0, 1.0) > 1.0e-3);
}

TEST_CASE ("targets: the 13.6 k calibration is the loaded model of 04 section 1.4", "[filter][review]")
{
    DiodeLadder f;
    f.prepare (192000.0);
    f.setTrims (13.6e3, 18.7);
    REQUIRE (f.getTrims().rbOhms == 13.6e3);
    REQUIRE (f.getTrims().kMax == 18.7);

    // 04 section 1.4, last column: peak above its own passband at r = 1, R_b = 13.6 k.
    const double fRes[] = { 194.0, 387.0, 800.0, 1614.0 };
    const double table[] = { 2.8, 8.0, 14.6, 23.1 };
    for (int i = 0; i < 4; ++i)
    {
        const double p = peakOverPlateau (f, fRes[i]);
        std::printf ("[targets] 13.6 k, 18.7: peak over plateau at %.0f Hz %+.2f dB (04 table %+.1f)\n", fRes[i], p, table[i]);
        INFO ("fRes " << fRes[i]);
        CHECK (p == Catch::Approx (table[i]).margin (0.5));
    }

    // 02 F9 holds at 13.6 k: +19 +- 3 dB at 975 Hz. F12 is missed by 0.5, 0.8 and 1.8 dB
    // at 250 Hz, 500 Hz and 1 kHz; the bounds below are that shortfall, so that a change
    // to the model which widens it is caught.
    const Heights h = peakHeights (f, "13.6 k, 18.7");
    CHECK (h.p975 == Catch::Approx (19.0).margin (3.0));
    CHECK (h.p150 < 6.0);
    CHECK (h.p250 == Catch::Approx (4.5).margin (0.3));
    CHECK (h.p500 == Catch::Approx (10.2).margin (0.3));
    CHECK (h.p1k == Catch::Approx (17.2).margin (0.3));

    // Zero resonance: the unloaded block model's level (02 F8: -5.1 dB at 110 Hz, -7.3 dB
    // at 200 Hz) times the loading factor of 02 section 3.4, R_b / (R_b + a), where a is
    // the source resistance of the top ladder node, 2 V_T over the tail current.
    const double tailAmps = 975.0 / DiodeLadder::kHzPerMicroamp * 1.0e-6;
    const double a = DiodeLadder::kTwoVt / tailAmps;
    const double loading = 20.0 * std::log10 (13.6e3 / (13.6e3 + a));
    CHECK (loading == Catch::Approx (-1.95).margin (0.02));
    const Levels l = zeroResonanceLevels (f, "13.6 k, 18.7");
    CHECK (l.at110 == Catch::Approx (-5.1 + loading).margin (0.5));
    CHECK (l.at200 == Catch::Approx (-7.3 + loading).margin (0.5));
}

TEST_CASE ("targets: the parameter table's default calibration meets 02 F8, F9, F12 and F13", "[filter][review]")
{
    // The trims a freshly loaded instrument runs with: the table's defaults, which are
    // also the filter struct's and the acceptance reference.
    DiodeLadder f;
    f.prepare (192000.0);
    f.setTrims (static_cast<double> (paramInfo (kCalRb).def) * 1.0e3, static_cast<double> (paramInfo (kCalKmax).def));
    std::printf ("[targets] parameter table defaults: cal_rb %.1f k, cal_kmax %.1f\n",
                 f.getTrims().rbOhms * 1.0e-3, f.getTrims().kMax);
    CHECK (f.getTrims().rbOhms == Catch::Approx (DiodeLadderTrims().rbOhms));
    CHECK (f.getTrims().kMax == Catch::Approx (DiodeLadderTrims().kMax));
    CHECK (f.getTrims().rbOhms == Catch::Approx (DiodeLadderTrims::acceptanceReference().rbOhms));
    checkPeakTargets (peakHeights (f, "table defaults"));
    checkZeroResonanceTargets (zeroResonanceLevels (f, "table defaults"));
    for (double fRes : { 300.0, 1000.0, 1200.0 })
    {
        INFO ("fRes " << fRes);
        CHECK (freeRunningLevel (f, fRes, 1.0) < 1.0e-4);
    }

    // The loop gain trim at either end of its range keeps F13 (no self-oscillation below
    // 1.2 kHz) and leaves the sub-audio guard idle at full resonance (F11b).
    for (double kMax : { 17.0, 20.0 })
    {
        f.setTrims (f.getTrims().rbOhms, kMax);
        INFO ("kMax " << kMax);
        CHECK (freeRunningLevel (f, 1200.0, 1.0) < 1.0e-4);
        CHECK (f.subAudioBypass (1.0) == 0.0);
    }
}

TEST_CASE ("targets: analogue response over the full 04 F1 band at the 4x and 8x rates", "[filter][review]")
{
    // 04 F1 asks for +-0.25 dB and +-3 degrees up to min (16 kHz, 4 fRes) at 4x. The filter
    // is the exact trapezoidal discretisation (asserted to 1e-6 dB in the linear tests), so
    // what is left is the frequency warping of that rule away from the resonant frequency,
    // where the stages are prewarped. Measured here, for fRes from 3 kHz up:
    //   4x (176.4 and 192 kHz): within 0.25 dB up to 9.5 kHz; 0.78 dB and 0.65 dB at 16 kHz
    //   8x (352.8 and 384 kHz): within 0.25 dB over the whole band (0.19 dB at 16 kHz)
    // The phase bound holds everywhere. So F1's magnitude bound is met at 8x and, at 4x,
    // only below 9.5 kHz: a known deviation, pinned here at its measured size.
    struct Rate { double fs; double fullBandDb; };
    for (const Rate& rate : { Rate { 176400.0, 0.80 }, Rate { 192000.0, 0.67 }, Rate { 352800.0, 0.25 }, Rate { 384000.0, 0.25 } })
    {
        DiodeLadder f;
        f.prepare (rate.fs);
        double worstFull = 0.0, worstInner = 0.0, worstDeg = 0.0;
        for (double fRes : { 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 10000.0 })
            for (double r : { 0.0, 0.5, 0.9 })
            {
                const auto ir = impulseResponse (f, fRes, r, 2.0);
                const auto rates = analogRates (f.getTrims(), fRes, r);
                double full = 0.0, inner = 0.0, deg = 0.0;
                for (double hz : logSpace (20.0, std::min (16000.0, 4.0 * fRes), 40))
                {
                    const Complex measured = ir.response (hz), model = linearResponse (rates, analogS (hz)).out;
                    const double db = std::abs (magDb (measured) - magDb (model));
                    full = std::max (full, db);
                    if (hz <= 9500.0) inner = std::max (inner, db);
                    deg = std::max (deg, std::abs (wrapDeg (phaseDeg (measured) - phaseDeg (model))));
                }
                INFO ("fs " << rate.fs << " fRes " << fRes << " r " << r << ": " << full << " dB over the band, "
                      << inner << " dB to 9.5 kHz, " << deg << " deg");
                CHECK (inner < 0.25);
                CHECK (full < rate.fullBandDb);
                CHECK (deg < 3.0);
                worstFull = std::max (worstFull, full);
                worstInner = std::max (worstInner, inner);
                worstDeg = std::max (worstDeg, deg);
            }
        std::printf ("[targets] fs %6.0f against the analogue model, fRes 1 to 10 kHz: %.3f dB to 9.5 kHz, %.3f dB to min (16 kHz, 4 fRes), %.2f deg\n",
                     rate.fs, worstInner, worstFull, worstDeg);
    }
}
