#pragma once
// Parameter attachments for the editor that never touch the audio thread beyond one relaxed
// atomic store.
//
// JUCE's ParameterAttachment (behind the APVTS Slider, Button and ComboBox attachments) is an
// AsyncUpdater: when a host automates a parameter from its audio thread, the listener posts a
// message there, which allocates, locks and writes to the message pipe. These classes keep
// JUCE's behaviour (the same ranges, text functions, gestures and notifications) but replace
// the async update: a change made on the message thread is applied at once, as JUCE does, and
// a change made on any other thread only sets a flag that the editor's 30 Hz timer collects
// with poll(). The message thread is the one that created the attachment (the editor's).
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace acid::ui
{
class PolledParameterAttachment final : private juce::AudioProcessorParameter::Listener
{
public:
    /** `onChange` gets the value in natural (denormalised) units, on the message thread. */
    PolledParameterAttachment (juce::RangedAudioParameter&, std::function<void (float)> onChange);
    ~PolledParameterAttachment() override;

    void sendInitialUpdate();
    void setValueAsCompleteGesture (float natural);
    void beginGesture();
    void setValueAsPartOfGesture (float natural);
    void endGesture();

    /** Applies a change another thread made since the last call. Message thread. */
    void poll();

    juce::RangedAudioParameter& getParameter() noexcept { return parameter; }

private:
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    void deliver();
    template <typename Fn> void ifChanged (float natural, Fn&&);

    juce::RangedAudioParameter& parameter;
    std::function<void (float)> onChange;
    const juce::Thread::ThreadID messageThread;
    std::atomic<bool> pending { false };
};

class PolledSliderAttachment final : private juce::Slider::Listener
{
public:
    PolledSliderAttachment (juce::RangedAudioParameter&, juce::Slider&);
    ~PolledSliderAttachment() override;
    void poll() { attachment.poll(); }

private:
    void setValue (float);
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override { attachment.beginGesture(); }
    void sliderDragEnded (juce::Slider*) override { attachment.endGesture(); }

    juce::Slider& slider;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
};

class PolledButtonAttachment final : private juce::Button::Listener
{
public:
    PolledButtonAttachment (juce::RangedAudioParameter&, juce::Button&);
    ~PolledButtonAttachment() override;
    void poll() { attachment.poll(); }

private:
    void setValue (float);
    void buttonClicked (juce::Button*) override;

    juce::Button& button;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
};

class PolledComboBoxAttachment final : private juce::ComboBox::Listener
{
public:
    PolledComboBoxAttachment (juce::RangedAudioParameter&, juce::ComboBox&);
    ~PolledComboBoxAttachment() override;
    void poll() { attachment.poll(); }

private:
    void setValue (float);
    void comboBoxChanged (juce::ComboBox*) override;

    juce::ComboBox& comboBox;
    juce::RangedAudioParameter& parameter;
    PolledParameterAttachment attachment;
    bool ignoreCallbacks = false;
};
}
