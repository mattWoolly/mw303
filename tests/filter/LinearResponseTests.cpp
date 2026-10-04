// Small-signal behaviour of the diode ladder against closed-form transfer functions.
// Test names carry the acceptance-target numbers of the research documents:
// "04 Fn" is docs/research/04-dsp-modeling.md section 6, "02 Fn" is the VCF table of
// docs/research/02 (Measurable acceptance targets).
#include "FilterTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace acid;
using namespace acid::test::filter;
using Catch::Approx;
using dsp::DiodeLadder;
using dsp::DiodeLadderTrims;

namespace
{
constexpr double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

/** The published block model: half-value capacitor, ladder buffered from the output
    amplifier, a fixed 92 Hz coupler, the full feedback network. */
DiodeLadderTrims blockModelTrims()
{
    DiodeLadderTrims t;
    t.rbOhms = 17.3e3;          // 1 / (2 pi 17.3 k 0.1 uF) = 92.0 Hz
    t.capRatio = 2.0;
    t.loadScale = 0.0;
    t.kMax = 18.7;
    return t;
}

struct Worst { double db = 0.0, deg = 0.0; };

/** Largest deviation of the measured response from the model over freqs. */
Worst compare (const ImpulseResponse& ir, const LinearRates& rates, const std::vector<double>& freqs, bool warped)
{
    Worst w;
    for (double hz : freqs)
    {
        const Complex s = warped ? warpedS (hz, ir.fs) : analogS (hz);
        const LinearResponse model = linearResponse (rates, s);
        const Complex measured = ir.response (hz);
        w.db = std::max (w.db, std::abs (magDb (measured) - magDb (model.out)));
        w.deg = std::max (w.deg, std::abs (wrapDeg (phaseDeg (measured) - phaseDeg (model.out))));
    }
    return w;
}
}

//==============================================================================
TEST_CASE ("linear: small-signal response equals the exact trapezoidal model at every rate", "[filter][linear]")
{
    struct Config
    {
        const char* name;
        DiodeLadderTrims trims;
        std::vector<double> fRes, r;
        double seconds;
        double tolDb, tolDeg;
    };

    std::vector<Config> configs;
    configs.push_back ({ "default trims", DiodeLadderTrims(), { 100.0, 300.0, 1000.0, 3000.0, 10000.0 }, { 0.0, 0.5, 0.9 }, 3.0, 1.0e-6, 1.0e-5 });
    {
        DiodeLadderTrims t; t.rbOhms = 28.0e3;
        configs.push_back ({ "rb 28 k", t, { 500.0, 2000.0 }, { 0.7 }, 3.0, 1.0e-6, 1.0e-5 });
    }
    {
        DiodeLadderTrims t; t.capRatio = 1.0; t.kMax = 17.0;
        configs.push_back ({ "equal capacitors", t, { 1000.0 }, { 0.5 }, 3.0, 1.0e-6, 1.0e-5 });
    }
    configs.push_back ({ "block model", blockModelTrims(), { 975.0 }, { 0.6 }, 3.0, 1.0e-6, 1.0e-5 });
    // (A high-pass inside the loop has its closed-loop corner lowered by 1 + K, so the
    // ideal ladder gets a 30 Hz coupler here to keep the impulse response short.)
    configs.push_back ({ "ideal ladder", DiodeLadderTrims::idealLadder (10.0, 30.0), { 1000.0 }, { 1.0 }, 3.0, 1.0e-6, 1.0e-5 });
    {
        // Guard engaged: the sub-audio loop sits at a loop gain of 0.85, so its ring takes
        // seconds to die. Longer capture, looser bound.
        DiodeLadderTrims t; t.rbOhms = 35.0e3; t.kMax = 20.0;
        configs.push_back ({ "guard engaged, stock range", t, { 300.0 }, { 1.0 }, 10.0, 1.0e-3, 1.0e-2 });
    }
    configs.push_back ({ "guard engaged, high resonance", DiodeLadderTrims(), { 250.0 }, { 1.95 }, 10.0, 1.0e-3, 1.0e-2 });

    for (double fs : kRates)
    {
        DiodeLadder f;
        f.prepare (fs);
        double worstDb = 0.0, worstDeg = 0.0;

        for (const auto& c : configs)
        {
            f.setTrims (c.trims);
            for (double fRes : c.fRes)
                for (double r : c.r)
                {
                    // Up to 20 fRes: beyond that the response is under -100 dB and the
                    // comparison would measure the rounding floor of the capture.
                    const auto freqs = logSpace (5.0, std::min ({ 20000.0, 0.45 * fs, 20.0 * fRes }), 24);
                    const double bypass = f.subAudioBypass (r);
                    const auto ir = impulseResponse (f, fRes, r, c.seconds);
                    const auto rates = discreteRates (f.getTrims(), fRes, r, fs, bypass);
                    const Worst w = compare (ir, rates, freqs, true);
                    INFO (c.name << ", fs " << fs << ", fRes " << fRes << ", r " << r << ", bypass " << bypass);
                    CHECK (w.db < c.tolDb);
                    CHECK (w.deg < c.tolDeg);
                    if (c.tolDb < 1.0e-4)
                    {
                        worstDb = std::max (worstDb, w.db);
                        worstDeg = std::max (worstDeg, w.deg);
                    }
                }
        }
        std::printf ("[linear] fs %6.0f: worst deviation from the exact trapezoidal model %.2e dB, %.2e deg\n", fs, worstDb, worstDeg);
    }
}

