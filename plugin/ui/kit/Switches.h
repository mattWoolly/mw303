#pragma once
// Click-to-select controls for bool and choice parameters. Both are juce::Slider subclasses
// with one integer step per position, so a SliderAttachment binds them to an
// AudioParameterBool, AudioParameterChoice or AudioParameterInt without extra glue.
// Position 0 is the first label. Clicking a position selects it; the wheel steps.
#include "kit/DragSlider.h"

namespace acid::ui
{
/** Vertical slide switch with two or three positions, labels (or icons) to the right. */
class ToggleSwitch final : public DragSlider
{
public:
    explicit ToggleSwitch (const juce::StringArray& positionLabels, const juce::String& caption = {});

    /** Replaces a position's text label with an icon (the waveform switch uses this). */
    void setPositionIcon (int position, draw::Icon icon);

    int getNumPositions() const noexcept { return labels.size(); }
    int getPosition() { return juce::roundToInt (getValue() - getMinimum()); }

    /** Height needed for the current number of positions, with or without a caption. */
    int preferredHeight() const noexcept;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> bodyArea() const noexcept;

    juce::StringArray labels;
    juce::Array<draw::Icon> icons;
};

/** Horizontal selector: a dark trough with one lit segment. Used for the play mode. */
class SegmentedSelector final : public DragSlider
{
public:
    explicit SegmentedSelector (const juce::StringArray& segmentLabels);

    int getNumSegments() const noexcept { return labels.size(); }
    int getSelectedSegment() { return juce::roundToInt (getValue() - getMinimum()); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
};
}
