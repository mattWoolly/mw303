#pragma once
// The embedded typefaces (assets/fonts, both SIL OFL 1.1). Nothing in the editor may fall
// back to a system font: every juce::Font used by the kit comes from here, and the
// LookAndFeel routes stock JUCE widgets to the same faces.
#include <juce_gui_basics/juce_gui_basics.h>

namespace acid::ui::fonts
{
/** Semi-condensed grotesque, medium weight: body text, menu items. */
juce::Font text (float height);

/** Semi-bold caps face for panel legends and button captions. `tracking` is extra kerning as a fraction of height. */
juce::Font legend (float height, float tracking = 0.04f);

/** Bold: section titles and the name plate. */
juce::Font bold (float height, float tracking = 0.0f);

/** Monospaced technical face for numeric readouts. */
juce::Font readout (float height);

juce::Typeface::Ptr textTypeface();
juce::Typeface::Ptr legendTypeface();
juce::Typeface::Ptr boldTypeface();
juce::Typeface::Ptr readoutTypeface();
}
