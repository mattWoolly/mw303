#pragma once
// Band-limited 1x/2x/4x/8x resampler for the distortion block: a cascade of up to three
// linear-phase half-band FIR stages, designed from the sample rate in prepare().
//
// One instance handles one channel. It owns no heap memory (every buffer is a fixed
// array), so it is trivially copyable and switching the factor never allocates.
//
// The round trip upsample() then downsample() is a pure delay of latencySamples() base
// rate samples (an exact integer at every factor) with a flat passband up to
// min (20 kHz, 0.45 * sampleRate).
#include <array>

namespace acid::fx
{
class Oversampler
{
public:
    static constexpr int kMaxFactor = 8;
    static constexpr int kNumStages = 3;
    static constexpr int kMaxPhaseTaps = 64;   // non-zero taps per half-band phase
    static constexpr int kMaxBlock = 32;       // largest numSamples accepted by upsample/downsample

    /** Designs the three half-band stages for this base sample rate and clears the state.
        Keeps the current factor (1 after construction). Does not allocate. */
    void prepare (double sampleRate) noexcept;

    /** Clears every delay line. The factor and the filters are kept. */
    void reset() noexcept;

    /** 1, 2, 4 or 8 (anything else is rounded down to the nearest of those). Clears the state. */
    void setFactor (int factor) noexcept;

    int factor() const noexcept { return 1 << numActive; }

    /** Round-trip delay of upsample() + downsample() at the current factor, in base-rate samples. */
    int latencySamples() const noexcept { return latency[static_cast<std::size_t> (numActive)]; }

    /** The same figure for any factor, without switching to it. */
    int latencyForFactor (int factor) const noexcept;

    /** Non-zero taps per phase of stage 0..2 (stage 0 is the 1x to 2x stage). For tests and reports. */
    int phaseTaps (int stage) const noexcept { return stages[static_cast<std::size_t> (stage)].taps; }

    /** in: numSamples at the base rate. out: numSamples * factor() at the high rate.
        numSamples must not exceed kMaxBlock. in and out must not overlap. */
    void upsample (const double* in, double* out, int numSamples) noexcept;

    /** in: numSamples * factor() at the high rate. out: numSamples at the base rate.
        numSamples must not exceed kMaxBlock. in and out must not overlap. */
    void downsample (const double* in, double* out, int numSamples) noexcept;

private:
    struct Stage
    {
        int taps = 4;                                        // M + 1, where the filter has 2M + 1 taps
        std::array<double, kMaxPhaseTaps> coef {};           // odd-offset taps times two (interpolation gain)
        std::array<double, 2 * kMaxPhaseTaps> upHist {};     // input history, written twice for contiguous reads
        std::array<double, 2 * kMaxPhaseTaps> downHist {};   // even-stream history
        std::array<double, kMaxPhaseTaps> downCentre {};     // odd-stream delay feeding the centre tap
        int upPos = 0, downPos = 0, centrePos = 0;

        void design (double transition, double attenuationDb) noexcept;
        void clear() noexcept;
        void up (const double* in, double* out, int n) noexcept;
        void down (const double* in, double* out, int n) noexcept;
    };

    static int stagesForFactor (int factor) noexcept;

    std::array<Stage, kNumStages> stages {};
    std::array<int, kNumStages + 1> latency {};      // per stage count, base-rate samples
    std::array<int, kNumStages + 1> padSamples {};   // extra high-rate delay that makes the latency an integer
    std::array<double, kMaxFactor> padLine {};
    int padPos = 0;
    int numActive = 0;
};
}
