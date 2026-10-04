#include "acid/fx/Distortion.h"
#include <algorithm>
#include <cmath>

namespace acid::fx
{
namespace
{
constexpr double kPreampDbPerPercent = 0.6;   // 0..100 % is 0..60 dB
constexpr double kColorMaxDb = 18.0;          // largest boost (or cut) of the color filter, at u = +-1
constexpr double kColorResoShare = 0.6;       // share of that budget the resonant peak takes at reso 100 %
constexpr double kColorSweepOctaves = 1.0;    // the corner moves down this far at u = +-1
constexpr double kSmoothSeconds = 0.015;
constexpr double kBiasSmoothSeconds = 0.2;   // slower than the DC blocker, so a bias move is not a thump
constexpr double kMixSeconds = 0.010;         // on/off crossfade
constexpr double kDuckSeconds = 0.003;        // fade each way round a structural change
constexpr double kDcBlockHz = 6.0;
constexpr double kButterworthQ = 0.70710678118654752;
constexpr double kMinFilterHz = 1.0;          // a high-pass or split below this is off
constexpr double kTinyState = 1.0e-30;
constexpr double kMaxInput = 1.0e6;           // largest input magnitude the wet path accepts

// Make-up gain of each clipper family, in dB, at preamp gains of 0, 6, 12 .. 60 dB,
// interpolated linearly in between. It holds the RMS level of a bass line at the nominal
// level where the clean signal had it, whatever the family and the drive: the clippers
// compress, so a driven signal is far denser (louder) than the clean one, by an amount
// that depends on the family (a fuzz gates, a folder folds, a transformer sags). With it
// the drive knob changes the character, not the loudness, and voicings level-match at any
// drive, not only where their trims were set. Measured (tests/dist, "voicings: level
// calibration", "[.calibrate]") on three bass lines at -6, 0 and +3 dB against the nominal
// level, taking the middle of the spread between them; below 6 dB the gated fuzz passes
// too little to make up (it would need 20 dB), so its first point is capped.
constexpr int kLevelPoints = 11;
constexpr double kLevelStepDb = 6.0;
constexpr double kFamilyLevelDb[kNumClipperFamilies][kLevelPoints] = {
    {  0.0, -0.2, -2.7, -5.7, -8.2, -10.0, -11.4, -12.5, -13.5, -14.3, -15.0 },   // diode pair
    { -0.3,  1.6, -0.2, -3.2, -5.3,  -6.9,  -8.1,  -9.1, -10.1, -10.8, -11.4 },   // op-amp hard
    { -0.1, -0.6, -3.2, -6.3, -8.7, -10.2, -11.1, -11.6, -12.0, -12.3, -12.5 },   // asymmetric soft
    {  2.5,  2.6,  0.1, -2.6, -4.9,  -6.7,  -8.0,  -8.8,  -9.3,  -9.6,  -9.9 },   // scoop hard
    { -0.6, -0.4, -2.1, -4.1, -5.1,  -5.7,  -6.0,  -6.2,  -6.4,  -6.4,  -6.5 },   // mid hump
    {  6.7,  6.7, -1.2, -5.7, -8.9, -11.3, -12.1, -12.5, -12.7, -12.9, -13.0 },   // gated fuzz
    { -0.5, -1.2, -4.0, -6.8, -8.5,  -9.5, -10.1, -10.5, -10.7, -10.9, -11.1 },   // console
    {  0.0, -0.1, -2.1, -4.3, -6.0,  -7.1,  -8.4, -10.0, -11.9, -13.6, -14.6 },   // fold
};

/** Level change, in dB, that the pre-clipper shaping (pre high-pass and pre EQ) makes on
    a typical bass line: the mean power gain at twelve log-spaced points from 55 Hz to
    3.5 kHz (a resonant line has its energy spread over that range). Analog responses;
    only the level matters here. */
double preShapingDb (const DistortionParams& p) noexcept
{
    constexpr int kPoints = 12;
    const bool hp = static_cast<double> (p.preHp) >= kMinFilterHz;
    const bool eq = p.preEqOn && p.preEqGain != 0.0f;
    if (! hp && ! eq) return 0.0;

    const double a = std::pow (10.0, static_cast<double> (p.preEqGain) / 40.0);
    const double q = detail::qFromOctaves (static_cast<double> (p.preEqWidth));
    double sum = 0.0, weights = 0.0;
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = 55.0 * std::pow (3500.0 / 55.0, i / (kPoints - 1.0));
        double m2 = 1.0;
        if (hp)
        {
            const double r = std::pow (f / static_cast<double> (p.preHp), 4.0);
            m2 *= r / (1.0 + r);
        }
        if (eq)
        {
            const double w = f / static_cast<double> (p.preEqFreq);
            const double re = (1.0 - w * w) * (1.0 - w * w);
            m2 *= (re + (a * w / q) * (a * w / q)) / (re + (w / (q * a)) * (w / (q * a)));
        }
        sum += m2;
        weights += 1.0;
    }
    return 10.0 * std::log10 (std::max (sum / weights, 1.0e-12));
}

float sanitise (float value, ParamId id) noexcept
{
    const ParamInfo& info = paramInfo (id);
    return std::isfinite (value) ? clamp (value, info.min, info.max) : info.def;
}

/** Color resonance in percent to the Q of the resonant peak. */
double colorQ (double percent) noexcept
{
    return 0.5 * std::pow (16.0, clamp (percent, 0.0, 100.0) * 0.01);
}

/** A corner frequency in octaves (log2 Hz), for gliding. Off (below 1 Hz) is 0. */
double logHz (double hz) noexcept
{
    return std::log2 (std::max (1.0, hz));
}

inline void flush (double& v) noexcept
{
    if (std::abs (v) < kTinyState) v = 0.0;
}

inline void flush (detail::SvfState& s) noexcept
{
    flush (s.ic1);
    flush (s.ic2);
}
}

