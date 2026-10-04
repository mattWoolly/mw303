// Square: the Q8 shaper model against the research tables and against an independent
// brute-force simulation of the same circuit.
#include "Fft.h"
#include "OscTestSupport.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
struct TableRow { const char* name; double hz; double duty; };

// docs/research/02, section 2.5 and acceptance target V6 (simulated there with a straight
// ramp from 11.5 V, beta 300).
const TableRow kDutyTable[] = {
    { "C1", kC1, 0.57 }, { "C2", kC2, 0.50 }, { "A2", kA2, 0.46 },
    { "C3", kC3, 0.45 }, { "C4", kC4, 0.41 }, { "C5", kC5, 0.35 },
};

/** Samples of one settled high part: from the turn-on crossing to the reset crossing. */
struct HighPart
{
    double start = 0.0, end = 0.0;      // fractional sample positions of the mid crossings
    double peak = 0.0;                  // highest level after turn-on
    double beforeReset = 0.0;           // level a few samples before the reset edge
    double low = 0.0;
};

HighPart lastHighPart (const std::vector<double>& sq)
{
    const auto shape = measureSquare (sq);
    HighPart h;
    h.low = dsp::SquareShaper::kCollectorRailVolts;
    REQUIRE (shape.rise.size() > 3);
    REQUIRE (shape.fall.size() > 3);
    // last complete rise -> fall pair
    std::size_t f = shape.fall.size() - 1;
    std::size_t r = shape.rise.size() - 1;
    while (r > 0 && shape.rise[r] > shape.fall[f]) --r;
    h.start = shape.rise[r];
    h.end = shape.fall[f];
    const auto a = static_cast<std::size_t> (h.start), b = static_cast<std::size_t> (h.end);
    for (std::size_t i = a; i + 6 < b; ++i) h.peak = std::max (h.peak, sq[i]);
    h.beforeReset = sq[b - 6];
    return h;
}
}

TEST_CASE ("square: duty cycle against the research table", "[osc][square][duty]")
{
    Report rep ("osc-square-duty.txt");
    rep.line ("Square duty (fraction of the period high), default trims (saw top 11.3 V, beta 300)");
    rep.line ("note      Hz   table   192 kHz  176.4 kHz    96 kHz");

    for (double rate : { 192000.0, 176400.0 })
    {
        double last = 1.0;
        for (const auto& row : kDutyTable)
        {
            const auto r = renderSettled (rate, row.hz, 8.0 / row.hz + 0.02);
            const double duty = measureSquare (r.square).duty;
            INFO (row.name << " at " << rate << ": " << duty << " (table " << row.duty << ")");
            CHECK (std::abs (duty - row.duty) <= 0.06);     // the document's tolerance
            CHECK (std::abs (duty - row.duty) <= 0.03);     // and what the model actually holds
            CHECK (duty < last);                            // strictly decreasing with pitch
            last = duty;
        }
        const auto c6 = renderSettled (rate, kC6, 0.05);
        CHECK (measureSquare (c6.square).duty < last);
    }

    for (const auto& row : kDutyTable)
    {
        const double seconds = 8.0 / row.hz + 0.02;
        rep.line ("%-4s %7.2f   %.2f    %.4f    %.4f    %.4f", row.name, row.hz, row.duty,
                  measureSquare (renderSettled (192000.0, row.hz, seconds).square).duty,
                  measureSquare (renderSettled (176400.0, row.hz, seconds).square).duty,
                  measureSquare (renderSettled (96000.0, row.hz, seconds).square).duty);
    }
}

TEST_CASE ("square: with the table's own saw top (11.5 V) the table is reproduced", "[osc][square][duty]")
{
    dsp::OscillatorParams p;
    p.sawTopVolts = 11.5;
    for (const auto& row : kDutyTable)
    {
        const auto r = renderSettled (192000.0, row.hz, 8.0 / row.hz + 0.02, p);
        const double duty = measureSquare (r.square).duty;
        INFO (row.name << ": " << duty << " (table " << row.duty << ")");
        CHECK (std::abs (duty - row.duty) <= 0.012);
    }
}

