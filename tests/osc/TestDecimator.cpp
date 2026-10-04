// Decimator: design figures, measured stop band and pass band for every factor and rate,
// DC, latency, block invariance.
#include "Fft.h"
#include "OscTestSupport.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace acid;
using namespace acid::test;
using Catch::Approx;
using Quality = dsp::Decimator::Quality;

namespace
{
/** The decimator is a filter at the input rate followed by a sampler. This recovers that
    filter's impulse response by sending an impulse into each of the `factor` input phases:
    an impulse at input p gives y_p[n] = h[n * factor + (factor - 1) - p]. */
std::vector<double> equivalentImpulseResponse (double baseRate, int factor, Quality quality, int numOut)
{
    std::vector<double> h (static_cast<std::size_t> (numOut * factor), 0.0);
    std::vector<double> in (static_cast<std::size_t> (numOut * factor)), out (static_cast<std::size_t> (numOut));
    for (int p = 0; p < factor; ++p)
    {
        dsp::Decimator dec;
        dec.prepare (baseRate, factor, quality);
        std::fill (in.begin(), in.end(), 0.0);
        in[static_cast<std::size_t> (p)] = 1.0;
        dec.process (in.data(), out.data(), numOut);
        for (int n = 0; n < numOut; ++n)
            h[static_cast<std::size_t> (n * factor + (factor - 1) - p)] = out[static_cast<std::size_t> (n)];
    }
    return h;
}

struct Response
{
    double passbandRippleDb = 0.0;      // largest deviation from 0 dB up to the pass-band edge
    double stopbandDb = -400.0;         // largest gain anywhere that folds onto the pass band
    double dcGain = 0.0;
    double delaySamples = 0.0;          // centroid of h, in input samples: the DC group delay
};

Response measure (double baseRate, int factor, Quality quality)
{
    const int numOut = 16384 / factor * 2;
    const auto h = equivalentImpulseResponse (baseRate, factor, quality, numOut);
    const std::size_t n = h.size();
    REQUIRE (isPowerOfTwo (n));
    // The response must have died away inside the window, or the FFT lies.
    double tail = 0.0;
    for (std::size_t i = n - n / 8; i < n; ++i) tail = std::max (tail, std::abs (h[i]));
    REQUIRE (tail < 1.0e-13);

    std::vector<Complex> spec (n);
    Response r;
    double moment = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        spec[i] = Complex (h[i], 0.0);
        r.dcGain += h[i];
        moment += static_cast<double> (i) * h[i];
    }
    r.delaySamples = moment / r.dcGain;
    fft (spec);

    const double inRate = baseRate * factor;
    const double binHz = inRate / static_cast<double> (n);
    const double edge = std::min (20000.0, 0.4535 * baseRate);
    for (std::size_t k = 0; k <= n / 2; ++k)
    {
        const double f = static_cast<double> (k) * binHz;
        const double db = 20.0 * std::log10 (std::max (std::abs (spec[k]), 1.0e-300));
        if (f <= edge)
            r.passbandRippleDb = std::max (r.passbandRippleDb, std::abs (db));
        else
        {
            // does f fold onto the pass band when sampled at baseRate?
            const double folded = std::abs (f - baseRate * std::round (f / baseRate));
            if (folded <= edge)
                r.stopbandDb = std::max (r.stopbandDb, db);
        }
    }
    return r;
}
}

TEST_CASE ("decimator: half-band design reproduces the research table", "[decimator][design]")
{
    double c[dsp::Decimator::kMaxCoefficients];
    // docs/research/04, section 5: stage, transition, coefficients, rejection.
    CHECK (dsp::Decimator::designHalfBand (c, 3, 2.0 * (0.25 - 20000.0 / 192000.0)) == Approx (101.8).margin (0.15));
    CHECK (dsp::Decimator::designHalfBand (c, 8, 2.0 * (0.25 - 20000.0 / 96000.0)) == Approx (127.9).margin (0.15));
    CHECK (dsp::Decimator::designHalfBand (c, 8, 2.0 * (0.25 - 20000.0 / 88200.0)) == Approx (104.1).margin (0.15));
    CHECK (dsp::Decimator::designHalfBand (c, 10, 2.0 * (0.25 - 20000.0 / 88200.0)) == Approx (130.0).margin (0.15));

    // Coefficients are strictly increasing inside (0, 1): the allpass sections are stable.
    for (int n : { 1, 2, 3, 4, 8, 10, 12 })
        for (double tbw : { 0.01, 0.0465, 0.0833, 0.2, 0.2917, 0.4 })
        {
            const double rejection = dsp::Decimator::designHalfBand (c, n, tbw);
            CHECK (rejection > 0.0);
            double last = 0.0;
            for (int i = 0; i < n; ++i)
            {
                CHECK (c[i] > last);
                CHECK (c[i] < 1.0);
                last = c[i];
            }
        }
}

