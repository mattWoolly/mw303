#include "editor/LearnIndicator.h"
#include "kit/SafeText.h"

namespace acid::ui
{
LearnIndicator::LearnIndicator()
{
    setWantsKeyboardFocus (false);
    setAlwaysOnTop (true);
    setInterceptsMouseClicks (true, false);
    setVisible (false);
}

void LearnIndicator::show (juce::Component& t, const juce::String& parameterName)
{
    target = &t;
    name = text::sanitise (parameterName);
    ticks = 0;
    ringOn = true;

    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalBounds());

    layoutBanner();
    setVisible (true);
    toFront (false);
    repaint();
}

void LearnIndicator::hide()
{
    target = nullptr;
    setVisible (false);
}

void LearnIndicator::layoutBanner()
{
    // Over the middle of the top bar (the preset row), clear of the control being learned
    // unless that control sits there: then under the bar.
    const auto w = 420.0f, h = 34.0f;
    banner = { (static_cast<float> (getWidth()) - w) * 0.5f, 7.0f, w, h };

    if (const auto ring = ringBounds(); ring.intersects (banner))
        banner.setY (ring.getBottom() + 8.0f);
}

juce::Rectangle<float> LearnIndicator::ringBounds() const
{
    auto* t = target.getComponent();
    auto* parent = getParentComponent();

    if (t == nullptr || parent == nullptr)
        return {};

    return parent->getLocalArea (t, t->getLocalBounds()).toFloat().expanded (4.0f);
}

void LearnIndicator::tick()
{
    if (! isVisible())
        return;

    if (target == nullptr)
    {
        hide();
        return;
    }

    if (++ticks % 12 == 0)
    {
        ringOn = ! ringOn;
        const auto ring = ringBounds();
        repaint (ring.getUnion (lastRing).expanded (3.0f).getSmallestIntegerContainer());
        lastRing = ring;
    }
}

void LearnIndicator::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto ring = ringBounds();
    lastRing = ring;

    if (! ring.isEmpty())
    {
        g.setColour (t.ledRed.withAlpha (ringOn ? 0.95f : 0.35f));
        g.drawRoundedRectangle (ring, 5.0f, 2.5f);
    }

    draw::dropShadow (g, banner, t, 4.0f);
    g.setColour (t.menuFill);
    g.fillRoundedRectangle (banner, 4.0f);
    g.setColour (t.ledRed);
    g.drawRoundedRectangle (banner.reduced (0.5f), 4.0f, 1.0f);

    auto b = banner.reduced (10.0f, 0.0f);
    draw::led (g, b.removeFromLeft (11.0f).withSizeKeepingCentre (11.0f, 11.0f), t.ledRed, ringOn, t);
    b.removeFromLeft (8.0f);

    const auto cancel = b.removeFromRight (104.0f).reduced (0.0f, 6.0f);
    draw::buttonFace (g, cancel, t, false, false);
    draw::text (g, "CANCEL (ESC)", cancel, fonts::legend (10.5f), t.buttonText);
    b.removeFromRight (8.0f);

    const auto caption = fonts::bold (11.0f, 0.10f);
    const auto captionW = juce::GlyphArrangement::getStringWidth (caption, "MIDI LEARN") + 10.0f;
    draw::text (g, "MIDI LEARN", b.removeFromLeft (captionW), caption, t.ledRed, juce::Justification::centredLeft);

    g.setColour (t.menuText);
    g.setFont (fonts::text (13.0f));
    g.drawText (name + ": move a controller", b, juce::Justification::centredLeft, true);
}

bool LearnIndicator::hitTest (int x, int y)
{
    return banner.contains (static_cast<float> (x), static_cast<float> (y));
}

void LearnIndicator::mouseDown (const juce::MouseEvent&)
{
    if (onCancel != nullptr)
        onCancel();
}
}
