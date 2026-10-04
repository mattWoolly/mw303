// The state and preset codec: golden output, lossless round trips over random documents,
// type rules, apply semantics, range checking, preservation of unknown content, schema
// handling and hostile input.
#include "acid/engine/StateCodec.h"
#include "acid/common/Rng.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <string>

using namespace acid;

namespace
{
seq::Pattern randomPattern (Rng& rng, bool arp)
{
    seq::Pattern p;
    const int nameLen = rng.nextInt (20);
    for (int i = 0; i < nameLen; ++i)
        p.name[i] = static_cast<char> (32 + rng.nextInt (95));   // printable ASCII, '|' '%' '=' '<' '&' included
    p.length = static_cast<std::uint8_t> (1 + rng.nextInt (seq::kMaxSteps));
    p.div = static_cast<seq::StepDiv> (rng.nextInt (7));
    p.divMod = static_cast<seq::DivMod> (rng.nextInt (3));
    p.shuffle = static_cast<std::uint8_t> (rng.nextInt (101));
    p.gateLength = static_cast<std::uint8_t> (rng.nextInt (101));
    p.direction = static_cast<seq::Direction> (rng.nextInt (4));
    p.transpose = static_cast<std::int8_t> (rng.nextInt (97) - 48);
    for (auto& s : p.steps)
    {
        s.state = static_cast<seq::StepState> (rng.nextInt (3));
        s.note = static_cast<std::uint8_t> (rng.nextInt (12));
        s.octave = static_cast<std::int8_t> (rng.nextInt (5) - 1);
        s.arpIndex = static_cast<std::uint8_t> (arp ? 1 + rng.nextInt (10) : 1 + rng.nextInt (10));
        s.accent = rng.chance (0.3);
        s.slide = rng.chance (0.3);
        s.vibrato = rng.chance (0.2);
        s.probability = static_cast<std::uint8_t> (rng.nextInt (101));
        s.ratchet = static_cast<std::uint8_t> (1 + rng.nextInt (4));
        s.gate = static_cast<std::uint8_t> (rng.nextInt (128));
        s.locks = static_cast<std::uint8_t> (rng.nextInt (64));
        const auto mag = [&rng] { return static_cast<std::uint8_t> (rng.chance (0.3) ? 255 : rng.nextInt (128)); };
        s.magGate = mag();
        s.magAccent = mag();
        s.magSlide = mag();
        s.magVibrato = mag();
        s.candNote = static_cast<std::uint8_t> (rng.nextInt (12));
        s.candOctave = static_cast<std::int8_t> (rng.nextInt (5) - 1);
    }
    p.gen.seed = static_cast<std::uint32_t> (rng.nextU64());
    p.gen.noteMask = static_cast<std::uint16_t> (rng.nextInt (4096));
    p.gen.root = static_cast<std::uint8_t> (rng.nextInt (12));
    p.gen.octaveMask = static_cast<std::uint8_t> (rng.nextInt (32));
    p.gen.useSlide = rng.chance (0.5);
    p.gen.useAccent = rng.chance (0.5);
    p.gen.useVibrato = rng.chance (0.5);
    p.gen.amountNote = static_cast<std::uint8_t> (rng.nextInt (101));
    p.gen.amountSlide = static_cast<std::uint8_t> (rng.nextInt (101));
    p.gen.amountAccent = static_cast<std::uint8_t> (rng.nextInt (101));
    p.gen.amountVibrato = static_cast<std::uint8_t> (rng.nextInt (101));
    seq::sanitizePattern (p);
    return p;
}

std::string randomText (Rng& rng, int maxLen)
{
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ,.-_&<>\"'/%|=\t";
    std::string s;
    const int n = rng.nextInt (maxLen + 1);
    for (int i = 0; i < n; ++i)
        s.push_back (kChars[rng.nextInt (static_cast<int> (sizeof (kChars)) - 1)]);
    return s;
}

PresetData randomPreset (Rng& rng)
{
    PresetData d;
    d.type = static_cast<PresetType> (rng.nextInt (kNumPresetTypes));
    d.name = randomText (rng, 24);
    d.author = randomText (rng, 12);
    d.tags = randomText (rng, 20);
    d.description = randomText (rng, 60);

    for (int i = 0; i < kNumParams; ++i)
    {
        if (! rng.chance (0.6)) continue;
        const ParamInfo& info = kParamTable[static_cast<std::size_t> (i)];
        float v = 0.0f;
        if (info.kind == ParamKind::Float)
        {
            const int kind = rng.nextInt (4);
            if (kind == 0) v = info.min;
            else if (kind == 1) v = info.max;
            else if (kind == 2) v = info.def;
            else v = static_cast<float> (static_cast<double> (info.min) + rng.nextDouble() * static_cast<double> (info.max - info.min));
        }
        else
        {
            v = static_cast<float> (static_cast<int> (info.min) + rng.nextInt (static_cast<int> (info.max - info.min) + 1));
        }
        d.setParam (static_cast<ParamId> (i), v);
    }

    d.settingsMask = static_cast<std::uint32_t> (rng.nextU64()) & kSetAll;
    d.settings.triggerMode = static_cast<seq::TriggerMode> (rng.nextInt (2));
    d.settings.launchQuant = static_cast<seq::LaunchQuant> (rng.nextInt (3));
    d.settings.slideIntoRest = static_cast<seq::SlideIntoRest> (rng.nextInt (3));
    d.settings.notePriority = static_cast<seq::NotePriority> (rng.nextInt (3));
    d.settings.followHostTransport = rng.chance (0.5);
    d.settings.hostLink = rng.chance (0.5);
    d.settings.restartOnTrigger = rng.chance (0.5);
    d.settings.internalBpm = static_cast<float> (20 + rng.nextInt (281));
    d.settings.hostNoteBase = static_cast<std::uint8_t> (rng.nextInt (128));
    d.settings.accentThreshold = static_cast<std::uint8_t> (rng.nextInt (128));
    d.settings.bendRange = static_cast<std::uint8_t> (rng.nextInt (13));
    d.settings.midiOut = rng.chance (0.5);
    d.settings.midiThru = rng.chance (0.5);
    d.settings.outAccentVelocity = static_cast<std::uint8_t> (rng.nextInt (128));
    d.settings.outNormalVelocity = static_cast<std::uint8_t> (rng.nextInt (128));
    d.settings.realtimeQuality = static_cast<Quality> (rng.nextInt (4));
    d.settings.offlineQuality = static_cast<Quality> (rng.nextInt (4));
    for (int i = 5; i > 0; --i)
        std::swap (d.settings.fxOrder[static_cast<std::size_t> (i)], d.settings.fxOrder[static_cast<std::size_t> (rng.nextInt (i + 1))]);

    if (rng.chance (0.6))
    {
        d.hasBank = true;
        const int n = rng.nextInt (6);
        std::array<bool, seq::kNumSlots> used {};
        for (int i = 0; i < n; ++i)
        {
            const int slot = rng.nextInt (seq::kNumSlots);
            if (used[static_cast<std::size_t> (slot)]) continue;
            used[static_cast<std::size_t> (slot)] = true;
            d.seqSlots.push_back ({ slot, randomPattern (rng, false) });
        }
        used = {};
        const int m = rng.nextInt (4);
        for (int i = 0; i < m; ++i)
        {
            const int slot = rng.nextInt (seq::kNumSlots);
            if (used[static_cast<std::size_t> (slot)]) continue;
            used[static_cast<std::size_t> (slot)] = true;
            d.arpSlots.push_back ({ slot, randomPattern (rng, true) });
        }
    }

    if (rng.chance (0.4))
    {
        d.hasPattern = true;
        d.pattern = randomPattern (rng, d.type == PresetType::Arp);
    }

    if (rng.chance (0.4))
    {
        d.hasTemplate = true;
        d.genParams = randomPattern (rng, false).gen;
    }

    if (rng.chance (0.5))
    {
        xml::Element ui;
        ui.name = "ui";
        ui.setAttribute ("zoom", "1.25");
        ui.setAttribute ("theme", randomText (rng, 8));
        d.extraElements.push_back (ui);
    }
    if (rng.chance (0.5))
    {
        xml::Element map;
        map.name = "midi_map";
        for (int i = 0; i < rng.nextInt (4); ++i)
        {
            xml::Element cc;
            cc.name = "cc";
            cc.setAttribute ("number", std::to_string (rng.nextInt (128)));
            cc.setAttribute ("param", kParamTable[static_cast<std::size_t> (rng.nextInt (kNumParams))].id);
            map.children.push_back (cc);
        }
        d.extraElements.push_back (map);
    }
    if (rng.chance (0.3))
    {
        xml::Element future;
        future.name = "song";
        future.text = randomText (rng, 30);
        d.extraElements.push_back (future);
    }
    if (rng.chance (0.3)) d.extraRootAttributes.push_back ({ "plugin_version", "9.9.9" });
    if (rng.chance (0.3)) d.extraSettingsAttributes.push_back ({ "future_knob", randomText (rng, 6) });

    // Unknown nodes at every level inside the known elements (SPEC 6.7: preserved on re-save).
    if (rng.chance (0.3)) d.extraParamsAttributes.push_back ({ "layout", randomText (rng, 6) });
    if (rng.chance (0.4))
    {
        xml::Element p;
        p.name = "p";
        p.setAttribute ("id", "future_param_" + std::to_string (rng.nextInt (100)));
        p.setAttribute ("v", std::to_string (rng.nextInt (1000)));
        d.extraParamElements.push_back (p);
    }
    if (rng.chance (0.3))
    {
        xml::Element g;
        g.name = "group";
        g.setAttribute ("name", randomText (rng, 8));
        g.text = randomText (rng, 10);
        d.extraParamElements.push_back (g);
    }
    for (int i = 0; i < kNumParams; ++i)
        if (d.hasParam[static_cast<std::size_t> (i)] && rng.chance (0.03))
            d.extraParamAttributes.push_back ({ i, { { "curve", randomText (rng, 4) }, { "lock", "1" } } });
    if (rng.chance (0.3)) d.extraBankAttributes.push_back ({ "song", std::to_string (rng.nextInt (8)) });
    if (rng.chance (0.3))
    {
        xml::Element chain;
        chain.name = "chain";
        chain.setAttribute ("order", "0,1,2");
        d.extraBankElements.push_back (chain);
    }
    for (auto& s : d.seqSlots)
        if (rng.chance (0.3)) s.extraAttributes.push_back ({ "color", std::to_string (rng.nextInt (16)) });
    for (auto& s : d.arpSlots)
        if (rng.chance (0.3)) s.extraAttributes.push_back ({ "mute", "1" });
    if (rng.chance (0.3)) d.extraPatternAttributes.push_back ({ "origin", randomText (rng, 6) });
    if (rng.chance (0.3)) d.extraTemplateAttributes.push_back ({ "swing_mask", std::to_string (rng.nextInt (4096)) });

    // The writer leaves out whatever the type does not carry, so the document that comes
    // back can only equal this once the same rule has been applied here.
    restrictToType (d);
    return d;
}

bool sameAttributes (const std::vector<xml::Attribute>& a, const std::vector<xml::Attribute>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].value != b[i].value) return false;
    return true;
}

