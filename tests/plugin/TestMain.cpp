// Catch2 main for the plugin tests: JUCE's message manager has to exist for the processor
// (timers, async host notifications), so the session runs inside a ScopedJuceInitialiser_GUI.
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <catch2/catch_session.hpp>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