//==============================================================================
DistortionParams distortionParamsFrom (const ParamValues& v) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };

    DistortionParams p;
    p.on = paramAsBool (v, kDistOn);
    p.model = paramAsInt (v, kDistModel);
    p.dynamics = f (kDistDynamics);
    p.dynPost = paramAsBool (v, kDistDynPost);
    p.drive = f (kDistDrive);
    p.color = f (kDistColor);
    p.clipper = paramAsInt (v, kDistClipper);
    p.preMin = f (kDistPreMin);
    p.preMax = f (kDistPreMax);
    p.knee = f (kDistKnee);
    p.bias = f (kDistBias);
    p.lowKeep = f (kDistLowKeep);
    p.preHp = f (kDistPreHp);
    p.preEqOn = paramAsBool (v, kDistPreEqOn);
    p.preEqGain = f (kDistPreEqGain);
    p.preEqFreq = f (kDistPreEqFreq);
    p.preEqWidth = f (kDistPreEqWidth);
    p.tiltOn = paramAsBool (v, kDistTiltOn);
    p.tiltAmt = f (kDistTiltAmt);
    p.tiltFreq = f (kDistTiltFreq);
    p.colorFreq = f (kDistColorFreq);
    p.colorReso = f (kDistColorReso);
    p.colorRange = f (kDistColorRange);
    p.postHp = f (kDistPostHp);
    p.volume = f (kDistVolume);
    return p;
}

double distortionPreampDb (double preampPercent) noexcept
{
    return kPreampDbPerPercent * clamp (preampPercent, 0.0, 100.0);
}

double distortionFamilyLevelDb (int clipperFamily, double preampDb) noexcept
{
    const auto& row = kFamilyLevelDb[clamp (clipperFamily, 0, kNumClipperFamilies - 1)];
    const double x = clamp (preampDb, 0.0, kLevelStepDb * (kLevelPoints - 1)) / kLevelStepDb;
    const int i = std::min (static_cast<int> (x), kLevelPoints - 2);
    const auto at = [&row] (int k) { return row[static_cast<std::size_t> (k)]; };
    return at (i) + (x - i) * (at (i + 1) - at (i));
}

//==============================================================================
void Distortion::Channel::resetWet() noexcept
{
    low.fill (0.0);
    lowPos = 0;
    splitA = splitB = preHp = preEq = color = postHp = {};
    tilt = colorShelf = dcBlock = {};
    clip.reset();
    os.reset();
}

