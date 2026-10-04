// Session state: byte-identical round trips, old and hostile documents, the type rules.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>

using namespace ptest;

namespace
{
std::string readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    std::string s ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    return s;
}
}

TEST_CASE ("state: save, load, save gives the same bytes", "[plugin][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();

    // Make the state interesting: awkward parameter values, patterns, settings, UI, MIDI map.
    const float awkward[] = { 15.9f, 7.9f, 0.1f, 33.333f, -6.25f, 1951.0f, 0.333f, 2.5f, 999.5f, 31800.0f };
    for (int i = 0; i < kNumParams; ++i)
        c.setParam (static_cast<ParamId> (i), awkward[static_cast<std::size_t> (i) % 10]);
    c.commitPattern (false, 0, d.patternFromText ("C C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A"));
    c.commitPattern (true, 7, d.patternFromText ("1 2+ 3 . ~ 1-:A"));
    Settings s = c.settings();
    s.internalBpm = 133.5f;
    s.fxOrder = { 5, 4, 3, 2, 1, 0 };
    s.launchQuant = seq::LaunchQuant::Immediate;
    c.setSettings (s);
    c.setZoom (1.25f, false);
    c.setTheme ("silver", false);
    c.setMidiMapping (74, kCutoff);
    c.setMidiMapping (71, kResonance);

    const std::string first = d.stateText();
    REQUIRE (first.find ("<mw12F schema=\"1\" type=\"state\"") == 0);
    CHECK (first.find ("internal_bpm=\"133.5\"") != std::string::npos);
    CHECK (first.find ("<ui zoom=\"1.25\" theme=\"silver\"") != std::string::npos);
    CHECK (first.find ("<cc number=\"71\" param=\"resonance\"/>") != std::string::npos);

    // A second instance loads it and saves it back.
    Driver e;
    e.setStateText (first);
    const std::string second = e.stateText();
    CHECK (second == first);

    // And the loaded instance agrees with the original on every parameter and pattern.
    for (int i = 0; i < kNumParams; ++i)
        CHECK (e.processor.controller().getParam (static_cast<ParamId> (i)) == c.getParam (static_cast<ParamId> (i)));
    CHECK (seq::samePattern (e.processor.controller().pattern (false, 0), c.pattern (false, 0)));
    CHECK (seq::samePattern (e.processor.controller().pattern (true, 7), c.pattern (true, 7)));
    CHECK (e.processor.controller().settings().internalBpm == 133.5f);
    CHECK (e.processor.controller().midiControllerFor (kCutoff) == 74);
    CHECK (e.processor.controller().zoom() == 1.25f);
    CHECK (e.processor.controller().theme() == "silver");

    SECTION ("saving twice without a change is byte-identical too")
    {
        CHECK (d.stateText() == first);
    }

    SECTION ("the state can be saved and loaded before prepareToPlay and after it")
    {
        d.prepare();
        CHECK (d.stateText() == first);
        e.prepare();
        e.setStateText (first);
        CHECK (e.stateText() == first);
    }
}

TEST_CASE ("state: a state document restores defaults for what it does not carry", "[plugin][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kCutoff, 90.0f);
    c.setParam (kResonance, 10.0f);
    c.commitPattern (false, 3, d.patternFromText ("C C C C"));
    c.setMidiMapping (20, kDecay);

    d.setStateText ("<mw12F schema=\"1\" type=\"state\"><params><p id=\"cutoff\" v=\"12\"/></params></mw12F>");
    CHECK (c.getParam (kCutoff) == 12.0f);
    CHECK (c.getParam (kResonance) == 75.0f);            // default
    CHECK (seq::samePattern (c.pattern (false, 3), seq::Pattern {}));   // every slot empty
    CHECK (c.midiControllerFor (kDecay) == -1);          // map cleared
    CHECK (c.settings().internalBpm == 120.0f);
}

