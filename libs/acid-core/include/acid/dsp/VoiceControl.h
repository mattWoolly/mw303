#pragma once
// The voice's control-path circuits as small state machines: the main envelope, the accent
// sweep network and the analog gate (docs/research/03 sections 3, 5, 6 and 11;
// docs/research/04 section 3 for the discretisation).
//
// Every RC state advances by its exact exponential with the driving value held over the
// tick, so the curves are the same at any sample rate. prepare() takes the rate tick() is
// called at (the voice's base rate); the set...() and configure...() functions derive
// coefficients and may be called whenever a knob moves. Nothing here allocates, locks or
// throws.
//
// Voice (Voice.h) owns one of each. They are public so that tests and plots can drive
// them with the research documents' reference values directly.
#include "acid/common/Math.h"
#include "acid/dsp/VoiceLaws.h"

namespace acid::dsp
{
//==============================================================================
/** Main envelope generator: one capacitor (C62), charged fast by the trigger pulse and
    discharged through the Decay pot, or through the 68 k alone while the accent latch is
    set. Output in volts at the buffer, 0 to 10 V. There is no sustain and no release:
    gate-off does nothing, and a slid note does not retrigger. */
class MainEnvelope
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        setTimes (attackSeconds, plainSeconds, accentSeconds);
        reset();
    }

    void reset() noexcept
    {
        volts = 0.0;
        attacking = false;
    }

    /** Time constants in seconds: capacitor charge during the trigger pulse, decay on a
        plain note, decay while the accent latch is set. */
    void setTimes (double attackTau, double plainTau, double accentTau) noexcept
    {
        attackSeconds = attackTau;
        plainSeconds = plainTau;
        accentSeconds = accentTau;
        attackCoeff = onePoleCoeff (attackTau, fs);
        plainDecay = std::exp (-1.0 / (std::max (plainTau, 1.0e-5) * fs));
        accentDecay = std::exp (-1.0 / (std::max (accentTau, 1.0e-5) * fs));
    }

    /** A gate edge: recharge to the peak from wherever the capacitor is. */
    void trigger() noexcept { attacking = true; }

    /** One tick. accentLatched selects the discharge path. Returns volts. */
    double tick (bool accentLatched) noexcept
    {
        if (attacking)
        {
            // The pulse charges towards a level above the peak and is cut off there, so
            // the peak is reached in 1.6 time constants and is the same on every trigger.
            volts += (kChargeTarget * laws::kMegPeakVolts - volts) * attackCoeff;
            if (volts >= laws::kMegPeakVolts)
            {
                volts = laws::kMegPeakVolts;
                attacking = false;
            }
        }
        else
        {
            volts *= accentLatched ? accentDecay : plainDecay;
            if (volts < kRestVolts) volts = 0.0;
        }
        return volts;
    }

    double value() const noexcept { return volts; }
    bool isAttacking() const noexcept { return attacking; }

private:
    static constexpr double kChargeTarget = 1.25;
    static constexpr double kRestVolts = 1.0e-12;

    double fs = 48000.0;
    double attackSeconds = laws::kMegAttackSeconds;
    double plainSeconds = 0.25;
    double accentSeconds = laws::kMegBaseSeconds;
    double attackCoeff = 1.0, plainDecay = 0.0, accentDecay = 0.0;
    double volts = 0.0;
    bool attacking = false;
};

//==============================================================================
/** Accent sweep network: the accent pot's wiper through a diode (D24) and R46 into the
    anticlockwise end of the resonance pot's second gang, C13 on the clockwise end, the
    gang's wiper through R72 into the cutoff summing node. The diode follows the Shockley
    law (0.45 V at the 50 uA of a full accent, less as the current falls), which is what
    sets the Accent knob's threshold near 45 % and the depth just above it.

    All voltages are relative to the 3.3 V bus the network rests at. The tap (the gang's
    wiper position, 0..1) chooses what the filter sees: at 0 the diode-fed divider (a spike
    that follows the envelope), at 1 the capacitor alone (the lagged bump). The charge on
    C13 is the accent memory: it only drains through R72, so consecutive accents climb.
    Never reset it per note.

    Speed::Normal is the stock circuit. Slow is a larger capacitor behind a smaller series
    resistor (slower to rise, climbs further, takes half a second to cool). Fast puts a
    small capacitor in series instead, so the first accent of a run is the strongest. The
    values of those two are this model's choice; none are published. */
