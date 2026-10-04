// Output ceiling (QA findings): stock knob settings reached +9 dBTP with every effect off, the
// Overdrive mod at 100 % +8.5 dBFS, and hostile or corrupted states +46 dBFS. The voice now
// models its output amplifier's rails (Voice.h) and the engine ends in a safety clip that
// never passes +12 dBFS (Engine.h, kSafetyKnee).
#include "EngineTestSupport.h"
#include "acid/dsp/Voice.h"
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace etest;

namespace
{
constexpr const char* kClassic = "C:A C+:A C:AS C+ ~ . D#:S D#+:A C . G-:AS G C+:A C+:A A#-:S C";

Rendered play (const ParamValues& params, double seconds = 2.0)
{
    Session s;
    s.setPattern (0, kClassic);
    s.params = params;
    s.params[kSeqRun] = 1.0f;
    s.host.bpm = 125.0;
    s.host.play();
    s.prepare (kFs, 512);
    return s.render (static_cast<std::int64_t> (seconds * kFs), 512);
}

float peak (const Rendered& r) { return std::max (peakOf (r.left), peakOf (r.right)); }
}

TEST_CASE ("output ceiling: the loudest stock settings round off near +6 dBFS", "[engine][ceiling]")
{
    // The QA case: the init sound with cutoff 65, Env Mod 100, resonance 0, Accent 100 on an
    // accented line; every effect off. Before the rails: +9.2 dBTP, 4124 samples over 0 dBFS.
    ParamValues p = plainParams();
    p[kCutoff] = 65.0f;
    p[kEnvMod] = 100.0f;
    p[kResonance] = 0.0f;
    p[kAccent] = 100.0f;
    const Rendered r = play (p);
    INFO ("peak " << 20.0 * std::log10 (peak (r)) << " dBFS");
    CHECK (allFinite (r));
    CHECK (peak (r) < static_cast<float> (dsp::Voice::kWorstCaseStockPeak));
    CHECK (peak (r) > 1.0f);     // still hot: the rails round accents off, they do not limit

    // The Overdrive mod at 100 %, distortion off. Before: +8.5 dBFS.
    ParamValues m = plainParams();
    m[kModOn] = 1.0f;
    m[kModOverdrive] = 100.0f;
    const Rendered o = play (m);
    INFO ("overdrive peak " << 20.0 * std::log10 (peak (o)) << " dBFS");
    CHECK (peak (o) <= static_cast<float> (dsp::Voice::kWorstCaseModsPeak));

    // A plain line at the default sound is far under the safety clip's knee, so the clip does
    // not touch it.
    const Rendered d = play (plainParams());
    CHECK (peak (d) < 0.5f * Engine::kSafetyKnee);
}

TEST_CASE ("output ceiling: every gain stacked to its maximum stays under +12 dBFS", "[engine][ceiling]")
{
    // What a hostile or corrupted session can ask for: distortion at full drive with +24 dB of
    // pre EQ and +24 dB of trim, four EQ bands at +24 dB, the limiter's +24 dB of gain with the
    // limiter off, master at +12 dB, the Overdrive mod and Accent at 100. Before: +46 dBFS.
    ParamValues p = plainParams();
    p[kAccent] = 100.0f;
    p[kModOn] = 1.0f;
    p[kModOverdrive] = 100.0f;
    p[kDistOn] = 1.0f;
    p[kDistDrive] = 100.0f;
    p[kDistPreEqOn] = 1.0f;
    p[kDistPreEqGain] = 24.0f;
    p[kDistVolume] = 24.0f;
    p[kEqOn] = 1.0f;
    p[kEqLowGain] = p[kEqLoMidGain] = p[kEqHiMidGain] = p[kEqHighGain] = 24.0f;
    p[kLimOn] = 0.0f;
    p[kLimGain] = 24.0f;
    p[kMasterVolume] = 12.0f;
    const Rendered r = play (p);
    INFO ("peak " << 20.0 * std::log10 (peak (r)) << " dBFS");
    CHECK (allFinite (r));
    CHECK (peak (r) <= Engine::kSafetyCeiling);
    CHECK (peak (r) > Engine::kSafetyKnee);     // it is the clip that holds it
}