TEST_CASE ("state: hostile and foreign input leaves the state alone", "[plugin][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kCutoff, 61.0f);
    const std::string before = d.stateText();

    SECTION ("random bytes")
    {
        Rng rng (99);
        for (int round = 0; round < 3; ++round)
        {
            std::vector<char> junk (1 << 20);
            for (auto& b : junk) b = static_cast<char> (rng.nextInt (256));
            d.processor.setStateInformation (junk.data(), static_cast<int> (junk.size()));
            CHECK (c.getParam (kCutoff) == 61.0f);
        }
        CHECK (d.stateText() == before);
    }

    SECTION ("empty, null and tiny input")
    {
        d.processor.setStateInformation (nullptr, 0);
        d.processor.setStateInformation ("", 0);
        d.processor.setStateInformation ("<", 1);
        d.processor.setStateInformation ("<mw12F", 6);
        CHECK (d.stateText() == before);
    }

    SECTION ("a different root, a newer schema, truncated XML")
    {
        d.setStateText ("<other schema=\"1\" type=\"state\"><params><p id=\"cutoff\" v=\"1\"/></params></other>");
        d.setStateText ("<mw12F schema=\"99\" type=\"state\"><params><p id=\"cutoff\" v=\"1\"/></params></mw12F>");
        d.setStateText ("<mw12F schema=\"1\" type=\"state\"><params><p id=\"cutoff\" v=\"1\"/>");
        d.setStateText ("<!DOCTYPE x><mw12F schema=\"1\" type=\"state\"><params><p id=\"cutoff\" v=\"1\"/></params></mw12F>");
        CHECK (d.stateText() == before);
        CHECK (c.getParam (kCutoff) == 61.0f);
    }

    SECTION ("out-of-range and unparsable values are repaired, never trusted")
    {
        d.setStateText ("<mw12F schema=\"1\" type=\"state\"><params><p id=\"cutoff\" v=\"1e99\"/><p id=\"resonance\" v=\"nan\"/>"
                        "<p id=\"pattern_slot\" v=\"-7\"/><p id=\"dist_model\" v=\"999\"/></params>"
                        "<settings internal_bpm=\"-3\" bend_range=\"500\"/>"
                        "<bank><seq slot=\"999\">acidpat 1|len=16</seq><seq slot=\"2\">garbage</seq></bank>"
                        "<midi_map><cc number=\"999\" param=\"cutoff\"/><cc number=\"7\" param=\"nope\"/><cc number=\"8\" param=\"decay\"/></midi_map></mw12F>");
        CHECK (c.getParam (kCutoff) == 100.0f);
        CHECK (c.getParam (kResonance) == 75.0f);
        CHECK (c.getParam (kPatternSlot) == 1.0f);
        CHECK (c.getParam (kDistModel) == 63.0f);
        CHECK (c.settings().internalBpm == 20.0f);
        CHECK (c.settings().bendRange == 12);
        CHECK (c.midiControllerFor (kCutoff) == -1);
        CHECK (c.midiControllerFor (kDecay) == 8);
        // and the result still saves and reloads cleanly
        const std::string text = d.stateText();
        Driver e;
        e.setStateText (text);
        CHECK (e.stateText() == text);
    }
}

TEST_CASE ("state: unknown content is carried through a round trip", "[plugin][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    d.setStateText ("<mw12F schema=\"1\" type=\"state\" name=\"Carried\" future_root=\"x\">\n"
                    "  <params future_params=\"1\">\n    <p id=\"cutoff\" v=\"40\" future_p=\"2\"/>\n    <p id=\"not_yet\" v=\"3\"/>\n  </params>\n"
                    "  <settings midi_out=\"0\" future_setting=\"yes\"/>\n"
                    "  <bank future_bank=\"b\">\n    <seq slot=\"1\">acidpat 1|len=4|steps=N:C:0</seq>\n    <future_child/>\n  </bank>\n"
                    "  <ui zoom=\"1.5\" future_ui=\"u\"/>\n"
                    "  <future_element a=\"1\"><inner/></future_element>\n"
                    "</mw12F>\n");
    const std::string saved = d.stateText();
    CHECK (saved.find ("future_root=\"x\"") != std::string::npos);
    CHECK (saved.find ("future_params=\"1\"") != std::string::npos);
    CHECK (saved.find ("<p id=\"cutoff\" v=\"40\" future_p=\"2\"/>") != std::string::npos);
    CHECK (saved.find ("<p id=\"not_yet\" v=\"3\"/>") != std::string::npos);
    CHECK (saved.find ("future_setting=\"yes\"") != std::string::npos);
    CHECK (saved.find ("future_bank=\"b\"") != std::string::npos);
    CHECK (saved.find ("<future_child/>") != std::string::npos);
    CHECK (saved.find ("future_ui=\"u\"") != std::string::npos);
    CHECK (saved.find ("<future_element a=\"1\">") != std::string::npos);
    CHECK (saved.find ("name=\"Carried\"") != std::string::npos);
    CHECK (d.processor.controller().zoom() == 1.5f);
    CHECK (d.processor.controller().getParam (kCutoff) == 40.0f);
    CHECK (d.processor.controller().settings().midiOut == false);

    Driver e;
    e.setStateText (saved);
    CHECK (e.stateText() == saved);
}

