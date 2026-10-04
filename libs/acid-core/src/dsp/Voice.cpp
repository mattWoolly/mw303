#include "acid/dsp/Voice.h"

#include <bit>
#include <cmath>

#if defined (__SSE2__) || defined (_M_X64)
 #include <xmmintrin.h>
#endif

namespace acid::dsp
{
namespace
{
/** Flush-to-zero and denormals-are-zero for the lifetime of the object, restored after.
    The voice's long tails (sub-hertz coupling filters, envelope decays) would otherwise
    run into denormal arithmetic during silence. No noise or offset is added anywhere for
    this purpose. */
class ScopedFlushDenormals
{
public:
#if defined (__SSE2__) || defined (_M_X64)
    ScopedFlushDenormals() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved | 0x8040u); }
    ~ScopedFlushDenormals() noexcept { _mm_setcsr (saved); }
private:
    unsigned int saved;
#elif defined (__aarch64__)
    ScopedFlushDenormals() noexcept
    {
        __asm__ volatile ("mrs %0, fpcr" : "=r" (saved));
        const unsigned long long flush = saved | (1ull << 24);
        __asm__ volatile ("msr fpcr, %0" : : "r" (flush));
    }
    ~ScopedFlushDenormals() noexcept { __asm__ volatile ("msr fpcr, %0" : : "r" (saved)); }
private:
    unsigned long long saved = 0;
#else
    ScopedFlushDenormals() noexcept {}
    ~ScopedFlushDenormals() noexcept {}
#endif
    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;
};

/** Clamp that maps NaN to the fallback. */
double sane (double v, double lo, double hi, double fallback) noexcept
{
    return (v == v) ? clamp (v, lo, hi) : fallback;
}

/** A percentage as a fraction of travel, 0..1. */
double fraction (double percent) noexcept { return sane (percent, 0.0, 100.0, 0.0) * 0.01; }

/** G = g / (1 + g) of a trapezoidal one-pole at cornerHz. */
double tptG (double cornerHz, double sampleRate) noexcept
{
    const double g = std::tan (std::min (kPi * cornerHz / sampleRate, 0.49 * kPi));
    return g / (1.0 + g);
}

// Amplifier input mix, per unit of the filter's output w: the output amplifier's gain
// times each path's divider (2.2 k against 222.2 k direct, against 102.2 k from the
// resonance wiper). The 2 V_T of the filter's scale and of the amplifier's tanh cancel.
constexpr double kMixDirect = DiodeLadder::kOutputAmpGain * 2.2 / 222.2;
constexpr double kMixWiper = DiodeLadder::kOutputAmpGain * 2.2 / 102.2;
constexpr double kDirectOhms = 222.2e3;
constexpr double kWiperOhms = 102.2e3;

constexpr double kInputScale = Oscillator::kFilterInputDivider / DiodeLadder::kTwoVt;
constexpr double kInputCouplerSeconds = 1.0e-6 * 222.2e3;     // C17 into R62 + R70: 0.72 Hz
constexpr double kResonanceLoadOhms = 51.0e3;                 // on the wiper, around the peak
constexpr double kGateSwing = laws::kGateVolts / laws::kGateToVegVolts;

constexpr double kMinLinearHz = 5.0;
constexpr double kMinCutoffHz = DiodeLadder::kMinFresHz;
constexpr double kFmHighPassHz = 5.0;
constexpr double kMufflerSplitHz = 150.0;
constexpr double kOutputHighPassHz[3] = { 5.0, 3.4, 1.6 };    // C38, C59, C56 into their loads
constexpr double kOutputKneeAmp = Voice::kOutputClipKnee / Voice::kOutputScale;   // the knee in amplifier units
constexpr double kOutputCeilingAmp = Voice::kOutputClipCeiling / Voice::kOutputScale;

constexpr double kDriftSeconds = 3.0;
constexpr double kDriftFullCents = 15.0;                      // rms at 100 %
constexpr double kNoiseFullRms = 0.004;                       // at the amplifier input, 0..24 kHz, 100 %
constexpr double kClickFreeAccentLagSeconds = 0.003;

/** Muffler (Mods): clips what rises above an ordinary note's level, on the band above
    150 Hz only, so the bass passes. Soft is a tanh at 1.5 times a plain note's peak;
    hard is a flat-topped clip with a short knee, lower on the negative side. */
double muffleSoft (const ladder::TanhTable& table, double x) noexcept
{
    constexpr double t = 1.5 * Voice::kPlainNotePeak;
    return t * ladder::fastTanh (table, x / t);
}

double muffleHard (double x) noexcept
{
    const double t = x >= 0.0 ? Voice::kPlainNotePeak : 0.75 * Voice::kPlainNotePeak;
    const double a = std::abs (x) / t;
    double y = a;
    if (a >= 1.4)      y = 1.0;
    else if (a > 0.6)  y = a - (a - 0.6) * (a - 0.6) / 1.6;
    return std::copysign (y * t, x);
}
}

