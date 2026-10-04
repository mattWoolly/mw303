#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace acid
{
inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

template <typename T>
constexpr T clamp (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

template <typename T>
constexpr T lerp (T a, T b, T t) noexcept { return a + (b - a) * t; }

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double gain) noexcept { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

/** MIDI note number (69 = A4 = 440 Hz) to frequency; fractional notes allowed. */
inline double midiNoteToHz (double note, double a4Hz = 440.0) noexcept
{
    return a4Hz * std::exp2 ((note - 69.0) / 12.0);
}

/** Per-sample coefficient for a one-pole lag with time constant tauSeconds:
    y += coeff * (x - y). Exact for a sampled RC (step-invariant). */
inline double onePoleCoeff (double tauSeconds, double sampleRate) noexcept
{
    return tauSeconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (tauSeconds * sampleRate));
}

/** Maps a normalised 0..1 control to an exponential range lo..hi (lo, hi > 0). */
inline double expMap (double norm, double lo, double hi) noexcept
{
    return lo * std::pow (hi / lo, clamp (norm, 0.0, 1.0));
}
}