class AccentSweep
{
public:
    enum class Speed { Normal, Fast, Slow };

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        configure (position, tapPosition, c13, speed);
        reset();
    }

    /** Panic only: discharges the capacitors to their rest level. */
    void reset() noexcept
    {
        u = 0.0;
        q = 0.0;
        w = 0.0;
        diodeVolts = 0.0;
        conducting = false;
    }

    /** accentPosition: the Accent pot, 0..1 (its source resistance feeds the network).
        tap: the gang's wiper, 0..1. c13Farads: the capacitor (1 uF stock). */
    void configure (double accentPosition, double tap, double c13Farads, Speed newSpeed = Speed::Normal) noexcept
    {
        configureNetwork (accentPosition, tap, c13Farads, newSpeed);
        updateRates();
    }

    /** The resistive half of configure(): everything the wiper voltage depends on at once.
        A handful of divisions and no transcendental calls, so a moving Resonance or Accent
        knob can call it every tick. The capacitor's charge and discharge rates keep their
        last values until updateRates(). */
    void configureNetwork (double accentPosition, double tap, double c13Farads, Speed newSpeed = Speed::Normal) noexcept
    {
        position = clamp (accentPosition, 0.0, 1.0);
        tapPosition = clamp (tap, 0.0, 1.0);
        c13 = clamp (c13Farads, 1.0e-8, 1.0e-3);
        speed = newSpeed;

        const double r46 = speed == Speed::Slow ? kSlowR46 : laws::kR46;
        const double ra = tapPosition * laws::kResPotOhms;
        rb = (1.0 - tapPosition) * laws::kResPotOhms;
        rs = r46 + position * (1.0 - position) * laws::kAccentPotOhms + ra;
        gs = 1.0 / rs;
        const double g7 = 1.0 / laws::kR72;

        // Diode off: C13 drains through the rest of the track and R72.
        offWiper = laws::kR72 / (laws::kR72 + rb);

        // Diode on: the wiper node is fed through rs, loaded by R72, and charges C13
        // through the rest of the track. The diode current is
        // currentA * (drive - diode drop) - currentB * u.
        onWiperDen = 1.0 / (1.0 + rb * (gs + g7));
        onTarget = gs / (gs + g7);
        currentA = gs * (1.0 - gs * rb * onWiperDen);
        currentB = gs * onWiperDen;

        // Fast: series capacitor, charging through rs and R72.
        fastGain = laws::kR72 / (rs + laws::kR72);
    }

    /** The other half: the exponential rates of the capacitors for the network as last
        configured (four exp calls). Time constants only, so calling it a fraction of a
        millisecond after configureNetwork() is inaudible. */
    void updateRates() noexcept
    {
        const double cap = speed == Speed::Slow ? c13 * kSlowCapScale : c13;
        const double g7 = 1.0 / laws::kR72;
        offDecay = std::exp (-1.0 / (cap * (rb + laws::kR72) * fs));
        onDecay = std::exp (-1.0 / (cap * (rb + 1.0 / (gs + g7)) * fs));
        // Fast: charging through rs and R72, bleeding through kFastBleed.
        fastCharge = std::exp (-1.0 / (kFastCap * (rs + laws::kR72) * fs));
        fastBleed = std::exp (-1.0 / (kFastCap * kFastBleedOhms * fs));
    }

    /** One tick. accentWiperVolts is the Accent pot's wiper in absolute volts (envelope
        times the loaded pot fraction); switchClosed is the accent latch. Returns the gang
        wiper's voltage above rest: multiply by octaves per volt for the cutoff offset. */
    double tick (double accentWiperVolts, bool switchClosed) noexcept
    {
        // The diode's anode above the rest level.
        const double e = accentWiperVolts - laws::kVq9;

        if (speed == Speed::Fast)
        {
            const double s = e - laws::kAccentDiodeVolts;
            conducting = switchClosed && s > q;
            if (conducting)
            {
                q = s + (q - s) * fastCharge;
                w = (s - q) * fastGain;
            }
            else
            {
                q *= fastBleed;
                w = 0.0;
            }
            u *= offDecay;
            if (std::abs (q) < kRest) q = 0.0;
            if (std::abs (u) < kRest) u = 0.0;
            diodeVolts = 0.0;
            return w;
        }

        // Current the diode would carry with no drop across it. It conducts when that is
        // positive, which is the same as the anode being above the diode-off wiper voltage.
        const double available = currentA * e - currentB * u;
        conducting = switchClosed && available > 0.0;
        if (conducting)
        {
            // Diode drop from the Shockley law, I = Is (exp (v / nVt) - 1), against the
            // network's load line I = available - currentA * v: Newton on v, warm-started
            // from the last tick. The function is convex, so after the first step the
            // iterates approach from above and cannot overshoot.
            double v = std::min (diodeVolts, kDiodeMaxVolts);
            for (int i = 0; i < kDiodeIterations; ++i)
            {
                const double ex = kDiodeIs * std::exp (v * (1.0 / kDiodeNVt));
                const double g = ex - kDiodeIs + currentA * v - available;
                const double dv = g / (ex * (1.0 / kDiodeNVt) + currentA);
                v = clamp (v - dv, 0.0, kDiodeMaxVolts);
                if (std::abs (dv) < 1.0e-10) break;
            }
            diodeVolts = v;

            // With the drop held over the tick the network is linear: exact update.
            const double s = e - v;
            const double target = s * onTarget;
            u = target + (u - target) * onDecay;
            w = (s * gs * rb + u) * onWiperDen;
        }
        else
        {
            u *= offDecay;
            if (std::abs (u) < kRest) u = 0.0;
            w = u * offWiper;
            diodeVolts = 0.0;
        }
        q *= fastBleed;
        return w;
    }

    /** C13's voltage above its 3.3 V rest level. */
    double capacitorVolts() const noexcept { return u; }
    /** Sets the capacitor's charge (tests and state restore). */
    void setCapacitorVolts (double volts) noexcept { u = volts; }
    double wiperVolts() const noexcept { return w; }
    bool diodeConducting() const noexcept { return conducting; }

