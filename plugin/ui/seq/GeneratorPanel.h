#pragma once
// The pattern generator's controls. It edits acid::seq::GenParams, which is stored inside
// the pattern, so it works through the same PatternEditModel as the step editors:
//
//   - every change applies an edited copy of the pattern with new GenParams. The owner
//     should respond to a change of the four amounts by re-running the generator's
//     density pass (no re-roll), which is what makes the amount knobs live.
//   - onDraw fires when DRAW is pressed: the owner picks a new seed, generates, and applies.
//
// Controls: a one-octave keyboard of pitch-class toggles in absolute pitch (the root key is
// marked, and the lit keys are the notes the piano roll's legend lights), the root, five
// octave toggles, three attribute toggles, four amount knobs, the seed (all eight hex digits:
// click it to type one and draw that line again), and the template row (a library of masks
// the owner fills from acid::seq::scaleTemplates(), grouped by category).
#include "seq/PatternEditModel.h"
#include "kit/Knob.h"
#include "kit/NumericDisplay.h"
#include "kit/Buttons.h"
#include "kit/Dropdown.h"

namespace acid::ui
{
class GeneratorPanel final : public juce::Component, public juce::TooltipClient, private PatternEditModel::Listener
{
public:
    explicit GeneratorPanel (PatternEditModel& model);
    ~GeneratorPanel() override;

    void setSurface (Surface s);

    /** DRAW was pressed. The owner generates a new pattern (new seed) and applies it to the model. */
    std::function<void()> onDraw;

    /** Fired after any template or amount edit, with the new parameters. When set, the owner
        applies them (and re-derives the densities); the panel does not touch the model itself. */
    std::function<void (const seq::GenParams&)> onParamsChanged;

    /** A template was picked in the template box or stepped with its arrows (index into the list given to setTemplateNames). */
    std::function<void (int index)> onTemplateChosen;
    /** KEEP was pressed: the owner flattens the generated line into plain steps. */
    std::function<void()> onFlatten;

    /** The seed field was edited: the owner draws with exactly this seed. */
    std::function<void (std::uint32_t seed)> onSeedEntered;

    void setTemplateNames (const juce::StringArray& names);
    /** Template names with a category caption each: the list opens as one submenu per category. */
    void setTemplates (const juce::StringArray& names, const juce::StringArray& categories);
    /** -1 shows "- Custom -". */
    void setTemplateIndex (int index);

    /** Pitch classes in the pool after the root is applied (bit n = pitch class n), for the piano-roll legend. */
    std::uint16_t getAbsoluteNoteMask() const;

    Knob& getAmountKnob (int index) noexcept { return amounts[index]; }   // 0 note, 1 slide, 2 accent, 3 vibrato
    PushButton& getDrawButton() noexcept { return drawButton; }
    Dropdown& getTemplateBox() noexcept { return templateBox; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct GestureListener;

    void patternChanged() override;
    void refresh();
    void edit (const std::function<void (seq::GenParams&)>& fn);
    juce::Rectangle<float> keyBounds (int pitchClass, bool& isBlack) const noexcept;
    void enterSeed();
    juce::Rectangle<float> chip (const juce::Rectangle<float>& row, int index, int count) const noexcept;

    PatternEditModel& model;
    Surface surface = Surface::Plate;
    NumericDisplay root { "ROOT", NumericDisplay::CaptionPlacement::Left };
    Knob amounts[4] { Knob (Knob::Size::Mini, "NOTE"), Knob (Knob::Size::Mini, "SLIDE"), Knob (Knob::Size::Mini, "ACC"),
                      Knob (Knob::Size::Mini, "VIB") };
    PushButton drawButton { "DRAW", draw::Icon::Dice };
    Dropdown templateBox;
    PushButton prevTemplate { {}, draw::Icon::Prev }, nextTemplate { {}, draw::Icon::Next }, keepButton { "KEEP" };
    std::unique_ptr<GestureListener> gestures;
    juce::Rectangle<float> keysArea, octaveRow, attrRow, seedArea, templateCaption;
    bool refreshing = false;
};
}
