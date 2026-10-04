#pragma once
// Private helpers shared by the acid_seq sources. Integer-only, so results are identical
// on every compiler and platform.
#include "acid/common/Rng.h"
#include "acid/seq/Pattern.h"
#include <cstdint>

namespace acid::seq::detail
{
/** Clamps any integral value into [lo, hi] as an int. */
template <typename T>
constexpr int clampInt (T v, int lo, int hi) noexcept
{
    const long long x = static_cast<long long> (v);
    return x < lo ? lo : (x > hi ? hi : static_cast<int> (x));
}

/** An independent stream for one lane: the seed and a per-lane salt are mixed through
    SplitMix64 once, and the mixed value seeds the stream. */
inline Rng laneRng (std::uint64_t seed, std::uint64_t salt) noexcept
{
    Rng mixer (seed ^ salt);
    return Rng (mixer.nextU64());
}

/** Uniform 0..127. */
inline std::uint8_t draw7 (Rng& r) noexcept { return static_cast<std::uint8_t> (r.nextU64() >> 57); }

/** Uniform integer in [0, n) for n >= 1, by multiply-shift of the top 32 bits. */
inline std::uint32_t drawBelow (Rng& r, std::uint32_t n) noexcept
{
    return static_cast<std::uint32_t> (((r.nextU64() >> 32) * static_cast<std::uint64_t> (n)) >> 32);
}

/** True with probability percent / 100 (percent 0..100). Always consumes one draw. */
inline bool drawPercent (Rng& r, int percent) noexcept
{
    return static_cast<int> (drawBelow (r, 100)) < percent;
}

/** Picks an index from integer weights (total > 0). Always consumes one draw. */
template <int N>
int pickWeighted (Rng& r, const int (&weights)[N]) noexcept
{
    int total = 0;
    for (int w : weights) total += w;
    if (total <= 0)
    {
        r.nextU64();
        return 0;
    }
    int u = static_cast<int> (drawBelow (r, static_cast<std::uint32_t> (total)));
    for (int i = 0; i < N; ++i)
    {
        if (u < weights[i]) return i;
        u -= weights[i];
    }
    return N - 1;
}

constexpr int positiveMod (long long v, int m) noexcept
{
    const long long r = v % m;
    return static_cast<int> (r < 0 ? r + m : r);
}

/** Mask bit k means pitch class (root + k) mod 12 is allowed; returns the absolute
    pitch-class mask. */
constexpr std::uint16_t absoluteNoteMask (std::uint16_t relMask, int root) noexcept
{
    std::uint16_t out = 0;
    for (int k = 0; k < 12; ++k)
        if ((relMask >> k) & 1u)
            out = static_cast<std::uint16_t> (out | (1u << positiveMod (root + k, 12)));
    return out;
}

// ---- Generator hooks used by the grid edits (defined in Generator.cpp) -----------------

/** The step a slide on step leads into for this length: the first later step that is not a
    Tie, cyclic over [0, length) for a step inside the length; for a hidden step the next
    hidden step that is not a Tie, and none past step 63. -1 when there is none. */
int slideTarget (const Pattern&, int step, int length) noexcept;

/** True when slideTarget exists and is a Note. */
bool slideTargetIsNote (const Pattern&, int step, int length) noexcept;

/** Applies the slide rule of applyDensities to one step (no-op unless the step has slide
    memory, its slide lane is unlocked and GenParams::useSlide is on). */
void rederiveSlide (Pattern&, int step) noexcept;
}