bool sameElements (const std::vector<xml::Element>& a, const std::vector<xml::Element>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (xml::write (a[i]) != xml::write (b[i])) return false;
    return true;
}

bool sameSlots (const std::vector<SlotPattern>& a, const std::vector<SlotPattern>& b)
{
    if (a.size() != b.size()) return false;
    for (const auto& x : a)
    {
        bool found = false;
        for (const auto& y : b)
            if (x.slot == y.slot)
            {
                if (! seq::samePattern (x.pattern, y.pattern)) return false;
                if (! sameAttributes (x.extraAttributes, y.extraAttributes)) return false;
                found = true;
            }
        if (! found) return false;
    }
    return true;
}

bool sameSettings (const Settings& a, const Settings& b, std::uint32_t mask)
{
    if ((mask & kSetTriggerMode) && a.triggerMode != b.triggerMode) return false;
    if ((mask & kSetLaunchQuant) && a.launchQuant != b.launchQuant) return false;
    if ((mask & kSetSlideIntoRest) && a.slideIntoRest != b.slideIntoRest) return false;
    if ((mask & kSetNotePriority) && a.notePriority != b.notePriority) return false;
    if ((mask & kSetFollowHost) && a.followHostTransport != b.followHostTransport) return false;
    if ((mask & kSetHostLink) && a.hostLink != b.hostLink) return false;
    if ((mask & kSetRestartOnTrigger) && a.restartOnTrigger != b.restartOnTrigger) return false;
    if ((mask & kSetInternalBpm) && a.internalBpm != b.internalBpm) return false;
    if ((mask & kSetHostNoteBase) && a.hostNoteBase != b.hostNoteBase) return false;
    if ((mask & kSetAccentThreshold) && a.accentThreshold != b.accentThreshold) return false;
    if ((mask & kSetBendRange) && a.bendRange != b.bendRange) return false;
    if ((mask & kSetMidiOut) && a.midiOut != b.midiOut) return false;
    if ((mask & kSetMidiThru) && a.midiThru != b.midiThru) return false;
    if ((mask & kSetOutAccentVelocity) && a.outAccentVelocity != b.outAccentVelocity) return false;
    if ((mask & kSetOutNormalVelocity) && a.outNormalVelocity != b.outNormalVelocity) return false;
    if ((mask & kSetRealtimeQuality) && a.realtimeQuality != b.realtimeQuality) return false;
    if ((mask & kSetOfflineQuality) && a.offlineQuality != b.offlineQuality) return false;
    if ((mask & kSetFxOrder) && a.fxOrder != b.fxOrder) return false;
    return true;
}