//==============================================================================
double Voice::outputStage (double x) noexcept
{
    const double ax = std::abs (x);
    if (! (ax > kOutputClipKnee))
        return x;
    constexpr double room = kOutputClipCeiling - kOutputClipKnee;
    return std::copysign (kOutputClipKnee + room * std::tanh ((ax - kOutputClipKnee) / room), x);
}

double Voice::outputGuard (double x) noexcept
{
    const double ax = std::abs (x);
    if (! (ax > kOutputClipCeiling))
        return x;
    constexpr double room = kOutputGuardCeiling - kOutputClipCeiling;
    return std::copysign (kOutputClipCeiling + room * std::tanh ((ax - kOutputClipCeiling) / room), x);
}

Voice::Voice() noexcept
    : tanhTable (&ladder::tanhTable())
{
}

void Voice::prepare (double sampleRate, int /*maxBlockSize*/, int oversampling)
{
    fs = (sampleRate >= 8000.0 && sampleRate <= 1.0e6) ? sampleRate : 48000.0;
    factor = oversampling >= 8 ? 8 : (oversampling >= 4 ? 4 : (oversampling >= 2 ? 2 : 1));
    fsOver = fs * static_cast<double> (factor);

    oscillator.prepare (fsOver);
    ladder.prepare (fsOver);
    ladder.setTolerance (kFilterTolerance);
    decimator.prepare (fs, factor);
    envelope.prepare (fs);
    sweep.prepare (fs);
    gateShaper.prepare (fs);

    smoothCoeff = onePoleCoeff (kSmoothingSeconds, fs);
    controlInterval = std::max (1, static_cast<int> (kControlSeconds * fs + 0.5));
    slowCoeff = onePoleCoeff (kSmoothingSeconds, fs / static_cast<double> (controlInterval));
    inputHpCoeff = onePoleCoeff (kInputCouplerSeconds, fsOver);

    // Input band limit: 6th-order Butterworth as three 2-pole sections, bilinear transform
    // with the corner prewarped. Off when the oversampled rate leaves no room above the
    // corner.
    bandLimitOn = fsOver >= kInputBandLimitMinRateRatio * kInputBandLimitHz;
    if (bandLimitOn)
    {
        const double k = std::tan (kPi * kInputBandLimitHz / fsOver);
        for (int i = 0; i < kInputBandLimitSections; ++i)
        {
            const double damping = 2.0 * std::cos (kPi * static_cast<double> (2 * i + 1)
                                                   / static_cast<double> (4 * kInputBandLimitSections));  // 1 / Q
            const double norm = 1.0 / (1.0 + k * damping + k * k);
            BandLimitSection& b = bandLimit[i];
            b.b0 = k * k * norm;
            b.b1 = 2.0 * b.b0;
            b.b2 = b.b0;
            b.a1 = 2.0 * (k * k - 1.0) * norm;
            b.a2 = (1.0 - k * damping + k * k) * norm;
        }
    }
    fmHpCoeff = onePoleCoeff (1.0 / (kTwoPi * kFmHighPassHz), fsOver);
    mufflerLpCoeff = onePoleCoeff (1.0 / (kTwoPi * kMufflerSplitHz), fsOver);
    waveStep = 1.0 / (0.001 * fs);
    for (int i = 0; i < 3; ++i)
        outHpG[i] = tptG (kOutputHighPassHz[i], fsOver);

    // Drift: white noise through two equal one-poles, normalised to unit rms.
    driftCoeff = onePoleCoeff (kDriftSeconds, fs);
    {
        const double a = 1.0 - driftCoeff;
        const double a2 = a * a;
        const double c4 = driftCoeff * driftCoeff * driftCoeff * driftCoeff;
        const double variance = (1.0 / 12.0) * c4 * (1.0 + a2) / ((1.0 - a2) * (1.0 - a2) * (1.0 - a2));
        driftNorm = 1.0 / std::sqrt (variance);
    }

    // Uniform noise of amplitude A has rms A / sqrt (12); keep the density constant.
    noiseRateScale = kNoiseFullRms * std::sqrt (12.0) * std::sqrt (fsOver / 48000.0);

    maxCutoffHz = std::min (DiodeLadder::kMaxFresOverFs * fsOver, laws::kCutoffCeilingHz);
    cpuPeriodSamples = kCpuInterruptSeconds * fs;
    cpuOffLagSamples = kCpuGateOffLagSeconds * fs;

    prepared = true;
    loadTargets();
    reset();
}

