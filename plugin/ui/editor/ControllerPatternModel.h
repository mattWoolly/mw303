#pragma once
// PatternEditModel over the plugin's Controller: the step editors read a cached copy of the
// slot being edited and hand edited copies to Controller::commitPattern, which sanitises,
// records undo and publishes to the audio thread. The editor refreshes the copy whenever the
// controller reports a pattern change (kChangedPatterns) and moves the play step from
// EngineStatus at 30 Hz.
#include "seq/PatternEditModel.h"
#include "Controller.h"
#include "acid/seq/PatternOps.h"

namespace acid::ui
{
class ControllerPatternModel final : public PatternEditModel, private juce::Timer
{
public:
    explicit ControllerPatternModel (plugin::Controller& c) : controller (c)
    {
        refresh();
    }

    ~ControllerPatternModel() override
    {
        stopTimer();
        controller.stopPreview();
    }

    /** The keyboard view's PREVIEW: the note sounds through the voice (Controller::previewNote,
        lock-free into the processor's MIDI input; only while the sequencer is stopped). A held
        note ends at releasePreview(), a one-shot after kPreviewMs; either ends when the next
        preview note starts. kMaxHeldMs is a backstop for a release that never comes. */
    void previewNote (int midiNote, bool accent, bool held) override
    {
        if (previewing >= 0)
            controller.previewNote (previewing, false, false);

        previewing = midiNote;
        controller.previewNote (midiNote, accent, true);
        startTimer (held ? kMaxHeldMs : kPreviewMs);
    }

    void releasePreview() override
    {
        stopTimer();

        if (previewing >= 0)
            controller.previewNote (previewing, false, false);

        previewing = -1;
    }

    /** The preview note sounding, or -1 (for tests). */
    int getPreviewNote() const noexcept { return previewing; }

    const seq::Pattern& getPattern() const override { return current; }
    bool isArpPattern() const override { return arp; }
    int getPlayStep() const override { return playStep; }
    int getSelectedStep() const override { return selectedStep; }

    void applyPattern (const seq::Pattern& edited) override
    {
        // The controller notifies the editor synchronously; the editor calls refresh(), which
        // picks the sanitised result up and tells the step editors.
        controller.commitPattern (arp, slot, edited);
    }

    void setSelectedStep (int step) override
    {
        const auto clamped = juce::jlimit (0, juce::jmax (0, static_cast<int> (current.length) - 1), step);

        if (clamped != selectedStep)
        {
            const auto previous = selectedStep;
            selectedStep = clamped;
            notifySelectedStepChanged (previous, clamped);
        }
    }

    void beginEditGesture() override { controller.beginGesture(); }
    void endEditGesture() override { controller.endGesture(); }

    /** Which slot the editors show. */
    void setSlot (bool isArp, int slotIndex)
    {
        if (isArp == arp && slotIndex == slot)
            return;

        arp = isArp;
        slot = slotIndex;
        current = controller.pattern (arp, slot);
        selectedStep = juce::jlimit (0, juce::jmax (0, static_cast<int> (current.length) - 1), selectedStep);
        notifyPatternChanged();
    }

    bool isArp() const noexcept { return arp; }
    int getSlot() const noexcept { return slot; }

    /** Re-reads the slot; notifies only when something differs. */
    void refresh()
    {
        auto p = controller.pattern (arp, slot);

        if (seq::samePattern (p, current))
            return;

        current = p;
        selectedStep = juce::jlimit (0, juce::jmax (0, static_cast<int> (current.length) - 1), selectedStep);
        notifyPatternChanged();
    }

    void setPlayStep (int step)
    {
        if (step != playStep)
        {
            const auto previous = playStep;
            playStep = step;
            notifyPlayStepChanged (previous, step);
        }
    }

private:
    static constexpr int kPreviewMs = 220, kMaxHeldMs = 10000;

    void timerCallback() override { releasePreview(); }

    plugin::Controller& controller;
    int previewing = -1;
    seq::Pattern current {};
    bool arp = false;
    int slot = 0, playStep = -1, selectedStep = 0;
};
}
