// dist_model: choosing a model writes the voicing's values into the Distortion Voicing
// parameters on the message thread; a hand edit of any voicing parameter shows Custom without
// resetting the values; presets carry their own values and are left alone.
#include "PluginTestSupport.h"
#include "acid/fx/DistortionVoicings.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
bool matchesVoicing (Controller& c, const fx::DistortionVoicing& v)
{
    ParamValues expected = c.paramValues();
    fx::writeVoicing (expected, v);
    const ParamValues actual = c.paramValues();
    for (const ParamId id : distortionVoicingParams())
        if (expected[static_cast<std::size_t> (id)] != actual[static_cast<std::size_t> (id)])
            return false;
    return true;
}
}

TEST_CASE ("dist_model: picking a model writes its voicing, editing shows Custom", "[plugin][distortion]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto voicings = fx::distortionVoicings();
    REQUIRE (voicings.size() >= 25);
    c.pump();
    d.processor.handlePendingMessages();

    // The host (or the editor) moves dist_model.
    c.setParam (kDistModel, 3.0f);
    CHECK ((! matchesVoicing (c, voicings[3]) || voicings[3].clipper == 0));   // nothing happens until the message thread runs
    d.processor.handlePendingMessages();
    CHECK (matchesVoicing (c, voicings[3]));
    CHECK (c.getParam (kDistModel) == 3.0f);

    // Another tick changes nothing.
    d.processor.handlePendingMessages();
    CHECK (matchesVoicing (c, voicings[3]));
    CHECK (c.getParam (kDistModel) == 3.0f);

    // Every voicing in turn.
    for (std::size_t i = 1; i < voicings.size(); ++i)
    {
        c.setParam (kDistModel, static_cast<float> (i));
        d.processor.handlePendingMessages();
        INFO (voicings[i].name);
        CHECK (matchesVoicing (c, voicings[i]));
        CHECK (c.getParam (kDistModel) == static_cast<float> (i));
    }

    // A hand edit: the knee moves, the model shows Custom (0), the other values stay.
    c.setParam (kDistModel, 5.0f);
    d.processor.handlePendingMessages();
    const ParamValues before = c.paramValues();
    const float knee = before[kDistKnee] > 50.0f ? 10.0f : 90.0f;
    c.setParam (kDistKnee, knee);
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistModel) == 0.0f);
    CHECK (c.getParam (kDistKnee) == knee);
    for (const ParamId id : distortionVoicingParams())
        if (id != kDistKnee)
            CHECK (c.getParam (id) == before[static_cast<std::size_t> (id)]);

    // Custom stays Custom while editing further.
    c.setParam (kDistBias, 12.0f);
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistModel) == 0.0f);
    CHECK (c.getParam (kDistBias) == 12.0f);

    // Choosing Custom again after a model brings the custom voicing back (the user's work is
    // not lost to the model that was written over it).
    const ParamValues custom = c.paramValues();
    c.setParam (kDistModel, 2.0f);
    d.processor.handlePendingMessages();
    REQUIRE (matchesVoicing (c, voicings[2]));
    c.setParam (kDistModel, 0.0f);
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistModel) == 0.0f);
    for (const ParamId id : distortionVoicingParams())
        CHECK (c.getParam (id) == custom[static_cast<std::size_t> (id)]);

    // An index the table does not have keeps the values (nothing to write).
    c.setParam (kDistModel, 63.0f);
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistKnee) == knee);
    CHECK (c.getParam (kDistModel) == 63.0f);
}

TEST_CASE ("dist_model: Custom keeps the model's values when no custom voicing was ever made", "[plugin][distortion]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto voicings = fx::distortionVoicings();

    // A session saved on model 2 with no custom voicing in it.
    ParamValues v = defaultParamValues();
    fx::writeVoicing (v, voicings[2]);
    v[kDistModel] = 2.0f;
    seq::PatternBank empty;
    REQUIRE (d.processor.loadStateText (writePreset (makePreset (PresetType::State, v, Settings {}, &empty))));
    d.processor.handlePendingMessages();
    REQUIRE (matchesVoicing (c, voicings[2]));

    c.setParam (kDistModel, 0.0f);
    d.processor.handlePendingMessages();
    CHECK (matchesVoicing (c, voicings[2]));
    CHECK (c.getParam (kDistModel) == 0.0f);
}

TEST_CASE ("dist_model: the custom voicing survives a session save and load while a model is selected", "[plugin][distortion]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto voicings = fx::distortionVoicings();

    c.setParam (kDistModel, 4.0f);
    d.processor.handlePendingMessages();
    c.setParam (kDistPreMax, 81.5f);            // a hand edit: Custom
    d.processor.handlePendingMessages();
    REQUIRE (c.getParam (kDistModel) == 0.0f);
    c.setParam (kDistModel, 3.0f);
    d.processor.handlePendingMessages();
    REQUIRE (matchesVoicing (c, voicings[3]));

    const std::string state = d.stateText();
    CHECK (state.find ("<dist_custom") != std::string::npos);

    Driver e;
    e.setStateText (state);
    e.processor.handlePendingMessages();
    auto& c2 = e.processor.controller();
    REQUIRE (matchesVoicing (c2, voicings[3]));
    CHECK (e.stateText() == state);             // save, load, save: the same bytes
    c2.setParam (kDistModel, 0.0f);
    e.processor.handlePendingMessages();
    CHECK (c2.getParam (kDistPreMax) == 81.5f);
}

TEST_CASE ("dist_model: a preset carrying model and values is applied as it is", "[plugin][distortion]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto voicings = fx::distortionVoicings();
    d.processor.handlePendingMessages();

    // A distortion preset says model 4 but with its own knee: that is what the user gets.
    PresetData p = makePreset (PresetType::Distortion, defaultParamValues(), Settings {});
    p.setParam (kDistModel, 4.0f);
    ParamValues v = defaultParamValues();
    fx::writeVoicing (v, voicings[4]);
    for (const ParamId id : distortionVoicingParams()) p.setParam (id, v[static_cast<std::size_t> (id)]);
    p.setParam (kDistKnee, 42.0f);
    d.processor.applyPresetData (p, false);
    d.processor.handlePendingMessages();
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistModel) == 4.0f);
    CHECK (c.getParam (kDistKnee) == 42.0f);

    // A preset that carries only the model (no voicing values) still gets the voicing: the
    // model changed as far as the tracker knows, so the values follow.
    PresetData m = makePreset (PresetType::Distortion, defaultParamValues(), Settings {});
    for (int i = 0; i < kNumParams; ++i) m.hasParam[static_cast<std::size_t> (i)] = false;
    m.setParam (kDistModel, 6.0f);
    d.processor.applyPresetData (m, false);
    d.processor.handlePendingMessages();
    CHECK (c.getParam (kDistModel) == 6.0f);
    CHECK (c.getParam (kDistKnee) == 42.0f);   // the preset said nothing about the knee: kept

    // State load: the saved combination comes back untouched after any number of ticks.
    c.setParam (kDistModel, 7.0f);
    d.processor.handlePendingMessages();
    c.setParam (kDistBias, -33.0f);
    d.processor.handlePendingMessages();
    REQUIRE (c.getParam (kDistModel) == 0.0f);
    const std::string state = d.stateText();
    Driver e;
    e.setStateText (state);
    e.processor.handlePendingMessages();
    e.processor.handlePendingMessages();
    CHECK (e.processor.controller().getParam (kDistModel) == 0.0f);
    CHECK (e.processor.controller().getParam (kDistBias) == -33.0f);
    CHECK (e.stateText() == state);
}
