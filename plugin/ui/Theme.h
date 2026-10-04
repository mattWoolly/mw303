#pragma once
// Colours and metrics for the two faceplate themes.
//
// Every kit component asks themeFor (*this) at paint time, which resolves to the theme held
// by the mw12FLookAndFeel in force for that component (Silver when there is none). Switching
// themes at run time is: lookAndFeel.setTheme (id); root.sendLookAndFeelChange();
//
// All sizes are design units against the 1180 x 880 baseline. The editor applies one zoom
// factor (0.75 to 2.0) as a transform on the root component; nothing in the kit reads pixels.
#include <juce_gui_basics/juce_gui_basics.h>

namespace acid::ui
{
enum class ThemeId { Silver = 0, Night = 1 };

/** LED lens colours. Red means playing, Blue means queued, Amber and Turquoise mark the two
    couple switches, Accent is the generic "on" lamp. */
enum class LedColour { Accent, Red, Blue, Amber, Turquoise };

/** What a control sits on: the light or dark plate, or one of the always-dark surfaces
    (sequencer screen, calibration bay). Decides the colour of its legends. */
enum class Surface { Plate, Dark };

struct Theme
{
    ThemeId id;
    const char* name;

    // Faceplate
    juce::Colour plateTop, plateBottom, plateEdgeLight, plateEdgeDark;
    juce::Colour sectionFill, sectionOutline, sectionRule;
    juce::Colour legend, legendDim, legendFaint;

    // Rotary controls
    juce::Colour knobSkirtTop, knobSkirtBottom, knobCapTop, knobCapBottom, knobRim, knobPointer;
    juce::Colour arcTrack, arcFill;
    juce::Colour trimBody, trimRotor, trimSlot;

    // Buttons and switch handles
    juce::Colour buttonTop, buttonBottom, buttonOutline, buttonText, buttonDownTop, buttonDownBottom;
    juce::Colour keyWhite, keyBlack, keyOutline;

    // Wells: recessed readouts, switch slots, selector troughs
    juce::Colour wellFill, wellOutline, wellText, wellTextDim;

    // The sequencer screen
    juce::Colour screenFill, screenRow, screenRowDark, screenGrid, screenGridBar, screenText, screenTextDim;
    juce::Colour screenDisabled, cursor;

    // Accent family
    juce::Colour accent, accentBright, accentDim, accentInk, accentOnPlate;

    // LEDs
    juce::Colour ledRed, ledBlue, ledAmber, ledTurquoise, ledBezel;

    // Calibration bay and its cover
    juce::Colour bayFill, bayOutline, bayText, bayTextDim, coverTop, coverBottom;

    // Menus, tooltips, text entry
    juce::Colour menuFill, menuOutline, menuText, menuTextDim, menuHighlight, menuHighlightText;
    juce::Colour shadow;

    juce::Colour led (LedColour c) const noexcept;

    /** Legend colour for a caption on the given surface. */
    juce::Colour caption (Surface s, bool enabled = true) const noexcept
    {
        if (s == Surface::Dark)
            return enabled ? screenText : screenTextDim.withAlpha (0.6f);

        return enabled ? legend : legendFaint;
    }
};

const Theme& theme (ThemeId id) noexcept;

/** The theme in force for a component: the one held by its mw12FLookAndFeel, Silver otherwise. */
const Theme& themeFor (const juce::Component& c) noexcept;

namespace metrics
{
inline constexpr int kDesignWidth = 1180;
inline constexpr int kDesignHeight = 880;
inline constexpr float kMinZoom = 0.75f;
inline constexpr float kMaxZoom = 2.0f;

inline constexpr float kCorner = 3.0f;          // buttons, wells
inline constexpr float kSectionCorner = 5.0f;
inline constexpr float kLegendHeight = 11.5f;   // control legends, caps
inline constexpr float kTitleHeight = 11.5f;    // section titles, caps, tracked
inline constexpr float kReadoutHeight = 13.0f;  // numeric readouts

inline constexpr int kKnobLargeW = 80, kKnobLargeH = 86;
inline constexpr int kKnobSmallW = 62, kKnobSmallH = 68;
inline constexpr int kKnobMiniW = 46, kKnobMiniH = 54;
inline constexpr int kDragPixelsFullRange = 220;   // vertical travel for a full sweep
inline constexpr float kFineDragFactor = 0.1f;     // Ctrl or Shift held
}
}
