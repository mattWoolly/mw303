#pragma once
// The interface between the step editors and whoever owns the pattern.
//
// The editors never mutate a pattern in place. They read the current one, edit a copy and
// hand it back through applyPattern(); the owner (the plugin's message-thread model)
// stores it, publishes a snapshot to the audio thread and calls notifyPatternChanged().
// That keeps the SPEC section 5 rule (whole snapshots, never in-place edits) out of the
// editors' way and makes undo a matter of keeping the copies.
//
// The play position and selection live here too, so the piano roll, the keyboard editor
// and the generator panel stay in step with each other.
#include "acid/seq/Pattern.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace acid::ui
{
class PatternEditModel
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /** The pattern's content, or which pattern is being edited, changed. */
        virtual void patternChanged() {}
        /** The playing step moved. Either argument may be -1 (stopped). */
        virtual void playStepChanged (int /*previousStep*/, int /*newStep*/) {}
        virtual void selectedStepChanged (int /*previousStep*/, int /*newStep*/) {}
    };

    virtual ~PatternEditModel() = default;

    /** The pattern being edited. The reference stays valid until the next applyPattern or pattern switch. */
    virtual const seq::Pattern& getPattern() const = 0;

    /** Replaces the edited pattern with a modified copy. Implementations call notifyPatternChanged(). */
    virtual void applyPattern (const seq::Pattern& edited) = 0;

    /** True when the pattern is an arpeggiator template (steps address held notes by Step::arpIndex). */
    virtual bool isArpPattern() const = 0;

    /** Step currently sounding, 0-based, or -1 when stopped. */
    virtual int getPlayStep() const = 0;

    /** Step selected for keyboard-style editing, 0-based. */
    virtual int getSelectedStep() const = 0;
    virtual void setSelectedStep (int step) = 0;

    /** Bracket a mouse gesture (a drag that applies many copies) so the owner can coalesce undo. */
    virtual void beginEditGesture() {}
    virtual void endEditGesture() {}

    /** Optional: audition a note while editing (the keyboard view's PREVIEW). With `held` the
        note sounds until releasePreview() (a key press, released with the mouse); without, the
        model ends it after a short time (a gesture that has no release, such as a click on an
        octave button). A new preview note ends the one before. */
    virtual void previewNote (int /*midiNote*/, bool /*accent*/, bool /*held*/) {}
    /** Ends the held preview note, if one is sounding. */
    virtual void releasePreview() {}

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

protected:
    void notifyPatternChanged()                        { listeners.call ([] (Listener& l) { l.patternChanged(); }); }
    void notifyPlayStepChanged (int previous, int now) { listeners.call ([=] (Listener& l) { l.playStepChanged (previous, now); }); }
    void notifySelectedStepChanged (int previous, int now) { listeners.call ([=] (Listener& l) { l.selectedStepChanged (previous, now); }); }

private:
    juce::ListenerList<Listener> listeners;
};

/** A self-contained model holding one pattern. The gallery's kit sheets use it; the editor
    uses ControllerPatternModel, backed by the plugin's pattern bank. */
class LocalPatternModel final : public PatternEditModel
{
public:
    LocalPatternModel() = default;

    const seq::Pattern& getPattern() const override { return pattern; }
    void applyPattern (const seq::Pattern& edited) override { pattern = edited; notifyPatternChanged(); }
    bool isArpPattern() const override { return arp; }
    int getPlayStep() const override { return playStep; }
    int getSelectedStep() const override { return selectedStep; }

    void setSelectedStep (int step) override
    {
        const auto clamped = juce::jlimit (0, juce::jmax (0, static_cast<int> (pattern.length) - 1), step);

        if (clamped != selectedStep)
        {
            const auto previous = selectedStep;
            selectedStep = clamped;
            notifySelectedStepChanged (previous, clamped);
        }
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

    void setArpPattern (bool isArp) { arp = isArp; notifyPatternChanged(); }

private:
    seq::Pattern pattern {};
    int playStep = -1, selectedStep = 0;
    bool arp = false;
};
}