bool samePreset (const PresetData& a, const PresetData& b, std::string& why)
{
    if (a.type != b.type) { why = "type"; return false; }
    if (a.name != b.name || a.author != b.author || a.tags != b.tags || a.description != b.description) { why = "strings"; return false; }
    if (a.hasParam != b.hasParam) { why = "hasParam"; return false; }
    for (int i = 0; i < kNumParams; ++i)
        if (a.hasParam[static_cast<std::size_t> (i)] && a.params[static_cast<std::size_t> (i)] != b.params[static_cast<std::size_t> (i)])
        {
            why = std::string ("param ") + kParamTable[static_cast<std::size_t> (i)].id;
            return false;
        }
    if (a.settingsMask != b.settingsMask) { why = "settingsMask"; return false; }
    if (! sameSettings (a.settings, b.settings, a.settingsMask)) { why = "settings"; return false; }
    if (a.hasBank != b.hasBank) { why = "hasBank"; return false; }
    if (! sameSlots (a.seqSlots, b.seqSlots) || ! sameSlots (a.arpSlots, b.arpSlots)) { why = "slots"; return false; }
    if (a.hasPattern != b.hasPattern || (a.hasPattern && ! seq::samePattern (a.pattern, b.pattern))) { why = "pattern"; return false; }
    if (a.hasTemplate != b.hasTemplate) { why = "hasTemplate"; return false; }
    if (a.hasTemplate)
    {
        const auto& x = a.genParams;
        const auto& y = b.genParams;
        if (x.noteMask != y.noteMask || x.root != y.root || x.octaveMask != y.octaveMask || x.useSlide != y.useSlide
            || x.useAccent != y.useAccent || x.useVibrato != y.useVibrato || x.amountNote != y.amountNote
            || x.amountSlide != y.amountSlide || x.amountAccent != y.amountAccent || x.amountVibrato != y.amountVibrato)
        {
            why = "template";
            return false;
        }
    }
    if (! sameElements (a.extraElements, b.extraElements)) { why = "extras"; return false; }
    if (! sameAttributes (a.extraRootAttributes, b.extraRootAttributes)) { why = "extraRootAttributes"; return false; }
    if (! sameAttributes (a.extraSettingsAttributes, b.extraSettingsAttributes)) { why = "extraSettingsAttributes"; return false; }
    if (! sameAttributes (a.extraParamsAttributes, b.extraParamsAttributes)) { why = "extraParamsAttributes"; return false; }
    if (! sameElements (a.extraParamElements, b.extraParamElements)) { why = "extraParamElements"; return false; }
    if (a.extraParamAttributes.size() != b.extraParamAttributes.size()) { why = "extraParamAttributes"; return false; }
    for (const auto& x : a.extraParamAttributes)
    {
        bool found = false;
        for (const auto& y : b.extraParamAttributes)
            if (x.param == y.param)
            {
                if (! sameAttributes (x.attributes, y.attributes)) { why = "extraParamAttributes values"; return false; }
                found = true;
            }
        if (! found) { why = "extraParamAttributes param"; return false; }
    }
    if (! sameAttributes (a.extraBankAttributes, b.extraBankAttributes)) { why = "extraBankAttributes"; return false; }
    if (! sameElements (a.extraBankElements, b.extraBankElements)) { why = "extraBankElements"; return false; }
    if (! sameAttributes (a.extraPatternAttributes, b.extraPatternAttributes)) { why = "extraPatternAttributes"; return false; }
    if (! sameAttributes (a.extraTemplateAttributes, b.extraTemplateAttributes)) { why = "extraTemplateAttributes"; return false; }
    return true;
}
}

