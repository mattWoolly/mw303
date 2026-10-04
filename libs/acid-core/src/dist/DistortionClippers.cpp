#include "acid/fx/DistortionClippers.h"

namespace acid::fx::detail
{
namespace
{
constexpr double kDiodeClip = 0.6;   // volts: the level a diode pair settles near

// With the drive at unity a family clips at kClipHeadroom times the nominal level, so a
// plain note passes nearly clean and an accent is caught.
constexpr double kClipLevel = kClipHeadroom * kNominalLevel;
constexpr double kVoltsPerUnit = kDiodeClip / kClipLevel;   // full-scale units to circuit volts
}

double driveCompensation (double gain) noexcept
{
    // A smooth 1 / min (gain, headroom), normalised to 1 at unity gain.
    constexpr double p = 4.0;
    const double h = std::pow (kClipHeadroom, -p);
    return std::pow ((std::pow (std::max (gain, 1.0), -p) + h) / (1.0 + h), 1.0 / p);
}

//==============================================================================
void DiodePairCoeffs::update (const ClipperControls& ctl) noexcept
{
    // The saturation term is set so that four clip voltages at the input give exactly one
    // across the diodes, whatever the knee. A soft knee is then a leaky (germanium-like)
    // junction and a hard one an LED-like junction with the same forward voltage.
    const double scale = kDiodeClip * diodeSoftness (ctl.knee, 0.025, 0.14);
    diode.set (1.0, 3.0 * kDiodeClip / std::sinh (kDiodeClip / scale), scale);
    inGain = ctl.gain * kVoltsPerUnit;
    offset = ctl.gain * ctl.bias * kBiasFullScale * kVoltsPerUnit;
    // Unity for small signals at unity drive.
    outGain = driveCompensation (ctl.gain) / (diode.smallSignal() * kVoltsPerUnit);
    rest = solveDiodePair (diode, offset);
}

//==============================================================================
void OpAmpHardCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double r1 = 47.0, c1 = 2.2e-6;      // 1.5 kHz leg
    constexpr double r2 = 560.0, c2 = 4.7e-6;     // 60 Hz leg
    constexpr double slewRate = 0.3e6;            // volts per second
    constexpr double unityGainHz = 316.0e3;       // 30 dB of open-loop gain left at 10 kHz
    const double t = 1.0 / sampleRate;

    // The drive is the feedback resistor: gain = 1 + Rf / (R1 || R2) once both legs conduct.
    const double rf = (ctl.gain - 1.0) * (r1 * r2 / (r1 + r2));
    const double g1 = 1.0 / (r1 + 0.5 * t / c1);   // trapezoidal companion conductances
    const double g2 = 1.0 / (r2 + 0.5 * t / c2);
    alpha = 1.0 / (1.0 + rf * (g1 + g2));
    rfG1 = rf * g1;
    rfG2 = rf * g2;
    leak1 = t / c1 * g1;
    leak2 = t / c2 * g2;

    inScale = kVoltsPerUnit;
    // A bias current into the inverting node shows up at the output times Rf. Referring it
    // to the 560 ohm leg makes it track the mid-band gain rather than the peak gain.
    offset = ctl.bias * kBiasFullScale * kVoltsPerUnit * rf / r2;

    loopC = alpha * t * slewRate;
    loopK = kTwoPi * unityGainHz / slewRate;

    // 1 k into silicon diodes; the junction is set so the rail gives one clip voltage.
    const double scale = kDiodeClip * diodeSoftness (ctl.knee, 0.02, 0.08);
    diode.set (1.0, (kOpAmpRail - kDiodeClip) / std::sinh (kDiodeClip / scale), scale);
    outGain = driveCompensation (ctl.gain) / (diode.smallSignal() * kVoltsPerUnit);

    // At rest the leg capacitors hold no charge, so the output sits at the offset.
    restOut = clamp (offset, -kOpAmpRail, kOpAmpRail);
    rest = solveDiodePair (diode, restOut);
}

