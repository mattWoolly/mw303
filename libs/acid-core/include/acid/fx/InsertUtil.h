#pragma once
// Building blocks shared by the five insert effects (EQ, delay, reverb, chorus, limiter).
// They live in acid::fx::insert so their names cannot collide with the rest of acid::fx.
// Everything here is real-time safe once prepared: only the prepare() functions allocate.
#include "acid/common/Math.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace acid::fx::insert
{
/** Flushes values below about 1e-36 to zero so decaying state never goes denormal.
    Plain arithmetic (no branch, no FPU mode change); kept intact without -ffast-math. */
inline double flushDenormal (double x) noexcept
{
    constexpr double tiny = 1.0e-20;
    return (x + tiny) - tiny;
}

/** Largest input magnitude an insert accepts: 120 dB over full scale. Anything larger is
    a fault upstream, and gains of up to +24 dB applied to it could still overflow. */
inline constexpr double kMaxInput = 1.0e6;

/** An audio sample with NaN and infinities replaced by silence and wild values clamped to
    +-kMaxInput. One bad sample from upstream must never reach filter or loop state, where
    it would stay for good. */
inline double finiteSample (float x) noexcept
{
    return std::isfinite (x) ? clamp (static_cast<double> (x), -kMaxInput, kMaxInput) : 0.0;
}

/** Sanitises a host value: NaN and infinities become the fallback, then the value is clamped. */
inline double finiteOr (double v, double fallback, double lo, double hi) noexcept
{
    return std::isfinite (v) ? clamp (v, lo, hi) : fallback;
}

/** Smooth, bounded saturation: identity for |x| <= knee, then a tanh curve that approaches
    +-ceiling without reaching it. C1-continuous at the knee. Requires 0 < knee < ceiling. */
inline double softLimit (double x, double knee, double ceiling) noexcept
{
    const double a = std::abs (x);
    if (a <= knee) return x;
    const double room = ceiling - knee;
    const double y = knee + room * std::tanh ((a - knee) / room);
    return x < 0.0 ? -y : y;
}

/** Equal-power crossfade gains for a 0..1 mix: dry = cos, wet = sin. */
struct MixGains
{
    double dry = 1.0, wet = 0.0;
    static MixGains equalPower (double mix01) noexcept
    {
        const double m = clamp (mix01, 0.0, 1.0) * 0.5 * kPi;
        return { std::cos (m), std::sin (m) };
    }
    static MixGains linear (double mix01) noexcept
    {
        const double m = clamp (mix01, 0.0, 1.0);
        return { 1.0 - m, m };
    }
};

/** One-pole parameter smoother. The time constant is set at prepare; snap() jumps straight
    to a value. Once within epsilon of the target the value lands on it exactly, so callers
    can test settled() and skip coefficient work. */
class Smoother
{
public:
    void setTimeConstant (double tauSeconds, double sampleRate) noexcept { coeff = onePoleCoeff (tauSeconds, sampleRate); }
    void setEpsilon (double e) noexcept { epsilon = e; }
    void setTarget (double t) noexcept { target = t; }
    void snap (double v) noexcept { target = v; value = v; }

    double next() noexcept
    {
        if (value != target)
        {
            value += coeff * (target - value);
            if (std::abs (target - value) <= epsilon) value = target;
        }
        return value;
    }

    double current() const noexcept { return value; }
    double targetValue() const noexcept { return target; }
    bool settled() const noexcept { return value == target; }

private:
    double coeff = 1.0, epsilon = 1.0e-9, value = 0.0, target = 0.0;
};

/** Click-free on/off for an effect: a gain that ramps 0..1 over a few milliseconds,
    shaped by a smoothstep so the crossfade has no corner. When fully off the caller
    skips all processing, so an inactive effect costs one branch per block. */
class OnOffFade
{
public:
    void prepare (double sampleRate, double fadeSeconds) noexcept
    {
        step = 1.0 / std::max (1.0, fadeSeconds * sampleRate);
    }

    /** Jumps straight to on or off. */
    void reset (bool on) noexcept { target = on; ramp = on ? 1.0 : 0.0; }
    void setOn (bool on) noexcept { target = on; }

    bool isOn() const noexcept { return target; }
    /** Fully off and asked to stay off: the effect does no work. */
    bool idle() const noexcept { return ! target && ramp <= 0.0; }
    /** Fully on: the effect output is used as is. */
    bool fullyOn() const noexcept { return target && ramp >= 1.0; }

    /** Advances one sample and returns the shaped wet weight 0..1. */
    double next() noexcept
    {
        if (target) ramp = std::min (1.0, ramp + step);
        else        ramp = std::max (0.0, ramp - step);
        return ramp * ramp * (3.0 - 2.0 * ramp);
    }

    /** Crossfades dry to processed by weight w; exact at both ends, so a fade that ends
        inside a block gives the same bits as an unfaded block. */
    static double mix (double dry, double processed, double w) noexcept
    {
        if (w >= 1.0) return processed;
        if (w <= 0.0) return dry;
        return dry + w * (processed - dry);
    }

private:
    double step = 1.0, ramp = 0.0;
    bool target = false;
};

/** Coefficients of the trapezoidal (zero-delay) state variable filter. g = tan(pi f / fs),
    k = 1 / Q. Stays well behaved when g and k move every sample. */
struct SvfCoeffs
{
    double g = 0.0, k = 2.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;

    void set (double gIn, double kIn) noexcept
    {
        g = gIn;
        k = kIn;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    static double gFor (double hz, double sampleRate) noexcept
    {
        return std::tan (kPi * clamp (hz, 1.0, 0.49 * sampleRate) / sampleRate);
    }
};

/** One channel of state for SvfCoeffs. tick() returns the low-pass output and writes the
    band-pass (unnormalised, peak gain Q) to `band`; high-pass is x - k * band - low. */
struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept { ic1 = ic2 = 0.0; }

    double tick (const SvfCoeffs& c, double x, double& band) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = flushDenormal (2.0 * v1 - ic1);
        ic2 = flushDenormal (2.0 * v2 - ic2);
        band = v1;
        return v2;
    }
};