TEST_CASE ("linear: guard states of the configurations above", "[filter][linear]")
{
    DiodeLadder f;
    f.prepare (192000.0);
    CHECK (f.subAudioBypass (1.0) == 0.0);              // stock range at default trims: untouched
    CHECK (f.subAudioBypass (1.25) == 0.0);             // the high-resonance mod engages it from about r 1.3
    CHECK (f.subAudioBypass (1.4) > 0.0);               // (1 % at 1.4, 4.5 % at 1.8, 5.7 % at 2.0)
    CHECK (f.subAudioBypass (1.8) > 0.0);
    CHECK (f.subAudioBypass (1.8) < 0.05);
    CHECK (f.subAudioBypass (1.95) > 0.0);
    f.setTrims (13.6e3, 18.7);
    CHECK (f.subAudioBypass (1.8) == 0.0);              // the lighter load of 04 section 1.4 never needs it below r 2
    CHECK (f.subAudioBypass (2.0) > 0.0);
    f.setTrims (DiodeLadderTrims());

    DiodeLadderTrims t;
    t.rbOhms = 35.0e3;
    t.kMax = 20.0;
    f.setTrims (t);
    CHECK (f.subAudioBypass (0.7) == 0.0);
    CHECK (f.subAudioBypass (1.0) > 0.0);
    CHECK (f.subAudioBypass (1.0) < 0.05);

    f.setTrims (DiodeLadderTrims::idealLadder());
    CHECK (f.subAudioBypass (0.5) == 1.0);              // no sub-audio network at all
}

//==============================================================================
TEST_CASE ("linear: response against the analogue transfer function (04 F1)", "[filter][linear]")
{
    // Trapezoidal integration warps frequency, and the ladder is prewarped so that the
    // resonant frequency is exact. Away from fRes the digital curve is the analogue one on a
    // stretched axis, so agreement is checked on the band where the stretch is small:
    // fRes up to fs/14 and f up to fs/24 at every rate. At 192 kHz (the 4x internal rate)
    // the document's full band, min (16 kHz, 4 fRes), is also checked, against a bound of
    // 0.7 dB instead of the document's 0.25: the warping error reaches 0.65 dB at the top
    // of the band for fRes of 4 to 5 kHz (0.78 dB at 176.4 kHz), which is a known deviation
    // from 04 F1 at 4x; 8x meets it. review_FilterTargets.cpp pins the size at both 4x
    // rates and both 8x rates. The error sits about 40 dB below the passband.
    const double fResList[] = { 100.0, 300.0, 1000.0, 3000.0, 4000.0, 5000.0, 10000.0 };
    const double rList[] = { 0.0, 0.5, 0.9 };

    for (double fs : kRates)
    {
        DiodeLadder f;
        f.prepare (fs);
        Worst inner, full;

        for (double fRes : fResList)
            for (double r : rList)
            {
                const auto ir = impulseResponse (f, fRes, r, 3.0);
                const auto rates = analogRates (f.getTrims(), fRes, r);
                INFO ("fs " << fs << ", fRes " << fRes << ", r " << r);

                if (fRes <= fs / 14.0)
                {
                    const auto freqs = logSpace (5.0, std::min ({ 16000.0, 4.0 * fRes, fs / 24.0 }), 40);
                    const Worst w = compare (ir, rates, freqs, false);
                    CHECK (w.db < 0.25);
                    CHECK (w.deg < 3.0);
                    inner.db = std::max (inner.db, w.db);
                    inner.deg = std::max (inner.deg, w.deg);
                }
                if (fs == 192000.0)
                {
                    const auto freqs = logSpace (5.0, std::min (16000.0, 4.0 * fRes), 40);
                    const Worst w = compare (ir, rates, freqs, false);
                    CHECK (w.db < 0.7);
                    CHECK (w.deg < 3.0);
                    full.db = std::max (full.db, w.db);
                    full.deg = std::max (full.deg, w.deg);
                }
            }

        std::printf ("[linear] fs %6.0f against the analogue model: %.3f dB, %.2f deg (f <= fs/24, fRes <= fs/14)", fs, inner.db, inner.deg);
        if (fs == 192000.0)
            std::printf ("; %.3f dB, %.2f deg over min (16 kHz, 4 fRes)", full.db, full.deg);
        std::printf ("\n");
    }
}