//==============================================================================
void AsymSoftCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double bypassCap = 22.0e-6;
    constexpr double swingRef = 3.75;   // volts of drain swing mapped to the clip level

    // The knee is how much of the source resistor is left unbypassed: local feedback
    // straightens the square law, so the stage stays linear longer and then turns sharply.
    rsu = (1.0 - clamp (ctl.knee, 0.0, 1.0)) * 0.75 * kJfetSourceR;
    rsb = kJfetSourceR - rsu;
    betaRsu = kJfetBeta * rsu;
    gateBias = 0.9 * ctl.bias;

    // DC operating point: the whole source resistor counts.
    const double v = gateBias - kJfetPinch;
    const double c = kJfetBeta * kJfetSourceR * v;
    const double id0 = 2.0 * kJfetBeta * v * v / ((1.0 + 2.0 * c) + std::sqrt (1.0 + 4.0 * c));
    restSource = id0 * rsb;
    restDrain = kJfetSupply - id0 * kJfetDrainR;

    const double gm = 2.0 * kJfetBeta * (gateBias - id0 * kJfetSourceR - kJfetPinch);
    const double stageGain = gm * kJfetDrainR / (1.0 + gm * rsu);
    inGain = ctl.gain * swingRef / (kClipLevel * stageGain);
    outGain = driveCompensation (ctl.gain) * kClipLevel / swingRef;

    sourceCoef = rsb > 1.0 ? 1.0 - std::exp (-1.0 / (rsb * bypassCap * sampleRate)) : 1.0;
}

//==============================================================================
void ScoopHardCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double boosterShare = 35.0 / 61.5;   // of the total gain in dB; the op-amp has the rest
    constexpr double rs = 2200.0, cap = 10.0e-9;

    const double g1 = std::pow (ctl.gain, boosterShare);
    const double g2 = ctl.gain / g1;
    inBoost = kVoltsPerUnit * g1 / kBoosterGain;   // the booster's own gain is fixed
    opGain = g2 - 1.0;
    offset = ctl.gain * ctl.bias * kBiasFullScale * kVoltsPerUnit;

    shelf.set (72.0, sampleRate);
    toneLow.set (234.0, sampleRate);
    toneHigh.set (1063.0, sampleRate);

    capK = rs * cap * sampleRate;
    const double scale = kDiodeClip * diodeSoftness (ctl.knee, 0.02, 0.08);
    diode.set (1.0 + capK, (kOpAmpRail - kDiodeClip) / std::sinh (kDiodeClip / scale), scale);
    outGain = driveCompensation (ctl.gain) / kVoltsPerUnit;

    // At rest the booster is quiet, the op-amp output sits at the offset and the capacitor
    // across the diodes carries no current; the tone stage passes DC at unity.
    DiodeLaw dc;
    dc.set (1.0, diode.sat, diode.scale);
    rest = solveDiodePair (dc, clamp (offset, -kOpAmpRail, kOpAmpRail));
}

//==============================================================================
void MidHumpCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double r1 = 4700.0, c1 = 47.0e-9, cf = 51.0e-12;
    constexpr double diodeDrop = 0.55, refCurrent = 0.2e-3;

    inScale = kVoltsPerUnit;
    outGain = driveCompensation (ctl.gain) / kVoltsPerUnit;
    leg.set (1.0 / (kTwoPi * r1 * c1), sampleRate);

    // Feedback network, scaled by R1: (R1 Cf / T + R1 / Rf) v + 2 Is R1 sinh (v / a) = ...
    // with Rf = (gain - 1) R1. At unity gain the feedback resistor is a short.
    capK = r1 * cf * sampleRate;
    const double scale = diodeDrop * diodeSoftness (ctl.knee, 0.03, 0.12);
    diode.set (capK + 1.0 / std::max (ctl.gain - 1.0, 1.0e-9), r1 * refCurrent / std::sinh (diodeDrop / scale), scale);

    // A DC error at the input that the leg capacitor did not block.
    offset = ctl.bias * kBiasFullScale * kVoltsPerUnit;

    // At rest the feedback capacitor carries no current: Rf and the diodes alone.
    DiodeLaw dc;
    dc.set (1.0 / std::max (ctl.gain - 1.0, 1.0e-9), diode.sat, diode.scale);
    restFb = solveDiodePair (dc, offset);
    rest = clamp (restFb, -kOpAmpRail, kOpAmpRail);
}