TEST_CASE ("state: golden document", "[state][golden]")
{
    PresetData d;
    d.type = PresetType::Scene;
    d.name = "Rubber Floor";
    d.author = "mwAudio";
    d.tags = "Seq,Saw,Medium";
    d.description = "A line with \"quotes\" & <angles>";
    d.setParam (kCutoff, 35.0f);
    d.setParam (kResonance, 75.5f);
    d.setParam (kWave, 1.0f);
    d.setParam (kCalC21, 15.9f);
    d.setParam (kMasterVolume, -6.25f);
    d.settingsMask = kSetAll;
    d.settings.triggerMode = seq::TriggerMode::HostNote;
    d.settings.launchQuant = seq::LaunchQuant::NextBar;
    d.settings.fxOrder = { 1, 0, 2, 3, 5, 4 };
    d.hasBank = true;
    seq::Pattern p;
    std::strcpy (p.name, "Night Line");
    p.steps[0].state = seq::StepState::Note;
    p.steps[1].state = seq::StepState::Note;
    p.steps[1].note = 7;
    p.steps[1].accent = true;
    p.steps[1].slide = true;
    d.seqSlots.push_back ({ 3, p });
    d.seqSlots.push_back ({ 0, p });
    xml::Element ui;
    ui.name = "ui";
    ui.setAttribute ("zoom", "1");
    ui.setAttribute ("theme", "night");
    d.extraElements.push_back (ui);

    const std::string expected =
        "<mw12F schema=\"1\" type=\"scene\" name=\"Rubber Floor\" author=\"mwAudio\" tags=\"Seq,Saw,Medium\" description=\"A line with &quot;quotes&quot; &amp; &lt;angles&gt;\">\n"
        "  <params>\n"
        "    <p id=\"wave\" v=\"1\"/>\n"
        "    <p id=\"cutoff\" v=\"35\"/>\n"
        "    <p id=\"resonance\" v=\"75.5\"/>\n"
        "    <p id=\"cal_c21\" v=\"15.9\"/>\n"
        "    <p id=\"master_volume\" v=\"-6.25\"/>\n"
        "  </params>\n"
        "  <settings trigger_mode=\"host_note\" launch_quant=\"next_bar\" slide_into_rest=\"release\" note_priority=\"last\""
        " follow_host=\"1\" host_link=\"1\" restart_on_trigger=\"1\" internal_bpm=\"120\" host_note_base=\"24\" accent_threshold=\"64\" bend_range=\"2\""
        " midi_out=\"1\" midi_thru=\"0\" out_accent_velocity=\"96\" out_normal_velocity=\"32\" realtime_quality=\"normal\""
        " offline_quality=\"high\" fx_order=\"1,0,2,3,5,4\"/>\n"
        "  <bank>\n"
        "    <seq slot=\"0\">" + seq::encodePattern (p) + "</seq>\n"
        "    <seq slot=\"3\">" + seq::encodePattern (p) + "</seq>\n"
        "  </bank>\n"
        "  <ui zoom=\"1\" theme=\"night\"/>\n"
        "</mw12F>\n";

    const std::string written = writePreset (d);
    CHECK (written == expected);
    CHECK (writePreset (d) == written);

    PresetData back;
    const PresetParseResult r = parsePreset (written, back);
    REQUIRE (r.ok);
    CHECK (r.adjusted == 0);
    CHECK (r.ignored == 0);
    CHECK (r.schema == 1);
    std::string why;
    CHECK (samePreset (d, back, why));
    INFO (why);
    CHECK (writePreset (back) == written);
}

TEST_CASE ("state: random documents round-trip losslessly and deterministically", "[state][roundtrip]")
{
    Rng rng (0x5EED);
    for (int i = 0; i < 3000; ++i)
    {
        const PresetData d = randomPreset (rng);
        const std::string once = writePreset (d);
        PresetData back;
        const PresetParseResult r = parsePreset (once, back);
        INFO ("seed index " << i << ": " << r.error);
        REQUIRE (r.ok);
        CHECK (r.adjusted == 0);
        // A <p> with an id this build does not know is counted as ignored and kept all the same.
        int unknownRows = 0;
        for (const auto& e : d.extraParamElements)
            if (e.name == "p") ++unknownRows;
        CHECK (r.ignored == unknownRows);
        std::string why;
        const bool same = samePreset (d, back, why);
        INFO ("difference: " << why);
        CHECK (same);
        CHECK (writePreset (back) == once);
    }
}

TEST_CASE ("state: type rules select the groups the spec lists", "[state][types]")
{
    ParamValues values = defaultParamValues();
    for (int i = 0; i < kNumParams; ++i) values[static_cast<std::size_t> (i)] = kParamTable[static_cast<std::size_t> (i)].max;
    Settings settings;
    settings.fxOrder = { 5, 4, 3, 2, 1, 0 };
    seq::PatternBank bank;
    bank.seq[5].steps[0].state = seq::StepState::Note;
    bank.arp[7].steps[2].arpIndex = 3;

    const auto groupsOf = [] (const PresetData& d) {
        std::string groups;
        for (int i = 0; i < kNumParams; ++i)
        {
            if (! d.hasParam[static_cast<std::size_t> (i)]) continue;
            const std::string g = std::string ("|") + kParamTable[static_cast<std::size_t> (i)].group + "|";
            if (groups.find (g) == std::string::npos) groups += g;
        }
        return groups;
    };

    const PresetData state = makePreset (PresetType::State, values, settings, &bank);
    CHECK (state.hasParam[kSeqRun]);
    CHECK (state.hasParam[kOvrNoteSet]);
    CHECK (state.settingsMask == kSetAll);
    CHECK (state.hasBank);
    CHECK (state.seqSlots.size() == 1);
    CHECK (state.arpSlots.size() == 1);
    CHECK (state.seqSlots[0].slot == 5);
    CHECK (state.arpSlots[0].slot == 7);

    const PresetData scene = makePreset (PresetType::Scene, values, settings, &bank);
    CHECK (scene.hasParam[kCutoff]);
    CHECK (scene.hasParam[kPlayMode]);
    CHECK (scene.hasParam[kPatternSlot]);
    CHECK_FALSE (scene.hasParam[kSeqRun]);
    CHECK_FALSE (scene.hasParam[kOvrAccentClr]);
    CHECK (scene.hasBank);

    const PresetData synth = makePreset (PresetType::Synth, values, settings, &bank);
    CHECK (groupsOf (synth) == "|Synth||Calibration||Mods||Distortion||Distortion Voicing||EQ||Delay||Reverb||Chorus||Limiter||Master|");
    CHECK (synth.settingsMask == kSetFxOrder);
    CHECK_FALSE (synth.hasBank);

    CHECK (groupsOf (makePreset (PresetType::Calibration, values, settings)) == "|Calibration|");
    CHECK (groupsOf (makePreset (PresetType::Distortion, values, settings)) == "|Distortion||Distortion Voicing|");
    CHECK (groupsOf (makePreset (PresetType::Eq, values, settings)) == "|EQ|");
    CHECK (groupsOf (makePreset (PresetType::Delay, values, settings)) == "|Delay|");
    CHECK (groupsOf (makePreset (PresetType::Reverb, values, settings)) == "|Reverb|");
    CHECK (groupsOf (makePreset (PresetType::Chorus, values, settings)) == "|Chorus|");
    CHECK (groupsOf (makePreset (PresetType::Limiter, values, settings)) == "|Limiter|");
    CHECK (makePreset (PresetType::Limiter, values, settings).settingsMask == 0);

    seq::Pattern p;
    std::strcpy (p.name, "Arp One");
    const PresetData arp = makePatternPreset (PresetType::Arp, p);
    CHECK (arp.type == PresetType::Arp);
    CHECK (arp.hasPattern);
    CHECK (arp.name == "Arp One");
    CHECK_FALSE (arp.anyParam());
    const std::string arpText = writePreset (arp);
    CHECK (arpText.find ("<pattern>") != std::string::npos);
    CHECK (arpText.find ("<params>") == std::string::npos);

    seq::GenParams g;
    g.noteMask = 0x0AB5;
    g.root = 7;
    const PresetData tpl = makeTemplatePreset (g);
    const std::string tplText = writePreset (tpl);
    CHECK (tplText.find ("<template note_mask=\"2741\" root=\"7\" octave_mask=\"6\" slide=\"1\" accent=\"1\" vibrato=\"0\""
                         " amount_note=\"50\" amount_slide=\"50\" amount_accent=\"50\" amount_vibrato=\"50\"/>") != std::string::npos);
    PresetData tplBack;
    REQUIRE (parsePreset (tplText, tplBack).ok);
    seq::GenParams g2;
    g2.seed = 99;
    CHECK (applyTemplate (tplBack, g2));
    CHECK (g2.noteMask == 0x0AB5);
    CHECK (g2.root == 7);
    CHECK (g2.seed == 99);     // the seed is not part of a template

    // Every type name round-trips.
    for (int i = 0; i < kNumPresetTypes; ++i)
    {
        PresetType t;
        CHECK (parsePresetType (presetTypeName (static_cast<PresetType> (i)), t));
        CHECK (t == static_cast<PresetType> (i));
    }
    PresetType t;
    CHECK_FALSE (parsePresetType ("bogus", t));
}

