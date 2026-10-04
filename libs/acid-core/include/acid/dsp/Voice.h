#pragma once
// The voice: one monophonic instrument (docs/SPEC.md section 6.1).
//
//   base rate, once per sample                       oversampled rate, M times per sample
//   -------------------------------------------      ------------------------------------------
//   knob smoothing (3 ms) and knob laws              oscillator (saw, square, sub)
//   main envelope, Env Mod                           input coupling 0.72 Hz, band limit 24 kHz
//   accent sweep network (C13)                       divider, overdrive
//   pitch: slide lag, vibrato trill, tuning, drift   diode ladder
//   cutoff: knob, envelope and accent in octaves     amplifier input mix: direct path and the
//   amplitude: gate shaper, decay, accent current      resonance-wiper make-up path, each through
//                                                      its own coupling corner
//   pitch, cutoff, resonance and gain are            amplifier: gain * tanh, optional noise
//   interpolated linearly across the M sub-steps     muffler (Mods), filter FM tap (Mods)
//   inverting mixer, level                           output stage: three coupling high-passes,
//                                                      output amplifier rails (soft clip)
//                                                    decimator
//
// Knob laws are only evaluated while a control is moving, and only the ones that control
// feeds: the cheap ones that shape the sound directly (cutoff, depths, resonance, levels)
// on every sample, time constants and trims on a grid of kControlSeconds. Pitch bend and
// the vibrato wheel are smoothed and applied directly and cost nothing extra.
//
// The band limit is a 6th-order Butterworth low-pass at kInputBandLimitHz on the oscillator
// signal, ahead of the filter's input pair. The oscillator's harmonics run at full strength
// up to half the oversampled rate, and the pair's tanh would mix the ultrasonic ones down to
// components that fold back into the audio band (-74 dBc at C6 without it, -115 dBc with
// it, at 48 kHz x4); the hardware's own reset edge takes some tens of microseconds, so
// nothing below 20 kHz is lost by it (0.46 dB at 20 kHz, 0.03 dB at 16 kHz, ahead of a
// filter that is 57 dB down four octaves above its resonant frequency). It is active when
// the oversampled rate is 80 kHz or more. It does nothing for the Overdrive mod: with the
// input pair driven into hard clipping the ladder itself makes harmonics above the
// oversampled Nyquist, and those fold (-58 dBc at C4, -36 dBc at C6, Overdrive 100 %, 48 kHz
// x4; 8x oversampling is the remedy, which is the engine's tier choice).
//
// Call order: prepare() once (not real-time safe in the strict sense only because it may
// be slow, it does not allocate either), then per host block: setParams() once, then for
// each stretch between events: noteOn / noteOff / setPitchBend / setVibratoWheel followed
// by process(). Events take effect at the first sample of the next process() call, so the
// caller splits blocks at event offsets. One instance, one thread; setParams, the event
// calls, process, reset and meters never allocate, lock or throw.
//
// Notes follow the hardware's semantics:
//   noteOn (legato = false)  latches pitch and accent, retriggers the main envelope and
//                            the amplitude envelope, opens the gate. Pitch jumps.
//   noteOn (legato = true)   latches pitch and accent only: no retrigger, and the 22 ms
//                            pitch lag is engaged for the whole of this note. (If the gate
//                            is low, the envelopes are triggered as well, since the gate
//                            edge is what fires them.)
//   noteOff                  closes the gate. The accent latch keeps its state through
//                            rests; the main envelope has no release stage.
// The oscillator free-runs: nothing resets its phase.
//
// Output: mono float at the base rate, polarity inverted relative to the filter output
// (the output mixer inverts). A plain C2 saw at default settings peaks at -12 dBFS, as the
// build spec asks. Accents rise above that: the amplifier's control current grows with the
// accent, and with Accent at 100 and stock controls the amplifier alone would reach
// +9.5 dBFS (saw, cutoff 65, Env Mod 100, resonance 0, C2), +16 dBFS with the Mods Overdrive
// and Filter FM at 100 as well. The hardware's output amplifier runs out of rail on exactly
// those notes ("some clipping can occur in the output amplifier ... on the loudest (for
// instance the accented) notes", research doc 03 section 9 and implication 16), and the
// voice models that: outputStage() is exactly linear up to kOutputClipKnee (0 dBFS), so a
// plain note, and anything else that stays under full scale, passes untouched (0 % added
// distortion), and above the knee it rounds off towards kOutputClipCeiling (+6 dBFS) along
// a tanh. It sits after the three coupling capacitors (the rails clip the AC signal, as in
// the circuit) and runs at the oversampled rate, ahead of the decimator, so what it adds is
// band-limited like the rest of the voice; the filter FM tap takes the amplifier output
// before it, as the circuit does. The measured worst cases over a grid of stock controls
// (both waveforms, cutoff 0 to 100, Env Mod 0 to 100, resonance 0 to 100, Decay 0 and 100,
// C1 to C4, 44.1 and 48 kHz x4, six accented 16ths at 125 BPM so that C13 accumulates) are
// the constants kWorstCaseStockPeak and kWorstCaseModsPeak below. Band-limiting a signal
// pressed against the rails (the Overdrive mod, whose ladder output is mostly ultrasonic
// harmonics) lifts some samples past the ceiling, by up to 47 % (+9.4 dBFS) on that grid;
// a guard at the base rate, outputGuard(), rounds that excess off between +6.0 and
// +6.8 dBFS. It touches nothing under the ceiling, and stock settings barely reach it.
#include "acid/common/Math.h"
#include "acid/common/Rng.h"
#include "acid/dsp/Decimator.h"
#include "acid/dsp/DiodeLadder.h"
#include "acid/dsp/Oscillator.h"
#include "acid/dsp/VoiceControl.h"
#include "acid/dsp/VoiceLaws.h"
#include "acid/engine/ParamTable.h"

