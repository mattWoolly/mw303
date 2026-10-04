#pragma once
// Drag source for exporting the current pattern as a MIDI file. On drag start it asks the
// owner for a file (written to the temp directory by onCreateFile) and starts an external
// file drag (XDND on Linux). The file must stay on disk after the drop: hosts read it
// later, so the owner deletes it on the next drag or when the editor closes.
// A visible export button with a file chooser must sit next to it as the guaranteed path.
//
// Shift-drag hands over the pattern in its own format instead (onCreatePatternFile: a .mw12f
// pattern file), for another instance or the file manager; the label says PATTERN while Shift
// is held over the handle.
#include "kit/Faceplate.h"

namespace acid::ui
{
class MidiDragHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragHandle();

    /** Writes the .mid and returns it. An invalid File cancels the drag. */
    std::function<juce::File()> onCreateFile;
    /** Shift-drag: writes the pattern as a .mw12f and returns it. */
    std::function<juce::File()> onCreatePatternFile;

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;

private:
    bool dragStarted = false, hover = false, native = false;
};
}