TEST_CASE ("state: content outside the type is counted, dropped and never applied", "[state][types]")
{
    // The reader drops it (and counts it), the writer never emits it, and the apply functions
    // check the rules again for a PresetData built in code.
    const char* doc = R"(<mw12F schema="1" type="eq">
  <params><p id="eq_low_gain" v="6"/><p id="cutoff" v="99"/><p id="seq_run" v="1"/></params>
  <settings trigger_mode="host_note" fx_order="5,4,3,2,1,0"/>
  <bank><seq slot="1">acidpat 1|len=16</seq></bank>
  <pattern>acidpat 1|len=8</pattern>
  <template note_mask="1"/>
</mw12F>
)";
    PresetData d;
    const PresetParseResult r = parsePreset (doc, d);
    REQUIRE (r.ok);
    CHECK (r.adjusted == 0);
    CHECK (r.ignored == 2 + 2 + 2 + 1 + 1);   // two params, two settings, the bank and its slot, the pattern, the template
    CHECK (d.hasParam[kEqLowGain]);
    CHECK_FALSE (d.hasParam[kCutoff]);
    CHECK_FALSE (d.hasParam[kSeqRun]);
    CHECK (d.settingsMask == 0);
    CHECK_FALSE (d.hasBank);
    CHECK (d.seqSlots.empty());
    CHECK_FALSE (d.hasPattern);
    CHECK_FALSE (d.hasTemplate);
    const std::string written = writePreset (d);
    CHECK (written.find ("<settings") == std::string::npos);
    CHECK (written.find ("<bank") == std::string::npos);
    CHECK (written.find ("cutoff") == std::string::npos);

    // Built in code, bypassing the reader: the apply functions still obey the type.
    PresetData hand;
    hand.type = PresetType::Calibration;
    hand.setParam (kCalTm3, 100.0f);
    hand.setParam (kCutoff, 99.0f);
    hand.setParam (kSeqRun, 1.0f);
    hand.settingsMask = kSetAll;
    hand.settings.triggerMode = seq::TriggerMode::HostNote;
    hand.hasBank = true;
    ParamValues values = defaultParamValues();
    Settings settings;
    seq::PatternBank bank;
    bank.seq[3].steps[0].state = seq::StepState::Note;
    applyPreset (hand, values, settings, bank);
    CHECK (values[kCalTm3] == 100.0f);
    CHECK (values[kCutoff] == 35.0f);
    CHECK (values[kSeqRun] == 0.0f);
    CHECK (settings.triggerMode == seq::TriggerMode::Live);
    CHECK (bank.seq[3].steps[0].state == seq::StepState::Note);
    CHECK (writePreset (hand).find ("cutoff") == std::string::npos);
    CHECK (writePreset (hand).find ("<bank") == std::string::npos);

    // A scene written by this build never carries the momentary controls, and a scene that
    // does (another build, a hand edit) does not replay them.
    PresetData scene;
    REQUIRE (parsePreset ("<mw12F schema=\"1\" type=\"scene\"><params><p id=\"seq_run\" v=\"1\"/><p id=\"ovr_note_set\" v=\"1\"/><p id=\"cutoff\" v=\"50\"/></params></mw12F>", scene).ok);
    CHECK_FALSE (scene.hasParam[kSeqRun]);
    CHECK_FALSE (scene.hasParam[kOvrNoteSet]);
    CHECK (scene.hasParam[kCutoff]);
}

