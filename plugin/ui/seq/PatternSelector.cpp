#include "seq/PatternSelector.h"

namespace acid::ui
{
namespace
{
constexpr int kBanks = 4, kSlots = 12, kColumns = 4;
constexpr float kBankH = 26.0f, kGap = 4.0f, kCaptionH = 13.0f, kStatusH = 22.0f;

juce::String noteName (int midiNote)
{
    static constexpr const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return kNames[((midiNote % 12) + 12) % 12];
}

juce::String slotText (int bank, int slot)
{
    if (bank < 0 || slot < 0)
        return "--";

    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + bank)) + "-" + juce::String (slot + 1).paddedLeft ('0', 2);
}
}

//==============================================================================
PatternSelector::KeyParam::KeyParam()
{
    dragChangesValue = false;
    showsValueTip = false;
    setInterceptsMouseClicks (false, false);
    setScrollWheelEnabled (false);
    setOpaque (false);
}

//==============================================================================
PatternSelector::PatternSelector()
{
    setWantsKeyboardFocus (false);
    bankKeys.param.name = "Pattern Bank";
    slotKeys.param.name = "Pattern";
    addAndMakeVisible (bankKeys);
    addAndMakeVisible (slotKeys);
    setSize (196, 150);
}

void PatternSelector::setSelected (int bank, int slot)
{
    if (bank != selectedBank || slot != selectedSlot)
    {
        selectedBank = bank;
        selectedSlot = slot;

        if (bank >= 0 && bank < kBanks)
            viewBank = bank;   // a new selection (a click, MIDI or automation) shows its bank

        repaint();
    }
}

void PatternSelector::setViewBank (int bank)
{
    bank = juce::jlimit (0, kBanks - 1, bank);

    if (bank != viewBank)
    {
        viewBank = bank;
        repaint();
    }
}

void PatternSelector::setPlaying (int bank, int slot)
{
    if (bank != playingBank || slot != playingSlot)
    {
        playingBank = bank;
        playingSlot = slot;
        repaint();
    }
}

void PatternSelector::setQueued (int bank, int slot)
{
    if (bank != queuedBank || slot != queuedSlot)
    {
        queuedBank = bank;
        queuedSlot = slot;
        repaint();
    }
}

void PatternSelector::setRunning (bool isRunning)
{
    if (running != isRunning)
    {
        running = isRunning;
        repaint();
    }
}

void PatternSelector::setHostNoteBase (int baseNote)
{
    if (hostNoteBase != baseNote)
    {
        hostNoteBase = baseNote;
        repaint();
    }
}

void PatternSelector::setUsedSlots (const std::array<std::uint16_t, 4>& usedMask)
{
    if (used != usedMask)
    {
        used = usedMask;
        repaint();
    }
}

juce::Rectangle<float> PatternSelector::bankKey (int bank) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const auto w = (b.getWidth() + kGap) / static_cast<float> (kBanks);
    return { b.getX() + w * static_cast<float> (bank), b.getY() + kCaptionH, w - kGap, kBankH };
}

juce::Rectangle<float> PatternSelector::slotKey (int slot) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const auto top = b.getY() + kCaptionH + kBankH + 6.0f + kCaptionH;
    const auto rows = kSlots / kColumns;
    const auto w = (b.getWidth() + kGap) / static_cast<float> (kColumns);
    const auto h = (b.getBottom() - kStatusH - 8.0f - top + kGap) / static_cast<float> (rows);
    return { b.getX() + w * static_cast<float> (slot % kColumns), top + h * static_cast<float> (slot / kColumns), w - kGap, h - kGap };
}

void PatternSelector::resized()
{
    bankKeys.setBounds (bankKey (0).getUnion (bankKey (kBanks - 1)).getSmallestIntegerContainer());
    slotKeys.setBounds (slotKey (0).getUnion (slotKey (kSlots - 1)).getSmallestIntegerContainer());
}

int PatternSelector::hitIndex (juce::Point<float> pos) const noexcept
{
    for (int i = 0; i < kBanks; ++i)
        if (bankKey (i).contains (pos))
            return i;

    for (int i = 0; i < kSlots; ++i)
        if (slotKey (i).contains (pos))
            return kBanks + i;

    return -1;
}

juce::String PatternSelector::bankLabel (int bank) const
{
    if (hostNoteBase < 0)
        return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + bank));

    const auto note = hostNoteBase + bank * kSlots;
    return noteName (note) + juce::String (note / 12 - 1);
}

juce::String PatternSelector::slotLabel (int slot) const
{
    return hostNoteBase < 0 ? juce::String (slot + 1) : noteName (hostNoteBase + slot);
}