//==============================================================================
TEST_CASE ("linear: core shape, order and -3 dB point (02 F1 to F4)", "[filter][linear]")
{
    // Resonance 0, capacitors as built, ladder unloaded. The one remaining coupling corner
    // (a 20 Hz high-pass in series, which keeps the capture short) is divided out exactly.
    const double fs = 192000.0, fRes = 200.0, couplerHz = 20.0;
    DiodeLadder f;
    f.prepare (fs);
    DiodeLadderTrims t = DiodeLadderTrims::idealLadder (0.0, couplerHz);
    t.capRatio = 33.0 / 18.0;
    f.setTrims (t);
    const auto captured = impulseResponse (f, fRes, 0.0, 1.0);

    struct Core
    {
        const ImpulseResponse& ir;
        double wq;
        double db (double hz) const
        {
            const Complex s = warpedS (hz, ir.fs);
            return magDb (ir.response (hz) * (s + wq) / s);
        }
    };
    const Core ir { captured, 2.0 * fs * std::tan (kTwoPi * couplerHz / (2.0 * fs)) };

    const double dc = ir.db (1.0);
    CHECK (dc == Approx (0.0).margin (0.01));

    // 02 F2: magnitudes at 0.25, 0.5, 1, 2, 4 x fRes.
    const double mult[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
    const double target[] = { -8.5, -15.2, -24.6, -38.3, -57.0, -79.1, -102.6, -126.5 };
    double level[8];
    for (int i = 0; i < 8; ++i)
    {
        level[i] = ir.db (mult[i] * fRes) - dc;
        INFO ("f / fRes = " << mult[i] << ": " << level[i] << " dB");
        CHECK (level[i] == Approx (target[i]).margin (i < 5 ? 0.15 : 0.3));
    }

    // 02 F4: local slopes above the resonant frequency. 02 F1: the asymptote.
    CHECK (level[3] - level[2] == Approx (-13.7).margin (0.15));
    CHECK (level[4] - level[3] == Approx (-18.6).margin (0.15));
    CHECK (level[7] - level[6] == Approx (-24.0).margin (0.5));

    // 02 F3: the -3 dB point sits 3.25 octaves below the resonant frequency.
    double lo = 0.02 * fRes, hi = fRes;
    for (int i = 0; i < 40; ++i)
    {
        const double mid = std::sqrt (lo * hi);
        if (ir.db (mid) - dc > -3.0103) lo = mid; else hi = mid;
    }
    const double corner = std::sqrt (lo * hi) / fRes;
    std::printf ("[linear] -3 dB point at %.4f fRes (%.2f octaves below)\n", corner, -std::log2 (corner));
    CHECK (corner == Approx (0.1055).epsilon (0.02));
}

TEST_CASE ("linear: resonance ratio and critical gain constants", "[filter][linear]")
{
    CHECK (DiodeLadder::resonanceRatio (2.0) == Approx (std::sqrt (2.0)).epsilon (1e-12));
    CHECK (DiodeLadder::resonanceRatio (33.0 / 18.0) == Approx (1.3838).epsilon (1e-4));
    CHECK (DiodeLadder::resonanceRatio (1.0) == Approx (1.1952).epsilon (1e-4));
    CHECK (DiodeLadder::criticalGain (2.0) == Approx (17.0).epsilon (1e-12));
    CHECK (DiodeLadder::criticalGain (33.0 / 18.0) == Approx (17.019).epsilon (1e-4));
    CHECK (DiodeLadder::criticalGain (1.0) == Approx (18.388).epsilon (1e-4));

    // 02 F6: 64.5 Hz per microamp at 300 K; the build uses 25 C (64.9).
    const double vt300 = 1.380649e-23 * 300.0 / 1.602176634e-19;
    const double hzPerAmp = DiodeLadder::resonanceRatio (33.0 / 18.0) / (8.0 * kPi * DiodeLadder::kLadderFarads * vt300);
    CHECK (hzPerAmp * 1.0e-6 == Approx (64.5).epsilon (0.01));
    const double hzPerAmp25 = DiodeLadder::resonanceRatio (33.0 / 18.0) / (8.0 * kPi * DiodeLadder::kLadderFarads * 0.5 * DiodeLadder::kTwoVt);
    CHECK (hzPerAmp25 * 1.0e-6 == Approx (DiodeLadder::kHzPerMicroamp).epsilon (0.002));

    CHECK (DiodeLadder::wiperFraction (0.5) == Approx (0.445).margin (0.001));
    CHECK (DiodeLadder::wiperFraction (0.0) == 0.0);
    CHECK (DiodeLadder::wiperFraction (1.0) == 1.0);
}

//==============================================================================
TEST_CASE ("linear: step response slowest time constant (04 F9)", "[filter][linear]")
{
    const double fs = 192000.0, fRes = 1000.0;
    for (double rho : { 2.0, 33.0 / 18.0 })
    {
        DiodeLadder f;
        f.prepare (fs);
        DiodeLadderTrims t = DiodeLadderTrims::idealLadder (0.0, 1.0e-3);
        t.capRatio = rho;
        f.setTrims (t);

        // Slowest pole of D (s) = (s^4 + 6 s^3 + 10 s^2 + 4 s) / rho + s^3 + 5 s^2 + 6 s + 1.
        double p = -0.15;
        for (int i = 0; i < 50; ++i)
        {
            const double d = (p * p * p * p + 6.0 * p * p * p + 10.0 * p * p + 4.0 * p) / rho + p * p * p + 5.0 * p * p + 6.0 * p + 1.0;
            const double dd = (4.0 * p * p * p + 18.0 * p * p + 20.0 * p + 4.0) / rho + 3.0 * p * p + 10.0 * p + 6.0;
            p -= d / dd;
        }
        const double w0 = kTwoPi * fRes / DiodeLadder::resonanceRatio (rho);
        const double tauExpected = 1.0 / (-p * w0);

        const double step = 1.0e-4;
        const auto n = static_cast<std::size_t> (0.02 * fs);
        std::vector<double> y (n);
        for (std::size_t i = 0; i < n; ++i) y[i] = f.process (step, fRes, 0.0);

        // Fit ln (step - y) over 2 to 5 time constants, where only the slowest pole is left.
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, count = 0.0;
        for (std::size_t i = static_cast<std::size_t> (2.0 * tauExpected * fs); i < static_cast<std::size_t> (5.0 * tauExpected * fs); ++i)
        {
            const double tSec = static_cast<double> (i + 1) / fs;
            const double v = std::log (step - y[i]);
            sx += tSec; sy += v; sxx += tSec * tSec; sxy += tSec * v; count += 1.0;
        }
        const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
        const double tau = -1.0 / slope;
        std::printf ("[linear] rho %.4f: slowest pole %.4f w0, time constant %.4f ms (expected %.4f ms)\n", rho, -p, tau * 1e3, tauExpected * 1e3);
        CHECK (-p == Approx (rho == 2.0 ? 0.152 : 0.149).margin (0.0006));
        CHECK (tau == Approx (tauExpected).epsilon (0.01));
    }
}

//==============================================================================
TEST_CASE ("linear: overlay on the published 10-pole block model (04 F8)", "[filter][linear]")
{
    // The published closed form, constants in rad/s, with its input-coupling block divided
    // out (the voice owns that block). Rendered at 768 kHz so that trapezoidal warping
    // at 20 kHz stays under 0.1 dB.
    const double fs = 768000.0;
    const double fc = 820.0;
    const double fRes = fc * std::pow (2.0, 0.25);

    const auto published = [fc] (double hz, double k)
    {
        const Complex s (0.0, kTwoPi * hz);
        const Complex x = s / (kTwoPi * fc);
        const Complex d4 = x * x * x * x + std::pow (2.0, 11.0 / 4.0) * x * x * x + 10.0 * std::sqrt (2.0) * x * x
                           + std::pow (2.0, 13.0 / 4.0) * x + 1.0;
        const Complex num = 1.06 * s * s * s * (s + 109.9) * (s + 34.0) * (s + 7.41);
        const Complex den = d4 * (s + 97.5) * (s + 38.5) * (s + 4.45) * (s + 578.1) * (s + 20.0) * (s + 7.41)
                            + 18.7 * k * s * s * s * s * (s + 46.5) * (s + 4.40);
        // Input block, unity high-frequency gain: HP (0.708) and two lead-lag pairs.
        const Complex inputBlock = (s / (s + kTwoPi * 0.708)) * ((s + kTwoPi * 17.495) / (s + kTwoPi * 15.517))
                                   * ((s + kTwoPi * 5.407) / (s + kTwoPi * 6.164));
        return num / den / inputBlock;
    };

    DiodeLadder f;
    f.prepare (fs);
    f.setTrims (blockModelTrims());
    const auto freqs = logSpace (2.0, 20000.0, 48);
    double worst = 0.0;

    for (double k : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 })
    {
        REQUIRE (f.subAudioBypass (k) == 0.0);
        const auto ir = impulseResponse (f, fRes, k, 4.0);
        double worstK = 0.0;
        for (double hz : freqs)
        {
            const double ours = magDb (1.06 * ir.potTopResponse (hz));
            const double ref = magDb (published (hz, k));
            worstK = std::max (worstK, std::abs (ours - ref));
        }
        INFO ("k = " << k);
        CHECK (worstK < 0.2);
        worst = std::max (worst, worstK);
    }
    std::printf ("[linear] block model overlay, 2 Hz to 20 kHz, k = 0..1: worst %.3f dB\n", worst);
}

