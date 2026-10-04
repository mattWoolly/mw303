#pragma once
// Buttons. Both are juce::Button subclasses: bind a bool parameter with a ButtonAttachment.
// Neither takes keyboard focus. A right-click opens the parameter menu when param.name is set.
#include "kit/Faceplate.h"
#include "kit/ParamMenu.h"

namespace acid::ui
{
/** A latching button whose state is shown by an LED. */
class LedButton final : public juce::Button
{
public:
    enum class Style
    {
        Lens,        // the LED alone (couple indicators, tab LEDs); the whole bounds are the hit area
        LensLabel,   // LED with a caption to its right, printed on the surface
        Cap          // a key cap with the LED inside and a caption
    };

    LedButton (const juce::String& caption, LedColour colour = LedColour::Accent, Style style = Style::Cap);

    ParamHooks param;

    void setLedColour (LedColour c) { colour = c; repaint(); }
    void setSurface (Surface s) { surface = s; repaint(); }

    /** Lights the LED independently of the toggle state (status lamps driven by the engine). */
    void setLit (std::optional<bool> forced) { forcedLit = forced; repaint(); }
    bool isLit() const { return forcedLit.has_value() ? *forcedLit : getToggleState(); }

    /** Cap style: draw the cap pressed in (a lit well) while the toggle is on, so a switch
        whose LED shows something else (RUN: the LED is the engine running, the cap the switch)
        still shows its own state. */
    void setLatchedCap (bool latched) { latchedCap = latched; repaint(); }

    /** Cap style: small text at the right of the cap (RUN shows the clock source). */
    void setBadge (const juce::String& badgeText) { if (badge != badgeText) { badge = badgeText; repaint(); } }
    const juce::String& getBadge() const noexcept { return badge; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    LedColour colour;
    Style style;
    Surface surface = Surface::Plate;
    std::optional<bool> forcedLit;
    bool latchedCap = false;
    juce::String badge;
};

/** A key cap with text, an icon, or both. Momentary by default; setClickingTogglesState (true)
    makes it latch, in which case the "on" state is drawn as a lit dark cap. */
class PushButton final : public juce::Button
{
public:
    explicit PushButton (const juce::String& caption = {}, draw::Icon icon = draw::Icon::None);

    ParamHooks param;

    void setIcon (draw::Icon i) { icon = i; repaint(); }

    /** Emphasis: the cap is drawn in the accent colour (Draw, Run). */
    void setProminent (bool p) { prominent = p; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    draw::Icon icon;
    bool prominent = false;
};

/** Shared by both buttons: opens the parameter menu for a button-like control. */
void showButtonParamMenu (juce::Button&, const ParamHooks&);
}
