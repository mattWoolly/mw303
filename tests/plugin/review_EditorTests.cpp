// Independent review tests for the editor (plugin/ui). Each case states what the contract
// (docs/SPEC.md sections 5, 6.6, 6.7 and docs/research/07 section 6) or the editor's own header
// promises; a failing case is a finding, with the reproduction in the case itself.
//
// Built against the editor sources (MW_UI_SOURCES) as mwacid_plugin_review_editor_tests. The
// cases run headless: nothing is put on the desktop.
#include <juce_audio_utils/juce_audio_utils.h>
#include <sstream>
#include "kit/Faceplate.h"
// The selector keeps its used-slot mask private and has no getter: read it for the check.
#define private public
#include "seq/PatternSelector.h"
#undef private
#include "mw12FEditor.h"
#include "PluginTestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace ptest;
using namespace acid::ui;

namespace
{
struct EditorRig
{
    ScratchConfigDir config;
    Driver driver;
    std::unique_ptr<mw12FEditor> editor;

    EditorRig()
    {
        driver.prepare();
        editor.reset (dynamic_cast<mw12FEditor*> (driver.processor.createEditor()));
        REQUIRE (editor != nullptr);
        editor->setZoom (1.0f);
    }

    ~EditorRig() { editor.reset(); }

    acid::plugin::Controller& controller() { return editor->getController(); }

    void settle()
    {
        driver.processor.handlePendingMessages();
        editor->tick();
    }
};

std::uint16_t usedMask (acid::plugin::Controller& c, bool arp, int bank)
{
    std::uint16_t m = 0;

    for (int s = 0; s < seq::kSlotsPerBank; ++s)
    {
        const auto p = c.pattern (arp, bank * seq::kSlotsPerBank + s);

        for (int i = 0; i < p.length; ++i)
            if (p.steps[static_cast<std::size_t> (i)].state != seq::StepState::Rest)
            {
                m = static_cast<std::uint16_t> (m | (1u << s));
                break;
            }
    }

    return m;
}

void putNote (acid::plugin::Controller& c, bool arp, int slot)
{
    auto p = c.pattern (arp, slot);
    p.steps[0].state = seq::StepState::Note;
    c.commitPattern (arp, slot, p);
}
}

TEST_CASE ("review editor: the selector's used-slot dots follow the bank the play mode edits", "[review][editor]")
{
    EditorRig r;
    auto& c = r.controller();
    putNote (c, false, 0);
    putNote (c, false, 1);
    putNote (c, true, 5);
    r.settle();

    auto& selector = r.editor->getPlate().sequencer.selector;
    REQUIRE (selector.used[0] == usedMask (c, false, 0));

    // Switch to the arpeggiator: the step editors now show the arp bank, so must the dots.
    c.setPlayMode (seq::PlayMode::Arpeggiator);
    r.settle();
    CHECK (r.editor->getPatternModel().isArp());
    CHECK (selector.used[0] == usedMask (c, true, 0));   // fails: still the sequencer bank's mask
}

TEST_CASE ("review editor: closing the editor while a live override is held releases it", "[review][editor]")
{
    EditorRig r;
    auto& c = r.controller();
    auto* set = dynamic_cast<PushButton*> (r.editor->getPlate().getControls().findButton (acid::kOvrAccentSet));
    REQUIRE (set != nullptr);

    set->setState (juce::Button::buttonDown);
    REQUIRE (c.getParam (acid::kOvrAccentSet) >= 0.5f);

    r.editor.reset();   // the host closes the window with the mouse still down
    CHECK (c.getParam (acid::kOvrAccentSet) < 0.5f);   // fails: the override stays forced on, and is saved in the session
}

