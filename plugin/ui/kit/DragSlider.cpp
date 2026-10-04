#include "kit/DragSlider.h"

namespace acid::ui
{
DragSlider::DragSlider()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (metrics::kDragPixelsFullRange);
    setVelocityModeParameters (1.0, 1, 0.0, false);   // our own fine mode replaces JUCE's velocity mode
    setScrollWheelEnabled (true);
    setPopupMenuEnabled (false);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setRepaintsOnMouseActivity (false);
    setRange (0.0, 1.0, 0.0);
    setDoubleClickReturnValue (true, 0.0);
}

DragSlider::~DragSlider()
{
    if (auto* f = Faceplate::find (*this))
        f->hideValueTip (this);
}

void DragSlider::setCaption (const juce::String& text)
{
    if (caption != text)
    {
        caption = text;
        repaint();
    }
}

void DragSlider::setBipolar (bool shouldBeBipolar)
{
    if (bipolar != shouldBeBipolar)
    {
        bipolar = shouldBeBipolar;
        repaint();
    }
}

juce::String DragSlider::menuName() const
{
    return param.name.isNotEmpty() ? param.name : caption;
}

bool DragSlider::isFine (const juce::ModifierKeys& mods) noexcept
{
    return mods.isCtrlDown() || mods.isShiftDown() || mods.isCommandDown();
}

void DragSlider::setValueAsGesture (double newValue)
{
    // juce::Slider only exposes gesture notification through its own mouse handling. The
    // double-click path is the one public entry that wraps a set in drag start and end, so
    // route through it with the target as the temporary return value.
    const auto hadDefault = isDoubleClickReturnEnabled();
    const auto defaultValue = getDoubleClickReturnValue();
    const auto target = juce::jlimit (getMinimum(), getMaximum(), newValue);

    setDoubleClickReturnValue (true, target);
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent synthetic (juce::Desktop::getInstance().getMainMouseSource(), {}, juce::ModifierKeys(),
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, this, this, now, {}, now, 2, false);
    juce::Slider::mouseDoubleClick (synthetic);
    setDoubleClickReturnValue (hadDefault, defaultValue);
}

void DragSlider::beginTypedEntry()
{
    if (auto* f = Faceplate::find (*this))
    {
        juce::Component::SafePointer<DragSlider> safe (this);
        f->beginTextEntry (*this, getValueText(), [safe] (const juce::String& text)
        {
            if (safe != nullptr && text.trim().isNotEmpty())
                safe->setValueAsGesture (safe->getValueFromText (text));
        });
    }
}

void DragSlider::showMenu()
{
    juce::Component::SafePointer<DragSlider> safe (this);
    ParamHooks hooks = param;
    hooks.name = menuName();

    ParamMenuRequest r;
    r.target = this;
    r.hooks = &hooks;
    r.valueText = getValueText();
    r.enterValue = [safe] { if (safe != nullptr) safe->beginTypedEntry(); };

    if (isDoubleClickReturnEnabled())
        r.resetToDefault = [safe] { if (safe != nullptr) safe->setValueAsGesture (safe->getDoubleClickReturnValue()); };

    if (auto* f = Faceplate::find (*this))
        f->hideValueTip (this);

    showParamMenu (r);
}

void DragSlider::updateValueTip()
{
    if (! showsValueTip)
        return;

    if (auto* f = Faceplate::find (*this))
        f->showValueTip (*this, menuName(), getValueText());
}

void DragSlider::mouseDown (const juce::MouseEvent& e)
{
    menuClick = e.mods.isPopupMenu();

    if (menuClick)
    {
        showMenu();
        return;
    }

    if (! dragChangesValue)
        return;

    dragging = true;
    downY = lastRawY = virtualY = e.position.y;
    downProportion = getNormalisedValue();
    juce::Slider::mouseDown (e);
    updateValueTip();
}

void DragSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (menuClick || ! dragChangesValue)
        return;

    // Accumulate a virtual pointer position so the fine modifier can be pressed or released
    // mid-drag without a jump, and clamp it to the travel so reversing at an end stop
    // responds at once.
    const auto dy = e.position.y - lastRawY;
    lastRawY = e.position.y;
    virtualY += dy * (isFine (e.mods) ? metrics::kFineDragFactor : 1.0f);

    const auto span = static_cast<float> (getMouseDragSensitivity());
    virtualY = juce::jlimit (downY - (1.0f - downProportion) * span, downY + downProportion * span, virtualY);

    juce::Slider::mouseDrag (e.withNewPosition (juce::Point<float> (e.position.x, virtualY)));
}

void DragSlider::mouseUp (const juce::MouseEvent& e)
{
    if (menuClick || ! dragChangesValue)
        return;

    dragging = false;
    juce::Slider::mouseUp (e);

    if (! hovering)
        if (auto* f = Faceplate::find (*this))
            f->hideValueTip (this);
}

void DragSlider::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    juce::Slider::mouseEnter (e);
    updateValueTip();
    repaint();
}

void DragSlider::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    juce::Slider::mouseExit (e);

    if (! dragging)
        if (auto* f = Faceplate::find (*this))
            f->hideValueTip (this);

    repaint();
}

void DragSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    juce::Slider::mouseDoubleClick (e);
}

void DragSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto w = wheel;

    if (isFine (e.mods))
    {
        w.deltaX *= 0.2f;
        w.deltaY *= 0.2f;
    }

    juce::Slider::mouseWheelMove (e, w);
}

void DragSlider::valueChanged()
{
    if (hovering || dragging)
        updateValueTip();
}

void DragSlider::enablementChanged()
{
    juce::Slider::enablementChanged();
    repaint();
}
}