//==============================================================================
void GatedFuzzCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double sourceR = 10.0e3, couplingCap = 220.0e-9, biasR = 68.0e3;
    constexpr double collectorIs = 1.0e-14, vceSat = 0.2;
    constexpr double voltsPerUnit = 1.0;   // the nominal level is a quarter of a volt at the input
    const double t = 1.0 / sampleRate;

    // Knee: a little emitter degeneration softens the exponential turn-on.
    const double re = 150.0 * clamp (ctl.knee, 0.0, 1.0);
    rePrime = re * (kFuzzBeta + 1.0);
    const double isb = collectorIs / kFuzzBeta;

    // Bias: the base supply. 0 leaves the transistor just shut (the gate), +1 (0.884 V)
    // centres the collector, -1 (0.25 V) shuts it hard.
    // At 0.46 V about half a microamp flows at rest, so a signal 50 dB below the nominal
    // level comes out more than 40 dB further down.
    const double vbb = ctl.bias >= 0.0 ? 0.46 + 0.424 * ctl.bias : 0.46 + 0.21 * ctl.bias;

    kp = 1.0 / (sourceR + t / couplingCap);
    const double a = kp + 1.0 / biasR;
    invA = 1.0 / a;
    biasTerm = vbb / biasR;
    sat = (1.0 + a * rePrime) * isb * invA;
    ibScale = a / (1.0 + a * rePrime);
    capStep = t / couplingCap;

    // Resting point: vbe + (Rb + Re') * Ib (vbe) = Vbb.
    const double rq = biasR + rePrime;
    const double vbeRest = solveJunction (rq * isb, kThermalVoltage, vbb);
    const double ibRest = (vbb - vbeRest) / rq;
    restCap = -(vbeRest + rePrime * ibRest);

    icSat = (kFuzzSupply - vceSat) / (kFuzzRc + re);
    rest = clamp (kFuzzBeta * ibRest, 0.0, icSat);
    inGain = ctl.gain * voltsPerUnit;
    outGain = kFuzzRc / (kFuzzSupply - vceSat) * 2.0 * kNominalLevel;
}

//==============================================================================
void ConsoleCoeffs::update (const ClipperControls& ctl, double sampleRate) noexcept
{
    constexpr double headroom = kClipLevel;            // mean of the two rails, in full-scale units
    constexpr double pairImbalance = 0.3;              // tanh (b): the rails sit at 0.7 and 1.3 times the headroom
    constexpr double fluxKnee = 2.2e-4;                // unit-seconds: the nominal level saturates the core below about 80 Hz
    constexpr double coreCornerHz = 5.0;               // magnetising inductance against the source resistance

    // Knee: the loop gain. Little feedback bends early and gently; a lot stays straight
    // and then hits the rail.
    const double loopGain = 40.0 * std::pow (3.0 / 40.0, clamp (ctl.knee, 0.0, 1.0));
    const double beta = 1.0 - 1.0 / loopGain;   // closed-loop gain of exactly one
    pairTanh = pairImbalance;
    pairB = std::atanh (pairImbalance);
    const double sech2 = 1.0 - pairImbalance * pairImbalance;
    loopC = beta * headroom;
    loopK = loopGain / (headroom * sech2);       // small-signal loop gain c k sech^2 (b) = beta * loopGain
    invBeta = 1.0 / beta;

    inGain = ctl.gain;
    offset = ctl.gain * ctl.bias * kBiasFullScale;
    outGain = driveCompensation (ctl.gain);

    ConsoleState scratch;
    restAmp = 0.0;
    for (int i = 0; i < 3; ++i)   // until the call sits on the last one: no ADAA step left
        restAmp = consoleAmp (*this, scratch, offset);

    dt = 1.0 / sampleRate;
    invDt = sampleRate;
    core.set (1.0, dt * kTwoPi * coreCornerHz * fluxKnee, fluxKnee);
}

//==============================================================================
void FoldCoeffs::update (const ClipperControls& ctl) noexcept
{
    const double scale = kDiodeClip * diodeSoftness (ctl.knee, 0.02, 0.12);
    cell.set (1.0, 3.0 * kDiodeClip / std::sinh (kDiodeClip / scale), scale);
    const double smallSignal = cell.smallSignal();   // of one diode stage
    cellGain = 2.0 / smallSignal;                     // slope +1 below the knee, -1 above

    // Height of one fold: the maximum of a cell's transfer curve. A real junction starts
    // to conduct well below its nominal forward voltage, so this is found by scanning.
    double foldPeak = 0.0;
    for (int i = 1; i <= 64; ++i)
    {
        const double v = 2.0 * kDiodeClip * static_cast<double> (i) / 64.0;
        foldPeak = std::max (foldPeak, cellGain * solveDiodePair (cell, v) - v);
    }

    // The end stage lifts the folds to 0.7 of its diodes' clip voltage, where a hard
    // junction is still an open circuit, so it only acts on what the cells did not fold.
    const double endScale = kDiodeClip * 0.03;
    end.set (1.0, 3.0 * kDiodeClip / std::sinh (kDiodeClip / endScale), endScale);
    endGain = 0.7 * kDiodeClip / foldPeak;

    // Unity for small signals at unity drive, and a fold peak at the clip level.
    const double voltsPerUnit = foldPeak / kClipLevel;
    inGain = ctl.gain * voltsPerUnit;
    offset = ctl.gain * ctl.bias * kBiasFullScale * voltsPerUnit;
    outGain = driveCompensation (ctl.gain) * kClipLevel / (foldPeak * endGain * end.smallSignal());
    FoldState scratch;
    rest = 0.0;
    // Repeated until every stage sits still on its input: the looser run-time tolerance
    // has converged and no ADAA step is left in the chain (one more call per stage).
    for (int i = 0; i < FoldCoeffs::kCells + 4; ++i)
        rest = foldChain (*this, scratch, offset);
}

