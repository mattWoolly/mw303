// The preset manager and the controller's preset flows: save, list, search, tags, favourites,
// rename, delete, import, export, load with the apply rules, previous and next, audition.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace ptest;

namespace
{
std::vector<FactoryFile> fakeFactory;
std::vector<std::string> fakeFactoryText;

std::span<const FactoryFile> makeFakeFactory()
{
    fakeFactory.clear();
    fakeFactoryText.clear();
    const auto add = [] (const char*, PresetData d) {
        fakeFactoryText.push_back (writePreset (d));
    };
    PresetData scene = makePreset (PresetType::Scene, defaultParamValues(), Settings {});
    scene.name = "Rubber Floor"; scene.author = "mwAudio"; scene.tags = "Seq,Saw,Medium"; scene.setParam (kCutoff, 61.0f);
    add ("scene/Rubber Floor.mw12f", scene);
    PresetData synth = makePreset (PresetType::Synth, defaultParamValues(), Settings {});
    synth.name = "Rubber Floor"; synth.author = "mwAudio"; synth.tags = "Square,Strong"; synth.setParam (kWave, 1.0f);
    add ("synth/Rubber Floor.mw12f", synth);
    PresetData cal = makePreset (PresetType::Calibration, defaultParamValues(), Settings {});
    cal.name = "Hot Unit"; cal.tags = "Bright"; cal.setParam (kCalTm3, 120.0f);
    add ("calibration/Hot Unit.mw12f", cal);
    seq::Pattern p;
    p.steps[0].state = seq::StepState::Note;
    p.steps[0].note = 7;
    PresetData pat = makePatternPreset (PresetType::Pattern, p);
    pat.name = "Seven";
    add ("pattern/Seven.mw12f", pat);
    const char* paths[] = { "scene/Rubber Floor.mw12f", "synth/Rubber Floor.mw12f", "calibration/Hot Unit.mw12f", "pattern/Seven.mw12f" };
    for (std::size_t i = 0; i < fakeFactoryText.size(); ++i)
        fakeFactory.push_back ({ paths[i], reinterpret_cast<const unsigned char*> (fakeFactoryText[i].data()), fakeFactoryText[i].size() });
    return fakeFactory;
}
}

