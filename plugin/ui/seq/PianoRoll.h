#pragma once
// Piano-roll step editor. Sixteen steps are visible; patterns up to 64 steps are reached by
// paging (pager strip under the grid, or the mouse wheel).
//
// Lanes, top to bottom:
//   header       step numbers; click selects a step. Red = playing, accent = selected.
//   octave       five rows, +3 down to -1
//   notes        twelve pitch-class rows (B at the top) with a keyboard legend, or, for an
//                arpeggiator pattern, ten rows addressing the held notes 10 down to 1
//   attributes   accent, slide, vibrato, and the ratchet (1 to 4 hits per step, drawn as ticks)
//   probability  one bar per step; drag to set 0..100 %
//
// Mouse, note lane: click an empty cell to place a note; drag to draw; click a note to
// remove it; drag right from a note to extend it with ties; right-button erases.
// Attribute lane: click or drag to paint a flag on or off (the first cell decides which). The
// ratchet row steps the hit count 1, 2, 3, 4 on a click, paints it on a drag, and resets on a
// right click.
//
// The component is opaque and repaints single columns when the play cursor moves.
#include "seq/PatternEditModel.h"
#include "kit/Draw.h"
#include <functional>

namespace acid::ui
{
class PianoRoll final : public juce::Component, private PatternEditModel::Listener
{
public:
    explicit PianoRoll (PatternEditModel& model);
    ~PianoRoll() override;

    static constexpr int kVisibleSteps = 16;
    static constexpr int kNumPages = seq::kMaxSteps / kVisibleSteps;

    void setPage (int page);
    int getPage() const noexcept { return page; }

    /** When on, the visible page follows the play cursor. */
    void setFollowPlayback (bool shouldFollow) noexcept { followPlayback = shouldFollow; }

    /** Tints the legend with the generator's pools while its tab is open. noteMask bit n marks
        pitch class n (already rotated by the root), octaveMask bit (octave - kMinOctave). */
    void setGeneratorHighlight (bool on, std::uint16_t noteMask = 0, std::uint8_t octaveMask = 0);

    std::function<void (int page)> onPageChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Lane { None, Header, Octave, Note, Attribute, Probability, Pager };
    enum class Gesture { None, Paint, Erase, TieOrErase, FlagOn, FlagOff, Ratchet, Octave, Probability };

    struct Hit
    {
        Lane lane = Lane::None;
        int step = -1;    // absolute step index
        int row = -1;
        float fraction = 0.0f;   // probability lane: 0 at the bottom, 1 at the top
    };

    void patternChanged() override;
    void playStepChanged (int previous, int now) override;
    void selectedStepChanged (int previous, int now) override;

    int noteRows() const noexcept;
    float columnX (int visibleColumn) const noexcept;
    juce::Rectangle<float> cell (const juce::Rectangle<float>& lane, int visibleColumn, int row, float rowHeight) const noexcept;
    juce::Rectangle<int> columnBounds (int absoluteStep) const noexcept;
    Hit locate (juce::Point<float>) const noexcept;
    int rowForStep (const seq::Pattern&, int step) const noexcept;   // note-lane row a step's bar sits on, or -1
    void setStepPitchFromRow (seq::Step&, int row) const noexcept;
    void applyGesture (const Hit&);
    void paintLegend (juce::Graphics&, const Theme&);
    void paintPager (juce::Graphics&, const Theme&, const seq::Pattern&);

    PatternEditModel& model;
    int page = 0;
    bool followPlayback = false, generatorHighlight = false;
    std::uint16_t highlightNotes = 0;
    std::uint8_t highlightOctaves = 0;

    // Geometry, rebuilt in resized().
    float legendW = 46.0f, colW = 0.0f, rowH = 0.0f, octH = 0.0f;
    juce::Rectangle<float> headerLane, octaveLane, noteLane, attrLane, probLane, pagerLane;

    // Gesture state.
    Gesture gesture = Gesture::None;
    seq::Pattern gestureBase {};
    int anchorStep = -1, lastStep = -1, lastRow = -1, flagRow = -1, ratchetValue = 1;
    bool gestureMoved = false;
};
}