TEST_CASE ("state: presets keep what they do not carry, session state restores defaults", "[state][apply]")
{
    ParamValues current = defaultParamValues();
    current[kCutoff] = 80.0f;
    current[kDelayMix] = 77.0f;
    Settings settings;
    settings.bendRange = 12;
    settings.midiThru = true;
    seq::PatternBank bank;
    bank.seq[1].steps[0].state = seq::StepState::Note;
    bank.seq[9].steps[0].state = seq::StepState::Tie;

    // An EQ preset: only its rows change.
    PresetData eq;
    eq.type = PresetType::Eq;
    eq.setParam (kEqLowGain, 6.0f);
    applyPreset (eq, current, settings, bank);
    CHECK (current[kEqLowGain] == 6.0f);
    CHECK (current[kCutoff] == 80.0f);
    CHECK (current[kDelayMix] == 77.0f);
    CHECK (settings.bendRange == 12);
    CHECK (bank.seq[1].steps[0].state == seq::StepState::Note);

    // A scene with a bank: omitted slots are empty slots, missing parameters keep.
    PresetData scene;
    scene.type = PresetType::Scene;
    scene.setParam (kResonance, 10.0f);
    scene.hasBank = true;
    seq::Pattern p;
    p.steps[3].state = seq::StepState::Note;
    scene.seqSlots.push_back ({ 4, p });
    scene.settingsMask = kSetMidiThru;
    scene.settings.midiThru = false;
    applyPreset (scene, current, settings, bank);
    CHECK (current[kResonance] == 10.0f);
    CHECK (current[kCutoff] == 80.0f);
    CHECK (bank.seq[1].steps[0].state == seq::StepState::Rest);
    CHECK (bank.seq[4].steps[3].state == seq::StepState::Note);
    CHECK (settings.midiThru == false);
    CHECK (settings.bendRange == 12);

    // A state document: what it lacks goes to the default.
    PresetData state;
    state.type = PresetType::State;
    state.setParam (kDecay, 90.0f);
    applyPreset (state, current, settings, bank);
    CHECK (current[kDecay] == 90.0f);
    CHECK (current[kCutoff] == paramInfo (kCutoff).def);
    CHECK (current[kResonance] == paramInfo (kResonance).def);
    CHECK (settings.bendRange == Settings {}.bendRange);
    CHECK (bank.seq[4].steps[3].state == seq::StepState::Rest);

    // A pattern document applies to a pattern, with sanitising.
    PresetData pat;
    pat.type = PresetType::Pattern;
    pat.hasPattern = true;
    pat.pattern.length = 200;
    pat.pattern.steps[0].note = 50;
    seq::Pattern target;
    CHECK (applyPattern (pat, target));
    CHECK (target.length == 64);
    CHECK (target.steps[0].note <= 11);
    CHECK_FALSE (applyPattern (eq, target));
}

TEST_CASE ("state: values are range-checked, unknown ids skipped, both counted", "[state][ranges]")
{
    const std::string doc =
        "<mw12F schema=\"1\" type=\"scene\">\n"
        "  <params>\n"
        "    <p id=\"cutoff\" v=\"250\"/>\n"
        "    <p id=\"resonance\" v=\"-3\"/>\n"
        "    <p id=\"wave\" v=\"0.7\"/>\n"
        "    <p id=\"decay\" v=\"abc\"/>\n"
        "    <p id=\"envmod\" v=\"nan\"/>\n"
        "    <p id=\"accent\" v=\"1e40\"/>\n"
        "    <p id=\"no_such_param\" v=\"1\"/>\n"
        "    <p v=\"1\"/>\n"
        "    <p id=\"tune\" v=\"+3.5\"/>\n"
        "  </params>\n"
        "  <settings bend_range=\"40\" host_note_base=\"-5\" trigger_mode=\"weird\" fx_order=\"0,0,1,2,3,4\" out_accent_velocity=\"x\"/>\n"
        "  <bank><seq slot=\"99\">acidpat 1|len=16</seq><seq slot=\"2\">garbage</seq><seq slot=\"3\">acidpat 1|len=16</seq></bank>\n"
        "</mw12F>\n";
    PresetData d;
    const PresetParseResult r = parsePreset (doc, d);
    REQUIRE (r.ok);
    CHECK (d.hasParam[kCutoff]);
    CHECK (d.params[kCutoff] == 100.0f);
    CHECK (d.params[kResonance] == 0.0f);
    CHECK (d.params[kWave] == 1.0f);
    CHECK_FALSE (d.hasParam[kDecay]);
    CHECK_FALSE (d.hasParam[kEnvMod]);
    CHECK (d.params[kAccent] == 100.0f);
    CHECK (d.params[kTune] == 3.5f);
    CHECK (d.settings.bendRange == 12);
    CHECK (d.settings.hostNoteBase == 0);
    CHECK (d.settings.triggerMode == seq::TriggerMode::Live);
    CHECK (d.settings.fxOrder == Settings {}.fxOrder);
    CHECK (d.settings.outAccentVelocity == 96);
    CHECK ((d.settingsMask & kSetFxOrder) != 0);
    CHECK (d.hasBank);
    REQUIRE (d.seqSlots.size() == 1);
    CHECK (d.seqSlots[0].slot == 3);
    CHECK (r.ignored == 3);        // no_such_param, the id-less p, slot 99
    CHECK (r.adjusted >= 9);       // cutoff, resonance, decay, envmod, accent, bend_range, host_note_base, trigger_mode, fx_order, velocity, slot 2

    // A value exactly on the limits is not an adjustment; formatting round-trips every float.
    Rng rng (3);
    for (int i = 0; i < 5000; ++i)
    {
        const ParamInfo& info = kParamTable[static_cast<std::size_t> (rng.nextInt (kNumParams))];
        float v = static_cast<float> (static_cast<double> (info.min) + rng.nextDouble() * static_cast<double> (info.max - info.min));
        if (info.kind != ParamKind::Float) v = std::round (v);
        const std::string text = formatParamValue (info, v);
        float back = 0.0f;
        bool clamped = false;
        REQUIRE (parseParamValue (info, text, back, clamped));
        CHECK (back == v);
        CHECK_FALSE (clamped);
    }
    CHECK (formatParamValue (paramInfo (kCutoff), -0.0f) == "0");
    CHECK (formatParamValue (paramInfo (kCutoff), 1.0f / 3.0f) == "0.33333334");
    CHECK (formatParamValue (paramInfo (kPatternSlot), 7.4f) == "7");
}

