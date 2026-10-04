#pragma once
// The sliding cover over the calibration bay. Add it as the last child of the bay (so it
// sits above the trimmers) and give it the bay's local bounds with setClosedBounds().
// Closed, it hides the bay; open, it slides right until only its grip strip is left, so
// the bay content must keep clear of the rightmost kGripWidth units.
//
// The slide is driven by a juce::Timer on the message thread (no modal loop, no animator
// thread). setAnimated (false) makes it jump, which is also what the settings menu's
// "disable cover animation" option should call.
#include "kit/Faceplate.h"

namespace acid::ui
{
class CoverPanel final : public juce::Component, private juce::Timer
{
public:
    CoverPanel (const juce::String& title, const juce::String& subtitle);

    static constexpr int kGripWidth = 26;

    void setClosedBounds (juce::Rectangle<int> boundsInParent);

    void setOpen (bool shouldBeOpen, bool animate = true);
    bool isOpen() const noexcept { return open; }

    /** 0 = closed, 1 = fully open. Useful for rendering a mid-travel frame. */
    void setOpenAmount (float amount);
    float getOpenAmount() const noexcept { return amount; }

    void setAnimated (bool b) noexcept { animated = b; }

    std::function<void (bool isNowOpen)> onOpenChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void applyPosition();

    juce::String title, subtitle;
    juce::Rectangle<int> closedBounds;
    bool open = false, animated = true, gripHover = false;
    float amount = 0.0f, animFrom = 0.0f, animTo = 0.0f;
    juce::uint32 animStart = 0;
};
}
