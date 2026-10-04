#include "kit/Knob.h"

namespace acid::ui
{
Knob::Knob (Size s, const juce::String& captionText) : size (s)
{
    setCaption (captionText);
    const auto pref = preferredSize (s);
    setSize (pref.x, pref.y);
}

juce::Point<int> Knob::preferredSize (Size s) noexcept
{
    switch (s)
    {
        case Size::Large: return { metrics::kKnobLargeW, metrics::kKnobLargeH };
        case Size::Small: return { metrics::kKnobSmallW, metrics::kKnobSmallH };
        case Size::Mini:  return { metrics::kKnobMiniW, metrics::kKnobMiniH };
    }

    return { metrics::kKnobLargeW, metrics::kKnobLargeH };
}

const Knob::Geometry& Knob::geometry() const noexcept
{
    static constexpr Geometry kLarge { 31.0f, 23.5f, 3.6f, 15.0f, metrics::kLegendHeight };
    static constexpr Geometry kSmall { 22.5f, 16.5f, 3.0f, 14.0f, metrics::kLegendHeight - 0.5f };
    static constexpr Geometry kMini { 16.0f, 11.0f, 2.6f, 13.0f, 10.0f };
    return size == Size::Large ? kLarge : (size == Size::Small ? kSmall : kMini);
}

juce::Point<float> Knob::centre() const noexcept
{
    const auto& geo = geometry();
    return { static_cast<float> (getWidth()) * 0.5f, geo.arcR + geo.thickness * 0.5f + 1.0f };
}

void Knob::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto& geo = geometry();
    const auto b = getLocalBounds().toFloat();

    // On the always-dark surfaces a knob is drawn in the Night colours whatever the theme,
    // so its value arc stays readable.
    const auto& knobTheme = getSurface() == Surface::Dark ? theme (ThemeId::Night) : t;
    draw::knobTrack (g, centre(), geo.arcR, geo.thickness, knobTheme);
    draw::knobValue (g, centre(), geo.capR, geo.arcR, geo.thickness, getNormalisedValue(), isBipolar() ? 0.5f : 0.0f, knobTheme, isEnabled(),
                     isHovering() || isBeingDragged());

    if (getCaption().isNotEmpty())
        draw::text (g, getCaption(), b.withTop (b.getBottom() - geo.labelH), fonts::legend (geo.font),
                    t.caption (getSurface(), isEnabled()), juce::Justification::centred);
}

//==============================================================================
Trimmer::Trimmer (const juce::String& captionText)
{
    setCaption (captionText);
    setSize (preferredSize().x, preferredSize().y);
    showsValueTip = false;   // the value is always printed next to the trimmer
}

void Trimmer::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    auto b = getLocalBounds().toFloat();
    const auto body = b.removeFromLeft (24.0f).withSizeKeepingCentre (22.0f, 22.0f);
    draw::trimmer (g, body, getNormalisedValue(), t, isHovering() || isBeingDragged());

    b.removeFromLeft (5.0f);
    const auto top = b.removeFromTop (b.getHeight() * 0.5f);
    draw::text (g, getCaption(), top.translated (0.0f, 1.5f), fonts::legend (10.0f, 0.05f), t.bayTextDim,
                juce::Justification::centredLeft);
    draw::text (g, getValueText(), b.translated (0.0f, -1.0f), fonts::readout (11.5f),
                isEnabled() ? t.wellText : t.wellTextDim, juce::Justification::centredLeft);
}
}
