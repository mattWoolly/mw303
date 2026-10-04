// The plugin entry point. Kept apart from the processor so the test executables can compile
// PluginProcessor.cpp without defining a plugin filter.
#include "PluginProcessor.h"

#include <clap-juce-extensions/clap-juce-extensions.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // The VST3, LV2 and Standalone wrappers set the processor's wrapperType before they call
    // this; clap-juce-extensions does not, but raises building_clap around the call.
    const bool clap = clap_juce_extensions::clap_properties::building_clap;
    auto* p = new mw12FProcessor();
    if (clap)
        p->setHostFormat (mw12FProcessor::HostFormat::Clap);
    return p;
}