#include <array>
#include <cstdint>

namespace acid::dsp
{
/** Every Synth, Calibration and Mods row of the parameter table, in the table's natural
    units. Defaults equal the table's. The laws that turn these into physics are in
    VoiceLaws.h and are applied inside the voice, after smoothing. */
struct VoiceParams
{
    // ---- Synth ----
    int wave = 0;                   // 0 saw, 1 square
    bool tuneExt = false;           // true: tuneCoarse + tuneFine replace tune
    double tune = 0.0;              // semitones, -12..12
    int tuneCoarse = 0;             // semitones, -12..12
    double tuneFine = 0.0;          // cents, -100..100
    double cutoff = 35.0;           // percent of the stock pot: 314 Hz..2394 Hz nominal
    double resonance = 75.0;        // percent of rotation
    double envMod = 45.0;           // percent of the stock pot
    double decay = 40.0;            // percent of the stock pot: 80 ms..1.08 s
    double accent = 70.0;           // percent of rotation; filter accent starts near 45 %
    double slideTime = 50.0;        // percent; 50 is the stock 22 ms
    bool sweepCouple = true;        // true: resonance sets the accent sweep tap
    double sweepAmt = 100.0;        // percent; the tap when uncoupled (100 = smooth)
    double envAttack = 0.0;         // percent; 0 is the stock 0.1 ms
    double accDecay = 0.0;          // percent; 0 is the stock 80 ms
    bool accVolCouple = true;       // true: accent also sets the amplitude accent
    double accVol = 100.0;          // percent; amplitude accent when uncoupled
    double vibSpeed = 50.0;         // percent; 50 is a 10 Hz trill
    double vibDepth = 50.0;         // percent; 100 is a 2 semitone spread