TEST_CASE ("square: duty matches a brute-force simulation of the circuit", "[osc][square][reference]")
{
    // Target O3: within 0.005 of the same circuit simulated at more than 30 times the rate
    // with no band-limiting.
    Report rep ("osc-square-reference.txt");
    rep.line ("note      Hz   model duty  reference duty   model Ve range     reference Ve range");
    const double rate = 192000.0;
    const double refRate = rate * 32.0;

    struct Case { const char* name; double hz; };
    for (const Case c : { Case { "C1", kC1 }, Case { "C2", kC2 }, Case { "A2", kA2 }, Case { "C4", kC4 }, Case { "C5", kC5 }, Case { "A5", 880.0 } })
    {
        dsp::Oscillator osc;
        osc.prepare (rate);
        const double seconds = 0.35;
        double veMin = 1.0e9, veMax = -1.0e9;
        std::vector<double> sq;
        const int total = static_cast<int> (seconds * rate);
        const int from = total - static_cast<int> (6.0 * rate / c.hz);
        for (int i = 0; i < total; ++i)
        {
            const auto o = osc.tick (c.hz);
            if (i >= from)
            {
                sq.push_back (o.square);
                veMin = std::min (veMin, osc.getShaper().emitterVolts());
                veMax = std::max (veMax, osc.getShaper().emitterVolts());
            }
        }
        const double duty = measureSquare (sq).duty;
        const auto ref = referenceShaper (osc, c.hz, dsp::SquareShaper::kDefaultBeta, seconds, refRate);

        rep.line ("%-4s %7.2f     %.4f        %.4f      %.3f to %.3f     %.3f to %.3f", c.name, c.hz, duty, ref.duty,
                  veMin, veMax, ref.emitterMin, ref.emitterMax);
        INFO (c.name << ": model " << duty << " reference " << ref.duty);
        CHECK (std::abs (duty - ref.duty) < 0.005);
        CHECK (veMin == Approx (ref.emitterMin).margin (0.01));
        CHECK (veMax == Approx (ref.emitterMax).margin (0.01));
    }
}

