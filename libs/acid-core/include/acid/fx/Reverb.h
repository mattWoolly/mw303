#pragma once
// Algorithmic stereo reverb with separate early-reflection and late sections.
//
//   input -> pre-delay (0..1000 ms, crossfaded) -> low cut (lowCutHz, 12 dB/oct)
//         -> early: 12 taps per channel at distances proportional to the early size
//                   (metres / 343 m/s, 0.31..3 times the size), 3 allpass diffusers
//                   (coefficient 0.7 * diffusion), one-pole brightness low-pass
//                   (600 Hz .. 27 kHz)
//         -> late:  4 allpass diffusers per channel (0.7 * diffusion), then a 16-line
//                   feedback delay network: orthonormal Hadamard mixing, line lengths
//                   spread 0.56..1.45 times a mean that scales with the room, each line
//                   followed by an in-loop allpass (0.6 * diffusion) and a damping filter
//                   that sets the decay per line (Jot) so every mode decays alike. Eight
//                   of the lines are read through slowly modulated Catmull-Rom taps (tail
//                   modulation, up to 1 ms); the other eight read whole-sample taps once
//                   the size has settled.
//
// Line lengths. The mean line length is the mean free path of the largest room
// (2/3 * 51.1 m / 343 m/s = 99 ms) times (size / 51.1)^0.6, so 25 ms at 5.1 m. A linear
// mapping would give 10 ms there, too few modes per hertz: small rooms rang metallic.
// The decay below still follows the real size.
//
// Decay follows Sabine for a cube of side lateSize: T60 = 0.161 * size / (6 * alpha),
// with the absorption alpha = 0.02 * 50^(1 - feedback). So decay grows with both size
// and feedback: 0.14 s (5.1 m, 0 %) to 2.4 s (27.9 m, 30 %) to 69 s (51.1 m, 100 %).
// Late brightness sets the high-frequency T60 as a fraction 0.08..1 of the low one.
//
// Stereo: L feeds the even lines, R the odd ones; the outputs use orthogonal sign
// patterns, so L and R tails are decorrelated and their mono sum keeps the energy of both.
// Every loop element has gain <= 1 (orthonormal matrix, Catmull-Rom, allpass, damping
// below 1) and a hard clamp far above signal level guards the loop, so it is stable at
// every setting.
//
// Tail modulation never goes below 10 % at the smallest late size (5.1 m), tapering to no
// floor at 15 m: a small room's sparse modes would otherwise hold a steady narrow peak.
//
// Mixes are equal power: early/late (0 = early only) and dry/wet.
// Size changes glide (150 ms): the taps slide, so a size sweep bends the tail's pitch.
// Bypass: 5 ms crossfade; switching off cuts the tail (state is cleared before switch-on).
#include "acid/engine/ParamTable.h"
#include "acid/fx/InsertUtil.h"
#include <array>

namespace acid::fx
{
/** One field per row of the Reverb group in ParamTable.h, plus a fixed wet low cut. */
struct ReverbParams
{
    bool on = false;
    float preDelayMs = 0.0f;           // 0..1000
    float earlySizeM = 10.4f;          // 1.1..20.5
    float lateSizeM = 27.9f;           // 5.1..51.1
    float earlyDiffPct = 50.0f;        // 0..100
    float lateDiffPct = 50.0f;
    float earlyBrightPct = 50.0f;
    float lateBrightPct = 50.0f;
    float feedbackPct = 30.0f;         // late decay, 0..100
    float earlyLatePct = 50.0f;        // 0 = early only, 100 = late only
    float modPct = 10.0f;              // tail modulation
    float mixPct = 25.0f;              // dry/wet
    float lowCutHz = 60.0f;            // not a host parameter: keeps sub-bass out of the tail (1..1000)
};

/** Reads the Reverb rows of a parameter snapshot. lowCutHz keeps its default. */
inline ReverbParams reverbParamsFrom (const ParamValues& v) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };
    ReverbParams p;
    p.on = paramAsBool (v, kRevOn);
    p.preDelayMs = f (kRevPreDelay);
    p.earlySizeM = f (kRevEarlySize);
    p.lateSizeM = f (kRevLateSize);
    p.earlyDiffPct = f (kRevEarlyDiff);
    p.lateDiffPct = f (kRevLateDiff);
    p.earlyBrightPct = f (kRevEarlyBright);
    p.lateBrightPct = f (kRevLateBright);
    p.feedbackPct = f (kRevFeedback);
    p.earlyLatePct = f (kRevEarlyLate);
    p.modPct = f (kRevMod);
    p.mixPct = f (kRevMix);
    return p;
}

class Reverb
{
public:
    static constexpr int kNumLines = 16;
    static constexpr int kNumTaps = 12;
    static constexpr int kNumEarlyAllpass = 3;
    static constexpr int kNumLateAllpass = 4;

    /** Allocates every line for the largest sizes at this rate. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    /** Once per block, before process(). */
    void setParams (const ReverbParams& p) noexcept;
    /** In place, any numSamples >= 0. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** The designed low-frequency T60 in seconds for a late size and feedback. */
    static double designT60 (double lateSizeM, double feedbackPct) noexcept;

private:
    struct Allpass
    {
        insert::DelayLine line;
        std::size_t delay = 1;

        double tick (double x, double g) noexcept
        {
            const double wd = line.read (delay);
            const double w = x + g * wd;
            line.push (w);
            return wd - g * w;
        }
    };

    void clearState() noexcept;
    void snapAll() noexcept;
    void updateTaps (double unit) noexcept;

    double sampleRate = 48000.0;

    insert::DelayLine pre[2];
    insert::CrossfadeTaps preTaps;
    std::size_t preMax = 1;

    insert::SvfCoeffs lowCutC;
    insert::SvfState lowCutS[2];
    double lowCutHz = -1.0;

    insert::DelayLine early[2];
    std::array<double, kNumTaps> tapGain[2] {}, tapFrac[2] {};
    std::array<std::size_t, kNumTaps> tapIdx[2] {};
    insert::Smoother earlyUnit;        // samples per unit tap distance (early size / c * fs)
    double tapUnit = -1.0;             // the unit tapIdx and tapFrac were computed for
    std::array<Allpass, kNumEarlyAllpass> earlyAp[2];
    insert::Smoother earlyDiff, earlyG;
    insert::OnePoleLp earlyLp[2];

    std::array<Allpass, kNumLateAllpass> lateAp[2];
    insert::Smoother lateDiff;

    std::array<insert::DelayLine, kNumLines> lines;
    std::array<Allpass, kNumLines> loopAp;
    std::array<insert::Smoother, kNumLines> lineLen;   // samples; targets are whole samples
    insert::Smoother loopDiff;
    std::array<insert::Smoother, kNumLines> gainDc, pole;
    std::array<double, kNumLines> damp {};
    static constexpr int kNumLfos = 4;
    std::array<double, kNumLfos> lfoC {}, lfoS {}, lfoCw {}, lfoSw {};
    insert::Smoother modDepth;         // samples
    bool coeffsMoving = false;

    insert::Smoother earlyMix, lateMix, dryGain, wetGain;
    insert::OnOffFade fade;

    bool needsClear = false;
    bool snapNext = true;
};
}