void Distortion::Channel::resetAll() noexcept
{
    dry.fill (0.0f);
    dryPos = 0;
    resetWet();
}

//==============================================================================
void Distortion::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = std::isfinite (newSampleRate) ? clamp (newSampleRate, 8000.0, 768000.0) : 48000.0;

    smoothCoef = 1.0 - std::exp (-static_cast<double> (kControlInterval) / (kSmoothSeconds * sampleRate));
    biasSmoothCoef = 1.0 - std::exp (-static_cast<double> (kControlInterval) / (kBiasSmoothSeconds * sampleRate));
    mixStep = 1.0 / (kMixSeconds * sampleRate);
    duckStep = 1.0 / (kDuckSeconds * sampleRate);
    dcCoefs.set (kDcBlockHz, sampleRate);

    for (auto& c : channels)
        c.os.prepare (sampleRate);
    dynamics.prepare (sampleRate);

    reset();
}

void Distortion::reset() noexcept
{
    activeFactor = requestedFactor;
    activeFamily = target.clipper;
    for (auto& c : channels)
    {
        c.os.setFactor (activeFactor);
        c.resetAll();
    }
    latency = channels[0].os.latencySamples();

    wetActive = target.on;
    mix = target.on ? 1.0 : 0.0;
    duck = 1.0;
    duckDir = 0;
    duckHold = 0;
    rightFollowsLeft = true;
    samplesToTick = 0;
    snap = true;
    clipFresh = true;

    dynamics.setAmount (static_cast<double> (target.dynamics));
    dynamics.reset();

    splitOn = preHpOn = postHpOn = false;   // every filter starts from rest at its target corner
    applySwitches();
    tiltDbApplied = tiltLogHzApplied = preEqDbApplied = preEqLogApplied = preEqWidthApplied = -1.0e9;
}

void Distortion::setOversampling (int factor) noexcept
{
    requestedFactor = factor >= 8 ? 8 : (factor >= 4 ? 4 : (factor >= 2 ? 2 : 1));
}

int Distortion::latencySamples() const noexcept
{
    return channels[0].os.latencyForFactor (requestedFactor);
}

void Distortion::setParams (const DistortionParams& p) noexcept
{
    target.on = p.on;
    target.model = p.model;
    target.dynamics = sanitise (p.dynamics, kDistDynamics);
    target.dynPost = p.dynPost;
    target.drive = sanitise (p.drive, kDistDrive);
    target.color = sanitise (p.color, kDistColor);
    target.clipper = clamp (p.clipper, 0, kNumClipperFamilies - 1);
    target.preMin = sanitise (p.preMin, kDistPreMin);
    target.preMax = sanitise (p.preMax, kDistPreMax);
    target.knee = sanitise (p.knee, kDistKnee);
    target.bias = sanitise (p.bias, kDistBias);
    target.lowKeep = sanitise (p.lowKeep, kDistLowKeep);
    target.preHp = sanitise (p.preHp, kDistPreHp);
    target.preEqOn = p.preEqOn;
    target.preEqGain = sanitise (p.preEqGain, kDistPreEqGain);
    target.preEqFreq = sanitise (p.preEqFreq, kDistPreEqFreq);
    target.preEqWidth = sanitise (p.preEqWidth, kDistPreEqWidth);
    target.tiltOn = p.tiltOn;
    target.tiltAmt = sanitise (p.tiltAmt, kDistTiltAmt);
    target.tiltFreq = sanitise (p.tiltFreq, kDistTiltFreq);
    target.colorFreq = sanitise (p.colorFreq, kDistColorFreq);
    target.colorReso = sanitise (p.colorReso, kDistColorReso);
    target.colorRange = sanitise (p.colorRange, kDistColorRange);
    target.postHp = sanitise (p.postHp, kDistPostHp);
    target.volume = sanitise (p.volume, kDistVolume);

    dynamics.setAmount (static_cast<double> (target.dynamics));
}

//==============================================================================
bool Distortion::switchesPending() const noexcept
{
    return (static_cast<double> (target.lowKeep) >= kMinFilterHz) != splitOn
        || (static_cast<double> (target.preHp) >= kMinFilterHz) != preHpOn
        || (static_cast<double> (target.postHp) >= kMinFilterHz) != postHpOn
        || target.dynPost != activeDynPost;
}