void Voice::reset() noexcept
{
    oscillator.reset();
    ladder.reset();
    ladder.resetStats();
    decimator.reset();
    envelope.reset();
    sweep.reset();
    gateShaper.reset();

    value = target;
    movingMask = 0;
    snapSmoothers = true;
    switchesChanged = false;
    controlCounter = 0;
    updateSlowTrims (true);

    cpuGate = false;
    accentLatched = false;
    slideOn = false;
    noteVibrato = false;
    triggeredThisTick = false;
    targetVolts = cvVolts = laws::kTuneReferenceVolts;
    vibTimer = 0.0;
    vibSign = 1.0;
    ampEnv = laws::kVegFloor;
    accentCurrent = 0.0;
    drift1 = drift2 = 0.0;
    cpuPhase = 0.0;
    pendingOn = {};
    pendingOff = -1;
    driftRng.seed (params.seed);
    noiseRng.seed (params.seed ^ 0x9E3779B97F4A7C15ull);

    waveMix = waveMixPrev = (params.wave == 1 ? 1.0 : 0.0);
    updateDerived (kAllGroups);
    pendingGroups = 0;

    hzNow = hzPrev = laws::oscillatorHz (cvVolts, octaveWidth, tuneSemis);
    cutoffNow = cutoffPrev = clamp (nominalHz * std::exp2 (-envOctPerVolt * laws::kVq9), kMinCutoffHz, maxCutoffHz);
    gainNow = gainPrev = 0.0;
    resNow = resPrev = resonanceR;
    wiperNow = wiperPrev = wiperFraction;

    // The input coupling capacitors start charged to each waveform's mean, as they are
    // in a unit that has been on for a few seconds.
    sawHp = oscillator.sawMeanVolts();
    squareHp = Oscillator::kSquareTypicalMeanVolts;
    for (BandLimitSection& b : bandLimit)
        b.z1 = b.z2 = 0.0;
    directZ = wiperZ = 0.0;
    mufflerZ = 0.0;
    fmZ = fmSignal = 0.0;
    outZ[0] = outZ[1] = outZ[2] = 0.0;
    overBuffer.fill (0.0);

    taps = {};
    taps.nominalCutoffHz = nominalHz;
    taps.cutoffHz = cutoffNow;
    taps.pitchVolts = cvVolts;
    taps.pitchHz = hzNow;
    taps.resonance = resonanceR;
    taps.ampEnvelope = ampEnv;
}

//==============================================================================
void Voice::setParams (const VoiceParams& p) noexcept
{
    if (p.wave != params.wave || p.tuneExt != params.tuneExt
        || p.sweepCouple != params.sweepCouple || p.accVolCouple != params.accVolCouple
        || p.calVcaClicks != params.calVcaClicks || p.modOn != params.modOn
        || p.modHiRes != params.modHiRes || p.modSweepSpeed != params.modSweepSpeed
        || p.modMuffler != params.modMuffler || p.modExtRange != params.modExtRange
        || p.modSubOct != params.modSubOct)
        switchesChanged = true;

    params = p;
    loadTargets();
    flagMoving();
}

std::uint32_t Voice::groupsOf (int smoothedIndex) noexcept
{
    switch (smoothedIndex)
    {
        case sTune: case sTm5: case sDrift:                 return gPitch;
        case sCutoff:                                       return gCutoff;
        case sTm3:                                          return gTm3;
        case sEnvMod:                                       return gEnvMod;
        case sCvScale: case sAccEnv:                        return gDepth;
        case sResonance:                                    return gResonance | gSweepNetwork | gSweepRates;
        case sSweepAmt:                                     return gSweepNetwork | gSweepRates;
        case sAccent:                                       return gAccent | gSweepNetwork | gSweepRates;
        case sAccVol: case sAccAmp:                         return gAccent;
        case sC13Log:                                       return gSweepRates;
        case sTracking: case sFm: case sSubLevel:
        case sNoise: case sSustain:                         return gLevels;
        case sOverdrive:                                    return gOverdrive;
        case sSlide:                                        return gSlide;
        case sVibSpeed: case sVibDepth:                     return gVibrato;
        case sEnvAttack: case sDecay: case sAccDecay:       return gEnvTimes;
        case sAmpDecayLog:                                  return gAmp;
        case sGateDelay: case sSoftAttackLog:               return gGate;
        case sC21Log:                                       return gCoupler;
        case sPw:                                           return gOscillator;
        default:                                            return 0;       // wheel and bend are read directly
    }
}

void Voice::flagMoving() noexcept
{
    for (int i = 0; i < kNumSmoothed; ++i)
        if (target[static_cast<std::size_t> (i)] != value[static_cast<std::size_t> (i)])
            movingMask |= std::uint64_t { 1 } << i;
}