    // ---- Calibration ----
    double calTm3 = 0.0;            // mV equivalent, -500..500; 500 mV is one octave of cutoff
    double calC21 = 15.9;           // nF against 222.2 k: 15.9 nF is 45 Hz, 10 nF is 72 Hz
    double calAccEnv = 1951.0;      // mV; accent sweep depth, 1951 is the stock 1.19 oct/V
    double calAccAmp = 1.1;         // V; accent amplitude current, 1.1 is stock (+11 dB at full accent)
    double calC13 = 1.0;            // uF; the accent sweep capacitor
    double calTm5 = 1.0;            // octaves per volt of the oscillator, pivot A2
    double calPw = 0.0;             // percent, -100..100; square pulse-width shift
    bool calVcaClicks = true;       // false: click-free gate ramps
    double calVcaNoise = 0.0;       // percent; amplifier noise, gated by the amplifier
    double calRb = 20.0;            // kOhm; ladder load
    double calKmax = 18.7;          // loop gain at full resonance
    double calCvScale = 0.88;       // multiplier on octaves per volt of envelope and accent
    double calGateDelay = 5.4;      // ms; gate turn-on delay after a long pause
    double calDrift = 10.0;         // percent; 100 is 15 cents rms of slow pitch wander
    bool calCpuJitter = false;      // true: gate edges snap to a free-running 1.8 ms grid

    // ---- Mods (all inert while modOn is false) ----
    bool modOn = false;
    double modOverdrive = 0.0;      // percent; 0 is stock level, 100 is 66.6 times
    double modFilterFm = 0.0;       // percent
    double modTracking = 0.0;       // percent; 100 is 2.7 kHz per octave above C2
    double modAmpDecay = 1500.0;    // ms; amplitude envelope time constant
    double modAmpSustain = 0.0;     // percent
    double modSoftAttack = 0.5;     // ms; gate rise time, 10 to 90 %
    bool modHiRes = false;          // doubles the feedback
    int modSweepSpeed = 0;          // 0 normal, 1 fast, 2 slow
    int modMuffler = 0;             // 0 off, 1 soft, 2 hard
    bool modExtRange = false;       // extended cutoff and Env Mod ranges
    double modSubLevel = 0.0;       // percent
    int modSubOct = 0;              // 0: one octave down, 1: two

    // ---- Extras (not host parameters) ----
    std::uint64_t seed = 0x6d77313246ull;   // drift and noise generators, applied at prepare() and reset()
};

/** Builds the voice's parameters from a host parameter snapshot. Values are copied in
    their natural units; out-of-range values are clamped inside the voice. */
VoiceParams voiceParamsFrom (const ParamValues& values) noexcept;

/** What the UI shows, as of the last processed sample. */
struct VoiceMeters
{
    float accentCharge = 0.0f;      // accent capacitor above its rest level, volts (0..about 1.6)
    float envelope = 0.0f;          // main envelope, 0..1 of its peak
    float amplitude = 0.0f;         // amplifier control current, 1 = a plain note at onset
    float cutoffHz = 0.0f;          // resonant frequency of the filter
    float pitchHz = 0.0f;           // oscillator frequency
    bool gate = false;              // the note gate
    bool accent = false;            // the accent latch
    bool slide = false;             // the pitch lag is engaged
};

/** Internal control signals as of the last processed sample. For tests and analysis only;
    read through Voice::debugTaps(). */
struct VoiceDebugTaps
{
    double envelopeVolts = 0.0;         // main envelope, 0..10 V
    double envOctaves = 0.0;            // Env Mod's contribution to the cutoff
    double accentOctaves = 0.0;         // the accent sweep's contribution
    double accentCapVolts = 0.0;        // C13 above rest
    double accentWiperVolts = 0.0;      // sweep wiper above rest
    double nominalCutoffHz = 0.0;       // knob, TM3 and tracking (bus at rest)
    double cutoffHz = 0.0;              // resonant frequency sent to the filter (before filter FM)
    double resonance = 0.0;             // the filter's r
    double pitchVolts = 0.0;            // pitch CV after the lag and the trill
    double pitchHz = 0.0;
    double driftCents = 0.0;
    double ampEnvelope = 0.0;           // amplitude envelope, 1 at the trigger
    double gateLevel = 0.0;             // analog gate, 0..1
    double gateCurrent = 0.0;           // gated envelope's share of the control current, 1 = plain onset
    double accentCurrent = 0.0;         // the accent path's share, same scale
    double vcaGain = 0.0;               // their sum
    double turnOnDelaySeconds = 0.0;    // delay chosen at the last gate edge
    double amplifierOut = 0.0;          // decimated output stage signal (couplings and rails), before the inverting mixer's level
    bool gate = false;
    bool accentLatched = false;
    bool slideOn = false;
    bool envelopeTriggered = false;     // a trigger happened on this sample
};

class Voice
{
public:
    /** Peak of the amplifier output (gain times tanh, before the output scale) that the
        output scale maps to -12 dBFS: a plain C2 saw at default settings (ladder load 20 k,
        loop gain 18.7, resonance 75, cutoff 35, Env Mod 45). */
    static constexpr double kPlainNotePeak = 0.209;
    static constexpr double kOutputScale = 0.2512 / kPlainNotePeak;

