#pragma once
// Turns what a host says about its transport, block by block, into one continuous musical
// position that is always running:
//
//   - while the host plays, the position is the host's (adopted every block it is given,
//     extrapolated through blocks where the host says nothing);
//   - while the host is stopped, or there is no host transport at all (standalone), a
//     sample-counting internal clock carries on from wherever the position was.
//
// A block with TransportInfo::valid == false is "no news", never "stopped": some plugin
// formats only deliver a position when it changes.
#include "acid/seq/PlayerTypes.h"
#include <array>
#include <cstdint>

namespace acid::seq
{
enum class ClockSource : std::uint8_t
{
    Internal = 0,   // host stopped or absent: counting samples at the last known tempo
    Host = 1        // following the host's song position
};

/** A run of samples inside one block over which the position advances linearly. */
struct ClockSegment
{
    int offset = 0;               // first sample of the segment inside the block
    int numSamples = 0;
    double startPpq = 0.0;        // position at the first sample
    bool discontinuity = false;   // the position jumped at the start of this segment
};

/** The clock for one block. A block is one segment unless a host loop wraps inside it. */
struct ClockBlock
{
    static constexpr int kMaxSegments = 8;

    std::array<ClockSegment, kMaxSegments> segments {};
    int numSegments = 0;

    double bpm = 120.0;
    double quartersPerSample = 120.0 / (60.0 * 48000.0);
    double endPpq = 0.0;          // position one sample past the end of the block

    ClockSource source = ClockSource::Internal;
    bool hostRolling = false;     // the host transport is playing
    bool hostStarted = false;     // it started at the top of this block
    bool hostStopped = false;     // it stopped at the top of this block
    bool relocated = false;       // it was already playing and its position jumped (seek or loop)

    int timeSigNum = 4;
    int timeSigDen = 4;
};

class PlayheadTracker
{
public:
    /** A host position this far (in quarter notes) from the extrapolated one is a jump.
        Smaller differences (tempo ramps, jitter) are adopted silently. */
    static constexpr double kRelocateToleranceQ = 1.0 / 64.0;

    static constexpr double kMinBpm = 10.0;
    static constexpr double kMaxBpm = 1000.0;

    /** Positions are kept within plus or minus this many quarter notes (years of music at
        any tempo). A host position or loop point beyond it is garbage and is treated like a
        missing one: "no news". The tracker's own position never leaves the range either: if
        counting samples ever carried it past the end, it starts again from 0 and the block
        is flagged as a jump. Everything downstream can therefore rely on a bounded,
        finite position. */
    static constexpr double kMaxAbsPpq = 1.0e9;

    /** True for a position the tracker accepts: finite and within kMaxAbsPpq. */
    static constexpr bool isUsablePpq (double q) noexcept { return q >= -kMaxAbsPpq && q <= kMaxAbsPpq; }

    void prepare (double newSampleRate) noexcept;

    /** Position back to 0, host forgotten. */
    void reset() noexcept;

    /** Call once per audio block, before anything that needs the clock.
        fallbackBpm is used until the host has reported a tempo (standalone: always).
        The returned reference stays valid until the next call. */
    const ClockBlock& process (const TransportInfo& host, int numSamples, double fallbackBpm) noexcept;

    /** Position at the first sample of the next block. Always within kMaxAbsPpq. */
    double position() const noexcept { return anchorPpq + static_cast<double> (samplesSinceAnchor) * quartersPerSample; }

    double bpm() const noexcept { return currentBpm; }
    bool hostRolling() const noexcept { return rolling; }

private:
    void setTempo (double newBpm) noexcept;

    double sampleRate = 48000.0;
    double currentBpm = 120.0;
    double quartersPerSample = 120.0 / (60.0 * 48000.0);

    // The position is anchorPpq + samplesSinceAnchor * quartersPerSample. It is never
    // accumulated by repeated addition, so it does not drift over long runs.
    double anchorPpq = 0.0;
    std::int64_t samplesSinceAnchor = 0;

    bool rolling = false;
    bool hostBpmKnown = false;
    double hostBpm = 120.0;
    int timeSigNum = 4;
    int timeSigDen = 4;

    ClockBlock block {};
};
}
