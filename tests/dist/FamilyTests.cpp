// The eight families through the whole block: that they are different circuits, that the
// console behaves like one, that bias moves cleanly, and that bad input cannot latch.
#include "DistTestUtil.h"
#include "acid/fx/Distortion.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <limits>

using namespace acid;
using namespace acid::fx;
using namespace disttest;

namespace
{
constexpr double kFs = 48000.0;
const char* const kNames[kNumClipperFamilies] = { "DiodePair", "OpAmpHard", "AsymSoft", "ScoopHard",
                                                  "MidHump",   "GatedFuzz", "Console",  "Fold" };

DistortionParams familyParams (int family, float drive, float knee = 100.0f)
{
    DistortionParams p;
    p.on = true;
    p.clipper = family;
    p.drive = drive;
    p.knee = knee;
    return p;
}

std::vector<float> run (const DistortionParams& p, const std::vector<float>& in, int factor = 4)
{
    Distortion d;
    d.prepare (kFs, 256);
    d.setOversampling (factor);
    d.setParams (p);
    d.reset();
    return render (d, in);
}

/** Harmonics 2 to 11 relative to the fundamental, dB, floored at -80 dB. */
std::vector<double> harmonicProfile (const DistortionParams& p, double hz)
{
    const auto out = run (p, sine (hz, kNominalLevel, kFs, 48000));
    const double f0 = tone (out, hz, kFs, 24000);
    std::vector<double> h;
    for (int k = 2; k <= 11; ++k)
        h.push_back (std::max (-80.0, db (tone (out, hz * k, kFs, 24000) / f0)));
    return h;
}

/** Total harmonic distortion (harmonics 2 to 15) in dB at a frequency. */
double thdDb (const DistortionParams& p, double hz)
{
    const auto out = run (p, sine (hz, kNominalLevel, kFs, 48000));
    double s = 0.0;
    for (int k = 2; k <= 15; ++k)
    {
        const double a = tone (out, hz * k, kFs, 24000);
        s += a * a;
    }
    return db (std::sqrt (s) / tone (out, hz, kFs, 24000));
}
}

TEST_CASE ("families: every pair is measurably a different circuit", "[dist][families]")
{
    // The distance between two families is the RMS difference of their harmonic profiles
    // (H2 to H11 in dB, floored at -80 dB) over 55, 220 and 1000 Hz and drives 20, 50 and 90.
    // Before the console stage was rebuilt it sat 2.9 dB (hard knee) and 6.4 dB (soft knee)
    // from the diode pair on a similar measure.
    for (const float knee : { 100.0f, 0.0f })
    {
        std::vector<std::vector<double>> profile (kNumClipperFamilies);
        for (int f = 0; f < kNumClipperFamilies; ++f)
            for (const double hz : { 55.0, 220.0, 1000.0 })
                for (const float drive : { 20.0f, 50.0f, 90.0f })
                {
                    const auto h = harmonicProfile (familyParams (f, drive, knee), hz);
                    profile[static_cast<std::size_t> (f)].insert (profile[static_cast<std::size_t> (f)].end(), h.begin(), h.end());
                }

        for (int a = 0; a < kNumClipperFamilies; ++a)
        {
            double nearest = 1.0e9;
            for (int b = 0; b < kNumClipperFamilies; ++b)
            {
                if (a == b) continue;
                const auto& pa = profile[static_cast<std::size_t> (a)];
                const auto& pb = profile[static_cast<std::size_t> (b)];
                double s = 0.0;
                for (std::size_t i = 0; i < pa.size(); ++i)
                    s += (pa[i] - pb[i]) * (pa[i] - pb[i]);
                const double distance = std::sqrt (s / static_cast<double> (pa.size()));
                INFO ("knee " << knee << ": " << kNames[a] << " to " << kNames[b] << " " << distance << " dB");
                CHECK (distance >= 6.0);
                nearest = std::min (nearest, distance);
            }
            if (a == 6)
            {
                INFO ("knee " << knee << ": the console's nearest neighbour is " << nearest << " dB away");
                CHECK (nearest >= 10.0);
            }
        }
    }
}

TEST_CASE ("families: the console has a class-A stage and a transformer that saturates on the bass", "[dist][families]")
{
    // Even harmonics with the bias at zero: the diode pair has none.
    const auto console = harmonicProfile (familyParams (6, 20.0f), 220.0);
    const auto diode = harmonicProfile (familyParams (0, 20.0f), 220.0);
    INFO ("H2: console " << console[0] << " dB, diode pair " << diode[0] << " dB");
    CHECK (console[0] > -30.0);
    CHECK (diode[0] <= -80.0);

    // Low notes saturate the core before the mids distort; the diode pair does not care.
    for (const float drive : { 0.0f, 20.0f })
    {
        const double c40 = thdDb (familyParams (6, drive), 40.0), c1k = thdDb (familyParams (6, drive), 1000.0);
        const double d40 = thdDb (familyParams (0, drive), 40.0), d1k = thdDb (familyParams (0, drive), 1000.0);
        INFO ("drive " << drive << ": console THD " << c40 << " dB at 40 Hz, " << c1k << " dB at 1 kHz; diode pair "
                       << d40 << " and " << d1k << " dB");
        CHECK (c40 > c1k + 6.0);
        CHECK (std::abs (d40 - d1k) < 1.0);
    }
}