TEST_CASE ("decimator: stage plan follows the transition width", "[decimator][design]")
{
    dsp::Decimator d;
    d.prepare (48000.0, 4);
    REQUIRE (d.numStages() == 2);
    CHECK (d.factor() == 4);
    CHECK (d.numCoefficients (0) == 3);
    CHECK (d.numCoefficients (1) == 8);
    CHECK (d.transitionWidth (0) == Approx (0.2917).margin (1.0e-4));
    CHECK (d.transitionWidth (1) == Approx (0.0833).margin (1.0e-4));

    d.prepare (44100.0, 8, Quality::Ultra);
    REQUIRE (d.numStages() == 3);
    CHECK (d.numCoefficients (0) == 4);
    CHECK (d.numCoefficients (1) == 4);
    CHECK (d.numCoefficients (2) == 10);
    CHECK (d.transitionWidth (2) == Approx (0.0465).margin (1.0e-4));

    // At 96 kHz and above even the last stage has a wide transition and needs 3.
    d.prepare (96000.0, 2);
    REQUIRE (d.numStages() == 1);
    CHECK (d.numCoefficients (0) == 3);
    CHECK (d.designedRejectionDb (0) > 100.0);

    d.prepare (48000.0, 1);
    CHECK (d.numStages() == 0);
    CHECK (d.factor() == 1);
    CHECK (d.latency() == 0.0);

    // Anything else is rounded down to a supported factor.
    d.prepare (48000.0, 3);  CHECK (d.factor() == 2);
    d.prepare (48000.0, 6);  CHECK (d.factor() == 4);
    d.prepare (48000.0, 64); CHECK (d.factor() == 8);
    d.prepare (48000.0, 0);  CHECK (d.factor() == 1);
}

TEST_CASE ("decimator: measured stop band, pass band, DC gain and delay", "[decimator][response]")
{
    Report rep ("decimator-response.txt");
    rep.line ("Measured from impulse responses. Stop band: every input frequency that folds onto 0 to 20 kHz.");
    rep.line ("base rate  factor  quality   coefficients   stop band (dB)  designed (dB)  pass-band ripple (dB)  DC gain error  delay (base samples)");

    for (double base : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int factor : { 1, 2, 4, 8 })
            for (Quality quality : { Quality::Standard, Quality::Ultra })
            {
                dsp::Decimator dec;
                dec.prepare (base, factor, quality);

                if (factor == 1)
                {
                    // Pass-through: exact, no delay.
                    Rng rng (7);
                    std::vector<double> in (64), out (64);
                    for (auto& v : in) v = rng.nextDouble() - 0.5;
                    dec.process (in.data(), out.data(), 64);
                    CHECK (in == out);
                    CHECK (dec.latencySamples() == 0);
                    continue;
                }

                const auto r = measure (base, factor, quality);
                double weakest = 1.0e9;
                char counts[32] = "";
                for (int s = 0; s < dec.numStages(); ++s)
                {
                    weakest = std::min (weakest, dec.designedRejectionDb (s));
                    std::snprintf (counts + std::strlen (counts), sizeof (counts) - std::strlen (counts), "%s%d", s ? "+" : "", dec.numCoefficients (s));
                }
                rep.line ("%8.0f     %d    %-8s     %-8s       %8.1f        %8.1f          %.2e            %.1e          %.3f",
                          base, factor, quality == Quality::Ultra ? "ultra" : "standard", counts, r.stopbandDb, -weakest,
                          r.passbandRippleDb, std::abs (r.dcGain - 1.0), r.delaySamples / factor);
                INFO ("base " << base << " factor " << factor << (quality == Quality::Ultra ? " ultra" : " standard"));

                // Stop band: as designed (the weakest stage sets it), and at least 95 dB.
                CHECK (r.stopbandDb < -weakest + 0.5);
                CHECK (r.stopbandDb < (quality == Quality::Ultra ? -120.0 : -95.0));

                // Pass band flat to a millionth of a dB, DC gain exactly one.
                CHECK (r.passbandRippleDb < 1.0e-6);
                CHECK (r.dcGain == Approx (1.0).margin (1.0e-12));

                // Reported latency is the measured low-frequency group delay.
                CHECK (dec.latency() == Approx (r.delaySamples / factor).margin (1.0e-6));
                CHECK (dec.latencySamples() == static_cast<int> (std::lround (dec.latency())));
                CHECK (dec.latency() > 0.5);
                CHECK (dec.latency() < 4.0);
            }
}

