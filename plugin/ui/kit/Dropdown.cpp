#include "kit/Dropdown.h"

namespace acid::ui
{
Dropdown::Dropdown()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setScrollWheelEnabled (true);
    setJustificationType (juce::Justification::centredLeft);
}

void Dropdown::setItems (const juce::StringArray& labels)
{
    clear (juce::dontSendNotification);
    addItemList (labels, 1);
}

void Dropdown::step (int delta)
{
    const auto n = getNumItems();

    if (n > 0)
        setSelectedItemIndex (((getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void Dropdown::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (param.name.isNotEmpty())
        {
            ParamMenuRequest r;
            r.target = this;
            r.hooks = &param;
            r.valueText = getText();
            showParamMenu (r);
        }
        else if (onContextMenu != nullptr)
        {
            onContextMenu();
        }

        return;
    }

    if (onOpen != nullptr && onOpen())
        return;

    juce::ComboBox::mouseDown (e);
}

void Dropdown::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onDoubleClick != nullptr)
        onDoubleClick();
}

void Dropdown::mouseEnter (const juce::MouseEvent& e)
{
    juce::ComboBox::mouseEnter (e);
    repaint();
}

void Dropdown::mouseExit (const juce::MouseEvent& e)
{
    juce::ComboBox::mouseExit (e);
    repaint();
}
}
