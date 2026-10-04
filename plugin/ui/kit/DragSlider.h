#pragma once
// Base for every value control in the kit. It is a juce::Slider, so the stock
// juce::SliderParameterAttachment / APVTS SliderAttachment binds it to a host parameter
// (float, int, bool or choice) and gets gesture begin and end for free.
//
// Interaction rules implemented here, identically for every subclass:
//   - vertical drag, metrics::kDragPixelsFullRange design units for the full range
//   - fine drag (one tenth speed) while Ctrl, Shift or Cmd is held; can be toggled mid-drag
//   - double-click (or Alt-click) returns to the default set with setDefaultValue()
//   - mouse wheel, fine with the same modifiers
//   - right-click opens the parameter menu (ParamMenu.h)
//   - hover and drag show the value tooltip of the enclosing Faceplate
//   - never takes keyboard focus; typed entry goes through Faceplate::beginTextEntry
#include "kit/Faceplate.h"
#include "kit/ParamMenu.h"

namespace acid::ui
{
class DragSlider : public juce::Slider
{
public:
    DragSlider();
    ~DragSlider() override;

    /** Name, MIDI learn and clear callbacks for the right-click menu. */
    ParamHooks param;

    /** The legend a subclass draws next to the control. Also the menu title when param.name is empty. */
    void setCaption (const juce::String& text);
    const juce::String& getCaption() const noexcept { return caption; }

    /** Value restored by double-click. The editor calls this again after a preset load so the
        control returns to the preset's value rather than the factory default. An attachment
        sets the parameter default on construction. */
    void setDefaultValue (double value) { setDoubleClickReturnValue (true, value); }

    /** Tells the control it sits on a dark surface (sequencer screen, calibration bay). */
    void setSurface (Surface s) { surface = s; Faceplate::invalidateStaticArtFor (*this); repaint(); }
    Surface getSurface() const noexcept { return surface; }

    /** Bipolar controls draw their value arc from the centre. */
    void setBipolar (bool shouldBeBipolar);
    bool isBipolar() const noexcept { return bipolar; }

    /** Formatted current value: the attachment's text function when attached, otherwise the slider's own. */
    juce::String getValueText() { return getTextFromValue (getValue()); }

    /** Sets a value inside a begin/end gesture pair, as a mouse action would. */
    void setValueAsGesture (double newValue);

    /** Opens the typed-entry box over this control. */
    void beginTypedEntry();

    /** Opens the right-click menu. Public so a host editor can trigger it from elsewhere. */
    void showMenu();

    float getNormalisedValue() { return static_cast<float> (valueToProportionOfLength (getValue())); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void valueChanged() override;
    void enablementChanged() override;

protected:
    /** False for click-to-select controls (switches, selectors): they handle clicks themselves. */
    bool dragChangesValue = true;
    bool showsValueTip = true;

    bool isHovering() const noexcept { return hovering; }
    bool isBeingDragged() const noexcept { return dragging; }
    void updateValueTip();
    juce::String menuName() const;

private:
    static bool isFine (const juce::ModifierKeys& mods) noexcept;

    juce::String caption;
    Surface surface = Surface::Plate;
    bool bipolar = false, hovering = false, dragging = false, menuClick = false;
    float lastRawY = 0.0f, virtualY = 0.0f, downY = 0.0f, downProportion = 0.0f;
};
}