    /** The output amplifier's rails, in output units (1 = 0 dBFS): linear up to the knee,
        then a tanh that approaches the ceiling. */
    static constexpr double kOutputClipKnee = 1.0;
    static constexpr double kOutputClipCeiling = 2.0;

    /** The output amplifier's transfer curve, in output units. Odd, monotonic, continuous
        in value and slope, exactly x for |x| <= kOutputClipKnee, at most kOutputClipCeiling
        in magnitude. NaN passes through. */
    static double outputStage (double x) noexcept;

    /** After the decimator: exactly x up to kOutputClipCeiling, then a tanh to
        kOutputGuardCeiling. Catches the decimator's overshoot on a signal pressed against
        the rails, which is the only way a sample gets past the ceiling. */
    static constexpr double kOutputGuardCeiling = 2.2;
    static double outputGuard (double x) noexcept;

    /** Measured worst-case output peaks (see the headroom note at the top of this file),
        rounded up: a fully accented note with stock controls (measured 2.10, +6.5 dBFS: the
        amplifier alone would reach 2.97, +9.5 dBFS), and the same with the Mods Overdrive
        and Filter FM at 100 % (bounded by kOutputGuardCeiling, +6.8 dBFS; the amplifier alone
        would reach 6.31, +16.0 dBFS). The headroom test in tests/dsp holds the grid under
        these. */
    static constexpr double kWorstCaseStockPeak = 2.15;
    static constexpr double kWorstCaseModsPeak = 2.21;

    /** Newton step size at which the filter's solver stops. The error left is of the order
        of its square: with 1e-4 the output is within -170 dB of the fully converged
        solution at stock settings, and most samples need one iteration instead of two. */
    static constexpr double kFilterTolerance = 1.0e-4;

    /** Grid on which time constants and trims are re-derived while a knob moves, and on
        which the ladder trims are smoothed (32 base samples at 48 kHz). */
    static constexpr double kControlSeconds = 1.0 / 1500.0;
    static constexpr double kSmoothingSeconds = 0.003;
    /** A smoothed control stops (jumps to its target) within this fraction of 1 + |target|,
        which is 14 time constants, 41 ms, after a full-scale change. */
    static constexpr double kSmoothingStop = 1.0e-6;
    /** Corner of the oscillator band limit ahead of the filter, the lowest oversampled
        rate at which it runs (as a multiple of the corner), and its order in 2-pole
        sections: three, a 6th-order Butterworth. */
    static constexpr double kInputBandLimitHz = 24000.0;
    static constexpr double kInputBandLimitMinRateRatio = 10.0 / 3.0;
    static constexpr int kInputBandLimitSections = 3;
    static constexpr double kCpuInterruptSeconds = 0.0018;
    static constexpr double kCpuGateOffLagSeconds = 0.00135;

    Voice() noexcept;

    /** sampleRate: the base (host) rate. oversampling: 1, 2, 4 or 8 (anything else is
        rounded down to one of those). maxBlockSize is accepted for symmetry with the other
        modules; the voice needs no block buffers. Derives every coefficient and resets. */
    void prepare (double sampleRate, int maxBlockSize, int oversampling);

    /** Panic: every state to rest, the accent capacitor and the oscillator included.
        Parameter smoothers jump to their targets. */
    void reset() noexcept;

    /** Once per block. Values are smoothed inside (3 ms), switches act at once. */
    void setParams (const VoiceParams& newParams) noexcept;
    const VoiceParams& getParams() const noexcept { return params; }