void Distortion::applySwitches() noexcept
{
    // Filters that come in start from rest; their coefficients follow the target frequency
    // in updateCoefficients() whether they are in or out.
    const bool split = static_cast<double> (target.lowKeep) >= kMinFilterHz;
    const bool pre = static_cast<double> (target.preHp) >= kMinFilterHz;
    const bool post = static_cast<double> (target.postHp) >= kMinFilterHz;
    // A filter that comes in starts from rest at its target corner (it was not heard gliding).
    for (auto& c : channels)
    {
        if (split && ! splitOn) { c.splitA = c.splitB = {}; c.low.fill (0.0); }
        if (pre && ! preHpOn) c.preHp = {};
        if (post && ! postHpOn) c.postHp = {};
    }
    if (split && ! splitOn) splitLog = logHz (std::max (20.0, static_cast<double> (target.lowKeep)));
    if (pre && ! preHpOn) preHpLog = logHz (static_cast<double> (target.preHp));
    if (post && ! postHpOn) postHpLog = logHz (static_cast<double> (target.postHp));
    if (split) splitCoefs.set (std::exp2 (splitLog), kButterworthQ, sampleRate);
    if (pre) preHpCoefs.set (std::exp2 (preHpLog), kButterworthQ, sampleRate);
    if (post) postHpCoefs.set (std::exp2 (postHpLog), kButterworthQ, sampleRate);
    splitLogApplied = splitLog;
    preHpLogApplied = preHpLog;
    postHpLogApplied = postHpLog;
    splitOn = split;
    preHpOn = pre;
    postHpOn = post;
    preHpHz = pre ? std::exp2 (preHpLog) : 0.0;
    activeDynPost = target.dynPost;
}

void Distortion::applyStructure() noexcept
{
    const bool factorChanged = activeFactor != requestedFactor;
    const bool familyChanged = activeFamily != target.clipper;
    activeFactor = requestedFactor;
    activeFamily = target.clipper;

    if (factorChanged || familyChanged)
    {
        for (auto& c : channels)
        {
            if (factorChanged)
            {
                c.os.setFactor (activeFactor);
                c.dry.fill (0.0f);   // the delay length changes: do not replay stale samples
            }
            c.resetWet();
        }
        latency = channels[0].os.latencySamples();
        clipDriveDb = -1.0;   // forces a clipper coefficient refresh, which settles the new state
        clipFresh = true;
        splitOn = preHpOn = postHpOn = false;   // the wet state was reset: every filter starts from rest
    }
    applySwitches();

    // Nothing is heard while the structure changes (it happens at the bottom of the duck, or
    // with the block silent): every smoothed control takes its target now rather than
    // gliding from the old voicing's values after the fade-in (a fold family coming in at the
    // previous voicing's preamp gain is a burst).
    snap = true;
}