//==============================================================================
TEST_CASE ("linear: passband drop with resonance (02 F7, F8; 04 section 1.2)", "[filter][linear]")
{
    const double fs = 192000.0;
    DiodeLadder f;
    f.prepare (fs);

    SECTION ("ideal ladder: DC gain is 1 / (1 + K)")
    {
        // Measured on the plateau between the coupler (5 Hz) and the first ladder pole
        // (4.3 kHz at fRes 40 kHz). K stays under the critical 17.
        f.setTrims (DiodeLadderTrims::idealLadder (18.7, 5.0));
        for (double r : { 0.0, 0.25, 0.5, 0.85 })
        {
            const auto ir = impulseResponse (f, 40000.0, r, 5.0);
            const double expected = -20.0 * std::log10 (1.0 + 18.7 * r);
            INFO ("r = " << r);
            CHECK (ir.db (300.0) == Approx (expected).margin (0.03));
        }
        CHECK (-20.0 * std::log10 (1.0 + 18.7) == Approx (-25.9).margin (0.05));
    }

    SECTION ("block model reproduces the published levels at 200 Hz")
    {
        f.setTrims (blockModelTrims());
        const double fRes = 820.0 * std::pow (2.0, 0.25);
        const double rot[] = { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 };
        const double target[] = { -7.3, -14.3, -18.6, -21.6, -23.8, -25.6 };
        for (int i = 0; i < 6; ++i)
        {
            const auto ir = impulseResponse (f, fRes, rot[i], 3.0);
            INFO ("k = " << rot[i]);
            CHECK (magDb (1.06 * ir.response (200.0)) == Approx (target[i]).margin (0.1));
            if (i == 0)
            {
                // 02 F8: zero resonance, broad maximum near 100 Hz.
                CHECK (magDb (1.06 * ir.response (110.0)) == Approx (-5.1).margin (0.2));
                CHECK (magDb (1.06 * ir.response (65.4)) == Approx (-5.6).margin (0.3));
                const Peak pk = findPeak (ir, 40.0, 400.0);
                CHECK (pk.hz > 70.0);
                CHECK (pk.hz < 140.0);
                CHECK (magDb (Complex (1.06, 0.0)) + pk.db == Approx (-5.0).margin (0.5));
            }
        }
    }

    SECTION ("default calibration stays within 1.5 dB of the same levels")
    {
        // Oscillator to test point: input divider 0.0099, output amplifier 106.
        const double scale = DiodeLadder::kInputDivider * DiodeLadder::kOutputAmpGain;
        const double rot[] = { 0.2, 0.4, 0.6, 0.8, 1.0 };
        const double target[] = { -14.3, -18.6, -21.6, -23.8, -25.6 };
        for (int i = 0; i < 5; ++i)
        {
            const auto ir = impulseResponse (f, 975.0, rot[i], 3.0);
            const double level = magDb (scale * ir.response (200.0));
            std::printf ("[linear] default trims, fRes 975 Hz, r %.1f: %.2f dB at 200 Hz (block model %.1f)\n", rot[i], level, target[i]);
            CHECK (level == Approx (target[i]).margin (1.5));
        }
        // 02 F8 at the default trims: -5 dB at 110 Hz and -7.3 dB at 200 Hz, +-1.5 dB. The
        // ladder is loaded by the output amplifier, so it sits 0.6 to 0.8 dB under the
        // unloaded block model the targets were written from.
        const auto ir0 = impulseResponse (f, 975.0, 0.0, 3.0);
        const double at110 = magDb (scale * ir0.response (110.0)), at200 = magDb (scale * ir0.response (200.0));
        std::printf ("[linear] default trims, r 0: %.2f dB at 110 Hz, %.2f dB at 200 Hz (02 F8: -5 and -7.3, +-1.5)\n", at110, at200);
        CHECK (at110 == Approx (-5.0).margin (1.5));
        CHECK (at200 == Approx (-7.3).margin (1.5));
    }
}