void Voice::loadTargets() noexcept
{
    const VoiceParams& p = params;
    const bool mods = p.modOn;
    const auto set = [this] (Smoothed id, double v) noexcept { target[static_cast<std::size_t> (id)] = v; };

    set (sTune, p.tuneExt ? static_cast<double> (clamp (p.tuneCoarse, -12, 12)) + sane (p.tuneFine, -100.0, 100.0, 0.0) * 0.01
                          : sane (p.tune, -12.0, 12.0, 0.0));
    set (sCutoff, fraction (p.cutoff));
    set (sResonance, fraction (p.resonance));
    set (sEnvMod, fraction (p.envMod));
    set (sDecay, fraction (p.decay));
    set (sAccent, fraction (p.accent));
    set (sSlide, fraction (p.slideTime));
    set (sSweepAmt, fraction (p.sweepAmt));
    set (sEnvAttack, fraction (p.envAttack));
    set (sAccDecay, fraction (p.accDecay));
    set (sAccVol, fraction (p.accVol));
    set (sVibSpeed, fraction (p.vibSpeed));
    set (sVibDepth, fraction (p.vibDepth));

    set (sTm3, sane (p.calTm3, -500.0, 500.0, 0.0));
    set (sC21Log, std::log (sane (p.calC21, 7.9, 31800.0, 15.9)));
    set (sAccEnv, sane (p.calAccEnv, 0.0, 4000.0, 1951.0));
    set (sAccAmp, sane (p.calAccAmp, 0.0, 3.5, 1.1));
    set (sC13Log, std::log (sane (p.calC13, 0.33, 4.7, 1.0)));
    set (sTm5, sane (p.calTm5, 0.9, 1.1, 1.0));
    set (sPw, sane (p.calPw, -100.0, 100.0, 0.0));
    set (sNoise, fraction (p.calVcaNoise));
    set (sCvScale, sane (p.calCvScale, 0.75, 1.05, 0.88));
    set (sGateDelay, sane (p.calGateDelay, 0.0, 6.0, 5.4));
    set (sDrift, fraction (p.calDrift));

    // Mods: with the master switch off every one of these sits at its stock value, so the
    // voice computes exactly what it would with the mod controls untouched.
    set (sOverdrive, mods ? fraction (p.modOverdrive) : 0.0);
    set (sFm, mods ? fraction (p.modFilterFm) : 0.0);
    set (sTracking, mods ? fraction (p.modTracking) : 0.0);
    set (sAmpDecayLog, std::log (mods ? sane (p.modAmpDecay, 7.0, 1500.0, 1500.0) : 1500.0));
    set (sSustain, mods ? fraction (p.modAmpSustain) : 0.0);
    set (sSoftAttackLog, std::log (mods ? sane (p.modSoftAttack, 0.3, 30.0, 0.5) : 0.5));
    set (sSubLevel, mods ? fraction (p.modSubLevel) : 0.0);

    rbTarget = sane (p.calRb, 10.0, 35.0, 20.0) * 1.0e3;
    kMaxTarget = sane (p.calKmax, 17.0, 20.0, 18.7);
}

void Voice::updateSlowTrims (bool snap) noexcept
{
    const auto step = [this, snap] (double& v, double t) noexcept
    {
        const double d = t - v;
        if (snap || std::abs (d) < 1.0e-6 * std::abs (t)) v = t;
        else v += d * slowCoeff;
    };
    step (rbValue, rbTarget);
    step (kMaxValue, kMaxTarget);
    ladder.setTrims (rbValue, kMaxValue);       // does nothing when neither moved
}

