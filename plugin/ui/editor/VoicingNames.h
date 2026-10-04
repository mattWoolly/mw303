#pragma once
// The distortion voicing names for the model box, without pulling the clipper headers into
// the editor. acid/fx/DistortionVoicings.h includes DistortionClippers.h, whose inline code
// compares floats for equality; compiled under the plugin's warning set (-Wfloat-equal) that
// is a warning in every TU that includes it. The one TU behind this header silences that
// warning for the include alone.
#include <juce_core/juce_core.h>

namespace acid::ui
{
/** acid::fx::distortionVoicings() names, in table order (index 0 is the custom voicing). */
juce::StringArray distortionVoicingNames();
}