TEST_CASE ("families: a bias move is not a thump", "[dist][families][bias]")
{
    // In silence every family sits at its resting point whatever the bias, so stepping the
    // bias end to end moves next to nothing: only the scoop stage's tone network lags the
    // gliding resting point a little (-59 dBFS peak, a slow drift). Before, three families
    // left their resting DC in the output and every family started from zero on reset;
    // the scoop stage stepped by about -23 dBFS.
    for (int f = 0; f < kNumClipperFamilies; ++f)
    {
        auto p = familyParams (f, 50.0f);
        p.bias = -100.0f;
        Distortion d;
        d.prepare (kFs, 256);
        d.setOversampling (2);
        d.setParams (p);
        d.reset();
        std::vector<float> x (48000, 0.0f);
        for (int i = 0; i < 48000; i += 256)
        {
            if (i == 9984) { p.bias = 100.0f; d.setParams (p); }
            d.process (x.data() + i, nullptr, std::min (256, 48000 - i));
        }
        INFO (kNames[f] << ": peak " << db (peak (x)) << " dBFS");
        CHECK (peak (x) < (f == 3 ? 1.5e-3 : 1.0e-4));
    }

    // With a tone, a biased clipper turns signal into DC, and the bias changes how much:
    // that cannot be removed statically. The bias glides (200 ms) slower than the DC
    // blocker (6 Hz) follows, so a step comes out as a slow drift rather than a thump.
    // Measure: the peak of the output low-passed at 20 Hz after a -100 to +100 step (the
    // whole range at once, the worst case), against the RMS of the output. With the old
    // 15 ms glide and 4 Hz blocker it was, family by family: -1.6, -13.6, -5.3, +0.9,
    // -17.8, -7.3, -11.1 and -4.4 dB. The mid-hump stage scores better with the fast glide
    // (its signal-dependent DC and its resting point move together), but without its
    // resting point removed it gives -4.6 dB here and steps by -20 dBFS in silence. The
    // budgets are the measured values plus 2 dB.
    const double budget[kNumClipperFamilies] = { -6.0, -20.0, -15.4, -2.9, -6.7, -17.3, -16.5, -2.5 };
    for (int f = 0; f < kNumClipperFamilies; ++f)
    {
        auto p = familyParams (f, 50.0f);
        p.bias = -100.0f;
        Distortion d;
        d.prepare (kFs, 256);
        d.setOversampling (2);
        d.setParams (p);
        d.reset();
        const int n = 48000 * 3;
        auto x = sine (110.0, kNominalLevel, kFs, n);
        for (int i = 0; i < n; i += 256)
        {
            if (i >= n / 2) { p.bias = 100.0f; d.setParams (p); }
            d.process (x.data() + i, nullptr, std::min (256, n - i));
        }
        const double a = 1.0 - std::exp (-kTwoPi * 20.0 / kFs);
        double s1 = 0.0, s2 = 0.0, after = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            s1 += a * (static_cast<double> (x[i]) - s1);
            s2 += a * (s1 - s2);
            if (i >= static_cast<std::size_t> (n / 2)) after = std::max (after, std::abs (s2));
        }
        std::vector<float> before (x.begin() + 48000, x.begin() + n / 2);
        const double relative = db (after) - db (rms (before));
        INFO (kNames[f] << ": thump " << relative << " dB against the output RMS (budget " << budget[f] << ")");
        CHECK (relative < budget[f]);
    }
}

TEST_CASE ("families: one non-finite input sample does not latch", "[dist][families]")
{
    for (int f = 0; f < kNumClipperFamilies; ++f)
        for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 3.0e38f })
            for (const int factor : { 1, 4 })
            {
                auto in = sine (220.0, kNominalLevel, kFs, 48000);
                in[12000] = bad;
                in[12001] = -bad;
                const auto out = run (familyParams (f, 70.0f), in, factor);
                std::vector<float> tail (out.begin() + 36000, out.end());
                INFO (kNames[f] << ", " << factor << "x, bad sample " << bad);
                CHECK (allFinite (out));
                CHECK (rms (tail) > 1.0e-3);
                CHECK (peak (out) < 8.0);
            }
}

TEST_CASE ("families: cost of each family at 4x", "[dist][.benchmark]")
{
    const auto in = acidLine (kFs, 4.0);
    for (int f = 0; f < kNumClipperFamilies; ++f)
    {
        Distortion d;
        d.prepare (kFs, 256);
        d.setOversampling (4);
        d.setParams (familyParams (f, 70.0f));
        d.reset();
        auto x = in;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < x.size(); i += 256)
            d.process (x.data() + i, nullptr, static_cast<int> (std::min<std::size_t> (256, x.size() - i)));
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("[benchmark] %-10s at 4x: %6.0f ns per sample, %6.1f x real time\n", kNames[f],
                     s / static_cast<double> (x.size()) * 1.0e9, 4.0 / s);
    }
}
