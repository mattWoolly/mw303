#include "acid/seq/PlayheadTracker.h"
#include "acid/common/Math.h"
#include "acid/seq/StepClock.h"
#include <cmath>

namespace acid::seq
{
void PlayheadTracker::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 1.0 ? newSampleRate : 48000.0;
    reset();
}

void PlayheadTracker::reset() noexcept
{
    currentBpm = 120.0;
    quartersPerSample = currentBpm / (60.0 * sampleRate);
    anchorPpq = 0.0;
    samplesSinceAnchor = 0;
    rolling = false;
    hostBpmKnown = false;
    hostBpm = 120.0;
    timeSigNum = 4;
    timeSigDen = 4;
    block = {};
    block.quartersPerSample = quartersPerSample;
}

void PlayheadTracker::setTempo (double newBpm) noexcept
{
    if (newBpm == currentBpm)
        return;

    // Re-anchor so that what has already elapsed keeps the tempo it elapsed at.
    anchorPpq = position();
    samplesSinceAnchor = 0;
    currentBpm = newBpm;
    quartersPerSample = currentBpm / (60.0 * sampleRate);
}

const ClockBlock& PlayheadTracker::process (const TransportInfo& host, int numSamples, double fallbackBpm) noexcept
{
    block.numSegments = 0;
    block.hostStarted = false;
    block.hostStopped = false;
    block.relocated = false;

    bool discontinuity = false;
    bool freshHostPosition = false;

    if (host.valid)
    {
        if (host.bpmValid && std::isfinite (host.bpm) && host.bpm > 0.0)
        {
            hostBpm = clamp (host.bpm, kMinBpm, kMaxBpm);
            hostBpmKnown = true;
        }

        if (host.timeSigNum > 0 && host.timeSigDen > 0)
        {
            timeSigNum = host.timeSigNum;
            timeSigDen = host.timeSigDen;
        }

        if (host.playing)
        {
            if (! rolling)
            {
                block.hostStarted = true;
                discontinuity = true;
            }

            // A position that is not finite, or absurdly far out, is no position at all:
            // the clock carries on counting samples from where it was.
            if (host.ppqValid && isUsablePpq (host.ppq))
            {
                // Compare against where the old tempo carried us, then adopt the host's value.
                if (rolling && std::abs (host.ppq - position()) > kRelocateToleranceQ)
                {
                    block.relocated = true;
                    discontinuity = true;
                }

                anchorPpq = host.ppq;
                samplesSinceAnchor = 0;
                freshHostPosition = true;
            }

            rolling = true;
        }
        else
        {
            // Stopped. The position the host reports now is its edit cursor: ignored. The
            // internal clock carries on from where the song position got to.
            block.hostStopped = rolling;
            rolling = false;
        }
    }

    const double wantedBpm = hostBpmKnown ? hostBpm
                                          : clamp (std::isfinite (fallbackBpm) ? fallbackBpm : 120.0, kMinBpm, kMaxBpm);
    setTempo (wantedBpm);

    // Last resort: counting samples for years (or adopting a position at the very edge of the
    // range and extrapolating from it) could carry the position out of the range the step
    // arithmetic is good for. Start again from 0 and say that the position jumped.
    if (! isUsablePpq (position()))
    {
        anchorPpq = 0.0;
        samplesSinceAnchor = 0;
        discontinuity = true;
        block.relocated = rolling;
    }

    block.bpm = currentBpm;
    block.quartersPerSample = quartersPerSample;
    block.source = rolling ? ClockSource::Host : ClockSource::Internal;
    block.hostRolling = rolling;
    block.timeSigNum = timeSigNum;
    block.timeSigDen = timeSigDen;

    if (numSamples <= 0)
    {
        block.endPpq = position();
        return block;
    }

    // A host that loops without splitting its blocks at the loop point wraps somewhere inside
    // this block. That is only trusted on a block that carries a fresh position and loop flags.
    const bool canLoop = freshHostPosition && host.looping
                         && isUsablePpq (host.loopStartPpq) && isUsablePpq (host.loopEndPpq)
                         && host.loopEndPpq - host.loopStartPpq >= 8.0 * quartersPerSample;

    double ppq = position();
    int offset = 0;
    int remaining = numSamples;
    bool split = false;

    while (remaining > 0)
    {
        ClockSegment seg { offset, remaining, ppq, discontinuity };

        if (canLoop && block.numSegments < ClockBlock::kMaxSegments - 1)
        {
            const std::int64_t untilWrap = sampleIndexAtOrAfter (host.loopEndPpq, ppq, quartersPerSample);

            if (untilWrap > 0 && untilWrap < remaining)
            {
                seg.numSamples = static_cast<int> (untilWrap);
                block.segments[static_cast<std::size_t> (block.numSegments++)] = seg;

                const double overshoot = ppq + static_cast<double> (untilWrap) * quartersPerSample - host.loopEndPpq;
                ppq = host.loopStartPpq + (overshoot > 0.0 ? overshoot : 0.0);
                offset += seg.numSamples;
                remaining -= seg.numSamples;
                discontinuity = true;
                split = true;
                continue;
            }
        }

        block.segments[static_cast<std::size_t> (block.numSegments++)] = seg;

        if (split)
        {
            anchorPpq = ppq;
            samplesSinceAnchor = 0;
        }

        samplesSinceAnchor += remaining;
        remaining = 0;
    }

    block.endPpq = position();
    return block;
}
}