//==============================================================================
TEST_CASE ("linear: the pot-top node is the output through the 3.18 Hz coupler", "[filter][linear]")
{
    for (double fs : { 44100.0, 192000.0 })
    {
        DiodeLadder f;
        f.prepare (fs);
        const auto ir = impulseResponse (f, 800.0, 0.6, 4.0);
        const double wp = 2.0 * fs * std::tan (kPi * 3.18 / fs);
        for (double hz : { 1.0, 3.18, 10.0, 100.0, 5000.0 })
        {
            const Complex s = warpedS (hz, fs);
            const Complex expected = s / (s + wp);
            const Complex measured = ir.potTopResponse (hz) / ir.response (hz);
            INFO ("fs " << fs << ", " << hz << " Hz");
            CHECK (std::abs (measured - expected) < 1.0e-6);
        }
        // Corner: -3.01 dB at 3.18 Hz.
        CHECK (magDb (ir.potTopResponse (3.18) / ir.response (3.18)) == Approx (-3.0103).margin (0.001));
    }
}

//==============================================================================
TEST_CASE ("linear: sub-audio peak of the default calibration (02 F11)", "[filter][linear]")
{
    DiodeLadder f;
    f.prepare (48000.0);
    const double rot[] = { 0.6, 0.8, 1.0 };
    double bump[3] {}, freq[3] {};
    for (int i = 0; i < 3; ++i)
    {
        const auto ir = impulseResponse (f, 975.0, rot[i], 8.0);
        const Peak low = findPeak (ir, 4.0, 30.0);
        bump[i] = low.db - ir.db (200.0);
        freq[i] = low.hz;
        std::printf ("[linear] sub-audio peak, default trims, r %.1f: %.1f dB above the 200 Hz plateau at %.1f Hz\n", rot[i], bump[i], low.hz);
        CHECK (low.hz > 8.0);
        CHECK (low.hz < 13.0);
    }
    // 02 F11 gives the unloaded block model's 10 to 15 dB, with "present, stable" as the
    // tolerance. The bump grows 7 dB from 60 to 100 % resonance at any ladder load, so no
    // trim puts all three readings inside that 5 dB band: at the default 20 k they are
    // 10.2, 13.7 and 17.2 dB (at 13.6 k: 6.8, 9.5 and 12.1), so the full-resonance bump
    // stands 2 dB above the block model's range. Pinned at the measured size; the guard
    // test above shows the loop is nowhere near the sub-audio oscillation F11 forbids.
    CHECK (bump[0] == Approx (10.2).margin (0.5));
    CHECK (bump[1] == Approx (13.7).margin (0.5));
    CHECK (bump[2] == Approx (17.2).margin (0.5));
    CHECK (freq[0] > freq[1]);
    CHECK (freq[1] > freq[2]);
}
