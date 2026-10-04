#pragma once
// Rotary controls: the panel Knob in three sizes and the calibration Trimmer.
// Both are juce::Slider subclasses (see DragSlider.h for the interaction rules).
#include "kit/DragSlider.h"

namespace acid::ui
{
/** Panel knob with a value arc and a caption underneath.
    Preferred sizes: Large 80 x 86, Small 62 x 68, Mini 46 x 54 (metrics::kKnob*).
    The knob draws its whole appearance itself (track, cap, arc, caption): the enclosing panel
    buffers it, so showing or hiding a knob never touches the plate's static artwork. */
class Knob final : public DragSlider
{
public:
    enum class Size { Large, Small, Mini };

    explicit Knob (Size size = Size::Large, const juce::String& caption = {});

    Size getKnobSize() const noexcept { return size; }
    static juce::Point<int> preferredSize (Size) noexcept;

    void paint (juce::Graphics&) override;

private:
    struct Geometry { float arcR, capR, thickness, labelH, font; };
    const Geometry& geometry() const noexcept;
    juce::Point<float> centre() const noexcept;

    Size size;
};

/** Screwdriver trimmer for the calibration bay: slotted rotor, designator and live value
    to its right. Preferred size 68 x 30. Drawn for a dark bay background. */
class Trimmer final : public DragSlider
{
public:
    explicit Trimmer (const juce::String& caption = {});

    static juce::Point<int> preferredSize() noexcept { return { 68, 30 }; }
    void paint (juce::Graphics&) override;
};
}
