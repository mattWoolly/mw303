#pragma once
// Closed-form step timing. Everything here works in musical time (quarter notes, "Q")
// and converts to samples only at the very end, so step events are sample-accurate,
// independent of the block size, and re-time themselves when the tempo changes.
//
// A StepClock describes one pattern's grid: step g (any integer, negative included)
// starts at   anchorQ + g * stepQ + (odd step ? swingQ : 0).
// Odd and even are judged on the absolute grid (g + gridIndex), so a pattern with an
// odd number of steps does not flip its shuffle feel on every pass.
#include "acid/common/Rng.h"
#include "acid/seq/Pattern.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace acid::seq
{
/** What the stock sequencer CPU adds to half a step when it times a gate, in seconds. */
inline constexpr double kAuthenticGateExtraSeconds = 1.35e-3;

/** Slack of the musical-time to sample mapping, in samples. An event whose exact position
    is less than this far past a sample is given to that sample, which keeps the mapping
    stable against floating-point noise when an event sits exactly on a sample. */
inline constexpr double kSampleEpsilon = 1.0e-3;

/** Shuffle at 100 % delays every second step by this fraction of a step (pair ratio 75:25). */
inline constexpr double kMaxShuffleDelay = 0.5;

/** Step indices stay within plus or minus this. It is far beyond any real song position (the
    playhead tracker keeps positions within 1e9 quarter notes) and small enough that a double
    still tells neighbouring steps apart and that sums of two indices cannot overflow. */
inline constexpr std::int64_t kMaxStepIndex = std::int64_t { 1 } << 50;

/** Nominal step length in quarter notes for a division and its modifier. */
constexpr double stepLengthQ (StepDiv div, DivMod mod) noexcept
{
    double q = 0.25;
    switch (div)
    {
        case StepDiv::d1_64: q = 0.0625; break;
        case StepDiv::d1_32: q = 0.125; break;
        case StepDiv::d1_16: q = 0.25; break;
        case StepDiv::d1_8:  q = 0.5; break;
        case StepDiv::d1_4:  q = 1.0; break;
        case StepDiv::d1_2:  q = 2.0; break;
        case StepDiv::d1_1:  q = 4.0; break;
    }

    switch (mod)
    {
        case DivMod::Full:    break;
        case DivMod::Dotted:  q *= 1.5; break;
        case DivMod::Triplet: q *= 2.0 / 3.0; break;
    }

    return q;
}

/** Delay of every second step in quarter notes. Dotted and triplet divisions ignore shuffle. */
constexpr double shuffleDelayQ (const Pattern& p) noexcept
{
    if (p.divMod != DivMod::Full)
        return 0.0;

    const double pct = p.shuffle > 100 ? 100.0 : static_cast<double> (p.shuffle);
    return kMaxShuffleDelay * (pct / 100.0) * stepLengthQ (p.div, p.divMod);
}

/** Pattern length clamped to the legal range, so a damaged pattern cannot index out of bounds. */
constexpr int patternLength (const Pattern& p) noexcept
{
    return p.length < 1 ? 1 : (p.length > kMaxSteps ? kMaxSteps : static_cast<int> (p.length));
}

/** Gate length in quarter notes before it is clamped against the next onset.
    A step's own gate (1..127) wins over the pattern's (1..100 %); with neither set the
    authentic gate applies: half a step plus 1.35 ms at the given tempo. */
constexpr double gateLengthQ (const Pattern& p, const Step& s, double bpm) noexcept
{
    const double stepQ = stepLengthQ (p.div, p.divMod);

    if (s.gate != 0)
        return stepQ * static_cast<double> (s.gate > 127 ? 127 : s.gate) / 127.0;

    if (p.gateLength != 0)
        return stepQ * static_cast<double> (p.gateLength > 100 ? 100 : p.gateLength) / 100.0;

    return 0.5 * stepQ + kAuthenticGateExtraSeconds * bpm / 60.0;
}

/** Index of the first sample at or after musical time eventQ, counted from the sample that
    sits at startQ. Negative when the event lies before startQ. The same rule is used for
    every event, so the absolute sample of an event does not depend on where blocks start. */
inline std::int64_t sampleIndexAtOrAfter (double eventQ, double startQ, double quartersPerSample) noexcept
{
    const double x = (eventQ - startQ) / quartersPerSample - kSampleEpsilon;

    if (! (x < 4.0e18))
        return std::numeric_limits<std::int64_t>::max();   // also catches +inf and NaN

    if (x < -4.0e18)
        return std::numeric_limits<std::int64_t>::min();

    return static_cast<std::int64_t> (std::ceil (x));
}

constexpr std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
{
    const std::int64_t q = a / b;
    return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/** Remainder that is always in 0..b-1 for b > 0. */
constexpr std::int64_t floorMod (std::int64_t a, std::int64_t b) noexcept
{
    const std::int64_t m = a % b;
    return m < 0 ? m + b : m;
}

/** Steps in one pass of a pattern. Ping-pong does not repeat its end steps. */
constexpr int cycleLength (int length, Direction dir) noexcept
{
    if (dir == Direction::PingPong)
        return length > 1 ? 2 * length - 2 : 1;

    return length;
}

/** Deterministic 64-bit hash of (pattern seed, absolute step index, purpose). Used for the
    probability roll and for Random direction, so a render never depends on the block size
    or on whether it runs in real time. */
constexpr std::uint64_t stepHash (std::uint32_t seed, std::int64_t absIndex, std::uint32_t salt) noexcept
{
    Rng outer ((static_cast<std::uint64_t> (salt) << 32) ^ static_cast<std::uint64_t> (seed));
    Rng inner (outer.nextU64() + static_cast<std::uint64_t> (absIndex));
    return inner.nextU64();
}

inline constexpr std::uint32_t kSaltProbability = 0x50524F42u;
inline constexpr std::uint32_t kSaltDirection = 0x44495221u;

/** True when a step with this probability (0..100 %) plays at absolute step index absIndex. */
constexpr bool stepRollPasses (std::uint32_t seed, std::int64_t absIndex, int probability) noexcept
{
    if (probability >= 100) return true;
    if (probability <= 0) return false;
    return static_cast<int> (stepHash (seed, absIndex, kSaltProbability) % 100u) < probability;
}

/** Pattern step (0..length-1) played at position cycleIndex (0..cycleLength-1) of a pass.
    Random ignores cycleIndex and hashes the absolute step index instead. */
constexpr int stepPosition (Direction dir, int length, int cycleIndex, std::uint32_t seed, std::int64_t absIndex) noexcept
{
    switch (dir)
    {
        case Direction::Forward:  return cycleIndex;
        case Direction::Reverse:  return length - 1 - cycleIndex;
        case Direction::PingPong: return cycleIndex < length ? cycleIndex : cycleLength (length, dir) - cycleIndex;
        case Direction::Random:   return static_cast<int> (stepHash (seed, absIndex, kSaltDirection) % static_cast<std::uint64_t> (length));
    }

    return cycleIndex;
}

/** One pattern's step grid in musical time. */
struct StepClock
{
    double stepQ = 0.25;           // nominal step length
    double swingQ = 0.0;           // delay of odd grid steps, 0..stepQ/2
    double anchorQ = 0.0;          // unshuffled time of step 0
    std::int64_t gridIndex = 0;    // absolute grid index of step 0: fixes shuffle parity and the hash index

    static constexpr StepClock forPattern (const Pattern& p, double anchor, std::int64_t index) noexcept
    {
        return { stepLengthQ (p.div, p.divMod), shuffleDelayQ (p), anchor, index };
    }

    /** Unshuffled time of step g. */
    constexpr double gridQ (std::int64_t g) const noexcept { return anchorQ + static_cast<double> (g) * stepQ; }

    constexpr bool isDelayed (std::int64_t g) const noexcept { return swingQ > 0.0 && floorMod (g + gridIndex, 2) == 1; }

    /** When step g starts, shuffle included. Strictly increasing in g. */
    constexpr double onsetQ (std::int64_t g) const noexcept { return gridQ (g) + (isDelayed (g) ? swingQ : 0.0); }

    /** Time from the onset of step g to the onset of step g + 1. */
    constexpr double stepDurationQ (std::int64_t g) const noexcept { return onsetQ (g + 1) - onsetQ (g); }

    /** The smallest g whose onset is at or after q.
        Total and bounded for every input: a q more than kMaxStepIndex steps away from the
        anchor saturates to plus or minus kMaxStepIndex (above the range the returned step
        then starts before q, and the caller must not try to step its way there), and a NaN
        gives 0. */
    std::int64_t firstStepAtOrAfter (double q) const noexcept
    {
        const double rel = std::floor ((q - anchorQ) / stepQ);
        const double limit = static_cast<double> (kMaxStepIndex);

        if (! (rel < limit))
            return rel >= limit ? kMaxStepIndex : 0;    // 0: NaN

        if (rel <= -limit)
            return -kMaxStepIndex;

        std::int64_t g = static_cast<std::int64_t> (rel) - 1;

        // Three passes at most in exact arithmetic (the shuffle delay is under one step). The
        // count is capped so that rounding at the far end of the range cannot spin here.
        for (int pass = 0; pass < 8 && onsetQ (g) < q; ++pass)
            ++g;

        return g;
    }

    /** The step whose onset is the latest one at or before q (the step sounding at q). */
    std::int64_t stepAt (double q) const noexcept
    {
        const std::int64_t g = firstStepAtOrAfter (q);
        return onsetQ (g) > q ? g - 1 : g;
    }
};

/** Grid index for a clock anchored at anchorQ: the anchor's position on the absolute grid of
    step length stepQ when it sits on that grid, otherwise 0. */
inline std::int64_t gridIndexFor (double anchorQ, double stepQ) noexcept
{
    const double x = anchorQ / stepQ;
    const double r = std::round (x);

    if (! (std::abs (r) < static_cast<double> (kMaxStepIndex)) || std::abs (x - r) > 1.0e-6)
        return 0;

    return static_cast<std::int64_t> (r);
}
}