//==============================================================================
void ClipperCoeffs::update (const ClipperControls& controls, double sampleRate) noexcept
{
    ClipperControls ctl = controls;
    ctl.gain = clamp (ctl.gain, 1.0, 1000.0);
    ctl.knee = clamp (ctl.knee, 0.0, 1.0);
    ctl.bias = clamp (ctl.bias, -1.0, 1.0);
    family = ctl.family;

    switch (family)
    {
        case ClipperFamily::DiodePair:  diodePair.update (ctl); break;
        case ClipperFamily::OpAmpHard:  opAmpHard.update (ctl, sampleRate); break;
        case ClipperFamily::AsymSoft:   asymSoft.update (ctl, sampleRate); break;
        case ClipperFamily::ScoopHard:  scoopHard.update (ctl, sampleRate); break;
        case ClipperFamily::MidHump:    midHump.update (ctl, sampleRate); break;
        case ClipperFamily::GatedFuzz:  gatedFuzz.update (ctl, sampleRate); break;
        case ClipperFamily::Console:    console.update (ctl, sampleRate); break;
        case ClipperFamily::Fold:       fold.update (ctl); break;
    }
}

void ClipperState::settle (const ClipperCoeffs& c) noexcept
{
    switch (c.family)
    {
        case ClipperFamily::OpAmpHard:
            opAmpHard.out = c.opAmpHard.restOut;
            opAmpHard.s1 = opAmpHard.s2 = 0.0;
            break;
        case ClipperFamily::ScoopHard:
            // The diode node and both tone-stage poles sit at the resting clip voltage.
            scoopHard.clip = c.scoopHard.rest;
            scoopHard.toneLow.s = scoopHard.toneHigh.s = c.scoopHard.rest;
            scoopHard.shelf.s = 0.0;
            break;
        case ClipperFamily::MidHump:
            midHump.fb = c.midHump.restFb;
            midHump.leg.s = 0.0;
            break;
        case ClipperFamily::Fold:
            for (auto& r : fold.restHistory)
                r = c.fold.rest;
            break;
        case ClipperFamily::DiodePair:
        case ClipperFamily::AsymSoft:
        case ClipperFamily::GatedFuzz:
        case ClipperFamily::Console:
            break;
    }
}

void processClipper (const ClipperCoeffs& c, ClipperState& st, double* buffer, int numSamples) noexcept
{
    switch (c.family)
    {
        case ClipperFamily::DiodePair:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = diodePairTick (c.diodePair, st.diodePair, buffer[i]);
            break;
        case ClipperFamily::OpAmpHard:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = opAmpHardTick (c.opAmpHard, st.opAmpHard, buffer[i]);
            break;
        case ClipperFamily::AsymSoft:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = asymSoftTick (c.asymSoft, st.asymSoft, buffer[i]);
            break;
        case ClipperFamily::ScoopHard:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = scoopHardTick (c.scoopHard, st.scoopHard, buffer[i]);
            break;
        case ClipperFamily::MidHump:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = midHumpTick (c.midHump, st.midHump, buffer[i]);
            break;
        case ClipperFamily::GatedFuzz:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = gatedFuzzTick (c.gatedFuzz, st.gatedFuzz, buffer[i]);
            break;
        case ClipperFamily::Console:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = consoleTick (c.console, st.console, buffer[i]);
            break;
        case ClipperFamily::Fold:
            for (int i = 0; i < numSamples; ++i)
                buffer[i] = foldTick (c.fold, st.fold, buffer[i]);
            break;
    }
}
}