void Voice::updateDerived (std::uint32_t groups) noexcept
{
    const auto v = [this] (Smoothed id) noexcept { return value[static_cast<std::size_t> (id)]; };
    const VoiceParams& p = params;
    const bool mods = p.modOn;
    const bool extended = mods && p.modExtRange;

    if ((groups & gSwitches) != 0)
    {
        mufflerMode = mods ? clamp (p.modMuffler, 0, 2) : 0;
        waveTarget = p.wave == 1 ? 1.0 : 0.0;
        sweepSpeed = ! mods ? AccentSweep::Speed::Normal
                   : (p.modSweepSpeed == 1 ? AccentSweep::Speed::Fast
                   : (p.modSweepSpeed == 2 ? AccentSweep::Speed::Slow : AccentSweep::Speed::Normal));
    }

    // Pitch.
    if ((groups & gPitch) != 0)
    {
        tuneSemis = v (sTune);
        octaveWidth = v (sTm5);
        driftCents = kDriftFullCents * v (sDrift);
    }
    if ((groups & gSlide) != 0)
        slideCoeff = onePoleCoeff (laws::slideSeconds (v (sSlide)), fs);
    if ((groups & gVibrato) != 0)
    {
        vibHalfSamples = std::max (laws::vibratoHalfPeriodSeconds (v (sVibSpeed)) * fs, 1.0);
        vibSpreadVolts = laws::vibratoSpreadSemitones (v (sVibDepth)) / 12.0;
    }

    // Cutoff: the knob and TM3 set a current (linear in Hz); envelope and accent are
    // summed in octaves about the 3.3 V bus, both scaled by the CV scale trim and by the
    // rise of the depth with the knob's current.
    if ((groups & gTm3) != 0)
        tm3Scale = laws::tm3Multiplier (v (sTm3));
    if ((groups & (gCutoff | gTm3)) != 0)
    {
        if ((groups & gCutoff) != 0)
            knobHz = laws::cutoffNominalHz (v (sCutoff), extended);
        nominalHz = knobHz * tm3Scale;
        depthRise = laws::depthRise (nominalHz);
        groups |= gDepth;
    }
    if ((groups & gEnvMod) != 0)
    {
        envLaw = laws::envModOctavesPerVolt (v (sEnvMod), extended);
        groups |= gDepth;
    }
    if ((groups & gDepth) != 0)
    {
        const double scale = v (sCvScale) * depthRise;
        envOctPerVolt = envLaw * scale;
        accOctPerVolt = laws::kKAcc * scale * v (sAccEnv) / 1951.0;
    }

    // Main envelope.
    if ((groups & gEnvTimes) != 0)
        envelope.setTimes (laws::envAttackSeconds (v (sEnvAttack)), laws::decaySeconds (v (sDecay)),
                           laws::accentDecaySeconds (v (sAccDecay)));

    // Accent: the pot's wiper feeds the sweep network and the amplifier branch.
    if ((groups & gAccent) != 0)
    {
        accentWiperGain = laws::accentWiperGain (v (sAccent));
        accentVolGain = p.accVolCouple ? accentWiperGain : laws::accentWiperGain (v (sAccVol));
        accentAmpPerVolt = v (sAccAmp) / 1.1 / (laws::kAccentVcaOhms * laws::kVegFullAmps);
    }

    // Resonance: the first gang's wiper (bowed by its load) sets the loop gain and the
    // make-up path; the second gang's position is the accent sweep's tap.
    if ((groups & gResonance) != 0)
    {
        wiperFraction = DiodeLadder::wiperFraction (v (sResonance), kResonanceLoadOhms);
        resonanceR = wiperFraction * ((mods && p.modHiRes) ? 2.0 : 1.0);
    }
    if ((groups & gSweepRates) != 0)
        c13Farads = std::exp (v (sC13Log)) * 1.0e-6;
    if ((groups & (gSweepNetwork | gSweepRates)) != 0)
        sweep.configureNetwork (v (sAccent), p.sweepCouple ? v (sResonance) : v (sSweepAmt), c13Farads, sweepSpeed);
    if ((groups & gSweepRates) != 0)
        sweep.updateRates();

    // Amplitude.
    if ((groups & gAmp) != 0)
        vegCoeff = onePoleCoeff (std::exp (v (sAmpDecayLog)) * 1.0e-3, fs);
    if ((groups & gGate) != 0)
    {
        gateShaper.setTimes (v (sGateDelay) * 1.0e-3, std::exp (v (sSoftAttackLog)) * 1.0e-3, ! p.calVcaClicks);
        accLagCoeff = onePoleCoeff (p.calVcaClicks ? laws::kAccentVcaLagSeconds : kClickFreeAccentLagSeconds, fs);
    }

    // Audio path.
    if ((groups & gLevels) != 0)
    {
        trackHzPerVolt = laws::trackingHzPerVolt (v (sTracking));
        vegTarget = laws::kVegFloor + (1.0 - laws::kVegFloor) * v (sSustain);
        fmHzPerUnit = laws::filterFmHzPerUnit (v (sFm));
        subLevel = v (sSubLevel);
        noiseAmp = v (sNoise) * noiseRateScale;
    }
    if ((groups & gOverdrive) != 0)
        overdrive = laws::overdriveGain (v (sOverdrive));
    if ((groups & gCoupler) != 0)
    {
        const double c21 = std::exp (v (sC21Log)) * 1.0e-9;
        couplerDirectG = tptG (1.0 / (kTwoPi * kDirectOhms * c21), fsOver);
        couplerWiperG = tptG (1.0 / (kTwoPi * kWiperOhms * c21), fsOver);
    }
    if ((groups & gOscillator) != 0)
    {
        OscillatorParams op;
        op.sawTopVolts = OscillatorParams::sawTopFromPwShift (v (sPw));
        op.subOctaves = (mods && p.modSubOct == 1) ? 2 : 1;
        op.squareInUse = waveTarget > 0.0 || waveMix > 0.0;
        oscillator.setParams (op);
    }
}

//==============================================================================
void Voice::noteOn (int midiNote, bool accent, bool legato, bool vibrato) noexcept
{
    if (! params.calCpuJitter)
    {
        // An edge still waiting from before the switch was turned off goes first.
        if (pendingOff >= 0) { applyNoteOff(); pendingOff = -1; }
        pendingOn.countdown = -1;
        applyNoteOn (midiNote, accent, legato, vibrato);
        return;
    }

    // The CPU acts on its next interrupt.
    const double wait = cpuPhase > 0.0 ? cpuPeriodSamples - cpuPhase : 0.0;
    pendingOn.countdown = static_cast<int> (std::ceil (wait));
    pendingOn.note = midiNote;
    pendingOn.accent = accent;
    pendingOn.legato = legato;
    pendingOn.vibrato = vibrato;
    if (pendingOff > pendingOn.countdown)
        pendingOff = pendingOn.countdown;       // a release issued earlier still comes first
}

