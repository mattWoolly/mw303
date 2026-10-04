#pragma once
// Shared helpers for the distortion tests: test signals, level and spectrum measurement.
#include "acid/common/Math.h"
#include "acid/fx/DistortionVoicings.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace disttest
{
using acid::kTwoPi;

inline std::vector<float> sine (double freq, double amp, double fs, int n, double phase = 0.0)
{
    std::vector<float> x (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t> (i)] = static_cast<float> (amp * std::sin (kTwoPi * freq * i / fs + phase));
    return x;
}

/** Amplitude of the component at freq (Hann window, amplitude-corrected). */
template <typename T>
double tone (const std::vector<T>& x, double freq, double fs, std::size_t from = 0)
{
    const std::size_t n = x.size() - from;
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (n));
        const double ph = kTwoPi * freq * static_cast<double> (i) / fs;
        re += w * static_cast<double> (x[from + i]) * std::cos (ph);
        im -= w * static_cast<double> (x[from + i]) * std::sin (ph);
    }
    return 4.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
}

template <typename T>
double rms (const std::vector<T>& x, std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        s += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, x.size() - from)));
}

template <typename T>
double peak (const std::vector<T>& x, std::size_t from = 0)
{
    double p = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        p = std::max (p, std::abs (static_cast<double> (x[i])));
    return p;
}

template <typename T>
double mean (const std::vector<T>& x, std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        s += static_cast<double> (x[i]);
    return s / static_cast<double> (std::max<std::size_t> (1, x.size() - from));
}

template <typename T>
bool allFinite (const std::vector<T>& x)
{
    return std::all_of (x.begin(), x.end(), [] (T v) { return std::isfinite (v); });
}

inline double db (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-15)); }

/** Power spectrum (|X[k]|^2, k = 0 .. n/2) of n = 2^m samples starting at `from`, with no
    window: meant for signals periodic in n samples, whose components all sit on bins. */
template <typename T>
std::vector<double> powerSpectrum (const std::vector<T>& x, std::size_t from, std::size_t n)
{
    std::vector<double> re (n), im (n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        re[i] = static_cast<double> (x[from + i]);
    for (std::size_t i = 1, j = 0; i < n; ++i)   // bit reversal
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / static_cast<double> (len);
        for (std::size_t i = 0; i < n; i += len)
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const double wr = std::cos (ang * static_cast<double> (k)), wi = std::sin (ang * static_cast<double> (k));
                const std::size_t a = i + k, b = i + k + len / 2;
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
    std::vector<double> p (n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k)
        p[k] = re[k] * re[k] + im[k] * im[k];
    return p;
}

/** Aliasing of a distortion output: the power of every component below maxHz that is not
    a harmonic of the test tone, relative to the power of the harmonics, in dB. The tone
    must sit on bin `bin` of an n-point spectrum (and be periodic in it); choose bin so its
    aliases do not land on harmonic bins (a prime does). DC is excluded. */
template <typename T>
double aliasRatioDb (const std::vector<T>& y, std::size_t from, std::size_t n, std::size_t bin, double fs, double maxHz)
{
    const auto p = powerSpectrum (y, from, n);
    const auto top = std::min (p.size() - 1, static_cast<std::size_t> (maxHz / fs * static_cast<double> (n)));
    double harmonic = 0.0, other = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        if (k % bin == 0) harmonic += p[k];
        else if (k <= top) other += p[k];
    }
    return 10.0 * std::log10 (std::max (other, 1.0e-300) / std::max (harmonic, 1.0e-300));
}

/** How to play a test bass line: see bassLine(). */
struct LineSpec
{
    int notes[8] = { 33, 33, 45, 33, 36, 33, 31, 45 };   // MIDI notes, one per sixteenth, repeating
    double resonanceK = 1.0 / 4.5;                         // damping of the two filter sections (1 / Q)
    double cutoffBase = 260.0;                             // Hz, where the envelope sweep ends
    double levelDb = 0.0;                                  // plain-note peak relative to the nominal level
};

/** A bass line of the kind this block is built for: a band-limited saw through a swept
    resonant low-pass, sixteenth notes at 125 BPM, every fourth note accented (louder, with
    a deeper sweep). Scaled so the plain notes peak at the nominal level (0.25) times
    levelDb; the accents come out a little over twice that. Deterministic. */
