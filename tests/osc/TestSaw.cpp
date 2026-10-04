// Saw core: levels, polarity, shape, pitch, free-running phase, flat top, rate independence.
#include "OscTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
/** Indices of the samples where the saw jumps up (the band-limited reset). */
std::vector<std::size_t> resetIndices (const std::vector<double>& saw, double span)
{
    std::vector<std::size_t> idx;
    for (std::size_t i = 1; i < saw.size(); ++i)
        if (saw[i] - saw[i - 1] > 0.25 * span && (idx.empty() || i - idx.back() > 4))
            idx.push_back (i);
    return idx;
}
}

TEST_CASE ("saw: levels, polarity and shape at the oscillator node", "[osc][saw]")
{
    const double rate = 192000.0;
    const auto r = renderSettled (rate, kA2, 0.25);
    const dsp::OscillatorParams defaults;
    const double top = defaults.sawTopVolts, bottom = defaults.sawBottomVolts, span = top - bottom;
    REQUIRE (top == 11.3);
    REQUIRE (bottom == 5.5);

    const auto resets = resetIndices (r.saw, span);
    REQUIRE (resets.size() > 20);

    // Between resets (five samples clear of each) the saw only falls and stays inside its
    // two levels: a slow fall and a fast rise, not the other way round.
    double lo = 1.0e9, hi = -1.0e9, maxStep = -1.0e9;
    for (std::size_t k = 0; k + 1 < resets.size(); ++k)
        for (std::size_t i = resets[k] + 5; i + 5 < resets[k + 1]; ++i)
        {
            lo = std::min (lo, r.saw[i]);
            hi = std::max (hi, r.saw[i]);
            maxStep = std::max (maxStep, r.saw[i + 1] - r.saw[i]);
        }
    CHECK (maxStep < 0.0);
    CHECK (hi <= top + 1.0e-9);
    CHECK (hi > top - 0.02 * span);
    CHECK (lo >= bottom - 1.0e-9);
    CHECK (lo < bottom + 0.02 * span);

    // Deviation from a straight ramp: under 5 % of peak to peak outside the first 5 % of
    // the cycle (the follower rounds the top a little).
    dsp::Oscillator osc;
    double worst = 0.0;
    for (double ph = 0.05; ph <= 1.0; ph += 0.001)
        worst = std::max (worst, std::abs (osc.sawVoltsAtPhase (ph) - (top - span * ph)));
    CHECK (worst < 0.05 * span);
    CHECK (worst > 0.002 * span);          // but it is not an ideal ramp either
    CHECK (osc.sawVoltsAtPhase (0.0) == Approx (top).margin (1.0e-9));
    CHECK (osc.sawVoltsAtPhase (1.0) == Approx (bottom).margin (1.0e-9));

    // The map is monotonic.
    for (double ph = 0.0; ph < 1.0; ph += 0.0005)
        REQUIRE (osc.sawVoltsAtPhase (ph + 0.0005) < osc.sawVoltsAtPhase (ph));

    // Mean level: what the coupling capacitor in front of the filter settles to.
    const auto cycles = static_cast<double> (resets.back() - resets.front());
    double sum = 0.0;
    for (std::size_t i = resets.front(); i < resets.back(); ++i) sum += r.saw[i];
    CHECK (sum / cycles == Approx (osc.sawMeanVolts()).margin (0.005));
    CHECK (osc.sawMeanVolts() == Approx (0.5 * (top + bottom)).margin (0.1));
}

TEST_CASE ("saw: reset rise time is under 40 microseconds at every rate", "[osc][saw]")
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0 })
    {
        const auto r = renderSettled (rate, kC2, 0.2);
        const double lo = 5.5, hi = 11.3;
        const auto t10 = crossings (r.saw, lo + 0.1 * (hi - lo), true);
        const auto t90 = crossings (r.saw, lo + 0.9 * (hi - lo), true);
        REQUIRE (t10.size() > 5);
        REQUIRE (t90.size() > 5);
        double worst = 0.0;
        for (std::size_t k = 1; k + 1 < t10.size(); ++k)
        {
            // the 90 % crossing that follows this 10 % crossing
            const auto it = std::lower_bound (t90.begin(), t90.end(), t10[k]);
            if (it == t90.end()) break;
            worst = std::max (worst, (*it - t10[k]) / rate);
        }
        INFO ("rate " << rate << " rise " << worst * 1.0e6 << " us");
        CHECK (worst < 40.0e-6);
    }
}

