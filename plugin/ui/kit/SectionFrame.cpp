#include "kit/SectionFrame.h"

namespace acid::ui
{
SectionFrame::SectionFrame (const juce::String& titleText, Style s) : title (titleText), style (s)
{
    setWantsKeyboardFocus (false);
}

void SectionFrame::setTitle (const juce::String& newTitle)
{
    if (title != newTitle)
    {
        title = newTitle;
        Faceplate::invalidateStaticArtFor (*this);
        repaint();
    }
}

juce::Rectangle<int> SectionFrame::getContentArea() const noexcept
{
    auto b = getLocalBounds().reduced (6, 0).withTrimmedBottom (6);
    return title.isEmpty() ? b.withTrimmedTop (6) : b.withTrimmedTop (kTitleHeight);
}

void SectionFrame::paint (juce::Graphics& g)
{
    paintStaticArtIfUncached (*this, g);
}

void SectionFrame::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);

    switch (style)
    {
        case Style::Panel:
            draw::sectionFrame (g, b, title, t);
            break;

        case Style::Screen:
            draw::screen (g, b, t);
            break;

        case Style::Bay:
        {
            const auto corner = metrics::kSectionCorner;
            g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.16f));
            g.fillRoundedRectangle (b.translated (0.0f, 1.0f), corner);
            g.setColour (t.bayFill);
            g.fillRoundedRectangle (b, corner);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (b.withHeight (3.0f), corner);
            g.setColour (t.bayOutline);
            g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

            if (title.isNotEmpty())
                draw::text (g, title, b.withHeight (static_cast<float> (kTitleHeight)).reduced (10.0f, 0.0f).withTrimmedTop (2.0f),
                            fonts::bold (metrics::kTitleHeight, 0.14f), t.bayText, juce::Justification::centredLeft);

            break;
        }
    }

    paintStaticExtras (g, t);
}
}
