#pragma once
// The right-click parameter menu: name and value, typed entry, reset, MIDI learn and clear.
// The menu is non-modal and opens inside the editor (the LookAndFeel parents it to the
// top-level component), so it works in hosts where separate popup windows misbehave.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace acid::ui
{
/** What a control needs to know about the parameter behind it. The editor fills this in
    when it attaches the control; every field is optional. */
struct ParamHooks
{
    juce::String name;                               // menu header and tooltip caption
    std::function<juce::String()> midiAssignment;    // e.g. "CC 74"; empty when unassigned
    std::function<void()> onMidiLearn;               // menu entry is disabled while null
    std::function<void()> onMidiClear;
    std::function<bool()> isLearning;                // true while MIDI learn is armed for this parameter
    std::function<void()> onCancelLearn;             // the menu offers "Cancel learn" while armed
};

struct ParamMenuRequest
{
    juce::Component* target = nullptr;
    const ParamHooks* hooks = nullptr;
    juce::String valueText;
    std::function<void()> enterValue;       // null: no typed entry for this control
    std::function<void()> resetToDefault;   // null: no reset entry
};

/** Builds the menu without showing it (used by the gallery to render it). */
juce::PopupMenu buildParamMenu (const ParamMenuRequest&);

/** Shows the menu asynchronously next to the target. */
void showParamMenu (const ParamMenuRequest&);
}
