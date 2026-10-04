#pragma once
// A numeric readout that is dragged vertically to change: integers (pattern length, delay
// fractions) and values with units (Hz, dB, ms). A juce::Slider, so it attaches like a knob.
#include "kit/DragSlider.h"

namespace acid::ui
{
class NumericDisplay final : public DragSlider
{
public:
    enum class CaptionPlacement { None, Above, Left };

    explicit NumericDisplay (const juce::String& caption = {}, CaptionPlacement placement = CaptionPlacement::Above);

    void setCaptionPlacement (CaptionPlacement p);

    /** Width reserved for a caption placed on the left. */
    void setCaptionWidth (int designUnits);

    /** Text shown instead of the number at the minimum value (for example "AUTH" for the stock gate length). */
    void setMinimumText (const juce::String& text);

    /** Readout font height in design units (default metrics::kReadoutHeight). */
    void setReadoutHeight (float h);

    void paint (juce::Graphics&) override;

private:
    CaptionPlacement placement;
    int captionWidth = 46;
    float readoutHeight = metrics::kReadoutHeight;
    juce::String minimumText;
};
}
