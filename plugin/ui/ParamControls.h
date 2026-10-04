#pragma once
// Builds kit controls from rows of acid::kParamTable and keeps a registry of which control
// shows which parameter, so the editor can attach everything in three loops:
//
//     controls.forEachSlider ([&] (acid::ParamId id, juce::Slider& s)
//     {
//         sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
//             apvts, acid::paramInfo (id).id, s));
//     });
//     // likewise forEachButton -> ButtonAttachment, forEachComboBox -> ComboBoxAttachment
//
// One parameter may have several controls (the two couple LEDs of a pair; a knob in an
// effect's basic view and a numeric field in its full view). Attached to the same
// parameter they stay in step by themselves. Without a processor (the gallery's kit sheets),
// call linkDuplicates() to mirror them locally.
#include "acid/engine/ParamTable.h"
#include "kit/Knob.h"
#include "kit/NumericDisplay.h"
#include "kit/Switches.h"
#include "kit/Buttons.h"
#include "kit/Dropdown.h"
#include <vector>

namespace acid::ui
{
/** Formats a value in the row's natural units the way the readouts show it. */
juce::String formatParamValue (const ParamInfo& info, double value);

/** Parses text typed by the user back into natural units (accepts "2.5k", "1.2 uF", a choice label...). */
double parseParamValue (const ParamInfo& info, const juce::String& text);

/** Short panel legend for a parameter: the display name without its section prefix, in caps. */
juce::String shortCaption (ParamId id);

/** Applies a row to a slider-type control: range, step, log skew, default, value text, menu name. */
void configureSlider (DragSlider& slider, ParamId id);

/** Re-installs the kit's value text functions on a slider. A SliderAttachment replaces them with
    the host parameter's plain text on construction; the editor calls this right after attaching. */
void applyValueText (DragSlider& slider, ParamId id);

class ControlSet
{
public:
    ControlSet() = default;

    Knob& knob (ParamId id, Knob::Size size, const juce::String& caption = {});
    Trimmer& trimmer (ParamId id, const juce::String& caption);
    NumericDisplay& numeric (ParamId id, const juce::String& caption,
                             NumericDisplay::CaptionPlacement placement = NumericDisplay::CaptionPlacement::Above);

    /** Bool or choice parameter as a slide switch. Empty labels: the row's choice labels in caps (Off/On for a bool). */
    ToggleSwitch& toggleSwitch (ParamId id, const juce::String& caption = {}, juce::StringArray labels = {});
    SegmentedSelector& selector (ParamId id, juce::StringArray labels = {});
    LedButton& ledButton (ParamId id, const juce::String& caption, LedColour colour = LedColour::Accent,
                          LedButton::Style style = LedButton::Style::Cap);
    /** Momentary push button on a bool parameter (the live overrides). */
    PushButton& pushButton (ParamId id, const juce::String& caption);
    Dropdown& dropdown (ParamId id);

    /** Registers a control built elsewhere (for example a TabBar LED) under a parameter. */
    void add (ParamId id, juce::Slider& slider)    { sliders.push_back ({ id, &slider }); }
    void add (ParamId id, juce::Button& button)    { buttons.push_back ({ id, &button }); }
    void add (ParamId id, juce::ComboBox& box)     { combos.push_back ({ id, &box }); }

    template <typename Fn> void forEachSlider (Fn&& fn)   { for (auto& e : sliders) fn (e.first, *e.second); }
    template <typename Fn> void forEachButton (Fn&& fn)   { for (auto& e : buttons) fn (e.first, *e.second); }
    template <typename Fn> void forEachComboBox (Fn&& fn) { for (auto& e : combos) fn (e.first, *e.second); }

    /** First control registered for a parameter, or nullptr. */
    juce::Slider* findSlider (ParamId id) const noexcept;
    juce::Button* findButton (ParamId id) const noexcept;
    juce::ComboBox* findComboBox (ParamId id) const noexcept;

    /** Stand-in for parameter attachments: sets every control to its row's default, mirrors
        controls that share a parameter, and calls onChange (id, value) on any edit.
        Do not use together with real attachments. */
    void linkDuplicates (std::function<void (ParamId, double)> onChange = {});

    /** Sets every control of a parameter (natural units) without notifying. For mocks and tests. */
    void setValue (ParamId id, double value);
    double getValue (ParamId id) const;

private:
    template <typename T, typename... Args> T& make (Args&&... args)
    {
        auto owned = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *owned;
        storage.push_back (std::move (owned));
        return ref;
    }

    std::vector<std::unique_ptr<juce::Component>> storage;
    std::vector<std::pair<ParamId, juce::Slider*>> sliders;
    std::vector<std::pair<ParamId, juce::Button*>> buttons;
    std::vector<std::pair<ParamId, juce::ComboBox*>> combos;
    std::function<void (ParamId, double)> changeCallback;
    bool mirroring = false;
};
}