TEST_CASE ("square: duty does not depend on the sample rate", "[osc][square][rates]")
{
    for (double hz : { kC2, kC4, kC5 })
    {
        const double reference = measureSquare (renderSettled (384000.0, hz, 0.15).square).duty;
        for (double rate : { 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            const double duty = measureSquare (renderSettled (rate, hz, 0.15).square).duty;
            INFO (hz << " Hz at " << rate << ": " << duty << " vs " << reference);
            CHECK (std::abs (duty - reference) < 0.003);
        }
        // Even with no oversampling at all the circuit model holds its duty.
        for (double rate : { 44100.0, 48000.0 })
            CHECK (std::abs (measureSquare (renderSettled (rate, hz, 0.15).square).duty - reference) < 0.01);
    }
}

TEST_CASE ("square: levels, polarity and droop", "[osc][square][levels]")
{
    const double rate = 192000.0;
    const double rail = dsp::SquareShaper::kCollectorRailVolts;
    Report rep ("osc-square-levels.txt");
    rep.line ("note   low     peak    before reset   p-p    droop / p-p");

    struct Case { const char* name; double hz; };
    double droopC2 = 0.0, droopC5 = 0.0;
    for (const Case c : { Case { "C1", kC1 }, Case { "C2", kC2 }, Case { "A2", kA2 }, Case { "C4", kC4 }, Case { "C5", kC5 } })
    {
        const auto r = renderSettled (rate, c.hz, 6.0 / c.hz + 0.01);
        const auto h = lastHighPart (r.square);
        const double pp = h.peak - rail;
        const double droop = (h.peak - h.beforeReset) / pp;
        rep.line ("%-4s  %.3f   %.3f      %.3f      %.2f     %.3f", c.name, rail, h.peak, h.beforeReset, pp, droop);

        // Low and flat at the 5.333 V rail, high at 8.5 to 9 V: 3.2 to 3.7 V peak to peak.
        CHECK (h.peak > 8.3);
        CHECK (h.peak < 9.2);
        CHECK (pp > 3.0);
        CHECK (pp < 3.9);
        if (c.hz == kC2) droopC2 = droop;
        if (c.hz == kC5) droopC5 = droop;

        // The low level is flat within 1 % of peak to peak (clear of the edges).
        const auto shape = measureSquare (r.square);
        const std::size_t fallAt = static_cast<std::size_t> (shape.fall[1]);
        const std::size_t riseAt = static_cast<std::size_t> (shape.rise[shape.rise[1] > shape.fall[1] ? 1 : 2]);
        REQUIRE (riseAt > fallAt + 20);
        double lowMin = 1.0e9, lowMax = -1.0e9;
        const std::size_t lowEnd = fallAt + (riseAt - fallAt) * 7 / 10;     // before the knee starts
        for (std::size_t i = fallAt + 8; i < lowEnd; ++i)
        {
            lowMin = std::min (lowMin, r.square[i]);
            lowMax = std::max (lowMax, r.square[i]);
        }
        CHECK (lowMax - lowMin < 0.01 * pp);
        CHECK (lowMin == Approx (rail).margin (0.01 * pp));
    }

    // The top droops as C11 discharges: a quarter of the swing at C2, almost nothing at C5.
    CHECK (droopC2 == Approx (0.25).margin (0.08));
    CHECK (droopC5 < 0.05);
    CHECK (droopC5 >= 0.0);

    // Polarity: the square is high during the LAST part of each falling ramp and drops when
    // the saw resets. So the square's falling edge coincides with the saw's rising edge.
    const auto r = renderSettled (rate, kA2, 0.1);
    const auto sawRise = crossings (r.saw, 8.4, true);
    const auto sqFall = crossings (r.square, 7.0, false);
    REQUIRE (sawRise.size() > 5);
    REQUIRE (sqFall.size() > 5);
    for (std::size_t k = 1; k + 1 < std::min (sawRise.size(), sqFall.size()); ++k)
    {
        const auto nearest = std::lower_bound (sqFall.begin(), sqFall.end(), sawRise[k] - 2.0);
        REQUIRE (nearest != sqFall.end());
        CHECK (std::abs (*nearest - sawRise[k]) < 1.0);
    }
}

TEST_CASE ("shaper: the emitter capacitor's two time constants", "[osc][square][droop]")
{
    // Driven with a constant input the two-state model reduces to one RC each way.
    const double rate = 192000.0;
    const auto timeConstant = [rate] (double sawVolts, double startSeconds, double stepSeconds, double& asymptote)
    {
        dsp::SquareShaper shaper;
        shaper.prepare (rate);
        const int i0 = static_cast<int> (startSeconds * rate), step = static_cast<int> (stepSeconds * rate);
        double y[3] = {};
        for (int i = 0; i <= i0 + 2 * step; ++i)
        {
            shaper.advance (1.0, sawVolts);
            if (i == i0) y[0] = shaper.emitterVolts();
            if (i == i0 + step) y[1] = shaper.emitterVolts();
            if (i == i0 + 2 * step) y[2] = shaper.emitterVolts();
        }
        // three equally spaced points of an exponential give its time constant and asymptote
        const double ratio = (y[2] - y[1]) / (y[1] - y[0]);
        asymptote = y[0] + (y[1] - y[0]) / (1.0 - ratio);
        return -(static_cast<double> (step) / rate) / std::log (ratio);
    };

    // Off (input far above the emitter): C11 charges towards +12 V through R45, 22 ms.
    double target = 0.0;
    const double tauOff = timeConstant (14.0, 0.002, 0.010, target);
    CHECK (tauOff == Approx (22.0e-3).epsilon (0.01));
    CHECK (target == Approx (12.0).margin (0.02));

    // Saturated (input held low): C11 discharges into R36 and, through the base, into R35,
    // against R45. 1 uF * (22k || 10k || 100k) = 6.43 ms; the research's 6.9 ms leaves the
    // base path out. The level it heads for is near 7.4 V.
    const double tauOn = timeConstant (6.5, 0.004, 0.006, target);
    CHECK (tauOn == Approx (6.43e-3).epsilon (0.04));
    CHECK (target > 7.0);
    CHECK (target < 7.8);
    INFO ("tau off " << tauOff * 1000.0 << " ms, tau on " << tauOn * 1000.0 << " ms towards " << target << " V");
    CHECK (tauOn < tauOff);
}

TEST_CASE ("square: one sharp edge, one soft edge whose width follows the period", "[osc][square][edges]")
{
    // Measured at 8 x 48 kHz so the edges are resolved.
    const double rate = 384000.0;
    const double rail = dsp::SquareShaper::kCollectorRailVolts;
    Report rep ("osc-square-edges.txt");
    rep.line ("note   turn-on 10-90 %% (us)   %% of period   reset edge 90-10 %% (us)");

    struct Case { const char* name; double hz; double lo, hi; };
    for (const Case c : { Case { "C1", kC1, 4.0, 12.0 }, Case { "C2", kC2, 0.9, 1.9 }, Case { "C3", kC3, 0.6, 1.4 },
                          Case { "C4", kC4, 0.6, 1.4 }, Case { "C5", kC5, 0.6, 1.4 }, Case { "C6", kC6, 0.6, 1.4 } })
    {
        const auto r = renderSettled (rate, c.hz, 6.0 / c.hz + 0.01);
        const auto h = lastHighPart (r.square);
        const double pp = h.peak - rail;
        const auto up10 = crossings (r.square, rail + 0.1 * pp, true);
        const auto up90 = crossings (r.square, rail + 0.9 * pp, true);
        const auto dn90 = crossings (r.square, rail + 0.9 * (h.beforeReset - rail), false);
        const auto dn10 = crossings (r.square, rail + 0.1 * (h.beforeReset - rail), false);
        REQUIRE (up10.size() > 2);
        REQUIRE (up90.size() > 2);
        REQUIRE (dn10.size() > 2);

        // last turn-on: the 10 % crossing just before the last 90 % crossing
        const double t90 = up90.back();
        const auto it10 = std::upper_bound (up10.begin(), up10.end(), t90);
        REQUIRE (it10 != up10.begin());
        const double riseSeconds = (t90 - *(it10 - 1)) / rate;
        const double risePercent = 100.0 * riseSeconds * c.hz;

        // last reset: the 90 % crossing just before the last 10 % crossing
        const double t10 = dn10.back();
        const auto it90 = std::upper_bound (dn90.begin(), dn90.end(), t10);
        REQUIRE (it90 != dn90.begin());
        const double fallSeconds = (t10 - *(it90 - 1)) / rate;

        rep.line ("%-4s        %8.1f          %5.2f               %5.1f", c.name, riseSeconds * 1.0e6, risePercent, fallSeconds * 1.0e6);
        INFO (c.name << " turn-on " << risePercent << " % of the period, reset " << fallSeconds * 1.0e6 << " us");
        CHECK (risePercent >= c.lo);
        CHECK (risePercent <= c.hi);
        CHECK (fallSeconds < 40.0e-6);
    }
}

TEST_CASE ("square: second harmonic follows the duty cycle", "[osc][square][harmonics]")
{
    // Target V9 (derived with an 11.5 V saw top): H2/H1 at the oscillator node, +-4 dB.
    const double rate = 192000.0;
    Report rep ("osc-square-harmonics.txt");
    rep.line ("note    H2/H1 (dB)   target");

    struct Case { const char* name; double hz; double target; double tol; };
    for (const Case c : { Case { "C1", kC1, -14.0, 4.0 }, Case { "A2", kA2, -18.0, 4.0 }, Case { "C4", kC4, -11.0, 4.0 }, Case { "C5", kC5, -7.0, 4.0 } })
    {
        const auto r = renderSettled (rate, c.hz, 1.4);
        const double h1 = toneAmplitude (r.square, rate, c.hz);
        const double h2 = toneAmplitude (r.square, rate, 2.0 * c.hz);
        const double db = 20.0 * std::log10 (h2 / h1);
        rep.line ("%-4s     %6.1f      %6.1f", c.name, db, c.target);
        CHECK (db == Approx (c.target).margin (c.tol));
    }

    // At C2 the duty is close to one half, so the second harmonic is weak and set by the
    // droop of the top; the research has -21 dB.
    const auto r = renderSettled (rate, kC2, 1.4);
    const double db = 20.0 * std::log10 (toneAmplitude (r.square, rate, 2.0 * kC2) / toneAmplitude (r.square, rate, kC2));
    rep.line ("C2       %6.1f       -21.0", db);
    CHECK (db < -15.0);
    CHECK (db > -32.0);
}

TEST_CASE ("square and saw sit at their documented relative levels", "[osc][levels]")
{
    // Target V10: square p-p / saw p-p = 0.58 +- 0.08, fundamentals equal within 1.5 dB.
    const double rate = 192000.0;
    Report rep ("osc-levels.txt");
    rep.line ("note   saw p-p   square p-p   ratio   saw H1 (V)   square H1 (V)   difference (dB)");

    struct Case { const char* name; double hz; };
    for (const Case c : { Case { "C2", kC2 }, Case { "A2", kA2 }, Case { "C4", kC4 } })
    {
        const auto r = renderSettled (rate, c.hz, 1.4);
        const auto h = lastHighPart (r.square);
        const double squarePp = h.peak - dsp::SquareShaper::kCollectorRailVolts;
        const double sawPp = 11.3 - 5.5;
        const double sawH1 = toneAmplitude (r.saw, rate, c.hz), squareH1 = toneAmplitude (r.square, rate, c.hz);
        const double db = 20.0 * std::log10 (squareH1 / sawH1);
        rep.line ("%-4s    %.2f       %.2f       %.3f     %.3f         %.3f           %+.2f", c.name, sawPp, squarePp, squarePp / sawPp, sawH1, squareH1, db);

        CHECK (squarePp / sawPp == Approx (0.58).margin (0.08));
        CHECK (std::abs (db) < 1.5);
        // An ideal 5.8 V saw has a fundamental of 5.8 / pi = 1.846 V.
        CHECK (sawH1 == Approx (5.8 / kPi).epsilon (0.03));
    }

    // Mean levels, for pre-charging the coupling capacitor in front of the filter.
    for (double hz : { kC1, kC2, kC4, kC6 })
    {
        const auto r = renderSettled (rate, hz, 20.0 / hz);
        const auto shape = measureSquare (r.square);
        const auto a = static_cast<std::size_t> (shape.rise[1]), b = static_cast<std::size_t> (shape.rise[shape.rise.size() - 2]);
        double sum = 0.0;
        for (std::size_t i = a; i < b; ++i) sum += r.square[i];
        const double mean = sum / static_cast<double> (b - a);
        rep.line ("square mean at %.1f Hz: %.3f V", hz, mean);
        CHECK (mean == Approx (dsp::Oscillator::kSquareTypicalMeanVolts).margin (0.35));
    }

    // In the filter's units (2 V_T = 51.4 mV after the 1/101 divider) the saw peaks near
    // +-0.56 and the square near +-0.33, as the research expects.
    const double scale = dsp::Oscillator::kFilterInputDivider / 0.0514;
    CHECK (dsp::Oscillator::kFilterInputDivider == Approx (1.0 / 101.0));
    CHECK (0.5 * 5.8 * scale == Approx (0.56).margin (0.03));
    const auto r = renderSettled (rate, kA2, 0.1);
    const auto h = lastHighPart (r.square);
    CHECK (0.5 * (h.peak - dsp::SquareShaper::kCollectorRailVolts) * scale == Approx (0.33).margin (0.04));
}

TEST_CASE ("square: the threshold settles within tens of milliseconds after a pitch jump", "[osc][square][blip]")
{
    const double rate = 192000.0;
    Report rep ("osc-square-blip.txt");
    rep.line ("Duty of the first cycles after a legato pitch jump, and how long it takes to settle");

    struct Jump { const char* name; double from, to; bool twoOctaves; };
    for (const Jump j : { Jump { "C2 to C3", kC2, kC3, false }, Jump { "C3 to C2", kC3, kC2, false },
                          Jump { "C3 to C4", kC3, kC4, false }, Jump { "C4 to C3", kC4, kC3, false },
                          Jump { "C4 to C5", kC4, kC5, false }, Jump { "C5 to C4", kC5, kC4, false },
                          Jump { "C2 to C4", kC2, kC4, true }, Jump { "C4 to C2", kC4, kC2, true },
                          Jump { "C1 to C3", kC1, kC3, true }, Jump { "C3 to C5", kC3, kC5, true } })
    {
        dsp::Oscillator osc;
        osc.prepare (rate);
        for (int i = 0; i < static_cast<int> (0.5 * rate); ++i) osc.tick (j.from);

        // After the jump: the square, and the emitter voltage averaged over each cycle.
        const int n = static_cast<int> (0.4 * rate);
        const int period = static_cast<int> (std::lround (rate / j.to));
        std::vector<double> square (static_cast<std::size_t> (n)), emitterMeans;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            square[static_cast<std::size_t> (i)] = osc.tick (j.to).square;
            sum += osc.getShaper().emitterVolts();
            if ((i + 1) % period == 0)
            {
                emitterMeans.push_back (sum / period);
                sum = 0.0;
            }
        }

        std::vector<double> when;
        const auto duty = dutyPerCycle (square, 7.0, &when);
        REQUIRE (duty.size() > 12);
        const double settled = duty.back();

        // Time after which every later cycle is within the tolerance of the settled duty.
        const auto settleTime = [&] (double tolerance)
        {
            double t = 0.0;
            for (std::size_t k = 0; k < duty.size(); ++k)
                if (std::abs (duty[k] - settled) > tolerance)
                    t = when[std::min (k + 1, when.size() - 1)] / rate;
            return t;
        };
        const double coarse = settleTime (0.005), fine = settleTime (0.002);

        double blip = 0.0;
        for (std::size_t k = 0; k < 8; ++k) blip = std::max (blip, std::abs (duty[k] - settled));

        // Time constant of the threshold itself: the cycle-averaged emitter voltage relaxes
        // exponentially to its new level.
        const double rest = emitterMeans.back();
        const std::size_t k2 = std::max<std::size_t> (1, static_cast<std::size_t> (0.010 * j.to));
        const double tau = static_cast<double> (k2) * period / rate
                           / std::log ((emitterMeans[0] - rest) / (emitterMeans[k2] - rest));

        rep.line ("%s: %.3f %.3f %.3f %.3f %.3f %.3f ... %.3f; within 0.005 after %4.1f ms, within 0.002 after %4.1f ms; threshold time constant %.1f ms",
                  j.name, duty[0], duty[1], duty[2], duty[3], duty[4], duty[5], settled, coarse * 1000.0, fine * 1000.0, tau * 1000.0);
        INFO (j.name);

        // The research (target V11): within 0.005 of settled after 30 ms give or take 15.
        CHECK (coarse < 0.045);
        CHECK (fine < 0.045);
        // It relaxes with a time constant between the on (6.4 ms) and off (22 ms) values of
        // C11, weighted by the duty; a few ms either side for the crude fit.
        CHECK (tau > 0.003);
        CHECK (tau < 0.022);

        if (j.twoOctaves)
        {
            CHECK (blip > 0.008);           // a visible blip
            CHECK (fine > 0.004);           // that takes more than a couple of cycles to settle
        }
    }

    // Target V11 in particular: C2 to C4 dips below its settled value on the first cycles.
    dsp::Oscillator osc;
    osc.prepare (rate);
    for (int i = 0; i < static_cast<int> (0.5 * rate); ++i) osc.tick (kC2);
    const auto after = render (osc, kC4, static_cast<int> (0.3 * rate));
    const auto duty = dutyPerCycle (after.square, 7.0);
    REQUIRE (duty.size() > 12);
    double lowest = 1.0;
    for (std::size_t k = 0; k < 6; ++k) lowest = std::min (lowest, duty[k]);
    CHECK (lowest < duty.back() - 0.005);
}