private:
    static constexpr double kRest = 1.0e-12;
    static constexpr double kSlowCapScale = 4.7;
    static constexpr double kSlowR46 = 4.7e3;
    static constexpr double kFastCap = 0.22e-6;
    static constexpr double kFastBleedOhms = 2.2e6;
    static constexpr double kDiodeIs = 2.52e-9;         // D24, saturation current
    static constexpr double kDiodeNVt = 1.752 * 0.02585; // emission coefficient times V_T
    static constexpr double kDiodeMaxVolts = 0.7;
    static constexpr int kDiodeIterations = 12;

    double fs = 48000.0;
    double position = 1.0, tapPosition = 1.0, c13 = 1.0e-6;
    Speed speed = Speed::Normal;

    double rb = 0.0, rs = laws::kR46 + laws::kResPotOhms, gs = 0.0;
    double offWiper = 1.0, offDecay = 0.0;
    double onWiperDen = 1.0, onTarget = 0.5, onDecay = 0.0;
    double currentA = 0.0, currentB = 0.0;
    double fastGain = 0.5, fastCharge = 0.0, fastBleed = 0.0;

    double u = 0.0;         // C13 above rest
    double q = 0.0;         // the Fast mode's series capacitor
    double w = 0.0;         // wiper above rest
    double diodeVolts = 0.0;
    bool conducting = false;
};

//==============================================================================
/** The analog gate that feeds the amplitude envelope (Q36 with C72), not the CPU's line.

    Turn-on: a delay that depends on how long the gate was low (C72 has to charge before
    Q36 conducts; it recovers with a 100 ms time constant after a note), then a fast
    exponential rise. Turn-off: the level holds for 8 ms, then falls with a 4.5 ms time
    constant. A gate that returns inside the hold never drops.

    level() is the gate voltage as a fraction of its 11.9 V.

    Click-free mode replaces the shape with raised-cosine ramps: up over at least 3 ms after
    the same delay, down over 16 ms from the moment the gate goes low. */
