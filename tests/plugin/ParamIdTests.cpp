// Parameter identity: hosts store the string ids (VST3 and CLAP hash them, LV2 puts them in
// URIs), so the list is golden. A new parameter is appended to the golden file; an id that
// changes or disappears fails here on purpose.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace ptest;

namespace
{
std::vector<std::string> readLines (const std::filesystem::path& path)
{
    std::vector<std::string> lines;
    std::ifstream f (path);
    std::string line;
    while (std::getline (f, line))
        if (! line.empty() && line[0] != '#')
            lines.push_back (line);
    return lines;
}
}

TEST_CASE ("params: the string ids match the golden list, in order", "[plugin][params]")
{
    const auto golden = readLines (std::filesystem::path (MW_TEST_SOURCE_DIR) / "param_ids.golden");
    REQUIRE (! golden.empty());

    std::vector<std::string> current;
    for (int i = 0; i < kNumParams; ++i)
        current.emplace_back (kParamTable[static_cast<std::size_t> (i)].id);

    // Every golden id must still exist at the same index (ids are never renamed or reordered);
    // new ones may follow.
    REQUIRE (current.size() >= golden.size());
    for (std::size_t i = 0; i < golden.size(); ++i)
    {
        INFO ("index " << i);
        CHECK (current[i] == golden[i]);
    }
    if (current.size() != golden.size())
        FAIL_CHECK ("new parameters were added: append them to tests/plugin/param_ids.golden");
}

TEST_CASE ("params: every host parameter carries its table id, name, range and kind", "[plugin][params]")
{
    ScratchConfigDir scratch;
    Driver d;
    mw12FProcessor& processor = d.processor;
    const auto& set = processor.parameterSet();

    const auto& hostParams = processor.getParameters();
    REQUIRE (hostParams.size() == kNumParams);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const ParamInfo& info = kParamTable[idx];
        HostParam* p = set.byId[idx];
        REQUIRE (p != nullptr);
        CHECK (p->getParameterID().toStdString() == info.id);
        CHECK (p->getName (256).toStdString() == info.name);
        CHECK (p->natural() == info.def);                 // the clean state
        CHECK (p->isDiscrete() == (info.kind != ParamKind::Float));
        CHECK (p->isBoolean() == (info.kind == ParamKind::Bool));
        CHECK (p->getNormalisableRange().start == info.min);
        CHECK (p->getNormalisableRange().end == info.max);
        // The default maps to its own normalised value and back exactly.
        CHECK (p->getValue() == p->getNormalisableRange().convertTo0to1 (info.def));
        CHECK (p->defaultNatural() == (findFactoryFile ("default.mw12f") != nullptr ? p->defaultNatural() : info.def));

        if (info.kind == ParamKind::Choice)
        {
            juce::StringArray labels;
            labels.addTokens (juce::String (info.choices), "|", "");
            CHECK (p->getAllValueStrings() == labels);
            CHECK (p->getNumSteps() == labels.size());
        }
        if (info.kind == ParamKind::Bool)
            CHECK (p->getNumSteps() == 2);
    }

    // The same object is what the processor exposes to the host (hashing is on the string id).
    for (auto* hp : hostParams)
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (hp);
        REQUIRE (withId != nullptr);
        CHECK (findParam (withId->getParameterID().toStdString()) >= 0);
    }
}

TEST_CASE ("params: a new instance reports the factory default scene as the default", "[plugin][params][factory]")
{
    // clap-validator's param-default-values: the value after construction equals getDefaultValue().
    ScratchConfigDir scratch;
    mw12FProcessor fresh;
    fresh.handlePendingMessages();
    for (HostParam* p : fresh.parameterSet().byId)
    {
        INFO (p->info().id);
        CHECK (p->getValue() == p->getDefaultValue());
        CHECK (p->natural() == p->defaultNatural());
    }
    if (const FactoryFile* f = findFactoryFile ("default.mw12f"))
    {
        PresetData d;
        REQUIRE (parsePreset (f->text(), d).ok);
        for (int i = 0; i < kNumParams; ++i)
            if (d.hasParam[static_cast<std::size_t> (i)])
                CHECK (fresh.parameterSet().byId[static_cast<std::size_t> (i)]->natural() == d.params[static_cast<std::size_t> (i)]);
    }
}

