#include "kit/Buttons.h"

namespace acid::ui
{
void showButtonParamMenu (juce::Button& b, const ParamHooks& hooks)
{
    ParamMenuRequest r;
    r.target = &b;
    r.hooks = &hooks;
    r.valueText = b.getToggleState() ? "On" : "Off";
    showParamMenu (r);
}

//==============================================================================
LedButton::LedButton (const juce::String& captionText, LedColour c, Style s)
    : juce::Button (captionText), colour (c), style (s)
{
    setButtonText (captionText);
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void LedButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat();
    const auto lit = isLit();
    const auto ledColour = t.led (colour);

    switch (style)
    {
        case Style::Lens:
        {
            const auto d = juce::jmin (10.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.48f);
            draw::led (g, b.withSizeKeepingCentre (d, d), ledColour, lit, t);

            if (highlighted)
            {
                g.setColour (t.caption (surface).withAlpha (0.35f));
                g.drawEllipse (b.withSizeKeepingCentre (d + 4.0f, d + 4.0f), 1.0f);
            }

            break;
        }

        case Style::LensLabel:
        {
            const auto d = 10.0f;
            draw::led (g, juce::Rectangle<float> (b.getX() + 5.0f, b.getCentreY() - d * 0.5f, d, d), ledColour, lit, t);
            draw::text (g, getButtonText(), b.withTrimmedLeft (d + 10.0f), fonts::legend (metrics::kLegendHeight - 0.5f),
                        t.caption (surface, isEnabled()).withMultipliedAlpha (highlighted ? 0.8f : 1.0f), juce::Justification::centredLeft);
            break;
        }

        case Style::Cap:
        {
            const auto face = b.withTrimmedBottom (1.0f);
            const auto latched = latchedCap && getToggleState();

            if (latched)
            {
                draw::well (g, face, t);
                g.setColour (t.accent.withAlpha (highlighted ? 1.0f : 0.8f));
                g.drawRoundedRectangle (face.reduced (0.5f), metrics::kCorner, 1.0f);
            }
            else
            {
                draw::buttonFace (g, face, t, down, highlighted);
            }

            const auto d = juce::jmin (9.0f, face.getHeight() - 8.0f);
            const auto hasText = getButtonText().isNotEmpty();
            const auto ink = latched ? t.wellText : t.buttonText;

            if (hasText)
            {
                draw::led (g, juce::Rectangle<float> (face.getX() + 6.0f, face.getCentreY() - d * 0.5f, d, d), ledColour, lit, t);
                auto textArea = face.withTrimmedLeft (d + 9.0f).withTrimmedRight (5.0f);

                if (badge.isNotEmpty())
                {
                    const auto badgeFont = fonts::legend (9.0f, 0.06f);
                    const auto w = juce::GlyphArrangement::getStringWidth (badgeFont, badge) + 4.0f;
                    draw::text (g, badge, textArea.removeFromRight (w), badgeFont, latched ? t.wellTextDim : t.buttonText.withAlpha (0.6f),
                                juce::Justification::centredRight);
                }

                draw::text (g, getButtonText(), textArea, fonts::legend (11.0f), isEnabled() ? ink : ink.withAlpha (0.4f),
                            juce::Justification::centred);
            }
            else
            {
                draw::led (g, face.withSizeKeepingCentre (d, d), ledColour, lit, t);
            }

            break;
        }
    }
}

void LedButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (param.name.isNotEmpty())
            showButtonParamMenu (*this, param);

        return;
    }

    juce::Button::mouseDown (e);
}

//==============================================================================
PushButton::PushButton (const juce::String& captionText, draw::Icon i) : juce::Button (captionText), icon (i)
{
    setButtonText (captionText);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void PushButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = themeFor (*this);
    const auto face = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    const auto on = getToggleState();
    auto ink = t.buttonText;

    if (on || prominent)
    {
        const auto lit = prominent && ! on;
        const auto top = lit ? t.accentBright.interpolatedWith (t.accent, 0.5f) : t.wellFill;
        const auto bottom = lit ? t.accent : t.wellFill;
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (face.translated (0.0f, 1.0f), metrics::kCorner);
        g.setGradientFill (juce::ColourGradient (down ? bottom : top, 0.0f, face.getY(), bottom, 0.0f, face.getBottom(), false));
        g.fillRoundedRectangle (face, metrics::kCorner);
        g.setColour (lit ? t.accentDim : t.wellOutline);
        g.drawRoundedRectangle (face.reduced (0.5f), metrics::kCorner, 1.0f);
        ink = lit ? t.accentInk : t.wellText;

        if (highlighted && ! lit)
            ink = t.accentBright;
    }
    else
    {
        draw::buttonFace (g, face, t, down, highlighted);
    }

    if (! isEnabled())
        ink = ink.withAlpha (0.35f);

    const auto content = face.translated (0.0f, down ? 0.5f : 0.0f);
    const auto caption = getButtonText();

    if (icon != draw::Icon::None && caption.isEmpty())
    {
        const auto s = juce::jmin (icon == draw::Icon::Cog ? 15.0f : 12.0f, content.getHeight() - 8.0f, content.getWidth() - 6.0f);
        draw::icon (g, icon, content.withSizeKeepingCentre (s, s), ink);
    }
    else if (icon != draw::Icon::None)
    {
        const auto font = fonts::legend (11.0f);
        const auto textW = juce::GlyphArrangement::getStringWidth (font, caption);
        const auto s = juce::jmin (11.0f, content.getHeight() - 8.0f);
        const auto x = content.getCentreX() - (s + 5.0f + textW) * 0.5f;
        draw::icon (g, icon, juce::Rectangle<float> (x, content.getCentreY() - s * 0.5f, s, s), ink);
        draw::text (g, caption, juce::Rectangle<float> (x + s + 5.0f, content.getY(), textW + 2.0f, content.getHeight()), font, ink,
                    juce::Justification::centredLeft);
    }
    else
    {
        draw::text (g, caption, content.reduced (3.0f, 0.0f), fonts::legend (11.0f), ink);
    }
}

void PushButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (param.name.isNotEmpty())
            showButtonParamMenu (*this, param);

        return;
    }

    juce::Button::mouseDown (e);
}
}
