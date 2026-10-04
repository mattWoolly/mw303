#include "seq/MidiDragHandle.h"

namespace acid::ui
{
MidiDragHandle::MidiDragHandle()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setSize (96, 24);
}

void MidiDragHandle::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);

    // A dashed outline says "pick me up" where a solid key cap would say "press me".
    g.setColour (t.screenRow);
    g.fillRoundedRectangle (b, metrics::kCorner);

    juce::Path outline;
    outline.addRoundedRectangle (b.reduced (0.5f), metrics::kCorner);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    const auto ink = hover ? t.accent : t.screenText;
    g.setColour (ink.withAlpha (0.8f));
    g.fillPath (dashed);

    auto content = b.reduced (6.0f, 0.0f);
    draw::icon (g, draw::Icon::Grip, content.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 14.0f), ink);
    draw::text (g, native ? "DRAG PATTERN" : "DRAG MIDI", content, fonts::legend (native ? 10.0f : 11.0f), ink);
}

void MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    const auto& create = e.mods.isShiftDown() && onCreatePatternFile != nullptr ? onCreatePatternFile : onCreateFile;

    if (dragStarted || e.getDistanceFromDragStart() < 6 || create == nullptr)
        return;

    dragStarted = true;
    const auto file = create();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MidiDragHandle::mouseUp (const juce::MouseEvent&)
{
    dragStarted = false;
}

void MidiDragHandle::mouseEnter (const juce::MouseEvent& e)
{
    hover = true;
    native = e.mods.isShiftDown() && onCreatePatternFile != nullptr;
    repaint();
}

void MidiDragHandle::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    native = false;
    repaint();
}

void MidiDragHandle::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    const auto shift = hover && mods.isShiftDown() && onCreatePatternFile != nullptr;

    if (shift != native)
    {
        native = shift;
        repaint();
    }
}
}