TEST_CASE ("presets: catalogue, search, tags, favourites and the file operations", "[plugin][presets]")
{
    ScratchConfigDir scratch;
    const auto root = scratch.dir / "mwAudio" / "mw12F";
    PresetManager pm (makeFakeFactory(), root, root);
    pm.rescan();

    const auto all = pm.list();
    REQUIRE (all.size() == 4);
    CHECK (std::filesystem::is_directory (root / "Scene"));       // created on first access
    CHECK (std::filesystem::is_directory (root / "Template"));

    SECTION ("names repeat across types without clashing")
    {
        PresetFilter f;
        f.search = "rubber";
        const auto hits = pm.list (f);
        REQUIRE (hits.size() == 2);
        CHECK (hits[0]->type != hits[1]->type);
        f.type = PresetType::Synth;
        REQUIRE (pm.list (f).size() == 1);
        CHECK (pm.list (f)[0]->id == "factory:synth/Rubber Floor.mw12f");
    }

    SECTION ("tags any and all, search over author and tags")
    {
        PresetFilter f;
        f.tags = { "saw", "Strong" };
        CHECK (pm.list (f).size() == 2);
        f.tagsAll = true;
        CHECK (pm.list (f).empty());
        f.tags = { "Seq", "medium" };
        CHECK (pm.list (f).size() == 1);
        PresetFilter g;
        g.search = "mwaudio";
        CHECK (pm.list (g).size() == 2);
        const auto tags = pm.tags (PresetType::Scene);
        CHECK (tags == std::vector<std::string> { "Medium", "Saw", "Seq" });
    }

    SECTION ("save, rename, delete, favourites, import and export")
    {
        PresetData user = makePreset (PresetType::Scene, defaultParamValues(), Settings {});
        user.name = "My Line / 1";
        user.tags = "Mine";
        std::string error;
        const PresetEntry* saved = pm.saveUser (user, false, error);
        REQUIRE (saved != nullptr);
        CHECK (! saved->factory);
        CHECK (saved->name == "My Line / 1");
        CHECK (std::filesystem::exists (root / "Scene" / "My Line _ 1.mw12f"));
        CHECK (pm.saveUser (user, false, error) == nullptr);        // exists
        CHECK (pm.saveUser (user, true, error) == saved);           // overwrite: the same entry object

        REQUIRE (pm.setFavourite (saved->id, true));
        CHECK (pm.find (saved->id)->favourite);
        PresetFilter fav;
        fav.favouritesOnly = true;
        CHECK (pm.list (fav).size() == 1);
        // favourites are global: a second manager sees them
        PresetManager other (makeFakeFactory(), root, root);
        other.rescan();
        CHECK (other.isFavourite (saved->id));

        const std::string oldId = saved->id;
        const PresetEntry* mine = pm.find (oldId);
        REQUIRE (pm.renameUser (*mine, "Renamed", error));   // the id changes: `saved` and `mine` are gone now
        CHECK (pm.find (oldId) == nullptr);
        const PresetEntry* renamed = pm.find ("user:Scene/Renamed.mw12f");
        REQUIRE (renamed != nullptr);
        CHECK (renamed->favourite);                                  // the favourite followed the rename

        const auto exported = scratch.dir / "export.mw12f";
        REQUIRE (pm.exportTo (*renamed, exported, error));
        CHECK (std::filesystem::exists (exported));
        REQUIRE (pm.deleteUser (*renamed, error));
        CHECK (pm.find ("user:Scene/Renamed.mw12f") == nullptr);

        // Import puts the file in its own type's directory, whatever the source location.
        const PresetEntry* imported = pm.importFile (exported, false, error);
        REQUIRE (imported != nullptr);
        CHECK (imported->type == PresetType::Scene);
        CHECK (std::filesystem::exists (root / "Scene" / "Renamed.mw12f"));

        // Factory entries are read-only.
        const PresetEntry* factory = pm.find ("factory:pattern/Seven.mw12f");
        REQUIRE (factory != nullptr);
        CHECK (! pm.deleteUser (*factory, error));
        CHECK (! pm.renameUser (*factory, "x", error));
    }

    SECTION ("step wraps inside the filtered list")
    {
        PresetFilter f;
        f.type = PresetType::Scene;
        const PresetEntry* first = pm.step (nullptr, f, 1);
        REQUIRE (first != nullptr);
        CHECK (pm.step (first, f, 1) == first);                      // one scene: wraps to itself
        PresetFilter none;
        none.type = PresetType::Eq;
        CHECK (pm.step (nullptr, none, 1) == nullptr);
    }

    SECTION ("hostile user files are skipped, not fatal")
    {
        std::ofstream (root / "Scene" / "broken.mw12f") << "<mw12F schema=\"1\" type=\"scene\"><params>";
        std::ofstream (root / "Scene" / "state.mw12f") << "<mw12F schema=\"1\" type=\"state\"/>";
        std::ofstream (root / "Scene" / "notes.txt") << "not a preset";
        pm.rescan();
        CHECK (pm.list().size() == 4);
    }
}