class GateShaper
{
public:
    enum class State { Off, Delay, Rise, On, Hold, Fall };

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        setTimes (maxDelaySeconds, riseSeconds, clickFree);
        reset();
    }

    void reset() noexcept
    {
        state = State::Off;
        gateLevel = 0.0;
        counter = 0;
        offSamples = kLongOff;
        lastDelay = 0.0;
        rampFrom = 0.0;
        rampPos = 0.0;
    }

    /** maxDelay: turn-on delay after a long pause, seconds (5.4 ms stock, 0 for none).
        rise: 10 to 90 % rise time in seconds (0.5 ms stock). */
    void setTimes (double maxDelay, double rise, bool clickFreeRamps) noexcept
    {
        maxDelaySeconds = clamp (maxDelay, 0.0, 0.1);
        riseSeconds = clamp (rise, 1.0e-5, 1.0);
        clickFree = clickFreeRamps;
        // Exponential towards kRiseTarget, cut off at 1: 10 % to 90 % takes 1.846 tau.
        riseCoeff = onePoleCoeff (riseSeconds / 1.846, fs);
        fallDecay = std::exp (-1.0 / (laws::kGateFallSeconds * fs));
        holdSamples = static_cast<int> (laws::kGateHoldSeconds * fs + 0.5);
        rampUpStep = 1.0 / (std::max (riseSeconds * 2.0, kClickFreeRiseSeconds) * fs);
        rampDownStep = 1.0 / (kClickFreeFallSeconds * fs);
    }

    /** The CPU gate went high. */
    void gateOn() noexcept
    {
        if (state == State::On || state == State::Rise || state == State::Delay)
            return;
        if (state == State::Hold)
        {
            // Q36 is still saturated: the gate never dropped.
            state = State::On;
            lastDelay = 0.0;
            return;
        }
        const double off = static_cast<double> (offSamples) / fs;
        lastDelay = laws::gateTurnOnDelay (offSamples >= kLongOff ? 1.0e3 : off, maxDelaySeconds);
        counter = static_cast<int> (lastDelay * fs + 0.5);
        state = State::Delay;
        if (counter <= 0) beginRise();
    }

    /** The CPU gate went low. */
    void gateOff() noexcept
    {
        offSamples = 0;
        if (clickFree)
        {
            if (state != State::Off) beginFall();
            return;
        }
        if (state == State::Rise || state == State::On)
        {
            state = State::Hold;
            counter = holdSamples;
        }
        else if (state == State::Delay)
        {
            state = gateLevel > 0.0 ? State::Fall : State::Off;
        }
    }

    /** One tick. Returns level(). */
    double tick() noexcept
    {
        if (offSamples < kLongOff) ++offSamples;

        switch (state)
        {
            case State::Off:
            case State::On:
                break;

            case State::Delay:
                // The level keeps falling while C72 charges.
                if (gateLevel > 0.0) fallStep();
                if (--counter <= 0) beginRise();
                break;

            case State::Rise:
                if (clickFree)
                {
                    rampPos += rampUpStep;
                    if (rampPos >= 1.0) { gateLevel = 1.0; state = State::On; }
                    else                gateLevel = rampFrom + (1.0 - rampFrom) * raisedCosine (rampPos);
                }
                else
                {
                    gateLevel += (kRiseTarget - gateLevel) * riseCoeff;
                    if (gateLevel >= 1.0) { gateLevel = 1.0; state = State::On; }
                }
                break;

            case State::Hold:
                if (--counter <= 0) state = State::Fall;
                break;

            case State::Fall:
                fallStep();
                if (gateLevel <= 0.0) state = State::Off;
                break;
        }
        return gateLevel;
    }

    double level() const noexcept { return gateLevel; }
    State getState() const noexcept { return state; }
    bool isClickFree() const noexcept { return clickFree; }
    /** The turn-on delay chosen at the last gateOn(), seconds. */
    double lastDelaySeconds() const noexcept { return lastDelay; }

private:
    static constexpr double kRiseTarget = 1.05;
    static constexpr double kFloor = 1.0e-4;
    static constexpr double kClickFreeRiseSeconds = 0.003;
    static constexpr double kClickFreeFallSeconds = 0.016;
    static constexpr int kLongOff = 1 << 30;

    static double raisedCosine (double p) noexcept { return 0.5 - 0.5 * std::cos (kPi * p); }

    void beginRise() noexcept
    {
        state = State::Rise;
        rampFrom = gateLevel;
        rampPos = 0.0;
    }

    void beginFall() noexcept
    {
        state = State::Fall;
        rampFrom = gateLevel;
        rampPos = 0.0;
    }

    void fallStep() noexcept
    {
        if (clickFree)
        {
            rampPos += rampDownStep;
            gateLevel = rampPos >= 1.0 ? 0.0 : rampFrom * (1.0 - raisedCosine (rampPos));
        }
        else
        {
            gateLevel *= fallDecay;
            if (gateLevel < kFloor) gateLevel = 0.0;
        }
    }

    double fs = 48000.0;
    double maxDelaySeconds = 0.0054;
    double riseSeconds = 0.0005;
    bool clickFree = false;

    double riseCoeff = 1.0, fallDecay = 0.0;
    double rampUpStep = 1.0, rampDownStep = 1.0;
    int holdSamples = 0;

    State state = State::Off;
    double gateLevel = 0.0;
    int counter = 0;
    int offSamples = kLongOff;
    double lastDelay = 0.0;
    double rampFrom = 0.0, rampPos = 0.0;
};
}
