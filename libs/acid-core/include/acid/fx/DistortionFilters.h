#pragma once
// Small linear filters used around the clipper: trapezoidal (zero-delay feedback) one-pole
// and state-variable sections. Coefficients are kept apart from state so two channels can
// share one set. Every coefficient is computed from the sample rate passed to set().
#include "acid/common/Math.h"

namespace acid::fx::detail
{
/** Cutoff clamped to a range the bilinear warp can represent at this rate. */
inline double clampCutoff (double hz, double sampleRate) noexcept
{
    return clamp (hz, 0.01, 0.49 * sampleRate);
}

//==============================================================================
struct OnePoleCoeffs
{
    double gain = 0.0;   // g / (1 + g)

    void set (double cutoffHz, double sampleRate) noexcept
    {
        const double g = std::tan (kPi * clampCutoff (cutoffHz, sampleRate) / sampleRate);
        gain = g / (1.0 + g);
    }
};

struct OnePoleState
{
    double s = 0.0;
};

/** One-pole low-pass. The matching high-pass is x minus this. */
inline double onePoleLp (const OnePoleCoeffs& c, OnePoleState& st, double x) noexcept
{
    const double v = (x - st.s) * c.gain;
    const double y = v + st.s;
    st.s = y + v;
    return y;
}

//==============================================================================
struct SvfCoeffs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.0;

    void set (double cutoffHz, double q, double sampleRate) noexcept
    {
        const double g = std::tan (kPi * clampCutoff (cutoffHz, sampleRate) / sampleRate);
        k = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
};

struct SvfOut
{
    double low, band, high;
};

/** Trapezoidal state-variable filter: low, band and high outputs from one tick.
    band is the unnormalised band-pass (peak gain Q). */
inline SvfOut svfTick (const SvfCoeffs& c, SvfState& st, double x) noexcept
{
    const double v3 = x - st.ic2;
    const double v1 = c.a1 * st.ic1 + c.a2 * v3;
    const double v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
    st.ic1 = 2.0 * v1 - st.ic1;
    st.ic2 = 2.0 * v2 - st.ic2;
    return { v2, v1, x - c.k * v1 - v2 };
}

/** Q of a peaking band from its width in octaves (the -3 dB points of the bell). */
inline double qFromOctaves (double octaves) noexcept
{
    const double w = std::exp2 (clamp (octaves, 0.1, 6.0));
    return std::sqrt (w) / (w - 1.0);
}
}