void Distortion::controlTick() noexcept
{
    // On and off. The wet path starts from rest when it is switched on and stops running
    // once the crossfade back to dry has finished.
    if (target.on && ! wetActive)
    {
        for (auto& c : channels)
            c.resetWet();
        clipFresh = true;
        dynamics.reset();
        wetActive = true;
        snap = true;
    }
    else if (! target.on && wetActive && mix <= 0.0)
    {
        wetActive = false;
    }

    // A new oversampling factor or clipper family resets state, and a filter switching in or
    // out (or the dynamics stage moving) changes the signal in one sample, so they happen at
    // zero gain. On the first tick after a reset nothing has been heard yet: at once.
    if (activeFactor != requestedFactor || activeFamily != target.clipper || switchesPending())
    {
        const bool audible = wetActive || channels[0].os.latencyForFactor (requestedFactor) != latency;
        if (! audible || snap || duck <= 0.0)
        {
            const bool firstTick = snap;
            applyStructure();
            duckDir = duck < 1.0 ? 1 : 0;
            // What changed sits ahead of the oversampling filters, whose transient (their
            // full length, twice the latency) reaches the output only after the delay: stay
            // silent until it has passed, then fade back in.
            if (duck <= 0.0 && audible && ! firstTick)
                duckHold = 2 * latency;
        }
        else
        {
            duckDir = -1;
        }
    }
    else if (duck < 1.0)
    {
        duckDir = 1;
    }
    else
    {
        duckDir = 0;
    }

    if (! wetActive)
        return;

    // Smoothed controls.
    const double drivePercent = lerp (static_cast<double> (target.preMin), static_cast<double> (target.preMax),
                                      static_cast<double> (target.drive) * 0.01);
    const double colorTarget = static_cast<double> (target.color) * 0.01 * static_cast<double> (target.colorRange) * 0.01;
    const auto step = [this] (double& value, double goal, double epsilon, double coef)
    {
        if (snap || std::abs (goal - value) <= epsilon) value = goal;
        else value += coef * (goal - value);
    };
    step (driveDb, distortionPreampDb (drivePercent), 1.0e-4, smoothCoef);
    step (knee, static_cast<double> (target.knee) * 0.01, 1.0e-5, smoothCoef);
    step (bias, static_cast<double> (target.bias) * 0.01, 1.0e-5, biasSmoothCoef);
    step (colorAmt, colorTarget, 1.0e-5, smoothCoef);
    step (trimDb, static_cast<double> (target.volume), 1.0e-4, smoothCoef);
    step (tiltDb, target.tiltOn ? static_cast<double> (target.tiltAmt) : 0.0, 1.0e-4, smoothCoef);
    step (tiltLogHz, std::log2 (std::max (1.0, static_cast<double> (target.tiltFreq))), 1.0e-5, smoothCoef);
    step (preEqDb, target.preEqOn ? static_cast<double> (target.preEqGain) : 0.0, 1.0e-4, smoothCoef);
    // Corner frequencies glide in octaves: a corner that jumps under a signal steps its level
    // (a pre EQ bell moving from 900 to 300 Hz under a bass note is a click).
    step (splitLog, logHz (std::max (20.0, static_cast<double> (target.lowKeep))), 1.0e-5, smoothCoef);
    step (preHpLog, logHz (static_cast<double> (target.preHp)), 1.0e-5, smoothCoef);
    step (postHpLog, logHz (static_cast<double> (target.postHp)), 1.0e-5, smoothCoef);
    step (preEqLog, logHz (static_cast<double> (target.preEqFreq)), 1.0e-5, smoothCoef);
    step (preEqWidth, static_cast<double> (target.preEqWidth), 1.0e-5, smoothCoef);

    updateCoefficients (snap);
    snap = false;
    flushDenormals();
}

