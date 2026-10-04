#pragma once
// LookAndFeel for the mw12F editor. It holds the active theme (kit components read it
// through themeFor()), routes every stock JUCE widget to the embedded fonts, and draws
// popup menus, combo boxes, tooltips and text editors in the house style.
//
// Usage:
//     mw12FLookAndFeel lookAndFeel;                 // owned by the editor
//     root.setLookAndFeel (&lookAndFeel);           // root is the Faceplate
//     lookAndFeel.setTheme (ThemeId::Night);        // run-time switch
//     root.sendLookAndFeelChange();
//
// Popup menus are parented to the nearest Faceplate (or the top-level component), so they
// are drawn inside the editor window and scale with the zoom transform.
#include "Theme.h"

namespace acid::ui
{
class mw12FLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit mw12FLookAndFeel (ThemeId initial = ThemeId::Silver);

    void setTheme (ThemeId id);
    ThemeId getThemeId() const noexcept { return themeId; }
    const Theme& getTheme() const noexcept { return theme (themeId); }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // Stock widgets, for anything the editor uses that is not a kit component.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
    juce::Font getLabelFont (juce::Label&) override;

    // Text the stock widgets draw goes through text::sanitise (kit/SafeText.h) first, so a
    // label, menu item, button or file name holding invalid UTF-8 can never reach JUCE's
    // text shaping. The editor cleans text where it enters; this is the second line.
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    void drawFileBrowserRow (juce::Graphics&, int width, int height, const juce::File& file, const juce::String& filename,
                             juce::Image* icon, const juce::String& fileSizeDescription,
                             const juce::String& fileTimeDescription, bool isDirectory, bool isItemSelected,
                             int itemIndex, juce::DirectoryContentsDisplayComponent&) override;

    // Combo boxes (kit Dropdown).
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    // Popup menus.
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSize() override { return 3; }

    // Tooltips (stock juce::TooltipWindow, if the editor uses one for help text).
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

private:
    void applyColours();

    ThemeId themeId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (mw12FLookAndFeel)
};
}