inline std::vector<float> bassLine (const LineSpec& spec, double fs, double seconds)
{
    const int n = static_cast<int> (fs * seconds);
    std::vector<double> raw (static_cast<std::size_t> (n));
    const double stepSeconds = 60.0 / 125.0 / 4.0;

    double phase = 0.0, ic1a = 0.0, ic2a = 0.0, ic1b = 0.0, ic2b = 0.0, plainPeak = 1.0e-9;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const int step = static_cast<int> (t / stepSeconds);
        const double tn = t - step * stepSeconds;
        const bool accent = (step % 4) == 2;
        const bool gateOn = tn < 0.6 * stepSeconds;
        const double freq = acid::midiNoteToHz (spec.notes[step % 8]);

        const double inc = freq / fs;
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        double saw = 2.0 * phase - 1.0;
        if (phase < inc) { const double u = phase / inc; saw -= u + u - u * u - 1.0; }
        else if (phase > 1.0 - inc) { const double u = (phase - 1.0) / inc; saw -= u * u + u + u + 1.0; }

        // Two cascaded resonant sections, cutoff swept by a decaying envelope.
        const double env = std::exp (-tn / (accent ? 0.06 : 0.11));
        const double cutoff = std::min (0.45 * fs, spec.cutoffBase * std::pow (accent ? 24.0 : 9.0, env));
        const double g = std::tan (acid::kPi * cutoff / fs), k = spec.resonanceK;
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const auto tick = [&] (double x, double& ic1, double& ic2)
        {
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return v2;
        };
        const double filtered = tick (tick (saw, ic1a, ic2a) * 0.5, ic1b, ic2b);

        const double amp = (gateOn ? 1.0 : std::exp (-(tn - 0.6 * stepSeconds) / 0.004)) * (accent ? 1.8 : 1.0);
        raw[static_cast<std::size_t> (i)] = amp * filtered;
        if (! accent) plainPeak = std::max (plainPeak, std::abs (amp * filtered));
    }

    const double scale = 0.25 * std::pow (10.0, spec.levelDb / 20.0) / plainPeak;
    std::vector<float> out (raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        out[i] = static_cast<float> (raw[i] * scale);
    return out;
}

/** The reference line: A1 and friends, moderate resonance, plain notes at the nominal level. */
inline std::vector<float> acidLine (double fs, double seconds)
{
    return bassLine (LineSpec {}, fs, seconds);
}

/** The lines the voicing trims and the family level tables are fitted on. */
inline std::vector<LineSpec> calibrationLines()
{
    LineSpec quiet;
    quiet.levelDb = -6.0;
    LineSpec high;
    const int notes[8] = { 38, 26, 38, 50, 41, 29, 45, 33 };
    std::copy (notes, notes + 8, high.notes);
    high.resonanceK = 1.0 / 8.0;
    high.cutoffBase = 400.0;
    high.levelDb = 3.0;
    LineSpec low;
    const int lowNotes[8] = { 26, 29, 26, 38, 24, 31, 26, 33 };
    std::copy (lowNotes, lowNotes + 8, low.notes);
    low.resonanceK = 1.0 / 6.0;
    LineSpec peaky;
    const int peakyNotes[8] = { 36, 48, 36, 39, 31, 36, 43, 34 };
    std::copy (peakyNotes, peakyNotes + 8, peaky.notes);
    peaky.resonanceK = 1.0 / 12.0;
    return { LineSpec {}, quiet, high, low, peaky };
}

/** Lines no fit has seen, for checking that level matching generalises. */
inline std::vector<LineSpec> heldOutLines()
{
    LineSpec a;   // other notes, more resonance
    const int notesA[8] = { 28, 40, 28, 31, 43, 28, 26, 38 };
    std::copy (notesA, notesA + 8, a.notes);
    a.resonanceK = 1.0 / 12.0;
    LineSpec b;   // higher, darker, quieter
    const int notesB[8] = { 45, 45, 57, 40, 43, 52, 45, 36 };
    std::copy (notesB, notesB + 8, b.notes);
    b.resonanceK = 1.0 / 6.0;
    b.cutoffBase = 180.0;
    b.levelDb = -4.0;
    LineSpec c;   // low and hot
    const int notesC[8] = { 24, 36, 24, 27, 24, 31, 34, 36 };
    std::copy (notesC, notesC + 8, c.notes);
    c.resonanceK = 1.0 / 10.0;
    c.levelDb = 4.0;
    return { a, b, c };
}

/** Runs a mono buffer through the block in blocks of blockSize and returns the output. */
inline std::vector<float> render (acid::fx::Distortion& d, const std::vector<float>& in, int blockSize = 256)
{
    std::vector<float> out = in;
    const int n = static_cast<int> (out.size());
    for (int i = 0; i < n; i += blockSize)
        d.process (out.data() + i, nullptr, std::min (blockSize, n - i));
    return out;
}

/** The neutral voicing ("Custom"), switched on, drive at zero. */
inline acid::fx::DistortionParams neutralParams()
{
    acid::fx::DistortionParams p;
    p.on = true;
    p.drive = 0.0f;
    return p;
}

inline acid::fx::DistortionParams voicingParams (std::size_t index, float drive)
{
    acid::fx::DistortionParams p;
    p.on = true;
    p.drive = drive;
    p.model = static_cast<int> (index);
    acid::fx::applyVoicing (p, acid::fx::distortionVoicings()[index]);
    return p;
}
}