TEST_CASE ("state: a preset document applied as state keeps what it does not carry", "[plugin][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kCutoff, 90.0f);
    c.setParam (kResonance, 10.0f);
    c.commitPattern (false, 3, d.patternFromText ("C C C C"));
    // A calibration preset carries only its group; cutoff, the bank and the Run switch stay.
    c.setParam (kSeqRun, 1.0f);
    d.setStateText ("<mw12F schema=\"1\" type=\"calibration\"><params><p id=\"cal_tm3\" v=\"100\"/><p id=\"cutoff\" v=\"1\"/><p id=\"seq_run\" v=\"0\"/></params>"
                    "<bank/></mw12F>");
    CHECK (c.getParam (kCalTm3) == 100.0f);
    CHECK (c.getParam (kCutoff) == 90.0f);
    CHECK (c.getParam (kSeqRun) == 1.0f);
    CHECK (seq::samePattern (c.pattern (false, 3), d.patternFromText ("C C C C")));
}

TEST_CASE ("state: golden session documents of every schema load", "[plugin][state][golden]")
{
    ScratchConfigDir scratch;
    const auto dir = std::filesystem::path (MW_TEST_SOURCE_DIR) / "golden";

    if (std::getenv ("MW_WRITE_GOLDEN") != nullptr)
    {
        // Regenerates the current-schema golden from the plugin itself (run once per schema).
        Driver d;
        auto& c = d.processor.controller();
        c.setParam (kCutoff, 42.0f);
        c.commitPattern (false, 0, d.patternFromText ("C D#:A G-:S G# . ~ C+:V"));
        c.setMidiMapping (74, kCutoff);
        c.setZoom (1.0f, false);
        c.setTheme ("night", false);
        c.setSceneName ("Golden One");
        std::ofstream (dir / ("state-schema" + std::to_string (kPresetSchema) + ".mw12f"), std::ios::binary) << d.stateText();
    }

    int count = 0;
    for (const auto& item : std::filesystem::directory_iterator (dir))
    {
        if (item.path().extension() != ".mw12f") continue;
        ++count;
        Driver d;
        const std::string text = readFile (item.path());
        REQUIRE (! text.empty());
        d.setStateText (text);
        INFO (item.path().filename().string());
        // The golden schema-1 state carries cutoff 42 and a note on step 1 of slot A1.
        CHECK (d.processor.controller().getParam (kCutoff) == 42.0f);
        CHECK (d.processor.controller().pattern (false, 0).steps[0].state == seq::StepState::Note);
        // Re-saving a document of the current schema reproduces it exactly.
        if (text.find ("schema=\"" + std::to_string (kPresetSchema) + "\"") != std::string::npos)
            CHECK (d.stateText() == text);
    }
    CHECK (count >= 1);
}

TEST_CASE ("state: programs are Init plus a few factory scenes, guarded", "[plugin][state][programs]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& p = d.processor;
    CHECK (p.getNumPrograms() >= 1);
    CHECK (p.getNumPrograms() <= 5);
    CHECK (p.getProgramName (0) == "Init");
    CHECK (p.getCurrentProgram() == 0);

    // Right after a state load, a program change is ignored (hosts send stale indices).
    p.controller().setParam (kCutoff, 77.0f);
    d.setStateText (d.stateText());
    p.setCurrentProgram (0);
    CHECK (p.controller().getParam (kCutoff) == 77.0f);
    // Selecting the current program again is a no-op in any case.
    juce::Thread::sleep (600);
    p.setCurrentProgram (0);
    CHECK (p.controller().getParam (kCutoff) == 77.0f);
    // The program index is saved with the state.
    CHECK (d.stateText().find ("program=\"0\"") != std::string::npos);
}