TEST_CASE ("saw: pitch is exact for every note from C1 to C7", "[osc][saw][pitch]")
{
    for (double rate : { 176400.0, 192000.0 })
    {
        double worstCents = 0.0;
        for (int note = 24; note <= 96; ++note)
        {
            const double f = midiNoteToHz (note);
            dsp::Oscillator osc;
            osc.prepare (rate);
            const auto r = render (osc, f, static_cast<int> (rate * 0.6));
            const double c = cents (measureFrequency (r.saw, rate), f);
            worstCents = std::max (worstCents, std::abs (c));
        }
        INFO ("rate " << rate << " worst " << worstCents << " cents");
        CHECK (worstCents < 0.5);
    }

    // The service-note anchors.
    CHECK (midiNoteToHz (45) == Approx (110.0));
    CHECK (midiNoteToHz (24) == Approx (32.70).margin (0.01));
    CHECK (midiNoteToHz (72) == Approx (523.25).margin (0.01));
}

TEST_CASE ("saw: square and sub share the saw's pitch exactly", "[osc][pitch]")
{
    const double rate = 192000.0;
    dsp::Oscillator osc;
    osc.prepare (rate);
    for (int i = 0; i < 96000; ++i) osc.tick (kC3);
    const auto r = render (osc, kC3, static_cast<int> (rate));
    CHECK (std::abs (cents (measureFrequency (r.square, rate), kC3)) < 0.5);
    CHECK (std::abs (cents (measureFrequency (r.sub, rate), kC3 * 0.5)) < 0.5);
}

TEST_CASE ("saw: levels do not depend on the sample rate", "[osc][saw][rates]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    {
        const auto r = renderSettled (rate, kC2, 0.3);
        const auto resets = resetIndices (r.saw, 5.8);
        REQUIRE (resets.size() > 10);
        double lo = 1.0e9, hi = -1.0e9, sum = 0.0;
        for (std::size_t k = 0; k + 1 < resets.size(); ++k)
            for (std::size_t i = resets[k] + 5; i + 5 < resets[k + 1]; ++i)
            {
                lo = std::min (lo, r.saw[i]);
                hi = std::max (hi, r.saw[i]);
            }
        for (std::size_t i = resets.front(); i < resets.back(); ++i) sum += r.saw[i];
        const double mean = sum / static_cast<double> (resets.back() - resets.front());
        INFO ("rate " << rate);
        CHECK (hi == Approx (11.3).margin (0.06));
        CHECK (lo == Approx (5.5).margin (0.06));
        CHECK (mean == Approx (dsp::Oscillator().sawMeanVolts()).margin (0.01));
    }
}

TEST_CASE ("saw: the core free-runs, its phase is continuous through everything a note does", "[osc][saw][phase]")
{
    const double rate = 192000.0;
    dsp::Oscillator osc;
    osc.prepare (rate);

    // Pitch jumps, trim changes and a frequency of zero never move the phase by more than
    // the travel of one sample.
    double last = osc.phase();
    double f = 110.0;
    for (int i = 0; i < 200000; ++i)
    {
        if (i % 9000 == 0) f = (i / 9000) % 2 == 0 ? 440.0 : 65.4;
        if (i == 50000) { dsp::OscillatorParams p; p.sawTopVolts = 10.9; p.shaperBeta = 150.0; p.subOctaves = 2; osc.setParams (p); }
        if (i == 120000) f = 0.0;
        if (i == 130000) f = 220.0;
        osc.tick (f);
        const double ph = osc.phase();
        double step = ph - last;
        if (step < 0.0) step += 1.0;
        REQUIRE (step <= f / rate * 1.0000001 + 1.0e-12);
        REQUIRE (step >= f / rate * 0.9999999 - 1.0e-12);
        last = ph;
    }

    // Zero frequency holds the ramp where it is.
    const double held = osc.phase();
    const double level = osc.tick (0.0).saw;
    for (int i = 0; i < 1000; ++i) osc.tick (0.0);
    CHECK (osc.phase() == held);
    CHECK (osc.tick (0.0).saw == Approx (level).margin (0.2));
}

