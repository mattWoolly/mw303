#pragma once
// Two-state nodal model of the square shaper (docs/research/02, section 2.5; 04, section 2.3).
//
// One PNP transistor (Q8) turns the falling saw into the square:
//
//     saw --R35 100k--+-- base                emitter -- R45 22k -- +12 V
//     saw --C10 10n--R34 10k--+               emitter -- C11 1 uF -- ground
//                                             collector -- R36 10k -- 5.333 V   (square out)
//
// C11 holds the emitter at a self-bias voltage that is the switching threshold. It recharges
// through R45 (22 ms) while Q8 is off and discharges through Q8 into R36 (6.9 ms) while it is
// on, so the threshold floats with pitch: the duty cycle falls as pitch rises, the top of the
// pulse droops, and the threshold takes 10 to 25 milliseconds to settle after a pitch jump.
// None of that is programmed; it is what the node equations do.
//
// The model: transport (Ebers-Moll) transistor, two algebraic unknowns (the two junction
// voltages) solved by Newton, two capacitor states integrated with the trapezoidal rule
// inside the same solve, so the result does not depend on the step length to first order
// (the duty cycle is the same to 0.001 from 88.2 kHz to 384 kHz and matches a brute-force
// simulation at 6 MHz to 0.0005).
// The caller owns time: advance() takes the step as a fraction of the sample period, which
// lets the oscillator split a sample exactly at the saw reset and refine the fast turn-on
// edge with sub-steps.
//
// Threading: one instance, one thread. Nothing here allocates, locks or throws.
#include <cstdint>

namespace acid::dsp
{
class SquareShaper
{
public:
    // Component values from the service notes.
    static constexpr double kR35 = 100.0e3;            // saw to base
    static constexpr double kR34 = 10.0e3;             // in series with C10, saw to base
    static constexpr double kC10 = 10.0e-9;            // speed-up capacitor
    static constexpr double kR45 = 22.0e3;             // emitter to +12 V
    static constexpr double kC11 = 1.0e-6;             // emitter to ground (nominal)
    static constexpr double kR36 = 10.0e3;             // collector load
    static constexpr double kEmitterRailVolts = 12.0;
    static constexpr double kCollectorRailVolts = 5.333;   // the square's flat low level

    // Transistor (small-signal PNP), transport model.
    static constexpr double kSaturationCurrent = 2.0e-14;  // amps
    static constexpr double kReverseBeta = 4.0;
    static constexpr double kThermalVolts = 0.02585;       // kT/q at 300 K
    static constexpr double kDefaultBeta = 300.0;
    static constexpr double kMinBeta = 50.0;
    static constexpr double kMaxBeta = 1000.0;

    /** Emitter voltage a settled mid-range note sits near; the power-on state. */
    static constexpr double kEmitterRestVolts = 8.6;

    /** Below this forward voltage on both junctions the transistor is treated as an open
        circuit (collector current under 3 nA, under 30 uV at the output, -100 dB). */
    static constexpr double kOffVolts = 0.30;

    static constexpr int kMaxIterations = 40;

    /** Derives the integration constants for the rate advance (1.0, ...) will be called at,
        and resets. */
    void prepare (double sampleRate) noexcept;

    /** Power-on state: C11 at its rest level, C10 discharged, transistor off. */
    void reset() noexcept;

    /** Forward current gain of Q8 (calibration range 100 to 400). Sets how sharp the
        turn-on knee is; moves the duty cycle by less than 0.015 over that range. */
    void setBeta (double beta) noexcept;

    /** Emitter capacitor in farads (nominal 1 uF; an aged electrolytic is 0.8 to 1.5 uF).
        Scales both threshold time constants. */
    void setEmitterCapacitor (double farads) noexcept;

    /** Advances the circuit by `fraction` sample periods (a piece of one, or several), with
        the saw input moving in a straight line to sawVolts (absolute volts at the follower
        output), and returns the collector voltage at the end. fraction == 0 is legal and
        means "the input jumped": the capacitor voltages stay and the node equations are
        solved again for the new input, which is how the caller gets the level on both
        sides of the saw reset. */
    double advance (double fraction, double sawVolts) noexcept;

    double collectorVolts() const noexcept { return vc; }
    double emitterVolts() const noexcept { return ve; }
    double baseVolts() const noexcept { return vb; }

    /** Voltage across C10 (saw side minus resistor side). */
    double speedUpCapVolts() const noexcept { return vC10; }

    /** Emitter-base voltage of the last solve. Rises as the saw falls; the transistor
        starts to matter once it passes kOffVolts and saturates near 0.6 V. */
    double emitterBaseVolts() const noexcept { return veb; }

    /** True when the last solve took the open-circuit path (collector exactly at its rail). */
    bool isOff() const noexcept { return off; }

    /** Solver health since prepare() or resetStats(). The targets are zero non-converged
        solves and a worst case far below kMaxIterations. */
    struct Stats
    {
        std::uint64_t solves = 0;           // Newton solves (open-circuit steps not counted)
        std::uint64_t iterations = 0;       // Newton iterations, summed
        std::uint64_t nonConverged = 0;
        std::uint32_t maxIterations = 0;
    };
    const Stats& getStats() const noexcept { return stats; }
    void resetStats() noexcept { stats = {}; }

private:
    void updateFullStep() noexcept;

    struct StepCoeffs
    {
        double qC10 = 0.0;      // half step over C10 (seconds per farad)
        double invOnePlusH = 1.0;
        double bc = 0.0;        // C10 companion: vC10' = ac + bc * (saw - base)
        double p = 0.0;         // half step over C11
        double invOnePlusPg = 1.0;
        double be = 0.0;        // C11 companion: ve' = al - be * emitterCurrent
        double gb = 0.0;        // conductance from saw to base through both branches
        double invGb = 0.0;
    };
    StepCoeffs coeffsFor (double fraction) const noexcept;

    double samplePeriod = 1.0 / 192000.0;
    double invBeta = 1.0 / kDefaultBeta;
    double c11 = kC11;
    StepCoeffs fullStep;

    // states
    double ve = kEmitterRestVolts;      // C11
    double vC10 = 0.0;                  // C10
    double iC10 = 0.0;                  // current through C10 at the last node
    double jC11 = 0.0;                  // current into C11 at the last node

    // last solution
    double vb = 0.0;
    double vc = kCollectorRailVolts;
    double veb = 0.0;
    double vcb = 0.0;
    double slope1 = 0.0;                // trend of veb and vcb per sample, for the warm start
    double slope2 = 0.0;
    double c1 = -2.0, c2 = -2.0;        // junction voltages of the last exact exponentials
    double e1 = 0.0, e2 = 0.0;          // and their values
    bool off = true;

    Stats stats;
};
}
