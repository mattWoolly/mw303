#pragma once
#include <cstdint>

namespace acid
{
/** Small deterministic PRNG (SplitMix64). Same seed, same sequence, on every platform.
    Used wherever the engine needs randomness (generator, arpeggiator random mode, drift)
    so that renders and saved seeds are reproducible. */
class Rng
{
public:
    explicit constexpr Rng (std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept : state (seed) {}

    constexpr void seed (std::uint64_t s) noexcept { state = s; }

    constexpr std::uint64_t nextU64() noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /** Uniform in [0, 1). */
    constexpr double nextDouble() noexcept
    {
        return static_cast<double> (nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

    /** Uniform in [0, 1). */
    constexpr float nextFloat() noexcept
    {
        return static_cast<float> (nextU64() >> 40) * (1.0f / 16777216.0f);
    }

    /** Uniform integer in [0, n). Returns 0 when n <= 0. */
    constexpr int nextInt (int n) noexcept
    {
        return n <= 0 ? 0 : static_cast<int> (nextDouble() * static_cast<double> (n));
    }

    /** True with probability p. */
    constexpr bool chance (double p) noexcept { return nextDouble() < p; }

private:
    std::uint64_t state;
};
}
