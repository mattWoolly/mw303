#include "mw12FLookAndFeel.h"
#include "Fonts.h"
#include "kit/Draw.h"
#include "kit/Faceplate.h"
#include "kit/SafeText.h"

namespace acid::ui
{
mw12FLookAndFeel::mw12FLookAndFeel (ThemeId initial) : themeId (initial)
{
    setDefaultSansSerifTypeface (fonts::textTypeface());
    applyColours();
}

void mw12FLookAndFeel::setTheme (ThemeId id)
{
    if (themeId != id)
    {
        themeId = id;
        applyColours();
    }
}

void mw12FLookAndFeel::applyColours()
{
    const auto& t = getTheme();

    // The base scheme colours every stock widget the editor does not draw itself (the file
    // dialogs: their list, path box and buttons); the overrides below then take the rest.
    setColourScheme (juce::LookAndFeel_V4::ColourScheme (t.screenFill, t.wellFill, t.menuFill, t.wellOutline, t.menuText, t.accentDim,
                                                         t.accentInk, t.accent, t.menuText));

    setColour (juce::ResizableWindow::backgroundColourId, t.plateBottom);

    setColour (juce::PopupMenu::backgroundColourId, t.menuFill);
    setColour (juce::PopupMenu::textColourId, t.menuText);
    setColour (juce::PopupMenu::headerTextColourId, t.menuTextDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.menuHighlight);
    setColour (juce::PopupMenu::highlightedTextColourId, t.menuHighlightText);

    setColour (juce::ComboBox::backgroundColourId, t.wellFill);
    setColour (juce::ComboBox::textColourId, t.wellText);
    setColour (juce::ComboBox::outlineColourId, t.wellOutline);
    setColour (juce::ComboBox::arrowColourId, t.wellTextDim);

    setColour (juce::Label::textColourId, t.legend);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, t.wellFill);
    setColour (juce::TextEditor::textColourId, t.wellText);
    setColour (juce::TextEditor::outlineColourId, t.wellOutline);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, t.accentBright);

    setColour (juce::TextButton::buttonColourId, t.buttonBottom);
    setColour (juce::TextButton::buttonOnColourId, t.wellFill);
    setColour (juce::TextButton::textColourOffId, t.buttonText);
    setColour (juce::TextButton::textColourOnId, t.wellText);
    setColour (juce::ToggleButton::textColourId, t.legend);

    setColour (juce::Slider::thumbColourId, t.knobPointer);
    setColour (juce::Slider::rotarySliderFillColourId, t.arcFill);
    setColour (juce::Slider::rotarySliderOutlineColourId, t.arcTrack);

    setColour (juce::ScrollBar::thumbColourId, t.screenGridBar);
    setColour (juce::TooltipWindow::backgroundColourId, t.menuFill);
    setColour (juce::TooltipWindow::textColourId, t.menuText);
    setColour (juce::TooltipWindow::outlineColourId, t.menuOutline);
}

juce::Typeface::Ptr mw12FLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // Fonts built by the kit already carry an embedded typeface. Anything else (a stock
    // widget asking for "sans-serif" or a system family) is redirected so no system font
    // is ever resolved through fontconfig.
    if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return fonts::readoutTypeface();

    if (font.isBold())
        return fonts::boldTypeface();

    return fonts::textTypeface();
}

void mw12FLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float, float, juce::Slider& slider)
{
    const auto b = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    draw::knob (g, b.getCentre(), r * 0.76f, r, juce::jmax (2.5f, r * 0.12f), pos, 0.0f, getTheme(), slider.isEnabled(),
                slider.isMouseOverOrDragging());
}

void mw12FLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool highlighted, bool down)
{
    const auto& t = getTheme();
    const auto face = button.getLocalBounds().toFloat().withTrimmedBottom (1.0f);

    if (button.getToggleState())
        draw::well (g, face, t);
    else
        draw::buttonFace (g, face, t, down, highlighted);
}

juce::Font mw12FLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::legend (juce::jmin (11.5f, static_cast<float> (buttonHeight) * 0.6f));
}

void mw12FLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto& t = getTheme();
    const auto b = button.getLocalBounds().toFloat();
    draw::led (g, juce::Rectangle<float> (b.getX() + 3.0f, b.getCentreY() - 5.0f, 10.0f, 10.0f), t.accent, button.getToggleState(), t);
    draw::text (g, button.getButtonText(), b.withTrimmedLeft (18.0f), fonts::legend (metrics::kLegendHeight),
                t.legend.withMultipliedAlpha (highlighted ? 0.8f : 1.0f), juce::Justification::centredLeft);
}

juce::Font mw12FLookAndFeel::getLabelFont (juce::Label& label)
{
    return fonts::text (label.getFont().getHeight());
}

void mw12FLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // LookAndFeel_V2::drawLabel with the text cleaned.
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    const auto alpha = label.isEnabled() ? 1.0f : 0.5f;

    if (! label.isBeingEdited())
    {
        const auto font = getLabelFont (label);
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (text::sanitise (label.getText()), area, label.getJustificationType(),
                          juce::jmax (1, static_cast<int> (static_cast<float> (area.getHeight()) / font.getHeight())),
                          label.getMinimumHorizontalScale());
        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    }
    else if (label.isEnabled())
    {
        g.setColour (label.findColour (juce::Label::outlineColourId));
    }

    g.drawRect (label.getLocalBounds());
}