/** Trapezoidal one-pole low-pass. g = tan(pi f / fs). */
struct OnePoleLp
{
    double s = 0.0;

    void reset() noexcept { s = 0.0; }

    double tick (double x, double g) noexcept
    {
        const double v = (x - s) * g / (1.0 + g);
        const double y = v + s;
        s = flushDenormal (y + v);
        return y;
    }
};

/** Power-of-two ring buffer of floats. Reads happen before the write of the current
    sample, so read(1) is the sample pushed last and a delay of d samples needs d >= 1.
    The first kGuard samples are mirrored past the end, so an interpolating read touches
    consecutive floats with a single wrap of the index.

    clear() is constant time: the line counts the samples pushed since the last clear and
    every read treats anything older as zero, so the result is the same as a memset of the
    whole buffer without touching it (a line can be many MB at high rates, far too much to
    clear inside one audio block). Non-finite input is stored as zero, so one bad sample
    can never circulate for ever. */
class DelayLine
{
public:
    /** Allocates room for delays up to maxDelaySamples (plus interpolation margin). */
    void prepare (std::size_t maxDelaySamples)
    {
        std::size_t n = 8;
        while (n < maxDelaySamples + kGuard) n <<= 1;
        buffer.assign (n + kGuard, 0.0f);
        size = n;
        mask = n - 1;
        writePos = 0;
        fresh = 0;
    }

    /** Every sample now reads as zero. Constant time, real-time safe. */
    void clear() noexcept { fresh = 0; }

    /** Longest delay any read may ask for. */
    std::size_t capacity() const noexcept { return size - kGuard; }

    void push (double x) noexcept
    {
        const auto v = std::isfinite (x) ? static_cast<float> (x) : 0.0f;
        buffer[writePos] = v;
        if (writePos < kGuard) buffer[size + writePos] = v;
        writePos = (writePos + 1) & mask;
        fresh += fresh < size ? 1u : 0u;
    }

