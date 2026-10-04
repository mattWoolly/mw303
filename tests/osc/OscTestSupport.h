#pragma once
// Shared helpers for the oscillator and decimator tests: rendering, edge timing, duty
// measurement, an independent brute-force reference of the shaper circuit, and a report file.
#include "acid/common/Math.h"
#include "acid/dsp/Decimator.h"
#include "acid/dsp/Oscillator.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace acid::test
{
// Notes the research tables use (A2 = 110 Hz tuning).
inline constexpr double kC1 = 32.7032, kC2 = 65.4064, kA2 = 110.0, kC3 = 130.8128,
                        kC4 = 261.6256, kC5 = 523.2511, kC6 = 1046.5023;

struct Rendered
{
    std::vector<double> saw, square, sub;
};

/** Runs the oscillator for n samples at a fixed pitch. */
inline Rendered render (dsp::Oscillator& osc, double hz, int n)
{
    Rendered r;
    r.saw.resize (static_cast<std::size_t> (n));
    r.square.resize (static_cast<std::size_t> (n));
    r.sub.resize (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto o = osc.tick (hz);
        const auto k = static_cast<std::size_t> (i);
        r.saw[k] = o.saw;
        r.square[k] = o.square;
        r.sub[k] = o.sub;
    }
    return r;
}

/** Fresh oscillator at `rate`, run for settleSeconds, then captureSeconds are returned. */
inline Rendered renderSettled (double rate, double hz, double captureSeconds,
                               const dsp::OscillatorParams& params = {}, double settleSeconds = 0.6)
{
    dsp::Oscillator osc;
    osc.prepare (rate);
    osc.setParams (params);
    const int settle = static_cast<int> (settleSeconds * rate);
    for (int i = 0; i < settle; ++i)
        osc.tick (hz);
    return render (osc, hz, static_cast<int> (captureSeconds * rate));
}

enum class Waveform { Saw, Square, Sub };

/** Oscillator at base * factor into the decimator; returns base-rate samples (double),
    after discarding settleSeconds. */
inline std::vector<double> renderDecimated (double baseRate, int factor, double hz, double seconds, Waveform wave,
                                            const dsp::OscillatorParams& params = {},
                                            dsp::Decimator::Quality quality = dsp::Decimator::Quality::Standard,
                                            double settleSeconds = 1.0)
{
    dsp::Oscillator osc;
    osc.prepare (baseRate * factor);
    osc.setParams (params);
    dsp::Decimator dec;
    dec.prepare (baseRate, factor, quality);

    const int settle = static_cast<int> (settleSeconds * baseRate);
    const int n = static_cast<int> (seconds * baseRate);
    std::vector<double> out (static_cast<std::size_t> (n));
    double group[8] = {};
    for (int i = -settle; i < n; ++i)
    {
        for (int m = 0; m < factor; ++m)
        {
            const auto o = osc.tick (hz);
            group[m] = wave == Waveform::Saw ? o.saw : (wave == Waveform::Square ? o.square : o.sub);
        }
        const double y = dec.processSample (group);
        if (i >= 0)
            out[static_cast<std::size_t> (i)] = y;
    }
    return out;
}

/** Sample positions (fractional, linear interpolation) where x crosses `level`. */
inline std::vector<double> crossings (const std::vector<double>& x, double level, bool rising)
{
    std::vector<double> t;
    for (std::size_t i = 1; i < x.size(); ++i)
    {
        const double a = x[i - 1] - level, b = x[i] - level;
        if (rising ? (a < 0.0 && b >= 0.0) : (a >= 0.0 && b < 0.0))
            t.push_back (static_cast<double> (i - 1) + a / (a - b));
    }
    return t;
}

/** Mean frequency from the first to the last rising crossing of the mid level. */
inline double measureFrequency (const std::vector<double>& x, double rate)
{
    const auto [mn, mx] = std::minmax_element (x.begin(), x.end());
    const auto t = crossings (x, 0.5 * (*mn + *mx), true);
    if (t.size() < 2) return 0.0;
    return rate * static_cast<double> (t.size() - 1) / (t.back() - t.front());
}

inline double cents (double f, double reference) { return 1200.0 * std::log2 (f / reference); }

struct SquareShape
{
    double low = 0.0, high = 0.0;       // extreme levels
    double duty = 0.0;                  // fraction of the period above the mid level
    std::vector<double> rise, fall;     // crossing positions of the mid level
};

/** Duty from interpolated crossings of the mid level, averaged over every whole cycle. The
    high part of the pulse runs from a rising crossing (turn-on) to the next falling one
    (the saw reset). */
inline SquareShape measureSquare (const std::vector<double>& sq)
{
    SquareShape s;
    const auto [mn, mx] = std::minmax_element (sq.begin(), sq.end());
    s.low = *mn;
    s.high = *mx;
    const double mid = 0.5 * (s.low + s.high);
    s.rise = crossings (sq, mid, true);
    s.fall = crossings (sq, mid, false);

    double highTime = 0.0;
    std::size_t f = 0, cycles = 0;
    for (std::size_t r = 0; r + 1 < s.rise.size(); ++r)
    {
        while (f < s.fall.size() && s.fall[f] <= s.rise[r]) ++f;
        if (f >= s.fall.size()) break;
        highTime += s.fall[f] - s.rise[r];
        ++cycles;
    }
    if (cycles > 0 && s.rise.size() > 1)
        s.duty = highTime / (s.rise[cycles] - s.rise[0]);
    return s;
}

/** Duty of each individual cycle: (rise k to the next fall) / (rise k to rise k+1). */
inline std::vector<double> dutyPerCycle (const std::vector<double>& sq, double mid, std::vector<double>* riseTimes = nullptr)
{
    const auto rise = crossings (sq, mid, true);
    const auto fall = crossings (sq, mid, false);
    std::vector<double> d;
    std::size_t f = 0;
    for (std::size_t r = 0; r + 1 < rise.size(); ++r)
    {
        while (f < fall.size() && fall[f] <= rise[r]) ++f;
        if (f >= fall.size()) break;
        d.push_back ((fall[f] - rise[r]) / (rise[r + 1] - rise[r]));
        if (riseTimes != nullptr) riseTimes->push_back (rise[r]);
    }
    return d;
}

struct ReferenceShape
{
    double duty = 0.0;
    double low = 0.0, high = 0.0;
    double emitterMin = 0.0, emitterMax = 0.0;
    double highAtTurnOn = 0.0;          // collector peak of the last cycle
    double highBeforeReset = 0.0;       // collector just before the last reset
};

/** Independent brute-force model of the shaper: forward Euler at `rate` (run it at 30 or
    more times the oscillator's rate), an unsmoothed saw with an instant reset, Newton on the
    node voltages (base and collector, not the junction voltages the production code uses).
    Shares only the component values and the follower map with the code under test. */
inline ReferenceShape referenceShaper (const dsp::Oscillator& osc, double hz, double beta, double seconds, double rate)
{
    using S = dsp::SquareShaper;
    const double is = S::kSaturationCurrent, vt = S::kThermalVolts, br = S::kReverseBeta;
    const double dt = 1.0 / rate;
    const double period = rate / hz;
    const long total = static_cast<long> (seconds * rate);
    const long measureFrom = total - static_cast<long> (std::floor (4.0 * period));

    double ve = S::kEmitterRestVolts, vcap = 0.0, vb = 11.0, vc = S::kCollectorRailVolts;
    double phase = 0.0;

    // pass 1 finds the extremes of the measured cycles, pass 2 is identical and counts.
    ReferenceShape out;
    std::vector<double> tail;
    tail.reserve (static_cast<std::size_t> (total - measureFrom + 8));
    out.emitterMin = 1.0e9;
    out.emitterMax = -1.0e9;
    double lastVc = vc;

    for (long i = 0; i < total; ++i)
    {
        phase += hz * dt;
        bool reset = false;
        if (phase >= 1.0) { phase -= 1.0; reset = true; }
        const double vs = osc.sawVoltsAtPhase (phase);
        const double vx = vs - vcap;

        double ie = 0.0;
        for (int it = 0; it < 200; ++it)
        {
            const double ee = std::exp (std::min ((ve - vb) / vt, 60.0));
            const double ec = std::exp (std::min ((vc - vb) / vt, 60.0));
            const double ibe = is / beta * (ee - 1.0), ibc = is / br * (ec - 1.0), icc = is * (ee - ec);
            const double f1 = (vs - vb) / S::kR35 + (vx - vb) / S::kR34 + ibe + ibc;
            const double f2 = icc - ibc - (vc - S::kCollectorRailVolts) / S::kR36;
            const double a11 = -1.0 / S::kR35 - 1.0 / S::kR34 - is / beta * ee / vt - is / br * ec / vt;
            const double a12 = is / br * ec / vt;
            const double a21 = is * (-ee + ec) / vt + is / br * ec / vt;
            const double a22 = -is * ec / vt - is / br * ec / vt - 1.0 / S::kR36;
            const double det = a11 * a22 - a12 * a21;
            double dVb = -(f1 * a22 - a12 * f2) / det;
            double dVc = -(a11 * f2 - a21 * f1) / det;
            const double m = std::max (std::abs (dVb), std::abs (dVc));
            if (m > 0.05) { dVb *= 0.05 / m; dVc *= 0.05 / m; }
            vb += dVb;
            vc += dVc;
            ie = icc + ibe;
            if (m < 1.0e-10) break;
        }

        if (i >= measureFrom)
        {
            tail.push_back (vc);
            out.emitterMin = std::min (out.emitterMin, ve);
            out.emitterMax = std::max (out.emitterMax, ve);
            if (reset) out.highBeforeReset = lastVc;
        }
        lastVc = vc;

        ve += dt * ((S::kEmitterRailVolts - ve) / S::kR45 - ie) / S::kC11;
        vcap += dt * ((vx - vb) / S::kR34) / S::kC10;
    }

    const auto [mn, mx] = std::minmax_element (tail.begin(), tail.end());
    out.low = *mn;
    out.high = *mx;
    out.highAtTurnOn = *mx;
    const auto shape = measureSquare (tail);
    out.duty = shape.duty;
    return out;
}

/** Measurement report: every line goes to stdout and to a text file under
    ACID_TEST_OUTPUT_DIR, so the numbers survive a quiet test run. One file per test case. */
class Report
{
public:
    explicit Report (const char* fileName)
    {
#ifdef ACID_TEST_OUTPUT_DIR
        file = std::fopen ((std::string (ACID_TEST_OUTPUT_DIR) + "/" + fileName).c_str(), "w");
#else
        (void) fileName;
#endif
    }

    ~Report()
    {
        if (file != nullptr) std::fclose (file);
    }

    Report (const Report&) = delete;
    Report& operator= (const Report&) = delete;

#if defined (__GNUC__)
    __attribute__ ((format (printf, 2, 3)))
#endif
    void line (const char* format, ...)
    {
        char text[512];
        va_list args;
        va_start (args, format);
        std::vsnprintf (text, sizeof (text), format, args);
        va_end (args);
        std::printf ("%s\n", text);
        if (file != nullptr) std::fprintf (file, "%s\n", text);
    }

private:
    std::FILE* file = nullptr;
};
}
