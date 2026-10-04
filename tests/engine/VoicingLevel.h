#pragma once
// Level of the distortion voicings on the instrument's own output: real voice lines rendered
// through the Engine with one voicing picked from the Distortion Model list (as the plugin
// does: fx::writeVoicing), measured as BS.1770-4 gated loudness. Used by the level test and
// by the hidden "[.calibrate]" fit of the trims in DistortionVoicings.cpp.
#include "EngineTestSupport.h"
#include "acid/fx/DistortionVoicings.h"

#include <atomic>
#include <cmath>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace vlevel
{
using namespace etest;

struct Line
{
    const char* name;
    const char* steps;
    double bpm;
};

/** The lines the trims are fitted on: the classic line the QA review used, the factory's
    reference line (the first-load line), a line with an accent on every step and a sparse
    low line. */
inline const std::vector<Line>& calibrationLines()
{
    static const std::vector<Line> lines {
        { "classic", "C:A C+:A C:AS C+ ~ . D#:S D#+:A C . G-:AS G C+:A C+:A A#-:S C", 125.0 },
        { "reference", "C C C+:A C D#:S F C:A . C C+:S C:A A#- C ~ G-:S C", 125.0 },
        { "anthem", "C:A C:A C:A C:AS C:A C:A C:A C:AS C:AS C:A C:A C:A C:A C:AS C:AS C:A", 130.0 },
        { "sparse", "C ~ . . . . C:A . . . G- ~ . . C:S D#", 120.0 },
    };
    return lines;
}

/** Lines no fit has seen: another key, another tempo, slides and octaves in other places. */
inline const std::vector<Line>& heldOutLines()
{
    static const std::vector<Line> lines {
        { "minor walk", "G- G- G:A G- . A#- C:S D# G- G-:A . F:S G G- ~ C:A", 135.0 },
        { "octaves", "E- E E-:A E . E- E:S G-:A E- E . E-:AS D E-:A . B-", 128.0 },
    };
    return lines;
}

struct Condition
{
    float cutoff = 50.0f, resonance = 70.0f, envMod = 55.0f, drive = 50.0f;
};

/** Stereo output of two bars of the line with the voicing picked, distortion on, every other
    effect off, Normal quality at 48 kHz. */
inline Rendered renderLine (const Line& line, int voicing, const Condition& c)
{
    Session s;
    s.setPattern (0, line.steps);
    s.params[kSeqRun] = 1.0f;
    s.params[kCutoff] = c.cutoff;
    s.params[kResonance] = c.resonance;
    s.params[kEnvMod] = c.envMod;
    s.params[kDistOn] = 1.0f;
    s.params[kDistDrive] = c.drive;
    s.params[kDistModel] = static_cast<float> (voicing);
    s.params[kRevOn] = 0.0f;
    s.params[kDelayOn] = 0.0f;
    s.params[kEqOn] = 0.0f;
    s.params[kLimOn] = 0.0f;
    fx::writeVoicing (s.params, fx::distortionVoicings()[static_cast<std::size_t> (voicing)]);
    s.settings.realtimeQuality = Quality::Normal;
    s.host.bpm = line.bpm;
    s.host.play();
    s.prepare (kFs, 512);
    const auto total = static_cast<std::int64_t> (2 * 4 * 60.0 / line.bpm * kFs);
    return s.render (total, 512);
}

/** One biquad, direct form I. */
struct Biquad
{
    double b0, b1, b2, a1, a2;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    double tick (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

/** ITU-R BS.1770-4 integrated loudness (LKFS) of a stereo signal from sample `from`:
    K-weighting at any rate, 400 ms blocks with 75 % overlap, the absolute gate at -70 LKFS
    and the relative gate 10 LU under the ungated level. */
inline double loudness (const std::vector<float>& left, const std::vector<float>& right, double fs, std::size_t from = 0)
{
    const auto kWeighting = [fs] {
        const double pi = 3.14159265358979323846;
        double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        double k = std::tan (pi * f0 / fs);
        const double vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
        double a0 = 1.0 + k / q + k * k;
        Biquad shelf { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                       2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        f0 = 38.13547087602444; q = 0.5003270373238773;
        k = std::tan (pi * f0 / fs);
        a0 = 1.0 + k / q + k * k;
        Biquad hp { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        return std::pair<Biquad, Biquad> { shelf, hp };
    };

    std::vector<double> z;    // per-sample weighted power, summed over the channels
    {
        auto [sl, hl] = kWeighting();
        auto [sr, hr] = kWeighting();
        z.resize (left.size() > from ? left.size() - from : 0);
        for (std::size_t i = 0; i < z.size(); ++i)
        {
            const double l = hl.tick (sl.tick (static_cast<double> (left[from + i])));
            const double r = hr.tick (sr.tick (static_cast<double> (right[from + i])));
            z[i] = l * l + r * r;
        }
    }

    const auto block = static_cast<std::size_t> (0.4 * fs), hop = block / 4;
    std::vector<double> blocks;
    for (std::size_t start = 0; start + block <= z.size(); start += hop)
    {
        double s = 0.0;
        for (std::size_t i = start; i < start + block; ++i) s += z[i];
        blocks.push_back (s / static_cast<double> (block));
    }
    const auto lk = [] (double power) { return -0.691 + 10.0 * std::log10 (power + 1.0e-30); };
    const auto gatedMean = [&] (double threshold) {
        double s = 0.0;
        int n = 0;
        for (const double b : blocks)
            if (lk (b) > threshold) { s += b; ++n; }
        return n > 0 ? s / n : 0.0;
    };
    const double ungated = gatedMean (-70.0);
    if (ungated <= 0.0) return -200.0;
    return lk (gatedMean (lk (ungated) - 10.0));
}

/** Loudness of a render, from its second beat on (the accent capacitor and the couplings
    settle over the first). */
inline double lineLoudness (const Line& line, int voicing, const Condition& c)
{
    const Rendered r = renderLine (line, voicing, c);
    return loudness (r.left, r.right, kFs, static_cast<std::size_t> (60.0 / line.bpm * kFs));
}

/** Runs job (i) for i in 0..n-1 on every core. */
inline void parallelFor (int n, const std::function<void (int)>& job)
{
    std::atomic<int> next { 0 };
    const unsigned cores = std::max (1u, std::min (std::thread::hardware_concurrency(), 64u));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < cores; ++t)
        pool.emplace_back ([&] {
            for (int i = next++; i < n; i = next++)
                job (i);
        });
    for (auto& t : pool) t.join();
}
}
