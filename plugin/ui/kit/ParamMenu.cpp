#include "kit/ParamMenu.h"

namespace acid::ui
{
namespace
{
enum MenuId { kEnterValue = 1, kReset = 2, kLearn = 3, kClear = 4, kCancelLearn = 5 };
}

juce::PopupMenu buildParamMenu (const ParamMenuRequest& r)
{
    juce::PopupMenu m;
    const auto name = (r.hooks != nullptr && r.hooks->name.isNotEmpty()) ? r.hooks->name : juce::String ("Parameter");
    m.addSectionHeader (name);
    m.addItem (-1, r.valueText.isEmpty() ? juce::String ("-") : r.valueText, false, false);
    m.addItem (kEnterValue, "Enter value...", r.enterValue != nullptr);
    m.addItem (kReset, "Reset to default", r.resetToDefault != nullptr);
    m.addSeparator();

    juce::String assignment;

    if (r.hooks != nullptr && r.hooks->midiAssignment != nullptr)
        assignment = r.hooks->midiAssignment();

    m.addItem (-2, "MIDI: " + (assignment.isEmpty() ? juce::String ("not assigned") : assignment), false, false);
    const auto armed = r.hooks != nullptr && r.hooks->isLearning != nullptr && r.hooks->isLearning();

    if (armed && r.hooks->onCancelLearn != nullptr)
        m.addItem (kCancelLearn, "Cancel learn (waiting for a controller)");
    else
        m.addItem (kLearn, "Learn", r.hooks != nullptr && r.hooks->onMidiLearn != nullptr);

    m.addItem (kClear, "Clear", r.hooks != nullptr && r.hooks->onMidiClear != nullptr && assignment.isNotEmpty());
    return m;
}

void showParamMenu (const ParamMenuRequest& r)
{
    if (r.target == nullptr)
        return;

    auto menu = buildParamMenu (r);
    // A PopupMenu takes its LookAndFeel from the menu, not from the target: hand it the
    // control's, so the menu is drawn in the house style and parented inside the plate.
    menu.setLookAndFeel (&r.target->getLookAndFeel());
    juce::Component::SafePointer<juce::Component> safe (r.target);
    auto enter = r.enterValue;
    auto reset = r.resetToDefault;
    auto learn = r.hooks != nullptr ? r.hooks->onMidiLearn : std::function<void()>();
    auto clear = r.hooks != nullptr ? r.hooks->onMidiClear : std::function<void()>();
    auto cancel = r.hooks != nullptr ? r.hooks->onCancelLearn : std::function<void()>();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (r.target),
                        [safe, enter, reset, learn, clear, cancel] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result == kEnterValue && enter != nullptr)  enter();
                            if (result == kReset && reset != nullptr)       reset();
                            if (result == kLearn && learn != nullptr)       learn();
                            if (result == kClear && clear != nullptr)       clear();
                            if (result == kCancelLearn && cancel != nullptr) cancel();
                        });
}
}