void Distortion::updateCoefficients (bool force) noexcept
{
    const double fs = sampleRate;

    // The low-keep split and the high-passes switch in and out only in applySwitches() (at
    // the bottom of a duck); while they are in, their corners follow the target here.
    if ((force || splitLog != splitLogApplied) && splitOn)
    {
        splitCoefs.set (std::exp2 (splitLog), kButterworthQ, fs);
        splitLogApplied = splitLog;
    }

    if ((force || preHpLog != preHpLogApplied) && preHpOn)
    {
        preHpHz = std::exp2 (preHpLog);
        preHpCoefs.set (preHpHz, kButterworthQ, fs);
        preHpLogApplied = preHpLog;
    }

    if (force || preEqDb != preEqDbApplied || preEqLog != preEqLogApplied || preEqWidth != preEqWidthApplied)
    {
        const bool on = preEqDb != 0.0;
        if (on)
        {
            // Bell from the band output: k = 1 / (Q A), out = x + k (A^2 - 1) band.
            const double a = std::pow (10.0, preEqDb / 40.0);
            const double q = detail::qFromOctaves (preEqWidth);
            preEqCoefs.set (std::exp2 (preEqLog), q * a, fs);
            preEqMix = preEqCoefs.k * (a * a - 1.0);
        }
        if (on && ! preEqOn)
            for (auto& c : channels)
                c.preEq = {};
        preEqOn = on;
        preEqDbApplied = preEqDb;
        preEqLogApplied = preEqLog;
        preEqWidthApplied = preEqWidth;
    }

    if (force || tiltDb != tiltDbApplied || tiltLogHz != tiltLogHzApplied)
    {
        // First-order tilt: 1/g at DC, g at the top, unity at the pivot.
        const double g = std::pow (10.0, tiltDb / 40.0);
        tiltCoefs.set (g * std::exp2 (tiltLogHz), fs);
        tiltDirect = g;
        tiltMix = 1.0 / g - g;
        tiltDbApplied = tiltDb;
        tiltLogHzApplied = tiltLogHz;
    }

    if (force || colorAmt != colorAmtApplied || target.colorFreq != applied.colorFreq || target.colorReso != applied.colorReso)
    {
        // See the header: a mirrored first-order shelf times a peaking band, both at the
        // swept corner, sharing the kColorMaxDb budget.
        const bool on = colorAmt != 0.0;
        if (on)
        {
            const double u = clamp (colorAmt, -1.0, 1.0);
            const double reso = clamp (static_cast<double> (target.colorReso), 0.0, 100.0) * 0.01;
            const double corner = static_cast<double> (target.colorFreq) * std::exp2 (-kColorSweepOctaves * std::abs (u));
            const double totalDb = u * kColorMaxDb;
            const double bellDb = totalDb * kColorResoShare * reso;
            const double shelfDb = totalDb - bellDb;

            // Shelf: y = x + (G - 1) * highpass(x), pole at corner * sqrt (G), zero at
            // corner / sqrt (G), so the shelf for 1/G is the exact inverse.
            const double g = dbToGain (shelfDb);
            colorShelfCoefs.set (corner * std::sqrt (g), fs);
            colorShelfMix = g - 1.0;

            // Peak: x + k (A^2 - 1) band with k = 1 / (Q A); A and 1/A are inverses.
            const double a = std::pow (10.0, bellDb / 40.0);
            colorCoefs.set (corner, colorQ (reso * 100.0) * a, fs);
            colorBellMix = colorCoefs.k * (a * a - 1.0);
        }
        if (on != colorOn)
            for (auto& c : channels)
            {
                c.colorShelf = {};
                c.color = {};
            }
        colorOn = on;
        colorAmtApplied = colorAmt;
    }

    if ((force || postHpLog != postHpLogApplied) && postHpOn)
    {
        postHpCoefs.set (std::exp2 (postHpLog), kButterworthQ, fs);
        postHpLogApplied = postHpLog;
    }

    // Level matching: the family's make-up for this preamp gain, and the pre-clipper
    // shaping taken back out in proportion to how linear the clipper still is (fully at
    // unity drive, not at all once it is driven hard and its output level no longer
    // follows its input).
    // The shaping that is actually running: the high-pass while it is in, the pre EQ at its
    // glided gain. The make-up glides with the controls (a corner jump would step it).
    {
        DistortionParams shaping = target;
        shaping.preHp = static_cast<float> (preHpOn ? preHpHz : 0.0);
        shaping.preEqOn = preEqOn;
        shaping.preEqGain = static_cast<float> (preEqDb);
        shaping.preEqFreq = static_cast<float> (std::exp2 (preEqLog));
        shaping.preEqWidth = static_cast<float> (preEqWidth);
        if (force || shaping.preHp != shapingApplied.preHp || shaping.preEqOn != shapingApplied.preEqOn
            || shaping.preEqGain != shapingApplied.preEqGain || shaping.preEqFreq != shapingApplied.preEqFreq
            || shaping.preEqWidth != shapingApplied.preEqWidth)
        {
            preShapeTarget = preShapingDb (shaping);
            shapingApplied = shaping;
        }
        if (force || std::abs (preShapeTarget - preShapeDb) <= 1.0e-4) preShapeDb = preShapeTarget;
        else preShapeDb += smoothCoef * (preShapeTarget - preShapeDb);
    }
    const double linearity = clamp (2.0 * detail::driveCompensation (dbToGain (driveDb)) - 1.0, 0.0, 1.0);
    trimGain = dbToGain (trimDb + distortionFamilyLevelDb (activeFamily, driveDb) - linearity * preShapeDb);

    if (force || driveDb != clipDriveDb || knee != clipKnee || bias != clipBias)
    {
        detail::ClipperControls ctl;
        ctl.family = static_cast<ClipperFamily> (activeFamily);
        ctl.gain = dbToGain (driveDb);
        ctl.knee = knee;
        ctl.bias = bias;
        clipCoefs.update (ctl, fs * static_cast<double> (activeFactor));
        const bool curveChanged = knee != clipKnee;
        for (auto& c : channels)
        {
            if (clipFresh) c.clip.settle (clipCoefs);
            if (curveChanged) c.clip.coefficientsChanged();
        }
        clipFresh = false;
        clipDriveDb = driveDb;
        clipKnee = knee;
        clipBias = bias;
    }

    applied = target;
}