TEST_CASE ("saw: optional flat top", "[osc][saw][flattop]")
{
    const double rate = 192000.0;
    const double hold = 20.0e-6;

    SECTION ("tuned: the period stays 1/f and the top is held")
    {
        dsp::OscillatorParams p;
        p.flatTopSeconds = hold;
        for (double f : { kC2, kC4, kC5, kC6 })
        {
            const auto r = renderSettled (rate, f, 0.5, p);
            CHECK (std::abs (cents (measureFrequency (r.saw, rate), f)) < 0.5);
        }

        // A 50 us hold is 9.6 samples: six samples after the step the held saw still sits
        // at the top, the plain one has already come down the rounded corner.
        dsp::OscillatorParams longHold;
        longHold.flatTopSeconds = 50.0e-6;
        const auto r = renderSettled (rate, kC4, 0.1, longHold);
        const auto resets = resetIndices (r.saw, 5.8);
        REQUIRE (resets.size() > 5);
        const auto none = renderSettled (rate, kC4, 0.1);
        const auto resetsNone = resetIndices (none.saw, 5.8);
        REQUIRE (resetsNone.size() > 5);
        const double held = r.saw[resets[2] + 6];
        const double plain = none.saw[resetsNone[2] + 6];
        INFO ("held " << held << " plain " << plain);
        CHECK (held > 11.3 - 0.005);
        CHECK (plain < 11.3 - 0.015);
    }

    SECTION ("untrimmed: the hold adds to the period and high notes go flat")
    {
        dsp::OscillatorParams p;
        p.flatTopSeconds = hold;
        p.flatTopDetunes = true;
        for (double f : { kC2, kC5 })
        {
            const auto r = renderSettled (rate, f, 0.5, p);
            const double expected = 1.0 / (1.0 / f + hold);
            CHECK (std::abs (cents (measureFrequency (r.saw, rate), expected)) < 0.5);
        }
        // 20 us at C5 is 18 cents flat, as the research computes.
        CHECK (cents (1.0 / (1.0 / kC5 + hold), kC5) == Approx (-18.0).margin (0.5));
    }

    SECTION ("the hold is clamped to 50 microseconds")
    {
        dsp::Oscillator osc;
        dsp::OscillatorParams p;
        p.flatTopSeconds = 1.0;
        osc.setParams (p);
        CHECK (osc.getParams().flatTopSeconds == Approx (50.0e-6));
    }
}

TEST_CASE ("saw: the pulse-width trim maps onto the saw top level", "[osc][trim]")
{
    using P = dsp::OscillatorParams;
    CHECK (P::sawTopFromPwShift (0.0) == Approx (11.3));
    CHECK (P::sawTopFromPwShift (0.0) == P::kDefaultSawTopVolts);
    CHECK (P::sawTopFromPwShift (100.0) == Approx (10.3));
    CHECK (P::sawTopFromPwShift (-100.0) == Approx (12.3));
    CHECK (P::sawTopFromPwShift (250.0) == Approx (10.3));
    CHECK (P::sawTopFromPwShift (-250.0) == Approx (12.3));

    // Positive shift widens the high part of the square: about 0.045 of the period for the
    // full travel, in even steps.
    const double rate = 192000.0;
    Report rep ("osc-pw-trim.txt");
    rep.line ("PW shift (%%)   saw top (V)   duty at C2   duty at C3   duty at C5");
    double last = 0.0, lowest = 0.0, centre = 0.0, highest = 0.0;
    for (double pw : { -100.0, -50.0, 0.0, 50.0, 100.0 })
    {
        P p;
        p.sawTopVolts = P::sawTopFromPwShift (pw);
        const double duty = measureSquare (renderSettled (rate, kC3, 0.2, p).square).duty;
        rep.line ("   %+6.0f        %5.2f        %.4f       %.4f       %.4f", pw, p.sawTopVolts,
                  measureSquare (renderSettled (rate, kC2, 0.3, p).square).duty, duty,
                  measureSquare (renderSettled (rate, kC5, 0.1, p).square).duty);
        INFO ("pw " << pw << " duty " << duty);
        if (pw > -100.0)
        {
            CHECK (duty > last + 0.015);
            CHECK (duty < last + 0.035);
        }
        if (pw == -100.0) lowest = duty;
        if (pw == 0.0) centre = duty;
        if (pw == 100.0) highest = duty;
        last = duty;
    }
    CHECK (highest - centre == Approx (0.045).margin (0.012));
    CHECK (centre - lowest == Approx (0.045).margin (0.012));
}