void PatternSelector::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat();
    const auto captionFont = fonts::legend (10.0f, 0.08f);
    const auto captionInk = surface == Surface::Dark ? t.screenTextDim : t.legendDim;

    draw::text (g, hostNoteBase < 0 ? "BANK" : "BANK  (HOST NOTE OCTAVE)", b.withHeight (kCaptionH), captionFont, captionInk,
                juce::Justification::centredLeft);
    draw::text (g, hostNoteBase < 0 ? "PATTERN" : "PATTERN  (HOST NOTE)",
                juce::Rectangle<float> (b.getX(), b.getY() + kCaptionH + kBankH + 6.0f, b.getWidth(), kCaptionH), captionFont, captionInk,
                juce::Justification::centredLeft);

    const auto drawKey = [&] (juce::Rectangle<float> key, const juce::String& label, bool pressed, bool outlined, bool playing, bool queued,
                              bool hasContent, bool hovered, bool dropTarget)
    {
        const auto ledD = 8.0f;
        const juce::Rectangle<float> ledArea (key.getX() + 5.0f, key.getCentreY() - ledD * 0.5f, ledD, ledD);

        if (pressed)
        {
            draw::well (g, key, t);
            g.setColour (t.accent.withAlpha (0.9f));
            g.drawRoundedRectangle (key.reduced (0.5f), metrics::kCorner, 1.0f);
        }
        else
        {
            draw::buttonFace (g, key, t, false, hovered);

            if (outlined)
            {
                g.setColour (t.accent.withAlpha (0.55f));
                g.drawRoundedRectangle (key.reduced (0.5f), metrics::kCorner, 1.0f);
            }
        }

        if (dropTarget)
        {
            g.setColour (t.accentBright);
            g.drawRoundedRectangle (key.reduced (1.0f), metrics::kCorner, 2.0f);
        }

        draw::led (g, ledArea, playing ? t.ledRed : t.ledBlue, playing || queued, t);
        draw::text (g, label, key.withTrimmedLeft (ledD + 6.0f), fonts::legend (hostNoteBase < 0 ? 12.0f : 11.0f),
                    pressed ? t.wellText : t.buttonText);

        if (hasContent)
        {
            g.setColour (pressed ? t.wellText : t.buttonText.withAlpha (0.55f));
            g.fillEllipse (key.getRight() - 7.0f, key.getY() + 4.0f, 3.0f, 3.0f);
        }
    };

    const auto playBank = running ? playingBank : -1;

    for (int i = 0; i < kBanks; ++i)
        drawKey (bankKey (i), bankLabel (i), i == viewBank, i == selectedBank && i != viewBank, i == playBank,
                 i == queuedBank && i != playBank, false, hover == i, false);

    const auto showsSelection = viewBank == selectedBank;

    for (int i = 0; i < kSlots; ++i)
        drawKey (slotKey (i), slotLabel (i), showsSelection && i == selectedSlot, false, viewBank == playBank && i == playingSlot,
                 viewBank == queuedBank && i == queuedSlot, (used[static_cast<std::size_t> (viewBank)] & (1 << i)) != 0,
                 hover == kBanks + i, dropHover == kBanks + i);

    paintStatus (g, t);
}

void PatternSelector::paintStatus (juce::Graphics& g, const Theme& t)
{
    // What the LEDs mean, spelled out: the playing slot and the one waiting for its launch
    // point. Stopped, the first entry names the selection RUN would start, unlit.
    const auto strip = getLocalBounds().toFloat().removeFromBottom (kStatusH);
    draw::well (g, strip.withTrimmedBottom (1.0f), t);

    const auto entry = [&] (juce::Rectangle<float> area, juce::Colour led, bool lit, const juce::String& label, const juce::String& value)
    {
        draw::led (g, juce::Rectangle<float> (area.getX() + 6.0f, area.getCentreY() - 4.5f, 8.0f, 8.0f), led, lit, t);
        auto text = area.withTrimmedLeft (19.0f).withTrimmedBottom (1.0f);
        draw::text (g, label, text.removeFromLeft (30.0f), fonts::legend (10.0f, 0.06f), t.wellTextDim, juce::Justification::centredLeft);
        draw::text (g, value, text, fonts::readout (12.0f), lit ? t.wellText : t.wellTextDim, juce::Justification::centredLeft);
    };

    const auto left = strip.withWidth (strip.getWidth() * 0.5f);

    if (running)
        entry (left, t.ledRed, playingBank >= 0, "PLAY", slotText (playingBank, playingSlot));
    else
        entry (left, t.ledRed, false, "SEL", slotText (selectedBank, selectedSlot));

    entry (strip.withTrimmedLeft (strip.getWidth() * 0.5f), t.ledBlue, queuedBank >= 0, "NEXT", slotText (queuedBank, queuedSlot));
}