    /** Integer delay, 1 <= d <= capacity(). */
    double read (std::size_t d) const noexcept { return d <= fresh ? buffer[(writePos - d) & mask] : 0.0f; }

    /** Fractional delay with linear interpolation, 1 <= d <= capacity(). */
    double readLinear (double d) const noexcept
    {
        const auto whole = static_cast<std::size_t> (d);     // d > 0: truncation is floor, and avoids a libm call
        const double frac = d - static_cast<double> (whole);
        const float* p = buffer.data() + ((writePos - whole - 1) & mask);
        double b = p[0], a = p[1];                           // a = x[n - whole], b = x[n - whole - 1]
        if (whole + 1 > fresh)
        {
            b = 0.0;
            if (whole > fresh) a = 0.0;
        }
        return a + frac * (b - a);
    }

    /** Fractional delay with 4-point Catmull-Rom interpolation (gain never above 1),
        2 <= d <= capacity(). */
    double readCubic (double d) const noexcept
    {
        const auto whole = static_cast<std::size_t> (d);
        const double t = d - static_cast<double> (whole);
        const float* p = buffer.data() + ((writePos - whole - 2) & mask);
        double x2 = p[0], x1 = p[1], x0 = p[2], xm1 = p[3];  // delays whole + 2, + 1, whole, whole - 1
        if (whole + 2 > fresh)
        {
            x2 = 0.0;
            if (whole + 1 > fresh) x1 = 0.0;
            if (whole > fresh) x0 = 0.0;
            if (whole - 1 > fresh) xm1 = 0.0;
        }
        const double c1 = 0.5 * (x1 - xm1);
        const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    static constexpr std::size_t kGuard = 4;
    std::vector<float> buffer;
    std::size_t size = 8, mask = 0, writePos = 0;
    std::size_t fresh = 0;   // samples pushed since the last clear, saturating at size
};

/** A pair of integer read taps that changes delay by crossfading from the old tap to the
    new one (no pitch glide, no click). A change requested mid-fade waits for the fade to
    finish, then fades again, so fast automation moves in fade-length steps. */
class CrossfadeTaps
{
public:
    void prepare (double sampleRate, double fadeSeconds) noexcept
    {
        step = 1.0 / std::max (1.0, fadeSeconds * sampleRate);
    }

    /** Jumps straight to the given delays (no fade). */
    void snap (std::size_t left, std::size_t right) noexcept
    {
        cur[0] = nxt[0] = pend[0] = left;
        cur[1] = nxt[1] = pend[1] = right;
        t = 0.0;
        fading = false;
    }

    void setTarget (std::size_t left, std::size_t right) noexcept { pend[0] = left; pend[1] = right; }

    /** Advances one sample; returns the shaped weight of the new tap (0 when not fading). */
    double next() noexcept
    {
        if (! fading)
        {
            if (pend[0] == cur[0] && pend[1] == cur[1]) return 0.0;
            nxt[0] = pend[0];
            nxt[1] = pend[1];
            t = 0.0;
            fading = true;
        }
        t += step;
        if (t >= 1.0)
        {
            cur[0] = nxt[0];
            cur[1] = nxt[1];
            fading = false;
            return 0.0;
        }
        return t * t * (3.0 - 2.0 * t);
    }

    /** Reads channel ch (0 or 1) from `line`, using the weight returned by next(). */
    double read (const DelayLine& line, int ch, double w) const noexcept
    {
        const auto c = static_cast<std::size_t> (ch);
        const double a = line.read (cur[c]);
        return w > 0.0 ? a + w * (line.read (nxt[c]) - a) : a;
    }

    std::size_t current (int ch) const noexcept { return cur[static_cast<std::size_t> (ch)]; }
    std::size_t longest() const noexcept { return std::max (std::max (cur[0], cur[1]), std::max (nxt[0], nxt[1])); }
    bool isFading() const noexcept { return fading; }

private:
    std::size_t cur[2] { 1, 1 }, nxt[2] { 1, 1 }, pend[2] { 1, 1 };
    double t = 0.0, step = 1.0;
    bool fading = false;
};
}
