// Settings::internalBpm: through the codec (attribute internal_bpm, 20..300) and through the
// engine (the internal clock steps at the published tempo, not at a constant).
#include "EngineTestSupport.h"
#include "acid/engine/StateCodec.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace etest;

TEST_CASE ("codec: internal_bpm is written, read back, range-checked and applied", "[engine][codec][internal_bpm]")
{
    PresetData d;
    d.type = PresetType::Scene;
    d.settingsMask = kSetInternalBpm;
    d.settings.internalBpm = 133.5f;

    const std::string written = writePreset (d);
    CHECK (written.find ("<settings internal_bpm=\"133.5\"/>") != std::string::npos);

    PresetData back;
    const PresetParseResult r = parsePreset (written, back);
    REQUIRE (r.ok);
    CHECK (r.adjusted == 0);
    CHECK ((back.settingsMask & kSetInternalBpm) != 0);
    CHECK (back.settings.internalBpm == 133.5f);

    Settings s;
    applySettings (back, s);
    CHECK (s.internalBpm == 133.5f);

    SECTION ("out of range values are clamped and counted")
    {
        PresetData high;
        const auto rh = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><settings internal_bpm=\"999\"/></mw12F>", high);
        REQUIRE (rh.ok);
        CHECK (rh.adjusted == 1);
        CHECK (high.settings.internalBpm == 300.0f);

        PresetData low;
        const auto rl = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><settings internal_bpm=\"5\"/></mw12F>", low);
        REQUIRE (rl.ok);
        CHECK (rl.adjusted == 1);
        CHECK (low.settings.internalBpm == 20.0f);

        PresetData junk;
        const auto rj = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><settings internal_bpm=\"fast\"/></mw12F>", junk);
        REQUIRE (rj.ok);
        CHECK (rj.adjusted == 1);
        CHECK (junk.settings.internalBpm == 120.0f);
        CHECK ((junk.settingsMask & kSetInternalBpm) != 0);
    }

    SECTION ("a state document without the attribute restores the default")
    {
        PresetData state;
        const auto rs = parsePreset ("<mw12F schema=\"1\" type=\"state\"><settings midi_out=\"1\"/></mw12F>", state);
        REQUIRE (rs.ok);
        Settings current;
        current.internalBpm = 77.0f;
        applySettings (state, current);
        CHECK (current.internalBpm == 120.0f);
    }

    SECTION ("a preset without the attribute keeps the current value")
    {
        PresetData scene;
        const auto rs = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><settings midi_out=\"1\"/></mw12F>", scene);
        REQUIRE (rs.ok);
        Settings current;
        current.internalBpm = 77.0f;
        applySettings (scene, current);
        CHECK (current.internalBpm == 77.0f);
    }

    SECTION ("a hand-built value outside the range is written sanitised")
    {
        PresetData wild;
        wild.type = PresetType::Scene;
        wild.settingsMask = kSetInternalBpm;
        wild.settings.internalBpm = 1.0e9f;
        CHECK (writePreset (wild).find ("internal_bpm=\"300\"") != std::string::npos);
        Settings s2;
        applySettings (wild, s2);
        CHECK (s2.internalBpm == 300.0f);
    }

    SECTION ("the state type carries it")
    {
        CHECK ((settingsFieldsInType (PresetType::State) & kSetInternalBpm) != 0);
        CHECK ((settingsFieldsInType (PresetType::Scene) & kSetInternalBpm) != 0);
        CHECK ((settingsFieldsInType (PresetType::Calibration) & kSetInternalBpm) == 0);
    }
}

TEST_CASE ("engine: the internal clock runs at Settings::internalBpm", "[engine][transport][internal_bpm]")
{
    // No host transport at all (the standalone), Run on, a note on every step.
    const auto onsetsAt = [] (float bpm) {
        Session s;
        s.params[kSeqRun] = 1.0f;
        s.params[kDecay] = 0.0f;
        s.setPattern (0, "C . . . C . . . C . . . C . . .");
        s.settings.internalBpm = bpm;
        s.host.present = false;
        s.prepare (kFs, 256);
        const Rendered r = s.render (static_cast<std::int64_t> (kFs * 4.0), 256);
        REQUIRE (allFinite (r));
        return detectOnsets (r.left, kFs);
    };

    const auto at90 = onsetsAt (90.0f);
    const auto at150 = onsetsAt (150.0f);
    REQUIRE (at90.size() >= 4);
    REQUIRE (at150.size() >= 4);

    // A quarter at 90 BPM is 32000 samples at 48 kHz, at 150 BPM 19200 samples.
    const auto spacing = [] (const std::vector<std::int64_t>& on) { return static_cast<double> (on[3] - on[1]) / 2.0; };
    CHECK (std::abs (spacing (at90) - 32000.0) < 150.0);
    CHECK (std::abs (spacing (at150) - 19200.0) < 150.0);

    SECTION ("a tempo published while running takes effect")
    {
        Session s;
        s.params[kSeqRun] = 1.0f;
        s.params[kDecay] = 0.0f;
        s.setPattern (0, "C . . . C . . . C . . . C . . .");
        s.settings.internalBpm = 60.0f;      // 48000 samples a quarter
        s.host.present = false;
        s.prepare (kFs, 256);
        const auto half = static_cast<std::int64_t> (kFs * 4.0);
        const Rendered r = s.render (half * 2, 256, [&] (std::int64_t pos) {
            if (pos == half)
            {
                s.settings.internalBpm = 240.0f;   // 12000 samples a quarter
                s.publish();
            }
        });
        const auto on = detectOnsets (r.left, kFs);
        int slow = 0, fast = 0;
        for (std::size_t i = 1; i < on.size(); ++i)
        {
            const auto gap = on[i] - on[i - 1];
            if (std::abs (gap - 48000) < 400) ++slow;
            if (std::abs (gap - 12000) < 400) ++fast;
        }
        CHECK (slow >= 2);
        CHECK (fast >= 12);
    }

    SECTION ("a value outside 20..300 is clamped, a non-finite one falls back to 120")
    {
        Session s;
        s.params[kSeqRun] = 1.0f;
        s.params[kDecay] = 0.0f;
        s.setPattern (0, "C . . . C . . . C . . . C . . .");
        s.settings.internalBpm = 2000.0f;     // clamped to 300: 9600 samples a quarter
        s.host.present = false;
        s.prepare (kFs, 256);
        const Rendered r = s.render (static_cast<std::int64_t> (kFs * 2.0), 256);
        const auto on = detectOnsets (r.left, kFs);
        REQUIRE (on.size() >= 4);
        CHECK (std::abs (static_cast<double> (on[3] - on[1]) / 2.0 - 9600.0) < 150.0);

        Session n;
        n.params[kSeqRun] = 1.0f;
        n.params[kDecay] = 0.0f;
        n.setPattern (0, "C . . . C . . . C . . . C . . .");
        n.settings.internalBpm = std::numeric_limits<float>::quiet_NaN();
        n.host.present = false;
        n.prepare (kFs, 256);
        const Rendered rn = n.render (static_cast<std::int64_t> (kFs * 3.0), 256);
        REQUIRE (allFinite (rn));
        const auto onN = detectOnsets (rn.left, kFs);
        REQUIRE (onN.size() >= 4);
        CHECK (std::abs (static_cast<double> (onN[3] - onN[1]) / 2.0 - 24000.0) < 150.0);
    }
}