TEST_CASE ("presets: loading applies only what the file carries", "[plugin][presets]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto root = PresetManager::defaultUserRoot();
    CHECK (root.string().rfind (scratch.dir.string(), 0) == 0);  // XDG_CONFIG_HOME honoured

    c.setParam (kCutoff, 90.0f);
    c.setParam (kResonance, 10.0f);
    c.commitPattern (false, 3, d.patternFromText ("C C C C"));
    c.setParam (kSeqRun, 1.0f);

    // Save a calibration preset from the live state, change the trimmer, load it back.
    std::string error;
    c.setParam (kCalTm3, 123.0f);
    const PresetEntry* cal = c.savePreset (PresetType::Calibration, "Trim A", "me", "Bright, Test", "desc", false, &error);
    REQUIRE (cal != nullptr);
    CHECK (cal->tags == std::vector<std::string> { "Bright", "Test" });
    c.setParam (kCalTm3, 0.0f);
    c.setParam (kCutoff, 20.0f);
    REQUIRE (c.loadPreset (*cal, &error));
    CHECK (c.getParam (kCalTm3) == 123.0f);
    CHECK (c.getParam (kCutoff) == 20.0f);                      // not in a calibration preset
    CHECK (c.getParam (kSeqRun) == 1.0f);
    CHECK (seq::samePattern (c.pattern (false, 3), d.patternFromText ("C C C C")));
    CHECK (c.lastLoaded (PresetType::Calibration) == cal);

    // A pattern preset lands in the selected slot only.
    c.selectSlot (5);
    c.commitPattern (false, 5, d.patternFromText ("G G"));
    const PresetEntry* pat = c.savePreset (PresetType::Pattern, "Two Gs", "", "", "", false, &error);
    REQUIRE (pat != nullptr);
    c.selectSlot (6);
    REQUIRE (c.loadPreset (*pat, &error));
    CHECK (seq::samePattern (c.pattern (false, 6), c.pattern (false, 5)));
    CHECK (seq::samePattern (c.pattern (false, 3), d.patternFromText ("C C C C")));

    // A scene replaces the bank and names the scene.
    const PresetEntry* scene = c.savePreset (PresetType::Scene, "Whole Thing", "me", "Seq", "", false, &error);
    REQUIRE (scene != nullptr);
    CHECK (c.sceneName() == "Whole Thing");
    c.clearPattern (false, 3);
    c.setParam (kCutoff, 1.0f);
    REQUIRE (c.loadPreset (*scene, &error));
    CHECK (c.getParam (kCutoff) == 20.0f);
    CHECK (seq::samePattern (c.pattern (false, 3), d.patternFromText ("C C C C")));
    CHECK (c.sceneName() == "Whole Thing");

    // A synth preset leaves the bank and the performance controls alone.
    const PresetEntry* synth = c.savePreset (PresetType::Synth, "Sound", "", "", "", false, &error);
    REQUIRE (synth != nullptr);
    c.clearPattern (false, 3);
    c.setParam (kSeqRun, 0.0f);
    c.setParam (kCutoff, 2.0f);
    REQUIRE (c.loadPreset (*synth, &error));
    CHECK (c.getParam (kCutoff) == 20.0f);
    CHECK (c.getParam (kSeqRun) == 0.0f);
    CHECK (c.pattern (false, 3).steps[0].state == seq::StepState::Rest);   // the cleared slot stays cleared

    // A template preset lands in the selected slot's generator.
    seq::GenParams g = c.genParams (false, 6);
    g.noteMask = 0x0AB5;
    g.root = 4;
    c.setGenParams (false, 6, g);
    c.selectSlot (6);
    const PresetEntry* tmpl = c.savePreset (PresetType::Template, "Mask", "", "", "", false, &error);
    REQUIRE (tmpl != nullptr);
    c.selectSlot (7);
    REQUIRE (c.loadPreset (*tmpl, &error));
    CHECK (c.genParams (false, 7).noteMask == 0x0AB5);
    CHECK (c.genParams (false, 7).root == 4);
    CHECK (c.genParams (false, 5).noteMask != 0x0AB5);

    SECTION ("previous and next walk the filtered list")
    {
        c.savePreset (PresetType::Calibration, "Trim B", "", "", "", false, &error);
        c.savePreset (PresetType::Calibration, "Trim C", "", "", "", false, &error);
        PresetFilter f;
        f.type = PresetType::Calibration;
        const PresetEntry* a = c.loadNextPreset (f, 1);
        REQUIRE (a != nullptr);
        const PresetEntry* b = c.loadNextPreset (f, 1);
        REQUIRE (b != nullptr);
        CHECK (a != b);
        const PresetEntry* back = c.loadNextPreset (f, -1);
        CHECK (back == a);
    }

    SECTION ("audition reverts everything unless kept")
    {
        c.setParam (kCutoff, 55.0f);
        c.commitPattern (false, 9, d.patternFromText ("A A A"));
        const std::string before = d.stateText();
        c.beginAudition();
        REQUIRE (c.loadPreset (*scene, &error));
        CHECK (c.getParam (kCutoff) == 20.0f);
        c.endAudition (false);
        CHECK (c.getParam (kCutoff) == 55.0f);
        CHECK (seq::samePattern (c.pattern (false, 9), d.patternFromText ("A A A")));
        CHECK (d.stateText() == before);

        c.beginAudition();
        REQUIRE (c.loadPreset (*scene, &error));
        c.endAudition (true);
        CHECK (c.getParam (kCutoff) == 20.0f);
    }

    SECTION ("import and export through the controller")
    {
        const auto file = scratch.dir / "dropped.mw12f";
        REQUIRE (c.exportPreset (*cal, file, &error));
        const PresetEntry* imported = c.importPresetFile (file, &error);
        // the same name already exists in the user library: refused, not overwritten
        CHECK (imported == nullptr);
        PresetData renamed;
        REQUIRE (c.presets().load (*cal, renamed, error));
        renamed.name = "Dropped";
        std::ofstream (file) << writePreset (renamed);
        imported = c.importPresetFile (file, &error);
        REQUIRE (imported != nullptr);
        CHECK (imported->type == PresetType::Calibration);
        CHECK (imported->name == "Dropped");
    }

    SECTION ("init scene is the factory default scene, or the table defaults")
    {
        c.initScene();
        CHECK (c.sceneName() == "Init");
        mw12FProcessor fresh;                 // what a new instance holds
        fresh.handlePendingMessages();
        CHECK (c.getParam (kCutoff) == fresh.controller().getParam (kCutoff));
        CHECK (seq::samePattern (c.pattern (false, 3), fresh.controller().pattern (false, 3)));
        CHECK (seq::samePattern (c.pattern (false, 0), fresh.controller().pattern (false, 0)));
    }
}