void mw12FLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    const auto original = button.getButtonText();
    const auto clean = text::sanitise (original);

    if (clean == original)
    {
        juce::LookAndFeel_V4::drawButtonText (g, button, highlighted, down);
        return;
    }

    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (clean, button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

void mw12FLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                          const juce::String& shortcutKeyText, const juce::Drawable* icon,
                                          const juce::Colour* textColour)
{
    juce::LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
                                             text::sanitise (itemText), text::sanitise (shortcutKeyText), icon, textColour);
}

void mw12FLookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int standardMenuItemHeight,
                                                  int& idealWidth, int& idealHeight)
{
    juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text::sanitise (itemText), isSeparator, standardMenuItemHeight, idealWidth,
                                                     idealHeight);
}

void mw12FLookAndFeel::drawFileBrowserRow (juce::Graphics& g, int width, int height, const juce::File& file,
                                           const juce::String& filename, juce::Image* icon, const juce::String& size,
                                           const juce::String& time, bool isDirectory, bool isItemSelected, int itemIndex,
                                           juce::DirectoryContentsDisplayComponent& dcc)
{
    juce::LookAndFeel_V4::drawFileBrowserRow (g, width, height, file, text::sanitise (filename), icon, size, time, isDirectory,
                                              isItemSelected, itemIndex, dcc);
}

void mw12FLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = getTheme();
    const auto b = juce::Rectangle<int> (0, 0, width, height).toFloat().withTrimmedBottom (1.0f);
    draw::well (g, b, t);

    if (box.isMouseOver (true) && box.isEnabled())
    {
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (b.reduced (0.5f), metrics::kCorner, 1.0f);
    }

    draw::icon (g, draw::Icon::ChevronDown, juce::Rectangle<float> (b.getRight() - 17.0f, b.getCentreY() - 5.5f, 11.0f, 11.0f),
                box.isEnabled() ? t.wellText : t.wellTextDim);
}

juce::Font mw12FLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return fonts::readout (juce::jmin (metrics::kReadoutHeight, static_cast<float> (box.getHeight()) * 0.62f));
}

void mw12FLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (5, 0, box.getWidth() - 23, box.getHeight() - 1);
    label.setFont (getComboBoxFont (box));
}

juce::PopupMenu::Options mw12FLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (22);
}

juce::Font mw12FLookAndFeel::getPopupMenuFont()
{
    return fonts::text (14.0f);
}

void mw12FLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = getTheme();
    g.fillAll (t.menuFill);
    g.setColour (t.menuOutline);
    g.drawRect (0, 0, width, height, 1);
}

void mw12FLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    const auto& t = getTheme();
    draw::text (g, sectionName.toUpperCase(), area.toFloat().reduced (8.0f, 0.0f), fonts::bold (11.0f, 0.12f), t.accent,
                juce::Justification::centredLeft);
}

juce::Component* mw12FLookAndFeel::getParentComponentForMenuOptions (const juce::PopupMenu::Options& options)
{
    if (auto* parent = options.getParentComponent())
        return parent;

    if (auto* target = options.getTargetComponent())
    {
        if (auto* plate = Faceplate::find (*target))
            return plate;

        return target->getTopLevelComponent();
    }

    return nullptr;
}

namespace
{
/** Help text wraps at this width (design units). */
constexpr float kTooltipMaxW = 300.0f;

juce::TextLayout tooltipLayout (const juce::String& tipText, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text::sanitise (tipText), fonts::text (13.0f), colour);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, kTooltipMaxW);
    return layout;
}

/** The width the lines actually use (the layout reports the width it was given). */
float usedWidth (const juce::TextLayout& layout)
{
    float w = 0.0f;

    for (int i = 0; i < layout.getNumLines(); ++i)
        w = juce::jmax (w, layout.getLine (i).getLineBoundsX().getEnd());

    return w;
}
}

juce::Rectangle<int> mw12FLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (tipText, getTheme().menuText);
    const auto w = juce::roundToInt (std::ceil (usedWidth (layout))) + 18;
    const auto h = juce::roundToInt (std::ceil (layout.getHeight())) + 10;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void mw12FLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& t = getTheme();
    const auto b = juce::Rectangle<int> (0, 0, width, height).toFloat();
    g.setColour (t.menuFill);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (t.menuOutline);
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
    tooltipLayout (text, t.menuText).draw (g, b.reduced (9.0f, 5.0f));
}

void mw12FLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool down)
{
    const auto& t = getTheme();
    const auto thumb = vertical ? juce::Rectangle<int> (x + 1, thumbStart, width - 2, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y + 1, thumbSize, height - 2);
    g.setColour (t.screenGridBar.withMultipliedAlpha (down ? 1.0f : (over ? 0.85f : 0.6f)));
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}
}
