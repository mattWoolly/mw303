// Reviewer tests for the state codec. Each case fails against the codec as delivered and
// passes once the matching review finding is fixed.
#include "acid/engine/StateCodec.h"
#include "acid/seq/PatternCodec.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace acid;

TEST_CASE ("review: whatever writePreset writes, parsePreset reads back", "[review][state]")
{
    // A name (or tags, description, extra attribute) with a control character is written raw;
    // the strict reader rejects it, so a saved session cannot be restored at all.
    PresetData d = makePreset (PresetType::State, defaultParamValues(), Settings {});
    d.name = std::string ("Acid\x01Line");
    d.description = std::string ("bell\x07");
    PresetData back;
    const PresetParseResult r = parsePreset (writePreset (d), back);
    INFO (r.error);
    REQUIRE (r.ok);
    PresetData again;
    REQUIRE (parsePreset (writePreset (back), again).ok);
    CHECK (writePreset (again) == writePreset (back));     // stable from the first re-save on
}

TEST_CASE ("review: loading applies only what the document's type carries", "[review][state][types]")
{
    // A calibration preset that (by hand edit, a bug in another build, or a malicious file)
    // also carries a bank, a non-calibration parameter, the Run switch and settings: today
    // applyPreset() applies all of it, and the empty <bank> wipes all 96 user patterns.
    const char* doc = R"(<mw12F schema="1" type="calibration" name="x">
  <params>
    <p id="cal_tm3" v="100"/>
    <p id="cutoff" v="99"/>
    <p id="seq_run" v="1"/>
  </params>
  <settings trigger_mode="host_note"/>
  <bank>
  </bank>
</mw12F>
)";
    PresetData d;
    REQUIRE (parsePreset (doc, d).ok);
    ParamValues v = defaultParamValues();
    Settings s;
    auto bank = std::make_unique<seq::PatternBank>();
    REQUIRE (seq::fromStepText ("C D E F", bank->seq[3]));
    applyPreset (d, v, s, *bank);

    CHECK (v[kCalTm3] == 100.0f);
    CHECK (v[kCutoff] == 35.0f);
    CHECK (v[kSeqRun] == 0.0f);
    CHECK (s.triggerMode == seq::TriggerMode::Live);
    CHECK (bank->seq[3].steps[0].state == seq::StepState::Note);
}

TEST_CASE ("review: unknown content inside known elements survives a re-save", "[review][state]")
{
    // SPEC 6.7: unknown elements and attributes are preserved when the plugin re-saves its
    // own state. Only root-level elements and settings attributes are. A parameter appended by
    // a newer build (same schema: appending is not an incompatible change) is lost after one
    // load and save in an older build, as are unknown attributes on <p> and anything new
    // inside <params> or <bank>.
    const char* doc = R"(<mw12F schema="1" type="state">
  <params>
    <p id="cutoff" v="50" curve="exp"/>
    <p id="future_param" v="3"/>
    <group name="new"/>
  </params>
  <bank song="1">
    <chain order="0,1"/>
  </bank>
  <ui zoom="1.5"/>
</mw12F>
)";
    PresetData d;
    REQUIRE (parsePreset (doc, d).ok);
    const std::string out = writePreset (d);
    INFO (out);
    CHECK (out.find ("<ui zoom=\"1.5\"/>") != std::string::npos);   // passes today
    CHECK (out.find ("future_param") != std::string::npos);
    CHECK (out.find ("curve=\"exp\"") != std::string::npos);
    CHECK (out.find ("<group") != std::string::npos);
    CHECK (out.find ("song=\"1\"") != std::string::npos);
    CHECK (out.find ("<chain") != std::string::npos);
}