TEST_CASE ("square: trims", "[osc][square][trim]")
{
    const double rate = 192000.0;

    SECTION ("beta moves the duty by less than 0.015 between 200 and 400")
    {
        for (double hz : { kC2, kC4 })
        {
            dsp::OscillatorParams lo, hi;
            lo.shaperBeta = 200.0;
            hi.shaperBeta = 400.0;
            const double a = measureSquare (renderSettled (rate, hz, 0.2, lo).square).duty;
            const double b = measureSquare (renderSettled (rate, hz, 0.2, hi).square).duty;
            INFO (hz << ": " << a << " " << b);
            CHECK (std::abs (a - b) < 0.015);
            CHECK (a != b);
        }
    }

    SECTION ("a larger emitter capacitor slows the threshold down")
    {
        // Ripple on the emitter at C2 scales inversely with C11.
        const auto ripple = [rate] (double farads)
        {
            dsp::Oscillator osc;
            osc.prepare (rate);
            dsp::OscillatorParams p;
            p.emitterCapFarads = farads;
            osc.setParams (p);
            double lo = 1.0e9, hi = -1.0e9;
            for (int i = 0; i < 192000; ++i)
            {
                osc.tick (kC2);
                if (i > 150000)
                {
                    lo = std::min (lo, osc.getShaper().emitterVolts());
                    hi = std::max (hi, osc.getShaper().emitterVolts());
                }
            }
            return hi - lo;
        };
        const double nominal = ripple (1.0e-6), big = ripple (1.5e-6);
        CHECK (nominal > 0.5);
        CHECK (big < nominal * 0.8);
        CHECK (big > nominal * 0.5);
    }

    SECTION ("trims are clamped to their accepted ranges")
    {
        dsp::Oscillator osc;
        dsp::OscillatorParams p;
        p.sawTopVolts = 20.0;
        p.sawBottomVolts = 0.0;
        p.subOctaves = 7;
        osc.setParams (p);
        CHECK (osc.getParams().sawTopVolts == Approx (12.5));
        CHECK (osc.getParams().sawBottomVolts == Approx (5.0));
        CHECK (osc.getParams().subOctaves == 2);
    }
}

