#pragma once
// A titled group. Its artwork is static, so inside a Faceplate it is drawn once into the
// plate cache; on its own it paints itself. Children go inside getContentArea().
#include "kit/Faceplate.h"

namespace acid::ui
{
class SectionFrame : public juce::Component, public StaticArt
{
public:
    enum class Style
    {
        Panel,    // raised group on the plate, title on a hairline rule
        Screen,   // the dark sequencer screen
        Bay       // the recessed calibration bay
    };

    explicit SectionFrame (const juce::String& title = {}, Style style = Style::Panel);

    void setTitle (const juce::String& newTitle);
    const juce::String& getTitle() const noexcept { return title; }
    Style getStyle() const noexcept { return style; }

    /** Local area below the title row, inset from the frame. */
    juce::Rectangle<int> getContentArea() const noexcept;

    static constexpr int kTitleHeight = 20;

    void paint (juce::Graphics&) override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

protected:
    /** Extra fixed legends drawn into the cache after the frame (rules, captions). */
    virtual void paintStaticExtras (juce::Graphics&, const Theme&) {}

private:
    juce::String title;
    Style style;
};
}
