#include "kit/NumericDisplay.h"

namespace acid::ui
{
NumericDisplay::NumericDisplay (const juce::String& captionText, CaptionPlacement p) : placement (p)
{
    setCaption (captionText);
    showsValueTip = false;   // the control is its own readout
    setSize (64, p == CaptionPlacement::Above ? 34 : 20);
}

void NumericDisplay::setCaptionPlacement (CaptionPlacement p)
{
    placement = p;
    repaint();
}

void NumericDisplay::setCaptionWidth (int designUnits)
{
    captionWidth = designUnits;
    repaint();
}

void NumericDisplay::setMinimumText (const juce::String& text)
{
    minimumText = text;
    repaint();
}

void NumericDisplay::setReadoutHeight (float h)
{
    readoutHeight = h;
    repaint();
}

void NumericDisplay::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    auto b = getLocalBounds().toFloat();
    const auto captionColour = t.caption (getSurface(), isEnabled());

    if (placement == CaptionPlacement::Above)
        draw::text (g, getCaption(), b.removeFromTop (13.0f), fonts::legend (10.5f), captionColour, juce::Justification::centred);
    else if (placement == CaptionPlacement::Left)
        draw::text (g, getCaption(), b.removeFromLeft (static_cast<float> (captionWidth)), fonts::legend (10.5f), captionColour,
                    juce::Justification::centredLeft);

    const auto wellArea = b.reduced (0.0f, 0.5f).withTrimmedBottom (1.0f);
    draw::well (g, wellArea, t);

    if (isHovering() || isBeingDragged())
    {
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (wellArea.reduced (0.5f), metrics::kCorner, 1.0f);
    }

    const auto atMinimum = minimumText.isNotEmpty() && getValue() <= getMinimum();
    draw::text (g, atMinimum ? minimumText : getValueText(), wellArea.reduced (3.0f, 0.0f), fonts::readout (readoutHeight),
                isEnabled() ? t.wellText : t.wellTextDim, juce::Justification::centred);
}
}