TEST_CASE ("presets: the factory table is embedded and the default scene loads when present", "[plugin][presets][factory]")
{
    ScratchConfigDir scratch;
    const auto files = factoryFiles();
    for (const auto& f : files)
    {
        INFO (f.path);
        PresetData d;
        CHECK (parsePreset (f.text(), d).ok);
    }
    mw12FProcessor fresh;                     // a new instance, as a host makes it
    fresh.handlePendingMessages();
    if (const FactoryFile* def = findFactoryFile ("default.mw12f"))
    {
        PresetData expected;
        REQUIRE (parsePreset (def->text(), expected).ok);
        for (int i = 0; i < kNumParams; ++i)
            if (expected.hasParam[static_cast<std::size_t> (i)])
                CHECK (fresh.controller().getParam (static_cast<ParamId> (i)) == expected.params[static_cast<std::size_t> (i)]);
        CHECK (fresh.controller().sceneName() == expected.name);
        for (const auto& slot : expected.seqSlots)
            CHECK (seq::samePattern (fresh.controller().pattern (false, slot.slot), slot.pattern));
    }
    else
    {
        CHECK (fresh.controller().getParam (kCutoff) == 35.0f);
    }
    // Every embedded preset other than states appears in the catalogue.
    std::size_t presets = 0;
    for (const auto& f : files)
    {
        PresetData p;
        if (parsePreset (f.text(), p).ok && p.type != PresetType::State) ++presets;
    }
    PresetFilter factoryOnly;
    factoryOnly.includeUser = false;
    CHECK (fresh.controller().presets().list (factoryOnly).size() == presets);
}