TEST_CASE ("state: unknown elements and attributes survive a round trip", "[state][forward]")
{
    const std::string doc =
        "<mw12F schema=\"1\" type=\"state\" plugin_version=\"2.0\" mood=\"blue\">\n"
        "  <params>\n"
        "    <p id=\"cutoff\" v=\"40\" extra=\"ignored\"/>\n"
        "  </params>\n"
        "  <settings midi_out=\"0\" future=\"yes\"/>\n"
        "  <ui zoom=\"1.5\" theme=\"day\" edit_view=\"roll\"/>\n"
        "  <midi_map>\n"
        "    <cc number=\"74\" param=\"cutoff\"/>\n"
        "    <cc number=\"71\" param=\"resonance\"/>\n"
        "  </midi_map>\n"
        "  <song><chain a=\"1\"/>text</song>\n"
        "</mw12F>\n";
    PresetData d;
    const PresetParseResult r = parsePreset (doc, d);
    REQUIRE (r.ok);
    REQUIRE (d.extraElements.size() == 3);
    CHECK (d.extra ("ui") != nullptr);
    CHECK (*d.extra ("ui")->attribute ("theme") == "day");
    CHECK (d.extra ("midi_map")->children.size() == 2);
    CHECK (d.extra ("song")->text == "text");
    REQUIRE (d.extraRootAttributes.size() == 2);
    CHECK (d.extraRootAttributes[0].name == "plugin_version");
    REQUIRE (d.extraSettingsAttributes.size() == 1);
    CHECK (d.extraSettingsAttributes[0].value == "yes");

    const std::string once = writePreset (d);
    CHECK (once.find ("plugin_version=\"2.0\" mood=\"blue\"") != std::string::npos);
    CHECK (once.find ("<settings midi_out=\"0\" future=\"yes\"/>") != std::string::npos);
    CHECK (once.find ("<cc number=\"74\" param=\"cutoff\"/>") != std::string::npos);
    CHECK (once.find ("<song>text<chain a=\"1\"/></song>") != std::string::npos);
    CHECK (once.find ("<p id=\"cutoff\" v=\"40\" extra=\"ignored\"/>") != std::string::npos);   // unknown attributes on known rows are kept
    PresetData again;
    REQUIRE (parsePreset (once, again).ok);
    CHECK (writePreset (again) == once);

    // setExtra replaces by name.
    xml::Element ui;
    ui.name = "ui";
    ui.setAttribute ("zoom", "2");
    d.setExtra (ui);
    CHECK (d.extraElements.size() == 3);
    CHECK (*d.extra ("ui")->attribute ("zoom") == "2");
}

namespace
{
int migrationCalls = 0;
void testMigration (xml::Element& root, int fromSchema)
{
    ++migrationCalls;
    if (fromSchema == 0)
    {
        // An imaginary schema 0 wrote "filter" for cutoff.
        if (xml::Element* params = root.child ("params"))
            for (auto& p : params->children)
                if (const std::string* id = p.attribute ("id"); id != nullptr && *id == "filter")
                    p.setAttribute ("id", "cutoff");
    }
}
}

TEST_CASE ("state: schema handling and the migration hook", "[state][schema]")
{
    PresetData d;
    const std::string newer = "<mw12F schema=\"2\" type=\"scene\"><params><p id=\"cutoff\" v=\"1\"/></params><new_thing/></mw12F>";
    PresetParseResult r = parsePreset (newer, d);
    CHECK_FALSE (r.ok);
    CHECK (r.schema == 2);

    PresetParseOptions skew;
    skew.maxForwardSkew = 1;
    r = parsePreset (newer, d, skew);
    REQUIRE (r.ok);
    CHECK (d.params[kCutoff] == 1.0f);
    CHECK (d.extra ("new_thing") != nullptr);
    // Its unknown content is kept, so it is written back under its own schema, not this build's.
    CHECK (d.schema == 2);
    CHECK (writePreset (d).find ("<mw12F schema=\"2\" type=\"scene\">") == 0);
    CHECK_FALSE (parsePreset (writePreset (d), d).ok);
    CHECK (parsePreset (writePreset (d), d, skew).ok);
    CHECK (d.extra ("new_thing") != nullptr);
    // A document of this build's schema or an older one is written with this build's.
    PresetData current;
    REQUIRE (parsePreset ("<mw12F schema=\"1\" type=\"scene\"/>", current).ok);
    CHECK (writePreset (current).find ("schema=\"1\"") != std::string::npos);

    // Schema 0 is below the current one: the hook is called once with fromSchema 0. (A real
    // schema never goes below 1; this exercises the chain.)
    PresetParseOptions migrate;
    migrate.migrate = &testMigration;
    migrationCalls = 0;
    r = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><params><p id=\"filter\" v=\"1\"/></params></mw12F>", d, migrate);
    REQUIRE (r.ok);
    CHECK (migrationCalls == 0);
    CHECK_FALSE (d.hasParam[kCutoff]);

    // Bad schema values and roots.
    CHECK_FALSE (parsePreset ("<mw12F schema=\"0\" type=\"scene\"/>", d).ok);
    CHECK_FALSE (parsePreset ("<mw12F schema=\"x\" type=\"scene\"/>", d).ok);
    CHECK_FALSE (parsePreset ("<mw12F type=\"scene\"/>", d).ok);
    CHECK_FALSE (parsePreset ("<mw12F schema=\"1\"/>", d).ok);
    CHECK_FALSE (parsePreset ("<mw12F schema=\"1\" type=\"nope\"/>", d).ok);
    CHECK_FALSE (parsePreset ("<other schema=\"1\" type=\"scene\"/>", d).ok);
    CHECK_FALSE (parsePreset ("not xml at all", d).ok);
    CHECK (parsePreset ("<mw12F schema=\"1\" type=\"scene\"/>", d).ok);
    CHECK (parsePreset ("<?xml version=\"1.0\"?>\n<mw12F schema=\"1\" type=\"pattern\"><pattern>acidpat 1|len=8</pattern></mw12F>", d).ok);
    CHECK (d.hasPattern);
    CHECK (d.pattern.length == 8);

    // A failed parse leaves the output untouched.
    PresetData untouched;
    untouched.name = "keep me";
    CHECK_FALSE (parsePreset ("<mw12F schema=\"1\" type=\"zzz\"/>", untouched).ok);
    CHECK (untouched.name == "keep me");
}

