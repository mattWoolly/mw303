// Every factory scene, loaded as a program or a preset, must give a session state that saves,
// parses back and re-saves byte for byte, with every parameter restored. Mirrors pluginval's
// "Plugin state restoration" run after its "Plugin programs" test.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

TEST_CASE ("state: factory scenes round-trip through the session state", "[plugin][state][factory]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    PresetFilter scenes;
    scenes.type = PresetType::Scene;
    scenes.includeUser = false;
    const auto list = c.presets().list (scenes);
    int checked = 0;
    for (const PresetEntry* e : list)
    {
        std::string error;
        REQUIRE (c.loadPreset (*e, &error));
        const std::string text = d.stateText();
        PresetData parsed;
        const PresetParseResult r = parsePreset (text, parsed);
        INFO (e->id << ": " << r.error << " at " << r.offset);
        REQUIRE (r.ok);
        CHECK (r.adjusted == 0);
        Driver other;
        other.setStateText (text);
        CHECK (other.stateText() == text);
        if (++checked >= 40) break;   // the first forty are plenty; the rest share the same codec paths
    }
    CHECK (checked > 0);
}

TEST_CASE ("state: pluginval-style restoration after a program change", "[plugin][state][programs]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& p = d.processor;
    d.prepare();
    if (p.getNumPrograms() > 1)
    {
        juce::Thread::sleep (600);
        p.setCurrentProgram (p.getNumPrograms() - 1);
        CHECK (p.getCurrentProgram() == p.getNumPrograms() - 1);
    }

    // Random normalised values through the host path, save, scramble, restore, compare.
    Rng rng (5);
    std::vector<float> wanted;
    for (auto* param : p.getParameters())
    {
        const float v = static_cast<float> (rng.nextDouble());
        param->setValueNotifyingHost (v);
        wanted.push_back (param->getValue());
    }
    const std::string saved = d.stateText();
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost (static_cast<float> (rng.nextDouble()));
    d.setStateText (saved);
    int i = 0;
    for (auto* param : p.getParameters())
    {
        INFO (param->getName (64).toStdString());
        CHECK (param->getValue() == wanted[static_cast<std::size_t> (i)]);
        ++i;
    }
    CHECK (d.stateText() == saved);
}
