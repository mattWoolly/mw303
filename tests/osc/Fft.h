#pragma once
// Small FFT and spectrum helpers for the oscillator and decimator tests.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace acid::test
{
using Complex = std::complex<double>;

inline bool isPowerOfTwo (std::size_t n) noexcept { return n >= 2 && (n & (n - 1)) == 0; }

/** In-place iterative radix-2 FFT (forward, no scaling). Size must be a power of two. */
inline void fft (std::vector<Complex>& a)
{
    const std::size_t n = a.size();
    if (! isPowerOfTwo (n)) return;

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * std::numbers::pi / static_cast<double> (len);
        // Twiddles come from cos and sin directly, not from repeated multiplication, to keep rounding low.
        std::vector<Complex> w (len / 2);
        for (std::size_t k = 0; k < len / 2; ++k)
            w[k] = Complex (std::cos (ang * static_cast<double> (k)), std::sin (ang * static_cast<double> (k)));
        for (std::size_t i = 0; i < n; i += len)
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const Complex x = a[i + k];
                const Complex y = a[i + k + len / 2] * w[k];
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
            }
    }
}

/** Modified Bessel function of the first kind, order zero. */
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 500; ++k)
    {
        term *= q / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;
        if (term < 1.0e-18 * sum) break;
    }
    return sum;
}

/** Kaiser window. beta = 30 puts every side lobe below -220 dB; the main lobe is then
    about +-10 bins wide. */
inline std::vector<double> kaiserWindow (std::size_t n, double beta)
{
    std::vector<double> w (n);
    const double denom = besselI0 (beta);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double r = 2.0 * static_cast<double> (i) / static_cast<double> (n - 1) - 1.0;
        w[i] = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
    }
    return w;
}

/** Half-width, in bins, of the Kaiser main lobe (centre to first null). */
inline double kaiserMainLobeBins (double beta) noexcept
{
    return std::sqrt (1.0 + (beta / std::numbers::pi) * (beta / std::numbers::pi));
}

/** Magnitude spectrum (bins 0..n/2) of a real signal through a Kaiser window, scaled so a
    sine of amplitude 1 centred on a bin reads 1. Uses the last n samples of x, n being the
    largest power of two that fits. */
inline std::vector<double> windowedSpectrum (const std::vector<double>& x, double beta = 30.0)
{
    std::size_t n = 2;
    while (n * 2 <= x.size()) n *= 2;
    const auto w = kaiserWindow (n, beta);
    double wSum = 0.0;
    for (double v : w) wSum += v;

    std::vector<Complex> a (n);
    const std::size_t off = x.size() - n;
    for (std::size_t i = 0; i < n; ++i) a[i] = Complex (x[off + i] * w[i], 0.0);
    fft (a);

    std::vector<double> mag (n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k) mag[k] = 2.0 * std::abs (a[k]) / wSum;
    return mag;
}

struct SpurReport
{
    double carrierLevel = 0.0;      // amplitude of the strongest on-grid line
    double carrierHz = 0.0;
    double worstSpurDbc = -400.0;   // strongest off-grid component relative to it
    double worstSpurHz = 0.0;
};

/** Looks for anything that is not on the grid of multiples of gridHz, below maxHz. The
    spectrum comes from windowedSpectrum(); fftSize is its transform length. Grid lines and
    DC are masked maskBins either side. */
inline SpurReport findSpurs (const std::vector<double>& mag, std::size_t fftSize, double sampleRate,
                             double gridHz, double maxHz, double maskBins = 14.0)
{
    SpurReport r;
    const double binHz = sampleRate / static_cast<double> (fftSize);
    const auto last = std::min (mag.size() - 1, static_cast<std::size_t> (maxHz / binHz));

    std::vector<char> masked (mag.size(), 0);
    for (double f = 0.0; f <= sampleRate * 0.5 + gridHz; f += gridHz)
    {
        const double c = f / binHz;
        const auto lo = static_cast<long> (std::floor (c - maskBins));
        const auto hi = static_cast<long> (std::ceil (c + maskBins));
        for (long k = std::max (0L, lo); k <= hi && k < static_cast<long> (mag.size()); ++k)
            masked[static_cast<std::size_t> (k)] = 1;
    }

    for (std::size_t k = 0; k <= last; ++k)
        if (masked[k] && mag[k] > r.carrierLevel && static_cast<double> (k) > maskBins)
        {
            r.carrierLevel = mag[k];
            r.carrierHz = static_cast<double> (k) * binHz;
        }

    double worst = 0.0;
    for (std::size_t k = 0; k <= last; ++k)
        if (! masked[k] && mag[k] > worst)
        {
            worst = mag[k];
            r.worstSpurHz = static_cast<double> (k) * binHz;
        }

    r.worstSpurDbc = 20.0 * std::log10 (std::max (worst, 1.0e-300) / std::max (r.carrierLevel, 1.0e-300));
    return r;
}

/** Amplitude of the component at exactly hz: the Kaiser-windowed signal correlated with a
    complex exponential at that frequency. No bin rounding, so no scalloping loss; other
    components more than a main lobe away do not leak in. */
inline double toneAmplitude (const std::vector<double>& x, double sampleRate, double hz, double beta = 30.0)
{
    const auto w = kaiserWindow (x.size(), beta);
    const double step = -2.0 * std::numbers::pi * hz / sampleRate;
    Complex acc (0.0, 0.0);
    double wSum = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double a = step * static_cast<double> (i);
        acc += x[i] * w[i] * Complex (std::cos (a), std::sin (a));
        wSum += w[i];
    }
    return 2.0 * std::abs (acc) / wSum;
}

/** Amplitude of the spectral line nearest to hz (peak over the main lobe). */
inline double lineLevel (const std::vector<double>& mag, std::size_t fftSize, double sampleRate, double hz,
                         double searchBins = 3.0)
{
    const double c = hz * static_cast<double> (fftSize) / sampleRate;
    const auto lo = static_cast<long> (std::floor (c - searchBins));
    const auto hi = static_cast<long> (std::ceil (c + searchBins));
    double best = 0.0;
    for (long k = std::max (0L, lo); k <= hi && k < static_cast<long> (mag.size()); ++k)
        best = std::max (best, mag[static_cast<std::size_t> (k)]);
    return best;
}
}