    void noteOn (int midiNote, bool accent, bool legato, bool vibrato) noexcept;
    void noteOff() noexcept;
    void setPitchBend (double semitones) noexcept;
    void setVibratoWheel (double depth01) noexcept;

    /** Renders numSamples of mono audio at the base rate. */
    void process (float* out, int numSamples) noexcept;

    /** Group delay of the decimator at low frequency, rounded to base-rate samples
        (0 at 1x, 2 or 3 otherwise). Envelope and gate timing are late by this much. */
    int latencySamples() const noexcept { return decimator.latencySamples(); }

    VoiceMeters meters() const noexcept;

    /** Test and analysis access to the control path. Not for the engine. */
    const VoiceDebugTaps& debugTaps() const noexcept { return taps; }
    const DiodeLadder& debugFilter() const noexcept { return ladder; }
    const Oscillator& debugOscillator() const noexcept { return oscillator; }
    AccentSweep& debugAccentSweep() noexcept { return sweep; }

    double getSampleRate() const noexcept { return fs; }
    int getOversampling() const noexcept { return factor; }

private:
    enum Smoothed : int
    {
        sTune, sCutoff, sResonance, sEnvMod, sDecay, sAccent, sSlide, sSweepAmt, sEnvAttack,
        sAccDecay, sAccVol, sVibSpeed, sVibDepth,
        sTm3, sC21Log, sAccEnv, sAccAmp, sC13Log, sTm5, sPw, sNoise, sCvScale, sGateDelay, sDrift,
        sOverdrive, sFm, sTracking, sAmpDecayLog, sSustain, sSoftAttackLog, sSubLevel,
        sWheel, sBend,
        kNumSmoothed
    };

    struct PendingNote
    {
        int countdown = -1;     // samples until it applies; -1: none
        int note = 0;
        bool accent = false, legato = false, vibrato = false;
    };

    /** Coefficient groups: which derived values each smoothed control feeds. Fast groups
        are re-derived on the sample their control moves, slow ones on the control grid. */
    enum Group : std::uint32_t
    {
        gSwitches = 1u << 0,        // muffler mode, waveform target
        gPitch = 1u << 1,           // tuning, octave width, drift depth
        gTm3 = 1u << 2,
        gCutoff = 1u << 3,          // knob to nominal Hz, depth rise
        gEnvMod = 1u << 4,
        gDepth = 1u << 5,           // octaves per volt of envelope and accent
        gResonance = 1u << 6,
        gAccent = 1u << 7,          // accent pot gains
        gLevels = 1u << 8,          // tracking, FM, sub, noise, sustain
        gOverdrive = 1u << 9,
        gSweepNetwork = 1u << 10,   // accent sweep network, resistive part
        kFastGroups = (1u << 11) - 1u,

        gSlide = 1u << 11,
        gVibrato = 1u << 12,
        gEnvTimes = 1u << 13,
        gSweepRates = 1u << 14,     // accent sweep capacitor rates, C13
        gAmp = 1u << 15,
        gGate = 1u << 16,
        gCoupler = 1u << 17,
        gOscillator = 1u << 18,
        kAllGroups = (1u << 19) - 1u,
        kSlowGroups = kAllGroups & ~kFastGroups
    };

    /** One 2-pole section of the input band limit, transposed direct form II. */
    struct BandLimitSection
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    static std::uint32_t groupsOf (int smoothedIndex) noexcept;
    void loadTargets() noexcept;
    void flagMoving() noexcept;
    void updateDerived (std::uint32_t groups) noexcept;
    void updateSlowTrims (bool snap) noexcept;
    void applyNoteOn (int midiNote, bool accent, bool legato, bool vibrato) noexcept;
    void applyNoteOff() noexcept;
    void controlTick() noexcept;

    // ---- configuration ----
    double fs = 48000.0;
    int factor = 4;
    double fsOver = 192000.0;
    bool prepared = false;

