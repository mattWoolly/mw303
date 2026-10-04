#include "editor/VoicingNames.h"

#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include "acid/fx/DistortionVoicings.h"
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace acid::ui
{
juce::StringArray distortionVoicingNames()
{
    juce::StringArray names;

    for (const auto& v : fx::distortionVoicings())
        names.add (v.name);

    return names;
}
}
