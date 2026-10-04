#pragma once
// Shared helpers for the insert-effect tests: stereo buffers, block rendering, a radix-2
// FFT, Goertzel, Schroeder decay measurement and WAV dumps to ACID_TEST_OUTPUT_DIR.
#include "Wav.h"
#include "acid/common/Math.h"
#include "acid/common/Rng.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace acid::test
{
struct Stereo
{
    std::vector<float> l, r;
    Stereo() = default;
    explicit Stereo (std::size_t n) : l (n, 0.0f), r (n, 0.0f) {}
    std::size_t size() const { return l.size(); }
};

/** Renders the whole buffer in place through fx in blocks of blockSize, calling
    setParams (fx, sampleIndex) before each block. */
template <typename Fx, typename SetFn>
void renderBlocks (Fx& fx, Stereo& buf, int blockSize, SetFn&& setParams)
{
    const int n = static_cast<int> (buf.size());
    for (int pos = 0; pos < n; pos += blockSize)
    {
        const int len = std::min (blockSize, n - pos);
        setParams (fx, pos);
        fx.process (buf.l.data() + pos, buf.r.data() + pos, len);
    }
}

template <typename Fx, typename P>
void renderConst (Fx& fx, Stereo& buf, const P& p, int blockSize = 256)
{
    renderBlocks (fx, buf, blockSize, [&p] (Fx& f, int) { f.setParams (p); });
}

inline Stereo impulse (std::size_t n, float amp = 1.0f, std::size_t at = 0)
{
    Stereo s (n);
    s.l[at] = s.r[at] = amp;
    return s;
}

inline Stereo sine (std::size_t n, double hz, double sr, double amp = 0.5)
{
    Stereo s (n);
    for (std::size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = static_cast<float> (amp * std::sin (kTwoPi * hz * static_cast<double> (i) / sr));
    return s;
}

inline Stereo noise (std::size_t n, std::uint64_t seed, double amp = 0.5)
{
    Rng rng (seed);
    Stereo s (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        s.l[i] = static_cast<float> (amp * (2.0 * rng.nextDouble() - 1.0));
        s.r[i] = static_cast<float> (amp * (2.0 * rng.nextDouble() - 1.0));
    }
    return s;
}

inline bool allFinite (const Stereo& s)
{
    for (std::size_t i = 0; i < s.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i])) return false;
    return true;
}

inline double peakAbs (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    double p = 0.0;
    for (std::size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, static_cast<double> (std::abs (v[i])));
    return p;
}

inline double peakAbs (const Stereo& s) { return std::max (peakAbs (s.l), peakAbs (s.r)); }

inline double energy (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    double e = 0.0;
    for (std::size_t i = from; i < std::min (to, v.size()); ++i) e += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return e;
}

inline double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    return std::sqrt (energy (v, from, to) / static_cast<double> (to - from));
}

inline double maxStep (const std::vector<float>& v, std::size_t from = 1, std::size_t to = SIZE_MAX)
{
    double m = 0.0;
    for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, v.size()); ++i)
        m = std::max (m, static_cast<double> (std::abs (v[i] - v[i - 1])));
    return m;
}

/** Magnitude of the DFT of v at hz (Goertzel, direct sum in double). */
inline double dftMagnitude (const std::vector<float>& v, double hz, double sr)
{
    const double w = kTwoPi * hz / sr;
    std::complex<double> acc {};
    for (std::size_t i = 0; i < v.size(); ++i)
        acc += static_cast<double> (v[i]) * std::polar (1.0, -w * static_cast<double> (i));
    return std::abs (acc);
}

/** In-place radix-2 complex FFT; size must be a power of two. */
inline void fft (std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Power spectrum (|X|^2) of v[from, from + n), n a power of two, Hann windowed. */
inline std::vector<double> powerSpectrum (const std::vector<float>& v, std::size_t from, std::size_t n)
{
    std::vector<std::complex<double>> a (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (n));
        a[i] = { (from + i < v.size() ? static_cast<double> (v[from + i]) : 0.0) * w, 0.0 };
    }
    fft (a);
    std::vector<double> p (n / 2);
    for (std::size_t i = 0; i < n / 2; ++i) p[i] = std::norm (a[i]);
    return p;
}

/** Decay time from the Schroeder backward integral: fits the -5..-35 dB span and
    extrapolates to 60 dB (T30). Returns 0 when the curve never reaches -35 dB. */
inline double schroederT60 (const std::vector<float>& ir, double sr)
{
    std::vector<double> edc (ir.size());
    double acc = 0.0;
    for (std::size_t i = ir.size(); i-- > 0;)
    {
        acc += static_cast<double> (ir[i]) * static_cast<double> (ir[i]);
        edc[i] = acc;
    }
    const double total = edc[0];
    double t5 = -1.0, t35 = -1.0;
    for (std::size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (edc[i] / total, 1.0e-30));
        if (t5 < 0.0 && db <= -5.0) t5 = static_cast<double> (i) / sr;
        if (t35 < 0.0 && db <= -35.0) { t35 = static_cast<double> (i) / sr; break; }
    }
    if (t5 < 0.0 || t35 < 0.0) return 0.0;
    return (t35 - t5) * 2.0;
}

inline double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        ab += static_cast<double> (a[i]) * static_cast<double> (b[i]);
        aa += static_cast<double> (a[i]) * static_cast<double> (a[i]);
        bb += static_cast<double> (b[i]) * static_cast<double> (b[i]);
    }
    return ab / std::sqrt (std::max (aa * bb, 1.0e-300));
}

inline void dumpWav (const std::string& name, const Stereo& s, double sr)
{
    std::vector<float> inter (s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        inter[2 * i] = s.l[i];
        inter[2 * i + 1] = s.r[i];
    }
    writeWav (std::string (ACID_TEST_OUTPUT_DIR) + "/" + name, inter, 2, sr);
}

inline constexpr double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
}
