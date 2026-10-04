#include "kit/Switches.h"

namespace acid::ui
{
namespace
{
constexpr float kRowH = 15.0f;
constexpr float kSlotW = 15.0f;
constexpr float kCaptionH = 14.0f;
}

ToggleSwitch::ToggleSwitch (const juce::StringArray& positionLabels, const juce::String& captionText)
    : labels (positionLabels)
{
    jassert (labels.size() >= 2);
    icons.insertMultiple (0, draw::Icon::None, labels.size());
    dragChangesValue = false;
    showsValueTip = false;
    setCaption (captionText);
    setRange (0.0, static_cast<double> (labels.size() - 1), 1.0);
    setDoubleClickReturnValue (false, 0.0);
    setSize (58, preferredHeight());
}

void ToggleSwitch::setPositionIcon (int position, draw::Icon icon)
{
    icons.set (position, icon);
    repaint();
}

int ToggleSwitch::preferredHeight() const noexcept
{
    return juce::roundToInt (kRowH * static_cast<float> (labels.size()) + 4.0f + (getCaption().isEmpty() ? 0.0f : kCaptionH + 3.0f));
}

juce::Rectangle<float> ToggleSwitch::bodyArea() const noexcept
{
    auto b = getLocalBounds().toFloat();
    const auto h = kRowH * static_cast<float> (labels.size()) + 4.0f;
    return b.withTop (b.getBottom() - h - (getCaption().isEmpty() ? 0.0f : kCaptionH + 3.0f)).withHeight (h);
}

void ToggleSwitch::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto body = bodyArea();
    const auto n = labels.size();
    const auto pos = juce::jlimit (0, n - 1, getPosition());

    // Longest label decides how the slot and labels are centred as a block.
    const auto font = fonts::legend (10.5f);
    float labelW = 0.0f;

    for (int i = 0; i < n; ++i)
        labelW = juce::jmax (labelW, icons[i] != draw::Icon::None ? 20.0f : juce::GlyphArrangement::getStringWidth (font, labels[i]));

    const auto blockW = kSlotW + 6.0f + labelW;
    const auto x0 = body.getCentreX() - blockW * 0.5f;
    const juce::Rectangle<float> slot (x0, body.getY(), kSlotW, body.getHeight());
    draw::well (g, slot, t, 3.0f);

    const auto handle = juce::Rectangle<float> (slot.getX() + 2.0f, slot.getY() + 2.0f + kRowH * static_cast<float> (pos),
                                                slot.getWidth() - 4.0f, kRowH);
    draw::buttonFace (g, handle, t, false, isHovering(), 2.0f);
    g.setColour (t.buttonOutline.withAlpha (0.8f));
    g.fillRect (handle.getX() + 3.0f, handle.getCentreY() - 0.5f, handle.getWidth() - 6.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> row (slot.getRight() + 6.0f, slot.getY() + 2.0f + kRowH * static_cast<float> (i), labelW + 2.0f, kRowH);
        const auto colour = i == pos ? t.caption (getSurface(), isEnabled())
                                     : (getSurface() == Surface::Dark ? t.screenTextDim : t.legendFaint);

        if (icons[i] != draw::Icon::None)
            draw::icon (g, icons[i], juce::Rectangle<float> (row.getX() + 1.0f, row.getCentreY() - 5.0f, 18.0f, 10.0f), colour);
        else
            draw::text (g, labels[i], row, font, colour, juce::Justification::centredLeft);
    }

    if (getCaption().isNotEmpty())
        draw::text (g, getCaption(), getLocalBounds().toFloat().removeFromBottom (kCaptionH),
                    fonts::legend (metrics::kLegendHeight - 0.5f), t.caption (getSurface(), isEnabled()), juce::Justification::centred);
}

void ToggleSwitch::mouseDown (const juce::MouseEvent& e)
{
    DragSlider::mouseDown (e);

    if (e.mods.isPopupMenu() || ! isEnabled())
        return;

    const auto body = bodyArea();
    const auto n = labels.size();
    int target = 0;

    if (n == 2 || ! body.contains (e.position))
        target = (getPosition() + 1) % n;   // a two-way switch flips wherever it is clicked
    else
        target = juce::jlimit (0, n - 1, static_cast<int> ((e.position.y - body.getY() - 2.0f) / kRowH));

    setValueAsGesture (getMinimum() + static_cast<double> (target));
}

//==============================================================================
SegmentedSelector::SegmentedSelector (const juce::StringArray& segmentLabels) : labels (segmentLabels)
{
    jassert (labels.size() >= 2);
    dragChangesValue = false;
    showsValueTip = false;
    setRange (0.0, static_cast<double> (labels.size() - 1), 1.0);
    setDoubleClickReturnValue (false, 0.0);
    setSize (48 * labels.size(), 24);
}

void SegmentedSelector::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    draw::well (g, b, t, 4.0f);

    const auto n = labels.size();
    const auto inner = b.reduced (2.0f);
    const auto segW = inner.getWidth() / static_cast<float> (n);
    const auto selected = juce::jlimit (0, n - 1, getSelectedSegment());
    const auto font = fonts::legend (11.5f, 0.08f);

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> seg (inner.getX() + segW * static_cast<float> (i), inner.getY(), segW, inner.getHeight());

        if (i == selected)
        {
            g.setGradientFill (juce::ColourGradient (t.accentBright.interpolatedWith (t.accent, 0.55f), 0.0f, seg.getY(),
                                                     t.accent, 0.0f, seg.getBottom(), false));
            g.fillRoundedRectangle (seg.reduced (0.5f), 2.5f);
            draw::text (g, labels[i], seg, font, t.accentInk);
        }
        else
        {
            draw::text (g, labels[i], seg, font, isEnabled() ? t.screenText : t.screenTextDim);

            if (i > 0 && i - 1 != selected)
            {
                g.setColour (t.screenTextDim.withAlpha (0.35f));
                g.fillRect (seg.getX() - 0.5f, seg.getY() + 4.0f, 1.0f, seg.getHeight() - 8.0f);
            }
        }
    }
}

void SegmentedSelector::mouseDown (const juce::MouseEvent& e)
{
    DragSlider::mouseDown (e);

    if (e.mods.isPopupMenu() || ! isEnabled())
        return;

    const auto n = labels.size();
    const auto segW = static_cast<float> (getWidth()) / static_cast<float> (n);
    const auto target = juce::jlimit (0, n - 1, static_cast<int> (e.position.x / segW));
    setValueAsGesture (getMinimum() + static_cast<double> (target));
}
}
