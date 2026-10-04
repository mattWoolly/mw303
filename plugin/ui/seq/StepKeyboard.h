#pragma once
// Keyboard-style step editor: pick a step, then set its note, octave and flags with
// buttons, the way a hardware step sequencer is programmed.
//
//   step row      sixteen step keys with LEDs (red = playing, accent = selected, dim = has a note)
//   page buttons  four pages of sixteen steps
//   prev / next   move the selected step. With AUTO INC the step advances when the next note
//                 is entered, so the flags pressed after a note apply to that note
//   octave        -1, 0, +1, +2, +3
//   flags         ACCENT, SLIDE, VIBRATO, the step states TIE and REST, RATCHET (1 to 4 hits
//                 per step, a click steps it) and GATE (the pattern's gate, or 25 to 100 % of
//                 the step for this step alone)
//   keyboard      one octave; the lit key is the selected step's note. For an arpeggiator
//                 pattern the keyboard is replaced by ten held-note index keys.
//
// Works on the same PatternEditModel as the piano roll and shares its selected step.
#include "seq/PatternEditModel.h"
#include "kit/Buttons.h"

namespace acid::ui
{
class StepKeyboard final : public juce::Component, private PatternEditModel::Listener
{
public:
    explicit StepKeyboard (PatternEditModel& model);
    ~StepKeyboard() override;

    void setAutoIncrement (bool on) { autoInc.setToggleState (on, juce::dontSendNotification); }
    bool getAutoIncrement() const { return autoInc.getToggleState(); }

    /** When on, a key press sounds the entered note until the key is released, and an octave
        change or switching PREVIEW on sounds the selected step briefly
        (PatternEditModel::previewNote). */
    void setNotePreview (bool on) { preview.setToggleState (on, juce::dontSendNotification); }

    int getPage() const noexcept { return page; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void patternChanged() override;
    void playStepChanged (int previous, int now) override;
    void selectedStepChanged (int previous, int now) override;

    void refreshButtons();
    void editSelected (const std::function<void (seq::Step&)>& fn);
    void enterNote (int pitchClassOrIndex);
    void moveSelection (int delta);
    /** Sounds the selected step's note when PREVIEW is on (not for arpeggiator patterns). */
    void previewSelected (bool held);
    juce::Rectangle<float> stepKey (int column) const noexcept;
    juce::Rectangle<float> keyBounds (int index, bool& isBlack) const noexcept;
    int keyAt (juce::Point<float>) const noexcept;

    PatternEditModel& model;
    int page = 0;

    PushButton pageButtons[4];
    PushButton prevButton { {}, draw::Icon::Prev }, nextButton { {}, draw::Icon::Next };
    LedButton autoInc { "AUTO INC" }, preview { "PREVIEW" };
    LedButton octaveButtons[5] { LedButton ("-1"), LedButton ("0"), LedButton ("+1"), LedButton ("+2"), LedButton ("+3") };
    LedButton accent { "ACCENT" }, slide { "SLIDE" }, vibrato { "VIBRATO" }, tie { "TIE" }, rest { "REST" };
    LedButton ratchet { "RATCHET 1" }, gate { "GATE PAT" };

    /** AUTO INC: the step the last note went into. The next note entry advances from it first. */
    int advanceFrom = -1;
    /** A key press is sounding a held preview note: the mouse release ends it. */
    bool previewHeld = false;

    juce::Rectangle<float> stepRow, readout, keyboard, octaveCaption, flagCaption;
};
}
