#pragma once
// A juce::ComboBox drawn as a recessed readout with a chevron. Bind a choice parameter with
// a ComboBoxAttachment (item ids are index + 1, as the attachment expects). The popup opens
// inside the editor and the box never takes keyboard focus.
#include "kit/Faceplate.h"
#include "kit/ParamMenu.h"

namespace acid::ui
{
class Dropdown final : public juce::ComboBox
{
public:
    Dropdown();

    ParamHooks param;

    /** Fills the list from labels, ids 1..n. */
    void setItems (const juce::StringArray& labels);

    /** Steps the selection (used by previous and next buttons). Wraps at the ends. */
    void step (int delta);

    /** Called on a left click before the popup opens. Return true to take the click over (the
        editor opens its preset browser instead of the list). */
    std::function<bool()> onOpen;

    /** Double-click hook (the editor renames a pattern through it). */
    std::function<void()> onDoubleClick;

    /** Right-click hook for a box without a parameter (the pattern name's menu). With
        param.name set, the right click opens the parameter menu instead. */
    std::function<void()> onContextMenu;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
};
}
