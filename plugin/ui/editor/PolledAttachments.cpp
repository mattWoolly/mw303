#include "editor/PolledAttachments.h"

namespace acid::ui
{
//==============================================================================
PolledParameterAttachment::PolledParameterAttachment (juce::RangedAudioParameter& p, std::function<void (float)> callback)
    : parameter (p), onChange (std::move (callback)), messageThread (juce::Thread::getCurrentThreadId())
{
    parameter.addListener (this);
}

PolledParameterAttachment::~PolledParameterAttachment()
{
    parameter.removeListener (this);
}

void PolledParameterAttachment::sendInitialUpdate()
{
    deliver();
}

template <typename Fn>
void PolledParameterAttachment::ifChanged (float natural, Fn&& fn)
{
    const auto normalised = parameter.convertTo0to1 (natural);

    if (! juce::approximatelyEqual (parameter.getValue(), normalised))
        fn (normalised);
}

void PolledParameterAttachment::setValueAsCompleteGesture (float natural)
{
    ifChanged (natural, [this] (float v)
    {
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (v);
        parameter.endChangeGesture();
    });
}

void PolledParameterAttachment::beginGesture()
{
    parameter.beginChangeGesture();
}

void PolledParameterAttachment::setValueAsPartOfGesture (float natural)
{
    ifChanged (natural, [this] (float v) { parameter.setValueNotifyingHost (v); });
}

void PolledParameterAttachment::endGesture()
{
    parameter.endChangeGesture();
}

void PolledParameterAttachment::parameterValueChanged (int, float)
{
    // Any thread. On the message thread the control follows at once, as with JUCE's own
    // attachment; anywhere else (a host's audio thread) only the flag is set: no message, no
    // allocation, no lock.
    if (juce::Thread::getCurrentThreadId() == messageThread)
    {
        pending.store (false, std::memory_order_relaxed);
        deliver();
    }
    else
    {
        pending.store (true, std::memory_order_release);
    }
}

void PolledParameterAttachment::poll()
{
    if (pending.exchange (false, std::memory_order_acquire))
        deliver();
}

void PolledParameterAttachment::deliver()
{
    if (onChange != nullptr)
        onChange (parameter.convertFrom0to1 (parameter.getValue()));
}

//==============================================================================
PolledSliderAttachment::PolledSliderAttachment (juce::RangedAudioParameter& param, juce::Slider& s)
    : slider (s), attachment (param, [this] (float f) { setValue (f); })
{
    // As juce::SliderParameterAttachment: the parameter's text, default and range.
    slider.valueFromTextFunction = [&param] (const juce::String& text) { return static_cast<double> (param.convertFrom0to1 (param.getValueForText (text))); };
    slider.textFromValueFunction = [&param] (double value) { return param.getText (param.convertTo0to1 (static_cast<float> (value)), 0); };
    slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));

    auto range = param.getNormalisableRange();
    juce::NormalisableRange<double> newRange {
        static_cast<double> (range.start), static_cast<double> (range.end),
        [range] (double start, double end, double normalised) mutable
        {
            range.start = static_cast<float> (start);
            range.end = static_cast<float> (end);
            return static_cast<double> (range.convertFrom0to1 (static_cast<float> (normalised)));
        },
        [range] (double start, double end, double mapped) mutable
        {
            range.start = static_cast<float> (start);
            range.end = static_cast<float> (end);
            return static_cast<double> (range.convertTo0to1 (static_cast<float> (mapped)));
        },
        [range] (double start, double end, double mapped) mutable
        {
            range.start = static_cast<float> (start);
            range.end = static_cast<float> (end);
            return static_cast<double> (range.snapToLegalValue (static_cast<float> (mapped)));
        }
    };
    newRange.interval = range.interval;
    newRange.skew = range.skew;
    newRange.symmetricSkew = range.symmetricSkew;
    slider.setNormalisableRange (newRange);

    attachment.sendInitialUpdate();
    slider.valueChanged();
    slider.addListener (this);
}

PolledSliderAttachment::~PolledSliderAttachment()
{
    slider.removeListener (this);
}

void PolledSliderAttachment::setValue (float v)
{
    const juce::ScopedValueSetter<bool> guard (ignoreCallbacks, true);
    slider.setValue (v, juce::sendNotificationSync);
}

void PolledSliderAttachment::sliderValueChanged (juce::Slider*)
{
    if (! ignoreCallbacks)
        attachment.setValueAsPartOfGesture (static_cast<float> (slider.getValue()));
}

//==============================================================================
PolledButtonAttachment::PolledButtonAttachment (juce::RangedAudioParameter& param, juce::Button& b)
    : button (b), attachment (param, [this] (float f) { setValue (f); })
{
    attachment.sendInitialUpdate();
    button.addListener (this);
}

PolledButtonAttachment::~PolledButtonAttachment()
{
    button.removeListener (this);
}

void PolledButtonAttachment::setValue (float v)
{
    const juce::ScopedValueSetter<bool> guard (ignoreCallbacks, true);
    button.setToggleState (v >= 0.5f, juce::sendNotificationSync);
}

void PolledButtonAttachment::buttonClicked (juce::Button*)
{
    if (! ignoreCallbacks)
        attachment.setValueAsCompleteGesture (button.getToggleState() ? 1.0f : 0.0f);
}

//==============================================================================
PolledComboBoxAttachment::PolledComboBoxAttachment (juce::RangedAudioParameter& param, juce::ComboBox& c)
    : comboBox (c), parameter (param), attachment (param, [this] (float f) { setValue (f); })
{
    attachment.sendInitialUpdate();
    comboBox.addListener (this);
}

PolledComboBoxAttachment::~PolledComboBoxAttachment()
{
    comboBox.removeListener (this);
}

void PolledComboBoxAttachment::setValue (float v)
{
    // As juce::ComboBoxParameterAttachment: the items span the parameter's normalised range.
    const auto index = juce::roundToInt (parameter.convertTo0to1 (v) * static_cast<float> (comboBox.getNumItems() - 1));

    if (index == comboBox.getSelectedItemIndex())
        return;

    const juce::ScopedValueSetter<bool> guard (ignoreCallbacks, true);
    comboBox.setSelectedItemIndex (index, juce::sendNotificationSync);
}

void PolledComboBoxAttachment::comboBoxChanged (juce::ComboBox*)
{
    if (ignoreCallbacks)
        return;

    const auto numItems = comboBox.getNumItems();
    const auto selected = static_cast<float> (comboBox.getSelectedItemIndex());
    const auto normalised = numItems > 1 ? selected / static_cast<float> (numItems - 1) : 0.0f;
    attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
}
}
