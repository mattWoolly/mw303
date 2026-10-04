// QA parity review: editor features the parity checklist (docs/research/01, "Parity checklist")
// and docs/SPEC.md section 2 promise, checked against a real editor over a real processor.
// Every case here failed when it was written; each one names the checklist item it covers.
// The full table is docs/parity.md.
//
// Not wired into tests/plugin/CMakeLists.txt (that list is explicit). Build it the way the QA
// pass did: a CMake hook deferred to the end of the top-level configure that calls
// mw_plugin_test_app (mwacid_plugin_review_qa_parity_tests TestMain.cpp
// review_qa_parity_editor.cpp ${MW_UI_SOURCES}) with plugin/ui on the include path and
// mwacid_assets linked, exactly like mwacid_plugin_review_editor_tests.
#include <juce_audio_utils/juce_audio_utils.h>
#include "mw12FEditor.h"
#include "PluginTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace ptest;
using namespace acid::ui;

namespace
{
struct QaEditorRig
{
    ScratchConfigDir config;
    Driver driver;
    std::unique_ptr<mw12FEditor> editor;

    QaEditorRig()
    {
        driver.prepare();
        editor.reset (dynamic_cast<mw12FEditor*> (driver.processor.createEditor()));
        REQUIRE (editor != nullptr);
        editor->setZoom (1.0f);
    }

    ~QaEditorRig() { editor.reset(); }

    acid::plugin::Controller& controller() { return editor->getController(); }

    void settle()
    {
        driver.processor.handlePendingMessages();
        editor->tick();
    }
};

float peakOf (const Rendered& r)
{
    float peak = 0.0f;

    for (auto v : r.left)
        peak = std::max (peak, std::abs (v));

    return peak;
}

bool offersMidiLearn (EditorPlate& plate, acid::ParamId id)
{
    auto& c = plate.getControls();
    return c.findSlider (id) != nullptr || c.findButton (id) != nullptr || c.findComboBox (id) != nullptr;
}
}

// Checklist 68 (SHOULD): Keyboard view Note Preview. StepKeyboard draws a PREVIEW button and
// calls PatternEditModel::previewNote on note entry, but the editor's ControllerPatternModel
// keeps the base class's empty implementation, so the button does nothing.
TEST_CASE ("qa parity: keyboard-view note preview sounds the entered note", "[qa][parity][editor]")
{
    QaEditorRig r;
    auto& c = r.controller();
    c.setParam (acid::kPlayMode, 1.0f);   // SEQ, sequencer stopped
    c.setParam (acid::kSeqRun, 0.0f);
    r.settle();

    // Silence first: nothing is playing.
    r.driver.playHead.playing = false;
    const auto before = r.driver.render (4800, 480);
    REQUIRE (peakOf (before) < 1.0e-4f);

    // What StepKeyboard::enterNote does with PREVIEW lit: ask the model to audition the note.
    r.editor->getPatternModel().previewNote (45, false, true);   // held, as while the key is down
    r.settle();

    const auto after = r.driver.render (9600, 480);
    CHECK (peakOf (after) > 1.0e-3f);
}

// Checklist 60 (MUST): MIDI-CC pattern control, learnable select pattern 1..12 and bank A..D.
// pattern_bank and pattern_slot are host parameters, but no control in the editor is bound to
// them (the PatternSelector carries no ParamHooks), so right-click MIDI Learn cannot be armed
// for them. RUN (seq_run) is learnable.
TEST_CASE ("qa parity: pattern bank and pattern slot can be MIDI-learned from the editor", "[qa][parity][editor]")
{
    QaEditorRig r;
    auto& plate = r.editor->getPlate();

    CHECK (offersMidiLearn (plate, acid::kSeqRun));
    CHECK (offersMidiLearn (plate, acid::kPatternBank));
    CHECK (offersMidiLearn (plate, acid::kPatternSlot));
}

// Checklist 80 and 81 (MUST): drag a pattern in native format to disk or another instance,
// and drag-and-drop load of one or many patterns onto pattern slots. The only drag source in
// the editor is the MIDI drag handle, and nothing in the editor accepts a file drop.
TEST_CASE ("qa parity: pattern files can be dropped onto the pattern selector", "[qa][parity][editor]")
{
    QaEditorRig r;
    auto& plate = r.editor->getPlate();

    const auto accepts = [] (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (dynamic_cast<juce::FileDragAndDropTarget*> (c) != nullptr)
                return true;

        return false;
    };

    CHECK (accepts (&plate.sequencer.selector));
}

// SPEC section 2 (Sequencer): "note, tie, rest, octave, accent, slide, vibrato, probability and
// ratchet per step". The piano roll and the keyboard view edit every one of these except the
// ratchet (and the per-step gate, Step::gate). Editing step 1 through every editor path leaves
// Step::ratchet at 1; the only way in is pasting compact step text. This case pins the gap
// through the keyboard view, whose flags are real buttons; the piano roll is one component
// whose lanes (PianoRoll.h: header, octave, notes, attributes, probability) have no ratchet row.
TEST_CASE ("qa parity: the step editors expose a ratchet control", "[qa][parity][editor]")
{
    QaEditorRig r;
    auto& keys = r.editor->getPlate().sequencer.keys;

    const auto hasChildNamed = [] (juce::Component& parent, const juce::String& text)
    {
        for (auto* child : parent.getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (child))
                if (b->getButtonText().containsIgnoreCase (text))
                    return true;

        return false;
    };

    // The keyboard view's flag buttons are ACCENT, SLIDE, VIBRATO, TIE, REST: no RATCHET.
    REQUIRE (hasChildNamed (keys, "ACCENT"));   // the lookup works on the buttons that exist
    CHECK (hasChildNamed (keys, "RATCHET"));
}