void Voice::noteOff() noexcept
{
    if (! params.calCpuJitter)
    {
        if (pendingOn.countdown >= 0)
        {
            applyNoteOn (pendingOn.note, pendingOn.accent, pendingOn.legato, pendingOn.vibrato);
            pendingOn.countdown = -1;
        }
        pendingOff = -1;
        applyNoteOff();
        return;
    }

    // Gate-off lands 1.35 ms later than gate-on within the interrupt, which is where the
    // sequencer's extra 1.35 ms of gate comes from: quantise on a grid shifted by that.
    double wait = std::fmod (cpuPeriodSamples - cpuPhase + cpuOffLagSamples, cpuPeriodSamples);
    if (wait < 0.0) wait = 0.0;
    pendingOff = static_cast<int> (std::ceil (wait));
    if (pendingOn.countdown >= 0 && pendingOff <= pendingOn.countdown)
        pendingOff = pendingOn.countdown + 1;   // never release a note before it started
}

void Voice::setPitchBend (double semitones) noexcept
{
    target[sBend] = sane (semitones, -48.0, 48.0, 0.0);
    if (target[sBend] != value[sBend]) movingMask |= std::uint64_t { 1 } << sBend;
}

void Voice::setVibratoWheel (double depth01) noexcept
{
    target[sWheel] = sane (depth01, 0.0, 1.0, 0.0);
    if (target[sWheel] != value[sWheel]) movingMask |= std::uint64_t { 1 } << sWheel;
}

void Voice::applyNoteOn (int midiNote, bool accent, bool legato, bool vibrato) noexcept
{
    // The latch strobe: pitch and accent change here and nowhere else.
    accentLatched = accent;
    noteVibrato = vibrato;
    targetVolts = laws::pitchVolts (static_cast<double> (clamp (midiNote, 0, 127)));

    // The slide switch is held for the whole of a slid-to note; otherwise the lag
    // capacitor follows the output, so the pitch jumps.
    slideOn = legato;
    if (! legato)
        cvVolts = targetVolts;

    // A gate edge fires both envelopes. A slid-to note has no edge.
    if (! cpuGate || ! legato)
    {
        envelope.trigger();
        ampEnv = 1.0;
        if (cpuGate) gateShaper.gateOff();
        gateShaper.gateOn();
        vibTimer = 0.0;
        vibSign = 1.0;
        triggeredThisTick = true;
    }
    cpuGate = true;
}

void Voice::applyNoteOff() noexcept
{
    if (cpuGate)
        gateShaper.gateOff();
    cpuGate = false;
}

