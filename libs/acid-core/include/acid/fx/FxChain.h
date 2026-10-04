#pragma once
// The effects chain: distortion, EQ, delay, reverb, chorus and limiter in series, in any
// order, with a click-free master bypass.
//
//   in (L, R) -+-> slot 0 -> slot 1 -> ... -> slot 5 --(bypass crossfade)--(reorder gate)--> out
//              |                                              ^
//              +--> dry, delayed by latencySamples() ---------+
//
// Call order: prepare() with audio stopped, then on the audio thread, once per block,
// setParams() followed by process(). setDistortionOversampling() may be called on the
// audio thread at any time. meters() reads what the last process() measured.
//
// Order. FxParams::order lists FxSlot values, order[0] first. Anything that is not a
// permutation of the six slots falls back to the default order. A new order is applied at
// once when it does not change the sequence of the blocks that are running (blocks that
// are off do nothing, so moving them is exact; an oversampled distortion counts as running
// even when off, because it still delays). Otherwise the swap is made under gates
// (kReorderFadeSeconds each way, smoothstep): one on the input of every running block whose
// running predecessor changes (the block, or the chain input, that feeds it), and one on the
// chain output when the last running block changes. A block whose feed is unchanged needs no
// gate: with every changed feed silent at the swap, every block's input is continuous, so
// its output is too. Gating inputs, not only the output, matters: a step at the input of
// the delay, reverb or chorus would come back out of their lines after the gates reopened.
// Every block keeps its state across the swap, so tails carry on. The cost is a short dip
// in what flows through a gated block, about 2 x kReorderFadeSeconds, instead of a click;
// a delay, reverb or chorus downstream of a gated block keeps that dip in its tail and
// replays it in the echoes (fainter each time). A true crossfade would need a second copy
// of every stateful block (tens of MB at 192 kHz). An order that arrives during the dip is
// folded into it: its gates close too, and the latest order is applied once all are shut.
//
// Bypass. fx_bypass crossfades (kBypassFadeSeconds) between the chain output and the dry
// input delayed by latencySamples(), so the latency a host compensates for is the same
// with bypass on or off. Once fully bypassed the six blocks are not run at all; when the
// bypass is released they are reset first (tails do not resume from where they stopped)
// and faded back in, their input fading in with them so no line ever holds a step. The
// reset is cheap: the delay lines clear in constant time (insert::DelayLine), so releasing
// the bypass, or switching a block on, costs no more than an ordinary block. A change of
// oversampling factor while the dry path is audible is made under the output gate.
//
// Bad input. A NaN or infinite input sample is replaced by silence, and anything beyond
// +-1e6 (120 dB over full scale) is clamped, before the dry path and the blocks.
//
// Latency is the distortion's oversampling delay at the requested factor (0 at 1x),
// whether the distortion is on or off and whether the chain is bypassed.
//
// Real time: after prepare() nothing here allocates, locks or throws. The class is not
// thread-safe: prepare() must never run while process() can (the engine must stop the
// audio callback, or prepare a fresh chain elsewhere and hand it over). Until the first
// prepare(), process() leaves the buffers untouched.
#include "acid/engine/ParamTable.h"
#include "acid/engine/Settings.h"
#include "acid/fx/Chorus.h"
#include "acid/fx/Delay.h"
#include "acid/fx/Distortion.h"
#include "acid/fx/DistortionVoicings.h"
#include "acid/fx/Eq.h"
#include "acid/fx/Limiter.h"
#include "acid/fx/Reverb.h"
#include <array>
#include <cstdint>

namespace acid::fx
{
using FxOrder = std::array<std::uint8_t, kNumFxSlots>;

inline constexpr FxOrder kDefaultFxOrder { 0, 1, 2, 3, 4, 5 };

/** True when the order is a permutation of the six FxSlot values. */
constexpr bool isValidFxOrder (const FxOrder& order) noexcept
{
    unsigned seen = 0;
    for (const auto slot : order)
    {
        if (slot >= kNumFxSlots) return false;
        seen |= 1u << slot;
    }
    return seen == (1u << kNumFxSlots) - 1u;
}

/** The order itself when valid, otherwise the default order. */
constexpr FxOrder sanitiseFxOrder (const FxOrder& order) noexcept
{
    return isValidFxOrder (order) ? order : kDefaultFxOrder;
}

/** Everything the chain needs for one block. Each member holds the rows of one
    ParamTable.h group in that group's natural units; defaults equal the table defaults. */
struct FxParams
{
    DistortionParams distortion;    // Distortion and Distortion Voicing rows
    EqParams eq;                    // EQ rows
    DelayParams delay;              // Delay rows. delay.bpm is ignored: the chain uses bpm below.
    ReverbParams reverb;            // Reverb rows (lowCutHz is not a row; leave it at its default)
    ChorusParams chorus;            // Chorus rows
    LimiterParams limiter;          // Limiter rows

