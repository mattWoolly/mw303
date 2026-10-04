#pragma once
// The MIDI learn state, shown where the user is looking and nowhere else: while learn is armed
// a blinking ring sits around the target control and a banner names the parameter with a
// CANCEL lamp. It has its own layer over the plate, so hovering other controls (whose value
// tips share the plate's single tooltip) never hides it.
//
// The overlay covers the plate but takes clicks only on the banner: a click there cancels.
// The editor also cancels on Escape, on a click anywhere else on the plate, and from the
// target's right-click menu ("Cancel learn").
#include "kit/Faceplate.h"

namespace acid::ui
{
class LearnIndicator final : public juce::Component
{
public:
    LearnIndicator();

    /** Shows the ring around `target` (any component inside the parent plate) and the banner. */
    void show (juce::Component& target, const juce::String& parameterName);
    void hide();
    bool isActive() const noexcept { return isVisible(); }
    juce::Component* getTarget() const noexcept { return target.getComponent(); }

    /** Called from the editor's timer: blinks the ring. */
    void tick();

    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Rectangle<int> getBannerBounds() const noexcept { return banner.toNearestInt(); }

private:
    juce::Rectangle<float> ringBounds() const;
    void layoutBanner();

    juce::Component::SafePointer<juce::Component> target;
    juce::String name;
    juce::Rectangle<float> banner, lastRing;
    int ticks = 0;
    bool ringOn = true;
};
}