TEST_CASE ("params: natural values survive the normalised round trip exactly", "[plugin][params]")
{
    ScratchConfigDir scratch;
    Driver d;
    mw12FProcessor& processor = d.processor;
    const auto& set = processor.parameterSet();

    // Awkward values for every row: log ranges, negative ranges, non-integers.
    const float awkward[] = { 15.9f, 7.9f, 31800.0f, 0.1f, 33.333f, -6.25f, 1951.0f, 0.333f, 2.5f, 999.5f };
    for (int i = 0; i < kNumParams; ++i)
    {
        HostParam* p = set.byId[static_cast<std::size_t> (i)];
        const ParamInfo& info = p->info();
        for (const float a : awkward)
        {
            const float v = p->legalValue (a);
            p->setNatural (v);
            CHECK (p->natural() == v);
            // What the host would send back for this value leaves it alone.
            p->setValue (p->getValue());
            CHECK (p->natural() == v);
            if (info.kind != ParamKind::Float)
                CHECK (v == std::round (v));
        }
    }

    SECTION ("a host-driven normalised value snaps discrete kinds and clamps floats")
    {
        Driver untouched;                       // the loop above already gave every parameter a host number
        const auto& set2 = untouched.processor.parameterSet();
        HostParam* wave = set2.byId[kWave];
        wave->setValue (0.7f);
        CHECK (wave->natural() == 1.0f);
        wave->setValue (0.2f);
        CHECK (wave->natural() == 0.0f);

        HostParam* on = set2.byId[kDistOn];
        on->setValue (0.49f);
        CHECK (on->natural() == 0.0f);
        on->setValue (0.51f);
        CHECK (on->natural() == 1.0f);
        CHECK (on->getValue() == 0.51f);       // the host's own number for "On" reads back
        on->setValue (0.9f);
        CHECK (on->natural() == 1.0f);
        CHECK (on->getValue() == 0.9f);        // the latest host number
        on->setNatural (1.0f, true);           // the host restores a state: it reads getValue back
        CHECK (on->getValue() == 0.51f);       // the number the host had before its last poke
        on->setNatural (0.0f);                 // a load or an editor move: the host is told the plain mapping
        CHECK (on->getValue() == 0.0f);        // (0.49 is two pokes back, gone)
        on->setNatural (1.0f);
        CHECK (on->getValue() == 1.0f);        // and reads it back: no older host number survives a notified write
        HostParam* fresh = set2.byId[kModHiRes];
        fresh->setNatural (1.0f);
        CHECK (fresh->getValue() == 1.0f);     // never set by the host: the plain mapping

        HostParam* cutoff = set2.byId[kCutoff];
        cutoff->setValue (2.0f);
        CHECK (cutoff->natural() == 100.0f);
        cutoff->setValue (-1.0f);
        CHECK (cutoff->natural() == 0.0f);

        HostParam* c21 = set2.byId[kCalC21];
        c21->setValue (0.0f);
        CHECK (c21->natural() == 7.9f);
        c21->setValue (1.0f);
        CHECK (c21->natural() == 31800.0f);
        c21->setValue (0.5f);
        CHECK (c21->natural() > 400.0f);        // log mapping: the midpoint is the geometric mean (about 501 nF)
        CHECK (c21->natural() < 600.0f);
    }

    SECTION ("text in natural units")
    {
        HostParam* cutoff = set.byId[kCutoff];
        cutoff->setNatural (35.0f);
        CHECK (cutoff->getText (cutoff->getValue(), 0) == "35");
        CHECK (cutoff->getValueForText ("50") == cutoff->getNormalisableRange().convertTo0to1 (50.0f));
        HostParam* wave = set.byId[kWave];
        CHECK (wave->getText (1.0f, 0) == "Square");
        CHECK (wave->getValueForText ("square") == 1.0f);
        HostParam* on = set.byId[kDistOn];
        CHECK (on->getText (1.0f, 0) == "On");
        CHECK (on->getValueForText ("off") == 0.0f);
    }
}
