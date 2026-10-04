#pragma once
// One-knob level-equalising compressor for the distortion block.
//
// The knob lowers the threshold and raises the ratio together, and the make-up gain is
// chosen so a signal peaking at the nominal level (-12 dBFS, a plain note from the voice)
// passes at unity: louder material (accents) is pulled down towards it, quieter material
// (decaying notes) is lifted towards it. Below about -54 dBFS the lift is released again,
// so silence and noise are not amplified and a note that follows a rest starts at unity.
//
// The detector is a peak detector with a 30 ms hold, so a steady tone down to about 17 Hz
// produces a constant gain (no ripple, no pumping). It has no look-ahead and no latency.
//
// Not thread-safe: prepare, setAmount and process belong to one thread at a time.
// Nothing here allocates.

namespace acid::fx
{
class Dynamics
{
public:
    /** Derives the time constants from the sample rate and resets. */
    void prepare (double sampleRate) noexcept;

    /** Detector and gain back to rest; the amount jumps to its target. */
    void reset() noexcept;

    /** 0..100 percent. 0 is exactly transparent once the internal smoothing has settled. */
    void setAmount (double percent) noexcept;

    /** True when process() would change the signal (amount above zero, or still fading out). */
    bool isActive() const noexcept { return amountTarget > 0.0 || amount > 0.0; }

    /** Advances one sample. peak is the largest absolute sample across the linked channels.
        Returns the linear gain to apply to every channel. */
    double processGain (double peak) noexcept;

    /** In-place stereo-linked processing. right may be nullptr for mono. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** The gain applied to the last sample, in dB (negative: reducing, positive: lifting). */
    double gainDb() const noexcept { return gain; }

private:
    double staticGainDb (double levelDb, double amt) const noexcept;

    double amountTarget = 0.0, amount = 0.0;   // 0..1
    double envelope = 0.0;
    double gain = 0.0;                         // dB
    int hold = 0, holdSamples = 1;
    double amountCoef = 1.0, envReleaseCoef = 1.0, attackCoef = 1.0, releaseCoef = 1.0;
};
}