TEST_CASE ("square: the solver converges everywhere", "[osc][square][solver]")
{
    // Random pitch jumps, trims and rates; the Newton solve must never give up and never
    // return anything but a voltage between the rails.
    Rng rng (0x5eed1234ull);
    std::uint64_t solves = 0, iterations = 0;
    std::uint32_t worst = 0;
    for (double rate : { 44100.0, 96000.0, 192000.0, 384000.0 })
    {
        dsp::Oscillator osc;
        osc.prepare (rate);
        double f = 110.0;
        for (int i = 0; i < 400000; ++i)
        {
            if (i % 2000 == 0)
            {
                f = expMap (rng.nextDouble(), 16.0, 0.45 * rate);
                if (rng.chance (0.3)) f = expMap (rng.nextDouble(), 20.0, 800.0);
                dsp::OscillatorParams p;
                p.sawTopVolts = lerp (9.5, 12.5, rng.nextDouble());
                p.sawBottomVolts = lerp (5.0, 6.5, rng.nextDouble());
                p.shaperBeta = lerp (50.0, 1000.0, rng.nextDouble());
                p.flatTopSeconds = rng.chance (0.5) ? 50.0e-6 * rng.nextDouble() : 0.0;
                p.flatTopDetunes = rng.chance (0.5);
                p.emitterCapFarads = lerp (0.5e-6, 2.0e-6, rng.nextDouble());
                osc.setParams (p);
            }
            const auto o = osc.tick (f);
            REQUIRE (std::isfinite (o.saw));
            REQUIRE (std::isfinite (o.square));
            // Between the rails, give or take the overshoot of a band-limited edge.
            REQUIRE (o.square > 4.3);
            REQUIRE (o.square < 10.6);
            REQUIRE (o.saw > 3.5);
            REQUIRE (o.saw < 13.5);
        }
        const auto& st = osc.getShaper().getStats();
        CHECK (st.nonConverged == 0);
        solves += st.solves;
        iterations += st.iterations;
        worst = std::max (worst, st.maxIterations);
    }
    INFO ("mean iterations " << static_cast<double> (iterations) / static_cast<double> (solves) << ", worst " << worst);
    CHECK (worst < static_cast<std::uint32_t> (dsp::SquareShaper::kMaxIterations));
    CHECK (static_cast<double> (iterations) / static_cast<double> (solves) < 4.0);    // under torture; about 1.1 on a steady note
}