TEST_CASE ("state: hostile documents are rejected or repaired, never crash", "[state][fuzz]")
{
    Rng rng (0xBAD);
    const PresetData seedPreset = randomPreset (rng);
    const std::string seed = writePreset (seedPreset);

    // Truncations at every length.
    for (std::size_t len = 0; len < seed.size(); len += 7)
    {
        PresetData d;
        (void) parsePreset (seed.substr (0, len), d);
    }

    // Deep nesting, huge attributes, bad entities, enormous numbers, control characters.
    {
        PresetData d;
        std::string deep = "<mw12F schema=\"1\" type=\"scene\">";
        for (int i = 0; i < 1000; ++i) deep += "<a>";
        CHECK_FALSE (parsePreset (deep, d).ok);

        std::string huge = "<mw12F schema=\"1\" type=\"scene\" name=\"" + std::string (2000000, 'n') + "\"><params><p id=\"cutoff\" v=\""
                         + std::string (100000, '9') + "\"/></params></mw12F>";
        const PresetParseResult r = parsePreset (huge, d);
        CHECK (r.ok);
        CHECK (d.name.size() == 2000000);
        CHECK (d.params[kCutoff] == 100.0f);

        CHECK_FALSE (parsePreset ("<mw12F schema=\"1\" type=\"scene\" name=\"&bogus;\"/>", d).ok);
        CHECK_FALSE (parsePreset ("<mw12F schema=\"1\" type=\"scene\" name=\"&#60;\"/>", d).ok);
        CHECK_FALSE (parsePreset ("<mw12F schema=\"1\" type=\"scene\" name=\"\x07\"/>", d).ok);
        CHECK (parsePreset ("<mw12F schema=\"1\" type=\"scene\"><params><p id=\"cutoff\" v=\"1e999\"/><p id=\"tune\" v=\"-1e999\"/></params></mw12F>", d).ok);
        CHECK (d.params[kCutoff] == 100.0f);
        CHECK (d.params[kTune] == -12.0f);
        // Written magnitudes beyond a double are clamped, the words for non-finite values are not numbers.
        PresetParseResult words = parsePreset ("<mw12F schema=\"1\" type=\"scene\"><params><p id=\"cutoff\" v=\"inf\"/><p id=\"tune\" v=\"-Infinity\"/>"
                                               "<p id=\"decay\" v=\"NaN\"/><p id=\"accent\" v=\"nan(1)\"/></params></mw12F>", d);
        CHECK (words.ok);
        CHECK (words.adjusted == 4);
        CHECK_FALSE (d.hasParam[kCutoff]);
        CHECK_FALSE (d.hasParam[kTune]);
        CHECK_FALSE (d.hasParam[kDecay]);
        CHECK_FALSE (d.hasParam[kAccent]);

        // A document built of a great many tiny nodes is refused by the node limit, well
        // below the byte limit, so a hostile file costs bounded memory.
        std::string bomb = "<mw12F schema=\"1\" type=\"scene\"><x>";
        for (int i = 0; i < 200000; ++i) bomb += "<a/>";
        bomb += "</x></mw12F>";
        CHECK (bomb.size() < xml::ParseLimits {}.maxBytes);
        const PresetParseResult bombed = parsePreset (bomb, d);
        CHECK_FALSE (bombed.ok);
        CHECK (bombed.error.find ("too many") != std::string::npos);
        CHECK (parsePreset ("<mw12F schema=\"99999999999999999999\" type=\"scene\"/>", d).ok == false);
        CHECK (parsePreset ("<mw12F schema=\"1\" type=\"scene\"><bank><seq slot=\"99999999999999999999\">x</seq><seq>y</seq><arp slot=\"-1\">z</arp></bank></mw12F>", d).ok);
        CHECK (d.seqSlots.empty());
        CHECK (parsePreset ("<mw12F schema=\"1\" type=\"template\"><template note_mask=\"-999999\" root=\"99\" amount_note=\"1e9\"/></mw12F>", d).ok);
        CHECK (d.hasTemplate);
        CHECK (d.genParams.noteMask == 0);
        CHECK (d.genParams.root == 11);
    }

    // Random mutations of a valid document: every result parses or fails cleanly, and what
    // parses writes back to something that parses to the same bytes.
    int parsed = 0;
    for (int i = 0; i < 3000; ++i)
    {
        std::string s = seed;
        const int mutations = 1 + rng.nextInt (8);
        for (int m = 0; m < mutations; ++m)
        {
            const auto at = static_cast<std::size_t> (rng.nextInt (static_cast<int> (s.size()) + 1));
            switch (rng.nextInt (6))
            {
                case 0: if (! s.empty()) s.erase (std::min (at, s.size() - 1), 1); break;
                case 1: s.insert (at, 1, static_cast<char> (rng.nextInt (256))); break;
                case 2: if (at < s.size()) s[at] = "<>&\"'/= \n0123456789-.|%:"[rng.nextInt (24)]; break;
                case 3: s = s.substr (0, at); break;
                case 4: s.insert (at, s.substr (std::min (at, s.size()), static_cast<std::size_t> (rng.nextInt (60)))); break;
                default: if (at < s.size()) s.erase (at, static_cast<std::size_t> (rng.nextInt (50))); break;
            }
        }
        PresetData d;
        const PresetParseResult r = parsePreset (s, d);
        if (! r.ok) continue;
        ++parsed;
        const std::string w = writePreset (d);
        PresetData again;
        REQUIRE (parsePreset (w, again).ok);
        CHECK (writePreset (again) == w);

        ParamValues values = defaultParamValues();
        Settings settings;
        seq::PatternBank bank;
        applyPreset (d, values, settings, bank);
        for (int k = 0; k < kNumParams; ++k)
        {
            const ParamInfo& info = kParamTable[static_cast<std::size_t> (k)];
            CHECK (values[static_cast<std::size_t> (k)] >= info.min);
            CHECK (values[static_cast<std::size_t> (k)] <= info.max);
        }
    }
    CHECK (parsed > 0);
}