    VoiceParams params;
    std::array<double, kNumSmoothed> target {};
    std::array<double, kNumSmoothed> value {};
    double smoothCoeff = 1.0;
    std::uint64_t movingMask = 0;           // smoothed values that have not reached their targets
    std::uint32_t pendingGroups = kAllGroups;   // coefficient groups waiting to be re-derived
    bool snapSmoothers = true;      // the next tick jumps to the targets (first block after reset)
    bool switchesChanged = false;   // a switch moved: re-derive everything on the next tick
    double rbTarget = 20.0e3, kMaxTarget = 18.7;
    double rbValue = 20.0e3, kMaxValue = 18.7;
    double slowCoeff = 1.0;
    int controlInterval = 32;       // base samples per control-grid step
    int controlCounter = 0;

    // ---- modules ----
    Oscillator oscillator;
    DiodeLadder ladder;
    Decimator decimator;
    MainEnvelope envelope;
    AccentSweep sweep;
    GateShaper gateShaper;
    const ladder::TanhTable* tanhTable = nullptr;

    // ---- derived from the smoothed knobs (updateDerived) ----
    double tuneSemis = 0.0, octaveWidth = 1.0;
    double knobHz = 500.0, tm3Scale = 1.0, nominalHz = 500.0;
    double envLaw = 0.0, depthRise = 1.0;
    double envOctPerVolt = 0.0, accOctPerVolt = 0.0;
    double accentWiperGain = 0.0, accentVolGain = 0.0, accentAmpPerVolt = 0.0;
    double slideCoeff = 1.0;
    double vibHalfSamples = 2400.0, vibSpreadVolts = 0.0;
    double resonanceR = 0.0, wiperFraction = 0.0;
    double trackHzPerVolt = 0.0;
    double vegCoeff = 0.0, vegTarget = laws::kVegFloor;
    double driftCents = 0.0;
    double overdrive = 1.0, fmHzPerUnit = 0.0, subLevel = 0.0, noiseAmp = 0.0;
    double couplerDirectG = 0.0, couplerWiperG = 0.0;
    double c13Farads = 1.0e-6;
    AccentSweep::Speed sweepSpeed = AccentSweep::Speed::Normal;
    int mufflerMode = 0;
    double waveTarget = 0.0;

    // ---- rate-derived constants (prepare) ----
    double accLagCoeff = 1.0;
    double inputHpCoeff = 0.0;
    double fmHpCoeff = 0.0;
    double mufflerLpCoeff = 0.0;
    double waveStep = 1.0;
    double outHpG[3] {};
    double driftCoeff = 0.0, driftNorm = 0.0;
    double noiseRateScale = 1.0;
    double maxCutoffHz = 20000.0;
    double cpuPeriodSamples = 86.4, cpuOffLagSamples = 64.8;

    // ---- control state ----
    bool cpuGate = false;
    bool accentLatched = false;
    bool slideOn = false;
    bool noteVibrato = false;
    bool triggeredThisTick = false;
    double targetVolts = laws::kTuneReferenceVolts;
    double cvVolts = laws::kTuneReferenceVolts;
    double vibTimer = 0.0;
    double vibSign = 1.0;
    double ampEnv = 0.0;
    double accentCurrent = 0.0;
    double drift1 = 0.0, drift2 = 0.0;
    double cpuPhase = 0.0;
    PendingNote pendingOn;
    int pendingOff = -1;
    Rng driftRng, noiseRng;

    // ---- per-sample values handed to the oversampled loop ----
    double hzNow = 110.0, hzPrev = 110.0;
    double cutoffNow = 500.0, cutoffPrev = 500.0;
    double gainNow = 0.0, gainPrev = 0.0;
    double resNow = 0.0, resPrev = 0.0;
    double wiperNow = 0.0, wiperPrev = 0.0;
    double waveMix = 0.0, waveMixPrev = 0.0;

    // ---- audio state ----
    bool bandLimitOn = false;
    BandLimitSection bandLimit[kInputBandLimitSections];
    double sawHp = 0.0, squareHp = 0.0;         // input coupling capacitors, volts
    double directZ = 0.0, wiperZ = 0.0;         // amplifier input couplers
    double mufflerZ = 0.0;
    double fmZ = 0.0, fmSignal = 0.0;
    double outZ[3] {};
    std::array<double, 8> overBuffer {};

    VoiceDebugTaps taps;
};
}