TEST_CASE ("square: a reset that lands exactly on a sample leaves no spike", "[osc][square][regression]")
{
    // When the period is a whole number of samples (or within rounding of one) the split of
    // the sample at the reset produces a sliver of a segment. That used to put a huge
    // slope into the corner correction.
    for (double rate : { 176400.0, 192000.0 })
        for (double samplesPerCycle : { 100.0, 64.0, 250.0, 100.2267573696145 })
        {
            const double hz = rate / samplesPerCycle;
            dsp::Oscillator osc;
            osc.prepare (rate);
            double lo = 1.0e9, hi = -1.0e9, sawLo = 1.0e9, sawHi = -1.0e9;
            for (int i = 0; i < static_cast<int> (rate * 2.0); ++i)
            {
                const auto o = osc.tick (hz);
                lo = std::min (lo, o.square);
                hi = std::max (hi, o.square);
                sawLo = std::min (sawLo, o.saw);
                sawHi = std::max (sawHi, o.saw);
            }
            INFO (hz << " Hz at " << rate);
            CHECK (lo > 5.333 - 0.4);
            CHECK (hi < 9.7);
            CHECK (sawLo > 5.5 - 0.4);
            CHECK (sawHi < 11.3 + 0.4);
        }
}

TEST_CASE ("square: the shaper idles while the saw is selected and comes back settled", "[osc][square][idle]")
{
    for (double rate : { 48000.0, 96000.0, 192000.0 })
        for (double hz : { kC2, kC4 })
        {
            dsp::Oscillator full, idle;
            full.prepare (rate);
            idle.prepare (rate);
            dsp::OscillatorParams notInUse;
            notInUse.squareInUse = false;
            idle.setParams (notInUse);

            // Half a second with the square unused: saw and sub are untouched, bit for bit,
            // and the coarse square stays between its rails.
            const int n = static_cast<int> (0.5 * rate) + 137;
            for (int i = 0; i < n; ++i)
            {
                const auto a = full.tick (hz), b = idle.tick (hz);
                REQUIRE (a.saw == b.saw);
                REQUIRE (a.sub == b.sub);
                REQUIRE (b.square > 5.0);
                REQUIRE (b.square < 9.5);
            }

            // The threshold capacitor has followed.
            INFO (rate << " Hz, " << hz << " Hz");
            CHECK (idle.getShaper().emitterVolts() == Approx (full.getShaper().emitterVolts()).margin (0.03));

            // Back in use: from the first whole cycle the duty is where the always-on
            // oscillator has it, and nothing spikes.
            idle.setParams ({});
            const int m = static_cast<int> (0.25 * rate);
            std::vector<double> a (static_cast<std::size_t> (m)), b (static_cast<std::size_t> (m));
            for (int i = 0; i < m; ++i)
            {
                const auto x = full.tick (hz), y = idle.tick (hz);
                REQUIRE (x.saw == y.saw);
                a[static_cast<std::size_t> (i)] = x.square;
                b[static_cast<std::size_t> (i)] = y.square;
                REQUIRE (y.square > 5.0);
                REQUIRE (y.square < 9.5);
            }
            const auto dutyFull = dutyPerCycle (a, 7.0), dutyIdle = dutyPerCycle (b, 7.0);
            REQUIRE (dutyIdle.size() > 8);
            REQUIRE (dutyFull.size() > 8);
            const double tolerance = rate >= 96000.0 ? 0.005 : 0.012;
            for (std::size_t k = 1; k < 8; ++k)
                CHECK (std::abs (dutyIdle[k] - dutyFull[k]) < tolerance);
            // and it ends up identical in shape
            CHECK (std::abs (dutyIdle.back() - dutyFull.back()) < 0.001);
        }
}