//==============================================================================
void Voice::controlTick() noexcept
{
    // Gate edges waiting for the CPU's interrupt grid.
    if (pendingOff == 0)     { applyNoteOff(); pendingOff = -1; }
    else if (pendingOff > 0) --pendingOff;
    if (pendingOn.countdown == 0)
    {
        applyNoteOn (pendingOn.note, pendingOn.accent, pendingOn.legato, pendingOn.vibrato);
        pendingOn.countdown = -1;
    }
    else if (pendingOn.countdown > 0)
    {
        --pendingOn.countdown;
    }
    cpuPhase += 1.0;
    if (cpuPhase >= cpuPeriodSamples) cpuPhase -= cpuPeriodSamples;

    // Knob smoothing: one pole on each knob value, before its law. Only the values that
    // are moving are touched, and only the coefficient groups they feed are re-derived:
    // the fast groups now, the slow ones on the control grid.
    if (snapSmoothers)
    {
        // First tick after prepare or reset: whatever setParams left jumps into place.
        if (movingMask != 0)
        {
            value = target;
            movingMask = 0;
            switchesChanged = true;
        }
        updateSlowTrims (true);
        snapSmoothers = false;
    }
    if (switchesChanged)
    {
        updateDerived (kAllGroups);
        pendingGroups = 0;
        switchesChanged = false;
    }
    if (movingMask != 0)
    {
        std::uint64_t bits = movingMask;
        do
        {
            const int index = std::countr_zero (bits);
            const auto i = static_cast<std::size_t> (index);
            bits &= bits - 1;
            const double d = target[i] - value[i];
            if (std::abs (d) < kSmoothingStop * (1.0 + std::abs (target[i])))
            {
                value[i] = target[i];
                movingMask &= ~(std::uint64_t { 1 } << index);
            }
            else
            {
                value[i] += d * smoothCoeff;
            }
            pendingGroups |= groupsOf (index);
        }
        while (bits != 0);
    }
    if (--controlCounter <= 0)
    {
        controlCounter = controlInterval;
        updateSlowTrims (false);
        if ((pendingGroups & kSlowGroups) != 0)
        {
            updateDerived (pendingGroups & kSlowGroups);
            pendingGroups &= kFastGroups;
        }
    }
    if ((pendingGroups & kFastGroups) != 0)
    {
        updateDerived (pendingGroups & kFastGroups);
        pendingGroups &= kSlowGroups;
    }

    // Waveform switch: a 1 ms crossfade.
    waveMixPrev = waveMix;
    if (waveMix != waveTarget)
    {
        waveMix = waveTarget > waveMix ? std::min (waveMix + waveStep, waveTarget)
                                       : std::max (waveMix - waveStep, waveTarget);
        if (waveMix == waveTarget) pendingGroups |= gOscillator;    // lets the square shaper idle again
    }

    // Main envelope and the two things it drives in the filter.
    const double meg = envelope.tick (accentLatched);
    const double envOct = envOctPerVolt * (meg - laws::kVq9);
    const double sweepWiper = sweep.tick (accentLatched ? meg * accentWiperGain : 0.0, accentLatched);
    const double accOct = accOctPerVolt * sweepWiper;

    // Pitch CV: the trill moves the target, the slide lag follows it.
    double pitchTarget = targetVolts;
    bool lag = slideOn;
    const double vibAmount = vibSpreadVolts * std::max (noteVibrato ? 1.0 : 0.0, value[sWheel]);
    if (vibAmount > 0.0)
    {
        vibTimer += 1.0;
        if (vibTimer >= vibHalfSamples)
        {
            vibTimer -= vibHalfSamples;
            if (vibTimer >= vibHalfSamples) vibTimer = 0.0;
            vibSign = -vibSign;
        }
        pitchTarget += 0.5 * vibSign * vibAmount;
        lag = true;
    }
    if (lag) cvVolts += (pitchTarget - cvVolts) * slideCoeff;
    else     cvVolts = pitchTarget;

    double cents = 0.0;
    if (driftCents > 0.0)
    {
        drift1 += ((driftRng.nextDouble() - 0.5) - drift1) * driftCoeff;
        drift2 += (drift1 - drift2) * driftCoeff;
        cents = drift2 * driftNorm * driftCents;
    }

    hzPrev = hzNow;
    hzNow = laws::oscillatorHz (cvVolts + value[sBend] * (1.0 / 12.0), octaveWidth, tuneSemis + cents * 0.01);

    // Cutoff. Tracking (Mods) adds to the knob's current, so it is linear in Hz.
    double linearHz = nominalHz + trackHzPerVolt * (cvVolts - 2.0);
    if (linearHz < kMinLinearHz) linearHz = kMinLinearHz;
    cutoffPrev = cutoffNow;
    cutoffNow = clamp (laws::cutoffCeiling (linearHz * std::exp2 (envOct + accOct)), kMinCutoffHz, maxCutoffHz);

    // Amplifier control current: the gated amplitude envelope plus the accent path, which
    // bypasses the gate.
    ampEnv += (vegTarget - ampEnv) * vegCoeff;
    const double gateLevel = gateShaper.tick();
    const double gateCurrent = gateShaper.isClickFree() ? std::max (ampEnv, 0.0) * gateLevel
                                                        : std::max (ampEnv - kGateSwing * (1.0 - gateLevel), 0.0);
    const double accentTarget = accentLatched
        ? std::max (meg * accentVolGain - laws::kAccentVcaThresholdVolts, 0.0) * accentAmpPerVolt : 0.0;
    accentCurrent += (accentTarget - accentCurrent) * accLagCoeff;
    if (accentCurrent < 1.0e-15) accentCurrent = 0.0;

    gainPrev = gainNow;
    gainNow = gateCurrent + accentCurrent;
    resPrev = resNow;
    resNow = resonanceR;
    wiperPrev = wiperNow;
    wiperNow = wiperFraction;

    taps.envelopeVolts = meg;
    taps.envOctaves = envOct;
    taps.accentOctaves = accOct;
    taps.accentCapVolts = sweep.capacitorVolts();
    taps.accentWiperVolts = sweepWiper;
    taps.nominalCutoffHz = linearHz;
    taps.cutoffHz = cutoffNow;
    taps.resonance = resonanceR;
    taps.pitchVolts = cvVolts;
    taps.pitchHz = hzNow;
    taps.driftCents = cents;
    taps.ampEnvelope = ampEnv;
    taps.gateLevel = gateLevel;
    taps.gateCurrent = gateCurrent;
    taps.accentCurrent = accentCurrent;
    taps.vcaGain = gainNow;
    taps.turnOnDelaySeconds = gateShaper.lastDelaySeconds();
    taps.gate = cpuGate;
    taps.accentLatched = accentLatched;
    taps.slideOn = slideOn;
    taps.envelopeTriggered = triggeredThisTick;
    triggeredThisTick = false;
}