    FxOrder order = kDefaultFxOrder;   // Settings::fxOrder, order[0] runs first
    bool bypass = false;               // fx_bypass
    float masterVolumeDb = 0.0f;       // master_volume: carried for the engine, NOT applied by the chain
    double bpm = 120.0;                // tempo for the synced delay; invalid or <= 0 becomes 120
};

/** Builds FxParams from a parameter snapshot, the settings (for the order) and the tempo. */
FxParams fxParamsFrom (const ParamValues&, const Settings&, double bpm) noexcept;

/** What the last process() call measured. Read on the audio thread after process() and
    publish it to the UI (atomics or a queue): this struct is not synchronised. */
struct FxMeters
{
    float peakLeft = 0.0f;              // largest |sample| of the chain output, linear
    float peakRight = 0.0f;
    float limiterReductionDb = 0.0f;    // deepest limiter gain reduction, dB, >= 0 (0 when off or bypassed)
    float dynamicsGainDb = 0.0f;        // distortion dynamics gain at the last sample, dB (+ lift, - cut)
    bool delayClamped = false;          // the delay asked for more than Delay::kMaxDelaySeconds
};

class FxChain
{
public:
    static constexpr double kReorderFadeSeconds = 0.005;   // each way
    static constexpr double kBypassFadeSeconds = 0.010;

    /** Allocates every block (delay and reverb lines: several MB at high rates). Not real-time
        safe. Re-applies the last setParams() at the new rate and ends with reset(). */
    void prepare (double sampleRate, int maxBlockSize);

    /** Clears every block's state, and snaps the order, the bypass and every block's on/off
        to the last setParams() with no fade. For an instant start (tests, offline renders)
        call setParams() and then reset(). Also usable as a panic. */
    void reset() noexcept;

    /** 1, 2, 4 or 8 (others round down). Real-time safe; latencySamples() follows at once. */
    void setDistortionOversampling (int factor) noexcept;

    /** Once per block, before process(). Values are sanitised by the blocks. */
    void setParams (const FxParams&) noexcept;

    /** Stereo, in place, any numSamples. Both pointers must be valid and distinct; for a mono
        source copy it to both channels first. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Delay through the chain in samples: the distortion's oversampling latency. */
    int latencySamples() const noexcept;

    FxMeters meters() const noexcept { return lastMeters; }

    /** The order currently in use (after sanitising; may lag setParams() during a reorder fade). */
    const FxOrder& activeOrder() const noexcept { return order; }

    // Read-only access to the blocks, for the editor's displays and for tests.
    const Distortion& distortionBlock() const noexcept { return distortion; }
    const Delay& delayBlock() const noexcept { return delay; }
    const Limiter& limiterBlock() const noexcept { return limiter; }

private:
    static constexpr int kChunk = 256;
    static constexpr int kDryRing = 1024;   // power of two, above the largest oversampling latency plus a chunk

    void applyParamsToBlocks (const FxParams&) noexcept;
    void updateGate() noexcept;
    int runningPredecessor (const FxOrder&, int slot) const noexcept;
    int lastRunning (const FxOrder&) const noexcept;
    bool sameRunningSequence (const FxOrder& a, const FxOrder& b) const noexcept;
    bool slotRunning (int slot) const noexcept;
    void runSlot (int slot, float* left, float* right, int n) noexcept;
    void resetBlocks() noexcept;
    void processChunk (float* left, float* right, int n) noexcept;

    double sampleRate = 48000.0;
    bool prepared = false;

    Distortion distortion;
    Eq eq;
    Delay delay;
    Reverb reverb;
    Chorus chorus;
    Limiter limiter;

    FxParams last {};
    FxOrder order = kDefaultFxOrder, pendingOrder = kDefaultFxOrder;
    std::array<bool, kNumFxSlots> slotOn {};
    std::array<int, kNumFxSlots> quietSamples {};   // samples since the slot was last on, saturating
    int settleSamples = 2400;                        // after this long off, a block is known to be idle

    // Reorder gates on the inputs of the blocks and on the output: 1 open, 0 shut. A gate
    // closes while its need flag is set and opens otherwise.
    std::array<double, kNumFxSlots> slotGate { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    std::array<int, kNumFxSlots> slotGateDir {};
    std::array<bool, kNumFxSlots> slotGateNeeded {};
    double outGate = 1.0, gateStep = 1.0;
    int outGateDir = 0;
    bool outGateNeeded = false;

    // Bypass crossfade: 1 chain, 0 dry.
    double engage = 1.0, engageStep = 1.0;
    bool bypassTarget = false;
    bool blocksStale = false;

    // Dry path, delayed by the latency, for the bypass crossfade.
    std::array<std::array<float, kDryRing>, 2> dryRing {};
    int dryPos = 0, dryLatency = 0;
    std::array<std::array<float, kChunk>, 2> dryBuf {};
    std::array<std::array<float, kChunk>, kNumFxSlots> slotCurve {};
    std::array<float, kChunk> outCurve {}, engageCurve {};

    FxMeters meter {}, lastMeters {};
};
}
