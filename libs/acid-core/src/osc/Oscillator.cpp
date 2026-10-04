#include "acid/dsp/Oscillator.h"

#include <cmath>

namespace acid::dsp
{
namespace
{
constexpr double kRail = SquareShaper::kCollectorRailVolts;

// Segments shorter than this fraction of a sample are not used to measure a slope.
constexpr double kMinSlopeLength = 1.0e-2;

/** 4-point polyBLEP: what a unit step at position u (0..1 inside the sample interval that
    ends at output n) adds to outputs n-2, n-1, n, n+1 once the plain samples are counted.
    It is the integral of the cubic B-spline kernel minus the step itself. */
inline void stepResidual (double u, double (&r)[4]) noexcept
{
    const double d = 1.0 - u;           // samples from the step to output n
    const double d2 = d * d, u2 = u * u;
    r[0] = d2 * d2 * (1.0 / 24.0);
    r[1] = 0.5 - u * (2.0 / 3.0) + u2 * u * (1.0 / 3.0) - u2 * u2 * 0.125;
    r[2] = -0.5 + d * (2.0 / 3.0) - d2 * d * (1.0 / 3.0) + d2 * d2 * 0.125;
    r[3] = -u2 * u2 * (1.0 / 24.0);
}

/** 4-point polyBLAMP: the same for a unit change of slope (volts per sample) at u. */
inline void kinkResidual (double u, double (&r)[4]) noexcept
{
    const double d = 1.0 - u;
    const double d2 = d * d, u2 = u * u;
    r[0] = d2 * d2 * d * (1.0 / 120.0);
    r[1] = (28.0 / 120.0) - 0.5 * u + u2 * (1.0 / 3.0) - u2 * u2 * (1.0 / 12.0) + u2 * u2 * u * (1.0 / 40.0);
    r[2] = (28.0 / 120.0) - 0.5 * d + d2 * (1.0 / 3.0) - d2 * d2 * (1.0 / 12.0) + d2 * d2 * d * (1.0 / 40.0);
    r[3] = u2 * u2 * u * (1.0 / 120.0);
}
}

void Oscillator::prepare (double sampleRate) noexcept
{
    const double fs = std::max (sampleRate, 1.0);
    samplePeriod = 1.0 / fs;
    maxFrequency = kMaxFrequencyOverFs * fs;
    shaper.prepare (fs);
    setParams (params);
    reset();
}

void Oscillator::setParams (const OscillatorParams& newParams) noexcept
{
    const bool wasInUse = params.squareInUse;
    params = newParams;
    params.sawBottomVolts = clamp (params.sawBottomVolts, 5.0, 6.5);
    params.sawTopVolts = clamp (params.sawTopVolts, 9.5, 12.5);
    params.flatTopSeconds = clamp (params.flatTopSeconds, 0.0, kMaxFlatTopSeconds);
    params.subOctaves = params.subOctaves >= 2 ? 2 : 1;

    topVolts = params.sawTopVolts;
    bottomVolts = params.sawBottomVolts;
    spanVolts = topVolts - bottomVolts;

    // Follower map in units of the swing: S (y) = softMin (offset + slope * y, ceiling) with
    // a hyperbolic knee, y = 1 - phase. Offset and ceiling make S (0) = 0 and S (1) = 1.
    const double ov = kMapOvershoot;
    const double k2 = kMapKnee * kMapKnee;
    mapOffset = 0.5 * (std::sqrt (ov * ov + 4.0 * ov * k2) - ov);
    mapCeiling = k2 / mapOffset;

    // dS/dy at both ends of the ramp, for the slope change at the reset.
    const auto slope = [this] (double y)
    {
        const double d = mapOffset + kMapSlope * y - mapCeiling;
        return 0.5 * kMapSlope * (1.0 - d / std::sqrt (d * d + kMapKnee4));
    };
    slopeAtBottom = spanVolts * slope (0.0);
    slopeAtTop = spanVolts * slope (1.0);

    // Mean of S over the ramp, in closed form.
    const double a = 2.0 * kMapKnee;
    const auto prim = [a] (double z) { return 0.5 * (z * std::sqrt (z * z + a * a) + a * a * std::asinh (z / a)); };
    const double z0 = mapOffset - mapCeiling;
    const double z1 = z0 + kMapSlope;
    const double meanS = 0.5 * (mapOffset + 0.5 * kMapSlope + mapCeiling) - (prim (z1) - prim (z0)) / (2.0 * kMapSlope);
    sawMean = bottomVolts + spanVolts * meanS;

    flatTop = params.flatTopSeconds;
    holdSamples = flatTop / samplePeriod;
    idleStride = std::max (1.0, std::round (kIdleStepSeconds / samplePeriod));

    if (params.squareInUse && ! wasInUse)
    {
        // Back in use: bring the shaper up to the present and start from a clean slate.
        flushIdle (nodeSaw);
        squareOut.fill (nodeSquare);
        squareSlope = 0.0;
        afterPartial = false;
        edgeVoltsLeft = 0.0;
        edgeDone = ! shaper.isOff();
    }

    shaper.setBeta (params.shaperBeta);
    shaper.setEmitterCapacitor (params.emitterCapFarads);
    updateSubValue();
}

void Oscillator::reset() noexcept
{
    rampPhase = 0.0;
    holdLeft = 0.0;
    divideBy2 = false;
    divideBy4 = false;
    updateSubValue();
    edgeVoltsLeft = 0.0;
    edgeDone = false;
    idleSpan = 0.0;
    u = 0.0;

    shaper.reset();
    nodeSaw = topVolts;
    nodeSquare = shaper.advance (0.0, topVolts) - kRail;
    squareSlope = 0.0;
    afterPartial = false;

    // Output history as if every signal had sat at its present value for ever, so the
    // first samples do not ramp up from zero.
    sawOut.fill (nodeSaw);
    squareOut.fill (nodeSquare);
    subOut.fill (subValue);
}

void Oscillator::updateSubValue() noexcept
{
    const bool high = params.subOctaves >= 2 ? divideBy4 : divideBy2;
    subValue = high ? kSubAmplitudeVolts : -kSubAmplitudeVolts;
}

void Oscillator::flushIdle (double sawVolts) noexcept
{
    if (idleSpan > 0.0)
    {
        nodeSquare = shaper.advance (idleSpan, sawVolts) - kRail;
        idleSpan = 0.0;
    }
}

void Oscillator::fire (double inc) noexcept
{
    // The core snaps back to the top. The capacitor states of the shaper carry through the
    // reset; its node equations are solved again for the new input, which gives the
    // square's level on the far side of the step.
    flushIdle (bottomVolts);
    const double squareBefore = nodeSquare;
    const double subBefore = subValue;

    rampPhase = 0.0;
    holdLeft = holdSamples;
    nodeSaw = topVolts;
    nodeSquare = shaper.advance (0.0, topVolts) - kRail;
    edgeVoltsLeft = 0.0;
    edgeDone = false;

    divideBy2 = ! divideBy2;
    if (! divideBy2)
        divideBy4 = ! divideBy4;
    updateSubValue();

    // Band-limit the step of each output with its own height, and the saw's change of
    // slope (steep at the bottom of the ramp, rounded or held flat at the top).
    double r[4], k[4];
    stepResidual (u, r);
    kinkResidual (u, k);
    const double sawStep = topVolts - bottomVolts;
    const double squareStep = params.squareInUse ? nodeSquare - squareBefore : 0.0;
    const double subStep = subValue - subBefore;
    const double sawKink = inc * (slopeAtBottom - (holdSamples > 0.0 ? 0.0 : slopeAtTop));
    sawOut.deposit (sawStep, r);
    sawOut.deposit (sawKink, k);
    if (squareStep != 0.0)
        squareOut.deposit (squareStep, r);
    if (subStep != 0.0)
        subOut.deposit (subStep, r);
    afterPartial = true;            // the square's own corner follows with the next segment
}

void Oscillator::segmentTo (double end, double sawEnd) noexcept
{
    // One linear piece of the saw, from the current position u to `end` (both inside the
    // current sample, 0..1), through the shaper.
    const double length = end - u;
    if (! (length > 0.0))
        return;

    if (! params.squareInUse)
    {
        // Idle: the shaper follows in long steps and nothing is corrected.
        idleSpan += length;
        if (idleSpan >= idleStride)
            flushIdle (sawEnd);
        nodeSaw = sawEnd;
        u = end;
        return;
    }

    const double squareEnd = shaper.advance (length, sawEnd) - kRail;

    // Corners of the square. A sample that is crossed in one plain segment contributes
    // nothing here; every node that touches a partial segment (a sub-step of the refined
    // turn-on edge, the pieces either side of a reset or of the end of a flat top) gets
    // its change of slope band-limited. Corrections therefore always come in the sets that
    // describe a whole feature. A sliver of a segment (a reset that falls almost exactly on
    // a sample) has no usable slope of its own; its corner waits for the next segment.
    const bool partial = length != 1.0;
    if (length >= kMinSlopeLength)
    {
        const double slope = partial ? (squareEnd - nodeSquare) / length : squareEnd - nodeSquare;
        if ((partial || afterPartial) && slope != squareSlope)
        {
            double k[4];
            kinkResidual (std::min (u, 1.0), k);
            squareOut.deposit (slope - squareSlope, k);
        }
        squareSlope = slope;
        afterPartial = partial;
    }
    else
    {
        afterPartial = true;
    }

    nodeSaw = sawEnd;
    nodeSquare = squareEnd;
    u = end;
}

void Oscillator::rampTo (double end, double du, double inc, double anchorU, bool endsAtReset) noexcept
{
    // The saw from u to `end`, in pieces no longer than du. Its phase is pinned to the
    // value rampPhase had at position anchorU, so the saw does not depend on how finely
    // the shaper happens to be stepped. At most kMaxSubSteps + 1 passes.
    while (u < end)
    {
        const double next = end - u > du * 1.000001 ? u + du : end;
        const bool last = next == end;
        segmentTo (next, last && endsAtReset ? bottomVolts : sawVoltsAtPhase (rampPhase + inc * (next - anchorU)));
    }
}

OscillatorOutput Oscillator::tick (double frequencyHz) noexcept
{
    const double f = frequencyHz > 0.0 ? std::min (frequencyHz, maxFrequency) : 0.0;

    // Ramp travel per sample, as a fraction of the swing.
    double inc = f * samplePeriod;
    if (flatTop > 0.0 && ! params.flatTopDetunes)
        inc /= std::max (1.0 - f * flatTop, 0.25);      // the ramp shares the period with the hold
    inc = std::min (inc, 0.5);

    // The turn-on edge of the square is fast and analytic, not a step, so it cannot be
    // corrected after the fact. Where the saw moves more than a fraction of the thermal
    // voltage per sample, the samples that contain the edge are solved in sub-steps.
    const double rampVolts = inc * spanVolts * kMapSlope;
    int steps = 1;
    if (rampVolts > kEdgeResolutionVolts && params.squareInUse)
    {
        if (edgeVoltsLeft <= 0.0 && ! edgeDone && holdLeft < 1.0
            && shaper.emitterBaseVolts() + 1.5 * rampVolts >= kEdgeStartVolts)
        {
            edgeVoltsLeft = kEdgeWindowVolts + 1.5 * rampVolts;
            edgeDone = true;                            // once per ramp
        }

        if (edgeVoltsLeft > 0.0)
            steps = std::min (kMaxSubSteps, static_cast<int> (std::ceil (rampVolts / kEdgeResolutionVolts)));
    }
    if (edgeVoltsLeft > 0.0)
        edgeVoltsLeft -= rampVolts;

    // The saw's own timeline through this sample: the rest of a flat top, the ramp, the
    // reset. The shaper's sub-steps only add nodes in between.
    const double du = steps == 1 ? 1.0 : 1.0 / static_cast<double> (steps);
    double anchorU = 0.0;           // where rampPhase was last pinned
    u = 0.0;
    for (int guard = 0; guard < 6 && u < 1.0; ++guard)
    {
        if (holdLeft > 0.0)
        {
            const double room = 1.0 - u;
            if (holdLeft > room)
            {
                // The flat top lasts beyond this sample.
                holdLeft -= room;
                segmentTo (1.0, topVolts);
                break;
            }

            // The ramp starts inside this sample: a corner in the saw.
            segmentTo (u + holdLeft, topVolts);
            holdLeft = 0.0;
            double k[4];
            kinkResidual (u, k);
            sawOut.deposit (-inc * slopeAtTop, k);
            anchorU = u;
            continue;
        }

        const double resetAt = inc > 0.0 ? std::max (u, anchorU + (1.0 - rampPhase) / inc) : 2.0;
        if (resetAt > 1.0)
        {
            rampTo (1.0, du, inc, anchorU, false);
            rampPhase += inc * (1.0 - anchorU);
            break;
        }

        rampTo (resetAt, du, inc, anchorU, true);
        fire (inc);                 // rampPhase is 0 from here
        anchorU = u;
    }

    OscillatorOutput out;
    out.saw = sawOut.next (nodeSaw);
    out.square = squareOut.next (nodeSquare) + kRail;
    out.sub = subOut.next (subValue);
    return out;
}
}
