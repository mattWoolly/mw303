#pragma once
// Pattern selector: four banks (A to D) of twelve slots.
//
// Two steps, as on the original: a bank key only chooses which bank the twelve pattern keys
// show (browsing never changes what plays), and a pattern key selects that bank's slot, which
// the sequencer queues for its launch point. The key of the selection is drawn pressed and
// lit; a bank being browsed away from the selection shows the selected bank's key outlined.
//
// LED language: red = playing (only while the sequencer runs), blue = queued (waiting for its
// launch point); the strip under the keys names both. Stopped, the strip reads SEL and the
// selection that RUN would start.
//
// Parameters: bankKeys and slotKeys are DragSlider value holders over the bank row and the
// slot grid. The panel registers them with the ControlSet under pattern_bank and pattern_slot,
// so the editor attaches them like any other control and the selector's right-click menu (the
// standard parameter menu: value, MIDI learn and clear) is theirs. They paint nothing and take
// no mouse input themselves: the selector draws the keys and handles every click. A click on
// a pattern key goes through onSelect (bank, slot), which the editor turns into one
// Controller::selectSlot, so bank and slot change together.
//
// Files: the selector is a drop target for pattern files (.mw12f of type pattern or arp, and
// .mid); onFilesDropped gets the files and the slot under the pointer (0..47), with the key
// highlighted while a drag hovers. Dragging a pattern key out of the selector asks onDragOut
// for a file (a .mw12f the editor writes) and starts an external file drag.
//
// All indices are 0-based; pass -1 for "none".
#include "kit/DragSlider.h"

namespace acid::ui
{
class PatternSelector final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              public juce::TooltipClient
{
public:
    /** A parameter-bound half of the selector (see the header comment). */
    class KeyParam final : public DragSlider
    {
    public:
        KeyParam();
        void paint (juce::Graphics&) override {}
    };

    PatternSelector();

    KeyParam bankKeys, slotKeys;

    /** The selection the parameters hold. Moving it also shows its bank. */
    void setSelected (int bank, int slot);
    int getSelectedBank() const noexcept { return selectedBank; }
    int getSelectedSlot() const noexcept { return selectedSlot; }

    /** The bank whose twelve patterns the keys show. */
    void setViewBank (int bank);
    int getViewBank() const noexcept { return viewBank; }

    void setPlaying (int bank, int slot);
    void setQueued (int bank, int slot);
    /** Whether the sequencer is running: the playing LEDs and the strip's PLAY entry are lit only then. */
    void setRunning (bool running);

    /** Host Note trigger mode: the keys show the MIDI note that starts each pattern (bank keys
        the octave, pattern keys the note name) from `baseNote`. -1 shows A..D and 1..12. */
    void setHostNoteBase (int baseNote);

    /** Marks slots that hold a non-empty pattern (a small dot on the key). Bit `slot` of usedMask[bank]. */
    void setUsedSlots (const std::array<std::uint16_t, 4>& usedMask);
    const std::array<std::uint16_t, 4>& getUsedSlots() const noexcept { return used; }

    void setSurface (Surface s) { surface = s; repaint(); }

    /** A pattern key was clicked: select (bank, slot). */
    std::function<void (int bank, int slot)> onSelect;
    /** A pattern key is being dragged out: write the slot (0..47) as a file and return it. An
        invalid File cancels the drag. */
    std::function<juce::File (int slotIndex)> onDragOut;
    /** Files were dropped on a slot (0..47; -1 off the keys: the selected slot). */
    std::function<void (const juce::StringArray& files, int slotIndex)> onFilesDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String getTooltip() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** The file extensions a drop accepts. */
    static bool isPatternFile (const juce::String& path);

private:
    juce::Rectangle<float> bankKey (int bank) const noexcept;
    juce::Rectangle<float> slotKey (int slot) const noexcept;
    int hitIndex (juce::Point<float>) const noexcept;   // 0..3 banks, 4..15 slots, -1 none
    void paintStatus (juce::Graphics&, const Theme&);
    juce::String bankLabel (int bank) const;
    juce::String slotLabel (int slot) const;
    void setDropHover (int index);

    int selectedBank = 0, selectedSlot = 0, viewBank = 0;
    int playingBank = -1, playingSlot = -1, queuedBank = -1, queuedSlot = -1;
    int hover = -1, dropHover = -1, pressedSlot = -1, hostNoteBase = -1;
    bool running = false, dragging = false, selectOnRelease = false;
    std::array<std::uint16_t, 4> used {};
    Surface surface = Surface::Plate;
};
}