TEST_CASE ("review editor: CANCEL in the preset browser does not revert a host state load", "[review][editor]")
{
    EditorRig r;
    auto& c = r.controller();

    c.setParam (acid::kCutoff, 90.0f);
    juce::MemoryBlock project;
    r.driver.processor.getStateInformation (project);   // the host's saved project: cutoff 90
    c.setParam (acid::kCutoff, 10.0f);

    r.editor->openPresetBrowser (acid::PresetType::Synth);
    r.driver.processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));   // host reloads the project
    r.settle();
    REQUIRE (c.getParam (acid::kCutoff) == 90.0f);

    r.editor->getPresetBrowser().close (false);   // the user cancels the browser
    CHECK (c.getParam (acid::kCutoff) == 90.0f);   // fails: the audition snapshot (cutoff 10) overwrites the host's load
}

TEST_CASE ("review editor: loading a preset of another type does not move every knob's double-click value", "[review][editor]")
{
    EditorRig r;
    auto& c = r.controller();
    c.setParam (acid::kCutoff, 80.0f);

    acid::plugin::PresetFilter f;
    f.type = acid::PresetType::Delay;
    const auto delays = c.presets().list (f);
    REQUIRE (! delays.empty());
    REQUIRE (c.loadPreset (*delays.front()));
    r.settle();

    // The delay preset carries no cutoff; the knob's double-click target should not become
    // whatever the cutoff happened to be when an unrelated preset was loaded.
    CHECK (r.editor->getPlate().synth.cutoff.getDoubleClickReturnValue() != 80.0);   // fails: 80, double-click is a no-op
}

TEST_CASE ("review editor: a UI click right after a host resize keeps the host's size", "[review][editor]")
{
    EditorRig r;
    r.editor->setSize (1500, 1119);   // the host drags the window larger (aspect kept)
    r.editor->tick();
    r.editor->getPlate().sequencer.setView (SequencerPanel::View::Keyboard);   // a click 1/30 s later
    r.editor->tick();
    CHECK (r.editor->getWidth() == 1500);   // fails: kChangedUi re-applies the stored zoom, the window snaps back to 1180
}

TEST_CASE ("review editor: the plate fits a window the host sized without the aspect ratio", "[review][editor]")
{
    EditorRig r;
    r.editor->setSize (1500, 900);
    const auto plateArea = r.editor->getLocalArea (&r.editor->getPlate(), r.editor->getPlate().getLocalBounds());
    CHECK (plateArea.getBottom() <= r.editor->getHeight());   // fails: scaled by width only, the bottom 219 px are cut off
}

TEST_CASE ("review editor: a parameter change does not re-render the whole static layer", "[review][editor]")
{
    EditorRig r;
    r.editor->setZoom (2.0f);
    juce::Image canvas (juce::Image::ARGB, r.editor->getWidth(), r.editor->getHeight(), true);
    const auto paintAll = [&] { juce::Graphics g (canvas); r.editor->paintEntireComponent (g, false); };
    paintAll();
    const auto before = r.editor->getPlate().getStaticRenderCount();

    // A host automating tune_ext (or play_mode): docs/research/07 section 6.3 rule 2.
    r.controller().setParam (acid::kTuneExt, 1.0f);
    paintAll();
    CHECK (r.editor->getPlate().getStaticRenderCount() == before);   // fails: the whole plate's art is rebuilt (about 90 ms at 200 %)
}

TEST_CASE ("review editor: full repaint at 200 % within the 8 ms budget", "[review][editor][budget]")
{
    EditorRig r;
    r.editor->setZoom (2.0f);
    juce::Image canvas (juce::Image::ARGB, r.editor->getWidth(), r.editor->getHeight(), true);
    const auto paintAll = [&] { juce::Graphics g (canvas); r.editor->paintEntireComponent (g, false); };
    paintAll();   // builds the static layer

    const int n = 20;
    const auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < n; ++i)
        paintAll();

    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / n;
    INFO ("full repaint at 200 %: " << ms << " ms");
    CHECK (ms < 8.0);   // fails: about 36 ms on this machine (docs/research/07 section 6.3 rule 5)
}
