// Independent QA (user-experience lens) review tests. Each case reproduces a defect found by
// driving the real editor over a real processor under Xvfb; a failing case is a finding.
// The cases run headless: nothing is put on the desktop.
//
// Built against the processor sources plus the editor (MW_UI_SOURCES) and the embedded fonts,
// like mwacid_plugin_review_editor_tests. tests/plugin/CMakeLists.txt does not list this file;
// the QA build adds it as mwacid_plugin_review_qa_ux_tests.
#include <juce_audio_utils/juce_audio_utils.h>
#include "mw12FEditor.h"
#include "PluginTestSupport.h"
#include "acid/seq/Generator.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace ptest;

namespace
{
int countNotes (const seq::Pattern& p)
{
    int n = 0;
    for (int i = 0; i < p.length; ++i)
        n += p.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note;
    return n;
}

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
};
}

// The generator header (acid/seq/Generator.h, "Hand edits") says a direct grid edit of a
// generated pattern must be followed by keepEdits, "otherwise the next applyDensities writes
// the generator's state, pitch and flags back over the edit". The editor's grid edits go
// through ControllerPatternModel::applyPattern -> Controller::commitPattern, which never calls
// it, so any density knob move silently reverts every hand edit made since the last Draw.
// Seen in the editor: Draw, click a note away and draw one on another row, nudge ACC from 50
// to 52 %: both edits are gone (qa-ux/out/05, script 05-gen2.txt).
TEST_CASE ("review qa: a hand edit on a generated pattern survives a density knob move", "[review][qa][ux]")
{
    ScratchConfigDir config;
    Driver d;
    d.prepare();
    auto& c = d.processor.controller();
    const int slot = 5;

    c.generate (false, slot, 0x1234567u);
    auto drawn = c.pattern (false, slot);
    REQUIRE (countNotes (drawn) > 2);

    // What a piano-roll click does: edit a copy and commit it.
    int erased = -1;
    for (int i = 0; i < drawn.length && erased < 0; ++i)
        if (drawn.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note)
            erased = i;
    REQUIRE (erased >= 0);
    auto edited = drawn;
    edited.steps[static_cast<std::size_t> (erased)].state = seq::StepState::Rest;
    c.commitPattern (false, slot, edited);
    REQUIRE (c.pattern (false, slot).steps[static_cast<std::size_t> (erased)].state == seq::StepState::Rest);

    // Nudge the accent density by 2 %: what a small turn of the ACC knob sends.
    auto gp = c.genParams (false, slot);
    gp.amountAccent = static_cast<std::uint8_t> (gp.amountAccent + 2);
    c.setGenParams (false, slot, gp);

    CHECK (c.pattern (false, slot).steps[static_cast<std::size_t> (erased)].state == seq::StepState::Rest);
}

// Import of a file this product exported (the lossless embedded pattern is used) reports
// notesRead = 0, and the editor's notice reads "Imported 0 notes" for a 14-note pattern
// (qa-ux/out/14/n.png). The report the editor formats must describe what was imported.
TEST_CASE ("review qa: importing a pattern exported by mw12F reports the notes it holds", "[review][qa][ux]")
{
    ScratchConfigDir config;
    Driver d;
    d.prepare();
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C D# G A# C+ . G F"));
    const auto file = std::filesystem::path (config.dir) / "roundtrip.mid";
    REQUIRE (c.exportPatternMidiFile (false, 0, file));

    seq::MidiImportReport report;
    REQUIRE (c.importMidiFile (file, false, 7, &report));
    REQUIRE (countNotes (c.pattern (false, 7)) == 7);
    CHECK (report.notesRead == 7);
}

// A user voicing (dist_model 0, "Custom") is overwritten by the first model the user steps to
// and is not restored when they step back to Custom: no undo covers parameters, so the work is
// lost. Editor: edit PRE MAX (model flips to Custom, 86.5 %), press the model ">" then "<":
// PRE MAX is 72 %, the previous model's value (script 10-preset.txt).
TEST_CASE ("review qa: stepping away from a custom distortion voicing and back restores it", "[review][qa][ux]")
{
    ScratchConfigDir config;
    Driver d;
    d.prepare();
    auto& c = d.processor.controller();

    c.setParam (kDistModel, 2.0f);
    d.processor.handlePendingMessages();
    c.setParam (kDistPreMax, 86.0f);            // a hand edit: the model becomes Custom
    d.processor.handlePendingMessages();
    REQUIRE (juce::roundToInt (c.getParam (kDistModel)) == 0);

    c.setParam (kDistModel, 1.0f);              // the model ">" button
    d.processor.handlePendingMessages();
    c.setParam (kDistModel, 0.0f);              // back to Custom
    d.processor.handlePendingMessages();

    CHECK (c.getParam (kDistPreMax) == 86.0f);
}

// Clicking a bank key alone queues slot <same number> of that bank: while A-01 plays, a click
// on C (to then pick a pattern there) queues C-01 at the end of the bar, and C-01 is empty, so
// the line drops out if the user is slower than one pattern. The parity target selects the
// bank first and switches on the pattern click (docs/research/01 section 4.2: "Click Bank then
// Pattern"). Screenshot: qa-ux/out/06/06-bank-queued.png ("NEXT C-01", empty pattern shown).
TEST_CASE ("review qa: a bank key click alone does not change the playing selection", "[review][qa][ux]")
{
    EditorRig r;
    auto& c = r.controller();
    c.selectSlot (0);
    auto& selector = r.editor->getPlate().sequencer.selector;
    REQUIRE (selector.getWidth() > 0);

    // Bank C's key: the third of four keys across the top row.
    const juce::Point<float> bankC (selector.getWidth() * 0.62f, selector.getHeight() * 0.12f);
    const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), bankC, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f,
                                  0.0f, 0.0f, 0.0f, &selector, &selector, juce::Time::getCurrentTime(), bankC, juce::Time::getCurrentTime(), 1, false);
    selector.mouseDown (click);

    CHECK (c.selectedSlot() == 0);
}
