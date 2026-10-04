#pragma once
// The effects-rack tab bar. Each tab has an on/off LED and can be dragged sideways to
// change the processing order. The left-to-right order is the signal order.
//
// Wiring in the editor:
//   - bar.getLedButton (id) is a juce::Button: attach each effect's "on" parameter to it.
//   - bar.onOrderChanged gives the new order as tab ids, first processed first: write it to
//     Settings::fxOrder. Call bar.setOrder() when settings are loaded.
//   - bar.onTabSelected switches the page shown under the bar.
#include "kit/Buttons.h"

namespace acid::ui
{
class TabBar final : public juce::Component
{
public:
    TabBar();

    void addTab (int id, const juce::String& name);
    int getNumTabs() const noexcept { return tabs.size(); }

    juce::Button& getLedButton (int id);

    /** Sets the left-to-right order as tab ids. Ids not listed keep their relative order at the end. */
    void setOrder (const juce::Array<int>& ids);
    juce::Array<int> getOrder() const;

    void setSelectedTab (int id, juce::NotificationType = juce::sendNotification);
    int getSelectedTab() const noexcept { return selectedId; }

    void setReorderable (bool b) noexcept { reorderable = b; }

    /** Draws a tab's name in the warning colour (the delay tab while its time is clamped). */
    void setTabWarning (int id, bool warn);

    /** Freezes the bar as if tab `id` were being dragged with its left edge at x (gallery renders). */
    void setDragPreview (int id, float x);

    std::function<void (int id)> onTabSelected;
    std::function<void (const juce::Array<int>& order)> onOrderChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Tab
    {
        int id = 0;
        juce::String name;
        bool warning = false;
        std::unique_ptr<LedButton> led;
    };

    int indexOfId (int id) const noexcept;
    int indexAt (float x) const noexcept;
    float tabWidth() const noexcept;
    juce::Rectangle<float> tabBounds (int index) const noexcept;
    void layoutLeds();

    juce::OwnedArray<Tab> tabs;   // in display order
    int selectedId = -1, hoverIndex = -1;
    bool reorderable = true;

    // Drag state: the tab being moved and where its left edge is drawn.
    int dragId = -1;
    bool dragMoved = false;
    float dragOffset = 0.0f, dragX = 0.0f;
};
}
