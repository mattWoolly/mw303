#include "kit/CoverPanel.h"

namespace acid::ui
{
namespace
{
constexpr int kSlideMs = 240;
}

CoverPanel::CoverPanel (const juce::String& titleText, const juce::String& subtitleText)
    : title (titleText), subtitle (subtitleText)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);
}

void CoverPanel::setClosedBounds (juce::Rectangle<int> boundsInParent)
{
    closedBounds = boundsInParent;
    applyPosition();
}

void CoverPanel::applyPosition()
{
    const auto travel = juce::jmax (0, closedBounds.getWidth() - kGripWidth);
    setBounds (closedBounds.translated (juce::roundToInt (static_cast<float> (travel) * amount), 0));
}

void CoverPanel::setOpenAmount (float newAmount)
{
    stopTimer();
    amount = juce::jlimit (0.0f, 1.0f, newAmount);
    open = amount > 0.5f;
    applyPosition();
    repaint();
}

void CoverPanel::setOpen (bool shouldBeOpen, bool animate)
{
    if (open == shouldBeOpen && ! isTimerRunning())
        return;

    open = shouldBeOpen;

    if (animate && animated && isShowing())
    {
        animFrom = amount;
        animTo = open ? 1.0f : 0.0f;
        animStart = juce::Time::getMillisecondCounter();
        startTimerHz (60);
    }
    else
    {
        stopTimer();
        amount = open ? 1.0f : 0.0f;
        applyPosition();
        repaint();
    }

    if (onOpenChanged != nullptr)
        onOpenChanged (open);
}

void CoverPanel::timerCallback()
{
    const auto elapsed = static_cast<float> (juce::Time::getMillisecondCounter() - animStart) / static_cast<float> (kSlideMs);
    const auto p = juce::jlimit (0.0f, 1.0f, elapsed);
    const auto eased = p * p * (3.0f - 2.0f * p);
    amount = animFrom + (animTo - animFrom) * eased;
    applyPosition();

    if (p >= 1.0f)
    {
        stopTimer();
        repaint();
    }
}

void CoverPanel::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    const auto corner = metrics::kSectionCorner - 1.0f;

    g.setGradientFill (juce::ColourGradient (t.coverTop, 0.0f, b.getY(), t.coverBottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);
    g.setColour (t.buttonOutline);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (t.id == ThemeId::Silver ? 0.75f : 0.10f));
    g.fillRect (b.getX() + corner, b.getY() + 1.0f, b.getWidth() - 2.0f * corner, 1.0f);

    // Grip strip on the left: the part that stays in view when the cover is open.
    auto plate = b;
    const auto grip = plate.removeFromLeft (static_cast<float> (kGripWidth));
    g.setColour (t.buttonOutline.withAlpha (0.55f));
    g.fillRect (grip.getRight() - 0.5f, grip.getY() + 5.0f, 1.0f, grip.getHeight() - 10.0f);

    const auto ink = gripHover ? t.accentOnPlate : t.legendDim;

    for (int i = -3; i <= 3; ++i)
    {
        if (i == 0)
            continue;

        const auto y = grip.getCentreY() + static_cast<float> (i) * 5.0f + (i > 0 ? 6.0f : -6.0f);
        g.setColour (t.legendFaint.withAlpha (0.8f));
        g.fillRoundedRectangle (grip.getX() + 7.0f, y - 0.75f, grip.getWidth() - 14.0f, 1.5f, 0.75f);
    }

    draw::icon (g, amount > 0.5f ? draw::Icon::Prev : draw::Icon::Next, grip.withSizeKeepingCentre (10.0f, 10.0f), ink);

    // Fixing screws and the engraved legend.
    const auto inset = 11.0f;
    draw::screw (g, { plate.getX() + inset, plate.getY() + inset }, 3.5f, t);
    draw::screw (g, { plate.getRight() - inset, plate.getY() + inset }, 3.5f, t);
    draw::screw (g, { plate.getX() + inset, plate.getBottom() - inset }, 3.5f, t);
    draw::screw (g, { plate.getRight() - inset, plate.getBottom() - inset }, 3.5f, t);

    const auto mid = plate.getCentreY();
    draw::text (g, title, plate.withHeight (18.0f).withCentre ({ plate.getCentreX(), mid - 8.0f }), fonts::bold (13.0f, 0.22f), t.legend);
    draw::text (g, subtitle, plate.withHeight (14.0f).withCentre ({ plate.getCentreX(), mid + 9.0f }), fonts::legend (10.0f, 0.10f), t.legendDim);

    g.setColour (t.sectionRule);
    const auto ruleW = juce::jmin (150.0f, plate.getWidth() * 0.3f);
    g.fillRect (plate.getCentreX() - ruleW * 0.5f, mid + 0.5f, ruleW, 1.0f);
}

void CoverPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.x <= static_cast<float> (kGripWidth) && ! e.mods.isPopupMenu())
        setOpen (! open);
}

void CoverPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.x > static_cast<float> (kGripWidth))
        setOpen (! open);
}

void CoverPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto over = e.position.x <= static_cast<float> (kGripWidth);

    if (over != gripHover)
    {
        gripHover = over;
        repaint (0, 0, kGripWidth, getHeight());
    }
}

void CoverPanel::mouseExit (const juce::MouseEvent&)
{
    if (gripHover)
    {
        gripHover = false;
        repaint (0, 0, kGripWidth, getHeight());
    }
}
}