void Distortion::flushDenormals() noexcept
{
    for (auto& c : channels)
    {
        flush (c.splitA);  flush (c.splitB);  flush (c.preHp);
        flush (c.preEq);   flush (c.color);   flush (c.postHp);
        flush (c.tilt.s);  flush (c.colorShelf.s);  flush (c.dcBlock.s);

        auto& k = c.clip;
        flush (k.opAmpHard.out);  flush (k.opAmpHard.s1);  flush (k.opAmpHard.s2);
        flush (k.asymSoft.vsb);
        flush (k.scoopHard.clip);  flush (k.scoopHard.shelf.s);
        flush (k.scoopHard.toneLow.s);  flush (k.scoopHard.toneHigh.s);
        flush (k.midHump.fb);  flush (k.midHump.leg.s);
        flush (k.gatedFuzz.q);
        flush (k.console.flux);
    }
}

//==============================================================================
void Distortion::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || numSamples <= 0) return;
    if (right == left) right = nullptr;

    int pos = 0;
    while (pos < numSamples)
    {
        if (samplesToTick <= 0)
        {
            controlTick();
            samplesToTick = kControlInterval;
        }
        const int n = std::min (numSamples - pos, samplesToTick);
        processSpan (left + pos, right != nullptr ? right + pos : nullptr, n);
        pos += n;
        samplesToTick -= n;
    }
}

void Distortion::processSpan (float* left, float* right, int n) noexcept
{
    // Off, settled, nothing to delay: leave the buffers alone.
    if (! wetActive && latency == 0 && duck >= 1.0 && duckDir == 0)
    {
        if (right == nullptr) rightFollowsLeft = true;
        return;
    }

    // While both channels carry the same samples one computation serves both. The first
    // time they differ, the right channel takes a copy of the left channel's state, which
    // is exactly the state it would have reached on its own.
    int numCh = 1;
    bool copyToRight = false;
    if (right == nullptr)
    {
        rightFollowsLeft = true;
    }
    else if (rightFollowsLeft && std::equal (left, left + n, right))
    {
        copyToRight = true;
    }
    else
    {
        if (rightFollowsLeft)
        {
            channels[1] = channels[0];
            rightFollowsLeft = false;
        }
        numCh = 2;
    }

    float* io[2] = { left, right };
    constexpr int mask = kDelaySize - 1;

    // Gain ramps for this span.
    double mixRamp[kControlInterval], duckRamp[kControlInterval];
    const double mixDelta = (wetActive && target.on) ? mixStep : -mixStep;
    for (int i = 0; i < n; ++i)
    {
        mix = clamp (mix + mixDelta, 0.0, 1.0);
        if (duckHold > 0 && duck <= 0.0) --duckHold;
        else duck = clamp (duck + static_cast<double> (duckDir) * duckStep, 0.0, 1.0);
        mixRamp[i] = mix;
        duckRamp[i] = duck;
    }

    // The dry signal, delayed to line up with the wet path.
    float dry[2][kControlInterval];
    for (int c = 0; c < numCh; ++c)
    {
        Channel& ch = channels[static_cast<std::size_t> (c)];
        for (int i = 0; i < n; ++i)
        {
            // A non-finite sample is stored as silence: the crossfade multiplies the dry
            // signal by zero, which would still give NaN.
            ch.dry[static_cast<std::size_t> (ch.dryPos)] = std::isfinite (io[c][i]) ? io[c][i] : 0.0f;
            dry[c][i] = ch.dry[static_cast<std::size_t> ((ch.dryPos - latency) & mask)];
            ch.dryPos = (ch.dryPos + 1) & mask;
        }
    }

    if (! wetActive)
    {
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
                io[c][i] = static_cast<float> (static_cast<double> (dry[c][i]) * duckRamp[i]);
    }
    else
    {
        double wet[2][kControlInterval];
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                // Non-finite input is silence; wild values are clamped (120 dB over full
                // scale) so the pre-clipper filters, with up to +24 dB of gain, stay finite.
                const double x = static_cast<double> (io[c][i]);
                wet[c][i] = std::isfinite (x) ? clamp (x, -kMaxInput, kMaxInput) : 0.0;
            }

        const bool dynOn = dynamics.isActive();
        const auto applyDynamics = [&]
        {
            for (int i = 0; i < n; ++i)
            {
                double peak = std::abs (wet[0][i]);
                if (numCh == 2) peak = std::max (peak, std::abs (wet[1][i]));
                const double g = dynamics.processGain (peak);
                for (int c = 0; c < numCh; ++c)
                    wet[c][i] *= g;
            }
        };

        if (dynOn && ! activeDynPost) applyDynamics();

        for (int c = 0; c < numCh; ++c)
            processWet (channels[static_cast<std::size_t> (c)], wet[c], n);

        if (dynOn && activeDynPost) applyDynamics();

        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const double w = std::isfinite (wet[c][i]) ? wet[c][i] : 0.0;
                const double m = mixRamp[i];
                io[c][i] = static_cast<float> ((static_cast<double> (dry[c][i]) * (1.0 - m) + w * m) * duckRamp[i]);
            }
    }

    if (copyToRight)
        std::copy (left, left + n, right);
}