void PatternSelector::mouseDown (const juce::MouseEvent& e)
{
    pressedSlot = -1;
    selectOnRelease = dragging = false;
    const auto hit = hitIndex (e.getEventRelativeTo (this).position);

    if (e.mods.isPopupMenu())
    {
        // The parameter menu of the half that was clicked: MIDI learn of bank or pattern select.
        if (hit >= 0 && hit < kBanks)
            bankKeys.showMenu();
        else if (hit >= kBanks)
            slotKeys.showMenu();

        return;
    }

    if (hit < 0)
        return;

    if (hit < kBanks)
    {
        setViewBank (hit);   // browse only: what plays changes on a pattern key
        return;
    }

    pressedSlot = hit - kBanks;

    // A press on the selected key has nothing to select (and may start a drag-out); any other
    // key selects on release, so dragging a pattern out never queues it.
    selectOnRelease = ! (viewBank == selectedBank && pressedSlot == selectedSlot);
}

void PatternSelector::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6 || onDragOut == nullptr)
        return;

    if (pressedSlot < 0)
        return;

    dragging = true;
    const auto file = onDragOut (viewBank * kSlots + pressedSlot);

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void PatternSelector::mouseUp (const juce::MouseEvent&)
{
    const auto slot = pressedSlot;
    const auto select = selectOnRelease && ! dragging;
    pressedSlot = -1;
    selectOnRelease = dragging = false;

    if (! select || slot < 0)
        return;

    setSelected (viewBank, slot);

    if (onSelect != nullptr)
        onSelect (viewBank, slot);
}

void PatternSelector::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitIndex (e.position);

    if (hit != hover)
    {
        hover = hit;
        repaint();
    }
}

void PatternSelector::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1)
    {
        hover = -1;
        repaint();
    }
}

juce::String PatternSelector::getTooltip()
{
    const auto mouse = getMouseXYRelative().toFloat();
    const auto hit = hitIndex (mouse);

    if (hit >= 0 && hit < kBanks)
        return "Bank " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + hit))
               + ": shows its twelve patterns. Nothing changes until you click a pattern key. Right-click: MIDI learn bank select.";

    if (hit >= kBanks)
    {
        const auto name = slotText (viewBank, hit - kBanks);
        juce::String tip = "Pattern " + name + ": click to select it; while running it plays from the launch point. Drag it out to "
                           "save a pattern file, or drop .mw12f and .mid files here. Right-click: MIDI learn pattern select.";

        if (hostNoteBase >= 0)
            tip = "Host note " + noteName (hostNoteBase + viewBank * kSlots + hit - kBanks)
                  + juce::String ((hostNoteBase + viewBank * kSlots + hit - kBanks) / 12 - 1) + " starts " + name + ". " + tip;

        return tip;
    }

    if (mouse.y > static_cast<float> (getHeight()) - kStatusH)
        return "PLAY: the pattern sounding (SEL while stopped: what RUN starts). NEXT: the pattern waiting for its launch point.";

    return {};
}

//==============================================================================
bool PatternSelector::isPatternFile (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".mw12f" || ext == ".mid" || ext == ".midi";
}

bool PatternSelector::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isPatternFile (f))
            return true;

    return false;
}

void PatternSelector::setDropHover (int index)
{
    if (index != dropHover)
    {
        dropHover = index;
        repaint();
    }
}

void PatternSelector::fileDragEnter (const juce::StringArray&, int x, int y)
{
    const auto hit = hitIndex ({ static_cast<float> (x), static_cast<float> (y) });

    if (hit >= 0 && hit < kBanks)
        setViewBank (hit);   // hovering a bank key while dragging flips to that bank

    setDropHover (hit >= kBanks ? hit : -1);
}

void PatternSelector::fileDragMove (const juce::StringArray& files, int x, int y)
{
    fileDragEnter (files, x, y);
}

void PatternSelector::fileDragExit (const juce::StringArray&)
{
    setDropHover (-1);
}

void PatternSelector::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto hit = hitIndex ({ static_cast<float> (x), static_cast<float> (y) });
    setDropHover (-1);

    juce::StringArray accepted;

    for (const auto& f : files)
        if (isPatternFile (f))
            accepted.add (f);

    if (accepted.isEmpty() || onFilesDropped == nullptr)
        return;

    onFilesDropped (accepted, hit >= kBanks ? viewBank * kSlots + hit - kBanks : -1);
}
}