TEST_CASE ("decimator: tones", "[decimator][response]")
{
    // A tone in the pass band comes through at unity; a tone in the first image band is gone.
    const double base = 48000.0;
    for (int factor : { 2, 4, 8 })
    {
        const double inRate = base * factor;
        const auto run = [&] (double hz)
        {
            dsp::Decimator dec;
            dec.prepare (base, factor);
            const int numOut = 8192;
            std::vector<double> in (static_cast<std::size_t> (numOut * factor)), out (static_cast<std::size_t> (numOut));
            for (std::size_t i = 0; i < in.size(); ++i) in[i] = std::sin (kTwoPi * hz * static_cast<double> (i) / inRate);
            dec.process (in.data(), out.data(), numOut);
            double peak = 0.0;
            for (int i = numOut / 2; i < numOut; ++i) peak = std::max (peak, std::abs (out[static_cast<std::size_t> (i)]));
            return peak;
        };
        CHECK (run (1000.0) == Approx (1.0).margin (2.0e-3));
        CHECK (run (15000.0) == Approx (1.0).margin (0.05));        // few samples per cycle: peak picking is coarse
        CHECK (run (base - 1000.0) < 1.0e-4);                       // would alias to 1 kHz
        CHECK (run (base + 5000.0) < 1.0e-4);
        if (factor >= 4) CHECK (run (2.0 * base - 3000.0) < 1.0e-4);
    }
}

TEST_CASE ("decimator: DC passes exactly and silence stays silent", "[decimator][dc]")
{
    for (double base : { 44100.0, 48000.0 })
        for (int factor : { 2, 4, 8 })
        {
            dsp::Decimator dec;
            dec.prepare (base, factor);
            // The oscillator's outputs ride on 7 to 8 V of DC.
            std::vector<double> in (static_cast<std::size_t> (4096 * factor), 8.4), out (4096);
            dec.process (in.data(), out.data(), 4096);
            CHECK (out.back() == Approx (8.4).margin (1.0e-11));
            for (std::size_t i = 2048; i < out.size(); ++i) REQUIRE (std::abs (out[i] - 8.4) < 1.0e-9);

            dec.reset();
            std::fill (in.begin(), in.end(), 0.0);
            dec.process (in.data(), out.data(), 4096);
            for (double v : out) REQUIRE (v == 0.0);

            // reset() really clears: the same input gives the same output again.
            Rng rng (11);
            for (auto& v : in) v = rng.nextDouble() - 0.5;
            std::vector<double> first (4096), second (4096);
            dec.reset();
            dec.process (in.data(), first.data(), 4096);
            dec.reset();
            dec.process (in.data(), second.data(), 4096);
            CHECK (first == second);
        }
}

TEST_CASE ("decimator: block size and sample type do not change the result", "[decimator][blocks]")
{
    Rng rng (42);
    for (int factor : { 1, 2, 4, 8 })
    {
        const int numOut = 8192;
        std::vector<double> in (static_cast<std::size_t> (numOut * factor));
        for (auto& v : in) v = 2.0 * rng.nextDouble() - 1.0;

        dsp::Decimator whole;
        whole.prepare (48000.0, factor);
        std::vector<double> reference (static_cast<std::size_t> (numOut));
        whole.process (in.data(), reference.data(), numOut);

        for (int block : { 1, 64, 4096 })
        {
            dsp::Decimator dec;
            dec.prepare (48000.0, factor);
            std::vector<double> out (static_cast<std::size_t> (numOut));
            for (int done = 0; done < numOut; done += block)
                dec.process (in.data() + done * factor, out.data() + done, std::min (block, numOut - done));
            CHECK (out == reference);       // bit-identical
        }

        // One sample at a time through processSample.
        dsp::Decimator single;
        single.prepare (48000.0, factor);
        for (int n = 0; n < numOut; ++n)
            REQUIRE (single.processSample (in.data() + n * factor) == reference[static_cast<std::size_t> (n)]);

        // Float output is the double result rounded; float input matches to float precision.
        dsp::Decimator toFloat, fromFloat;
        toFloat.prepare (48000.0, factor);
        fromFloat.prepare (48000.0, factor);
        std::vector<float> outF (static_cast<std::size_t> (numOut)), inF (in.size()), outFF (static_cast<std::size_t> (numOut));
        for (std::size_t i = 0; i < in.size(); ++i) inF[i] = static_cast<float> (in[i]);
        toFloat.process (in.data(), outF.data(), numOut);
        fromFloat.process (inF.data(), outFF.data(), numOut);
        for (int n = 0; n < numOut; ++n)
        {
            REQUIRE (outF[static_cast<std::size_t> (n)] == static_cast<float> (reference[static_cast<std::size_t> (n)]));
            REQUIRE (std::abs (static_cast<double> (outFF[static_cast<std::size_t> (n)]) - reference[static_cast<std::size_t> (n)]) < 1.0e-5);
        }
    }
}