void Distortion::processWet (Channel& ch, double* buffer, int n) noexcept
{
    constexpr int mask = kDelaySize - 1;
    double lowBand[kControlInterval];

    if (splitOn)
    {
        // Fourth-order Linkwitz-Riley split. The low band is two Butterworth sections; the
        // high band is the matching all-pass minus the low band, so the two always sum to
        // an all-pass: flat, with no notch at the crossover.
        for (int i = 0; i < n; ++i)
        {
            const auto first = detail::svfTick (splitCoefs, ch.splitA, buffer[i]);
            const double allPass = buffer[i] - 2.0 * splitCoefs.k * first.band;
            const double low = detail::svfTick (splitCoefs, ch.splitB, first.low).low;
            ch.low[static_cast<std::size_t> (ch.lowPos)] = low;
            lowBand[i] = ch.low[static_cast<std::size_t> ((ch.lowPos - latency) & mask)];
            ch.lowPos = (ch.lowPos + 1) & mask;
            buffer[i] = allPass - low;
        }
    }

    if (preHpOn)
        for (int i = 0; i < n; ++i)
            buffer[i] = detail::svfTick (preHpCoefs, ch.preHp, buffer[i]).high;

    if (preEqOn)
        for (int i = 0; i < n; ++i)
            buffer[i] += preEqMix * detail::svfTick (preEqCoefs, ch.preEq, buffer[i]).band;

    if (activeFactor == 1)
    {
        detail::processClipper (clipCoefs, ch.clip, buffer, n);
    }
    else
    {
        double high[kControlInterval * Oversampler::kMaxFactor];
        ch.os.upsample (buffer, high, n);
        detail::processClipper (clipCoefs, ch.clip, high, n * activeFactor);
        ch.os.downsample (high, buffer, n);
    }

    for (int i = 0; i < n; ++i)
    {
        double y = buffer[i];
        y = tiltDirect * y + tiltMix * detail::onePoleLp (tiltCoefs, ch.tilt, y);
        if (colorOn)
        {
            y += colorShelfMix * (y - detail::onePoleLp (colorShelfCoefs, ch.colorShelf, y));
            y += colorBellMix * detail::svfTick (colorCoefs, ch.color, y).band;
        }
        if (postHpOn) y = detail::svfTick (postHpCoefs, ch.postHp, y).high;
        y -= detail::onePoleLp (dcCoefs, ch.dcBlock, y);
        if (splitOn) y += lowBand[i];
        buffer[i] = y * trimGain;
    }
}
}