//==============================================================================
void Voice::process (float* out, int numSamples) noexcept
{
    if (out == nullptr || numSamples <= 0)
        return;
    if (! prepared)
    {
        for (int n = 0; n < numSamples; ++n) out[n] = 0.0f;
        return;
    }

    const ScopedFlushDenormals flush;
    const ladder::TanhTable& table = *tanhTable;
    const int steps = factor;
    const double invSteps = 1.0 / static_cast<double> (steps);

    for (int n = 0; n < numSamples; ++n)
    {
        controlTick();

        // Control values move linearly across the oversampled sub-steps.
        double hz = hzPrev, cutoff = cutoffPrev, gain = gainPrev, res = resPrev, wiper = wiperPrev, mix = waveMixPrev;
        const double dHz = (hzNow - hzPrev) * invSteps;
        const double dCutoff = (cutoffNow - cutoffPrev) * invSteps;
        const double dGain = (gainNow - gainPrev) * invSteps;
        const double dRes = (resNow - resPrev) * invSteps;
        const double dWiper = (wiperNow - wiperPrev) * invSteps;
        const double dMix = (waveMix - waveMixPrev) * invSteps;

        for (int m = 0; m < steps; ++m)
        {
            hz += dHz; cutoff += dCutoff; gain += dGain; res += dRes; wiper += dWiper; mix += dMix;

            // Oscillator, in volts. Each waveform has its own input coupling capacitor
            // here, so the waveform crossfade carries no DC step.
            const OscillatorOutput o = oscillator.tick (hz);
            sawHp += (o.saw - sawHp) * inputHpCoeff;
            squareHp += (o.square - squareHp) * inputHpCoeff;
            const double sawAc = o.saw - sawHp;
            double volts = sawAc + mix * ((o.square - squareHp) - sawAc) + subLevel * o.sub;

            // Band limit ahead of the input pair (see Voice.h).
            if (bandLimitOn)
            {
                for (BandLimitSection& b : bandLimit)
                {
                    const double y = b.b0 * volts + b.z1;
                    b.z1 = b.b1 * volts - b.a1 * y + b.z2;
                    b.z2 = b.b2 * volts - b.a2 * y;
                    volts = y;
                }
            }

            // Filter. FM (Mods) is linear in Hz and one-sided: it cannot push the control
            // current below zero.
            double fRes = cutoff;
            if (fmHzPerUnit > 0.0)
            {
                fRes += fmHzPerUnit * fmSignal;
                if (! (fRes > kMinCutoffHz)) fRes = kMinCutoffHz;
            }
            ladder.process (volts * (kInputScale * overdrive), fRes, res);

            // Amplifier input: the direct path and the resonance-wiper path, each through
            // its own coupling capacitor (trapezoidal one-poles).
            const double top = ladder.potTop();
            double v = (top - directZ) * couplerDirectG;
            double lp = v + directZ;
            directZ = lp + v;
            const double direct = top - lp;
            v = (top - wiperZ) * couplerWiperG;
            lp = v + wiperZ;
            wiperZ = lp + v;
            double vd = kMixDirect * direct + kMixWiper * wiper * (top - lp);
            if (noiseAmp > 0.0)
                vd += noiseAmp * (noiseRng.nextDouble() - 0.5);

            // Amplifier: gain linear in control current, tanh on the signal input.
            double a = gain * ladder::fastTanh (table, vd);

            if (mufflerMode != 0)
            {
                mufflerZ += (a - mufflerZ) * mufflerLpCoeff;
                const double high = a - mufflerZ;
                a = mufflerZ + (mufflerMode == 1 ? muffleSoft (table, high) : muffleHard (high));
            }

            // Filter FM source: the amplifier output after the muffler, AC coupled; used
            // on the next sub-step.
            fmZ += (a - fmZ) * fmHpCoeff;
            fmSignal = (a - fmZ) * kOutputScale;

            // Output stage: the three coupling capacitors, then the output amplifier's rails
            // (Voice.h). Only the hottest accents and the Overdrive mod reach the knee, so the
            // test is all a plain note pays.
            for (int i = 0; i < 3; ++i)
            {
                const double hv = (a - outZ[i]) * outHpG[i];
                const double hlp = hv + outZ[i];
                outZ[i] = hlp + hv;
                a -= hlp;
            }
            if (std::abs (a) > kOutputKneeAmp)
                a = outputStage (kOutputScale * a) * (1.0 / kOutputScale);

            overBuffer[static_cast<std::size_t> (m)] = a;
        }

        // The inverting mixer and the level. The rails held the oversampled signal under the
        // ceiling; band-limiting a signal that is pressed against them (the Overdrive mod)
        // can lift a sample past it, which the analog stage never does. A guard rounds that
        // excess off; nothing below the ceiling reaches it.
        double y = decimator.processSample (overBuffer.data());
        if (std::abs (y) > kOutputCeilingAmp)
            y = outputGuard (kOutputScale * y) * (1.0 / kOutputScale);
        taps.amplifierOut = y;
        out[n] = static_cast<float> (-kOutputScale * y);
    }
}

VoiceMeters Voice::meters() const noexcept
{
    VoiceMeters m;
    m.accentCharge = static_cast<float> (taps.accentCapVolts);
    m.envelope = static_cast<float> (taps.envelopeVolts / laws::kMegPeakVolts);
    m.amplitude = static_cast<float> (taps.vcaGain);
    m.cutoffHz = static_cast<float> (taps.cutoffHz);
    m.pitchHz = static_cast<float> (taps.pitchHz);
    m.gate = taps.gate;
    m.accent = taps.accentLatched;
    m.slide = taps.slideOn;
    return m;
}
}
