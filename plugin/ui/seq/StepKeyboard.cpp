#include "seq/StepKeyboard.h"

namespace acid::ui
{
namespace
{
constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr int kWhitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr int kBlackPc[5] = { 1, 3, 6, 8, 10 };
constexpr float kBlackOffset[5] = { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };   // boundary between white keys the black key straddles
constexpr int kStepsPerPage = 16;
}

StepKeyboard::StepKeyboard (PatternEditModel& m) : model (m)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = pageButtons[i];
        b.setButtonText (juce::String (i * kStepsPerPage + 1) + "-" + juce::String ((i + 1) * kStepsPerPage));
        b.onClick = [this, i]
        {
            const auto offset = model.getSelectedStep() % kStepsPerPage;
            model.setSelectedStep (i * kStepsPerPage + offset);
            page = juce::jmin (i, (static_cast<int> (model.getPattern().length) - 1) / kStepsPerPage);
            refreshButtons();
            repaint();
        };
        addAndMakeVisible (b);
    }

    prevButton.onClick = [this] { moveSelection (-1); };
    nextButton.onClick = [this] { moveSelection (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    autoInc.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (autoInc);
    preview.onClick = [this] { if (preview.getToggleState()) previewSelected (false); };
    addAndMakeVisible (preview);

    for (int i = 0; i < 5; ++i)
    {
        auto& b = octaveButtons[i];
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            editSelected ([i] (seq::Step& s) { s.octave = static_cast<std::int8_t> (seq::kMinOctave + i); });
            previewSelected (false);
        };
        addAndMakeVisible (b);
    }

    const auto flag = [this] (LedButton& b, std::function<void (seq::Step&)> fn)
    {
        b.setClickingTogglesState (false);
        b.onClick = [this, fn] { editSelected (fn); };
        addAndMakeVisible (b);
    };

    flag (accent,  [] (seq::Step& s) { s.accent = ! s.accent; });
    flag (slide,   [] (seq::Step& s) { s.slide = ! s.slide; });
    flag (vibrato, [] (seq::Step& s) { s.vibrato = ! s.vibrato; });
    flag (tie,     [] (seq::Step& s) { s.state = s.state == seq::StepState::Tie ? seq::StepState::Rest : seq::StepState::Tie; });
    flag (rest,    [] (seq::Step& s) { s.state = s.state == seq::StepState::Rest ? seq::StepState::Note : seq::StepState::Rest; });
    flag (ratchet, [] (seq::Step& s) { s.ratchet = static_cast<std::uint8_t> (juce::jlimit (1, 4, static_cast<int> (s.ratchet)) % 4 + 1); });
    flag (gate,    [] (seq::Step& s)
    {
        // Pattern gate, then 25, 50, 75 and 100 % of the step (Step::gate is n / 127 of it).
        static constexpr std::uint8_t kGates[5] = { 0, 32, 64, 95, 127 };
        int i = 0;

        while (i < 4 && kGates[i] < s.gate)
            ++i;

        s.gate = kGates[(i + (kGates[i] == s.gate ? 1 : 0)) % 5];
    });
    tie.setLedColour (LedColour::Amber);
    rest.setLedColour (LedColour::Amber);

    autoInc.setTooltip ("Auto increment: the next note entered goes to the following step, so the flags you press after a note "
                        "apply to that note.");
    preview.setTooltip ("Note preview: a key sounds the note while you hold it, and an octave change sounds the step, while the sequencer "
                        "is stopped.");
    ratchet.setTooltip ("Ratchet: how many times the step strikes (1 to 4). Click to step through.");
    gate.setTooltip ("Gate of this step alone: PAT uses the pattern's gate length, or 25, 50, 75 or 100 % of the step.");
    tie.setTooltip ("Tie: hold the previous note through this step.");
    rest.setTooltip ("Rest: silence this step (its pitch is kept).");
    accent.setTooltip ("Accent: louder and brighter, charging the accent sweep.");
    slide.setTooltip ("Slide: glide from this note into the next one, gate held.");
    vibrato.setTooltip ("Vibrato on this step.");
    prevButton.setTooltip ("Previous step");
    nextButton.setTooltip ("Next step");

    model.addListener (this);
    refreshButtons();
}

StepKeyboard::~StepKeyboard()
{
    if (previewHeld)
        model.releasePreview();

    model.removeListener (this);
}

void StepKeyboard::resized()
{
    auto b = getLocalBounds().reduced (10, 8);

    stepRow = b.removeFromTop (58).toFloat();
    b.removeFromTop (10);

    auto row = b.removeFromTop (26);

    for (auto& pb : pageButtons)
    {
        pb.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (4);
    }

    row.removeFromLeft (12);
    prevButton.setBounds (row.removeFromLeft (34));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromLeft (34));
    row.removeFromLeft (12);
    autoInc.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (4);
    preview.setBounds (row.removeFromLeft (88));
    row.removeFromLeft (12);
    readout = row.toFloat();

    b.removeFromTop (14);

    // Body: octave buttons on the left (highest at the top, like the roll's octave lane),
    // the keyboard in the middle, step flags on the right.
    // The flag stack has seven buttons; the octave stack's five use the same height.
    const auto gap = 4, captionH = 16;
    const auto buttonH = juce::jlimit (20, 34, (b.getHeight() - captionH - 6 * gap) / 7);
    const auto stackH = captionH + 7 * buttonH + 6 * gap;
    const auto top = b.getY() + juce::jmax (0, (b.getHeight() - stackH) / 2);

    auto left = b.removeFromLeft (78).withY (top).withHeight (stackH);
    auto right = b.removeFromRight (104).withY (top).withHeight (stackH);
    octaveCaption = left.removeFromTop (captionH).toFloat();
    flagCaption = right.removeFromTop (captionH).toFloat();

    for (int i = 4; i >= 0; --i)
    {
        octaveButtons[i].setBounds (left.removeFromTop (buttonH));
        left.removeFromTop (gap);
    }

    for (auto* fb : { &accent, &slide, &vibrato, &tie, &rest, &ratchet, &gate })
    {
        fb->setBounds (right.removeFromTop (buttonH));
        right.removeFromTop (gap);
    }

    keyboard = b.reduced (26, 0).withY (top + captionH).withHeight (stackH - captionH).toFloat();
}

juce::Rectangle<float> StepKeyboard::stepKey (int column) const noexcept
{
    const auto w = stepRow.getWidth() / static_cast<float> (kStepsPerPage);
    // A wider gap after every fourth step marks the beat.
    return juce::Rectangle<float> (stepRow.getX() + w * static_cast<float> (column), stepRow.getY(), w, stepRow.getHeight())
        .withTrimmedRight (column % 4 == 3 ? 7.0f : 3.0f);
}

juce::Rectangle<float> StepKeyboard::keyBounds (int index, bool& isBlack) const noexcept
{
    // Arpeggiator patterns: ten equal index keys. Otherwise a one-octave keyboard, pitch class order.
    if (model.isArpPattern())
    {
        isBlack = false;
        const auto w = juce::jmin (56.0f, keyboard.getWidth() / 10.0f);
        const auto h = juce::jmin (keyboard.getHeight(), 96.0f);
        const auto x0 = keyboard.getCentreX() - w * 5.0f;
        return juce::Rectangle<float> (x0 + w * static_cast<float> (index) + 2.0f, keyboard.getCentreY() - h * 0.5f, w - 4.0f, h);
    }

    const auto whiteW = juce::jmin (74.0f, keyboard.getWidth() / 7.0f);
    const auto x0 = keyboard.getCentreX() - whiteW * 3.5f;

    for (int i = 0; i < 7; ++i)
        if (kWhitePc[i] == index)
        {
            isBlack = false;
            return juce::Rectangle<float> (x0 + whiteW * static_cast<float> (i), keyboard.getY(), whiteW - 3.0f, keyboard.getHeight());
        }

    for (int i = 0; i < 5; ++i)
        if (kBlackPc[i] == index)
        {
            isBlack = true;
            const auto bw = whiteW * 0.62f;
            return juce::Rectangle<float> (x0 + whiteW * kBlackOffset[i] - bw * 0.5f - 1.5f, keyboard.getY(), bw, keyboard.getHeight() * 0.58f);
        }

    isBlack = false;
    return {};
}

int StepKeyboard::keyAt (juce::Point<float> pos) const noexcept
{
    bool black = false;

    if (model.isArpPattern())
    {
        for (int i = 0; i < seq::kMaxArpIndex; ++i)
            if (keyBounds (i, black).contains (pos))
                return i;

        return -1;
    }

    for (auto pc : kBlackPc)
        if (keyBounds (pc, black).contains (pos))
            return pc;

    for (auto pc : kWhitePc)
        if (keyBounds (pc, black).contains (pos))
            return pc;

    return -1;
}

void StepKeyboard::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto& p = model.getPattern();
    g.fillAll (t.screenFill);

    const auto selected = model.getSelectedStep();
    const auto playStep = model.getPlayStep();
    const auto length = static_cast<int> (p.length);
    const auto arp = model.isArpPattern();

    // Step keys.
    for (int c = 0; c < kStepsPerPage; ++c)
    {
        const auto s = page * kStepsPerPage + c;
        const auto key = stepKey (c);
        const auto inPattern = s < length;
        const auto& st = p.steps[static_cast<std::size_t> (s)];
        auto ledArea = key.withHeight (16.0f).withSizeKeepingCentre (9.0f, 9.0f);
        const auto cap = key.withTrimmedTop (18.0f);

        if (s == playStep)
            draw::led (g, ledArea, t.ledRed, true, t);
        else if (s == selected && inPattern)
            draw::led (g, ledArea, t.accent, true, t);
        else
            draw::led (g, ledArea, t.accent, false, t);

        if (! inPattern)
        {
            g.setColour (t.screenRowDark);
            g.fillRoundedRectangle (cap, metrics::kCorner);
            draw::text (g, juce::String (s + 1), cap, fonts::readout (12.0f), t.screenTextDim.withAlpha (0.4f));
            continue;
        }

        draw::buttonFace (g, cap, t, s == selected, false);

        if (s == selected)
        {
            g.setColour (t.accent);
            g.drawRoundedRectangle (cap.reduced (0.5f), metrics::kCorner, 1.5f);
        }

        draw::text (g, juce::String (s + 1), cap.withTrimmedBottom (13.0f).translated (0.0f, 2.0f), fonts::legend (12.0f), t.buttonText);

        // What the step holds, in small print under the number.
        juce::String content = "-";

        if (st.state == seq::StepState::Tie)
            content = "TIE";
        else if (st.state == seq::StepState::Note)
            content = arp ? "#" + juce::String (static_cast<int> (st.arpIndex)) : juce::String (kNoteNames[st.note % 12]);

        draw::text (g, content, cap.withTop (cap.getBottom() - 15.0f), fonts::readout (10.5f),
                    st.state == seq::StepState::Rest ? t.buttonText.withAlpha (0.35f) : (t.id == ThemeId::Silver ? t.accentOnPlate : t.accent));
    }

    // Readout of the selected step.
    draw::well (g, readout, t);
    const auto& sel = p.steps[static_cast<std::size_t> (juce::jlimit (0, seq::kMaxSteps - 1, selected))];
    juce::String info = "STEP " + juce::String (selected + 1).paddedLeft ('0', 2) + "  ";

    if (sel.state == seq::StepState::Rest)      info << "REST";
    else if (sel.state == seq::StepState::Tie)  info << "TIE";
    else if (arp)                               info << "NOTE " << static_cast<int> (sel.arpIndex);
    else                                        info << kNoteNames[sel.note % 12];

    info << "  OCT " << (sel.octave > 0 ? "+" : "") << static_cast<int> (sel.octave);

    if (sel.accent)   info << "  ACC";
    if (sel.slide)    info << "  SLD";
    if (sel.vibrato)  info << "  VIB";
    if (sel.ratchet > 1) info << "  R" << static_cast<int> (sel.ratchet);

    if (autoInc.getToggleState() && advanceFrom == selected)
        info << "  > NEXT NOTE: STEP " << (selected + 1) % juce::jmax (1, length) + 1;

    draw::text (g, info, readout.reduced (8.0f, 0.0f), fonts::readout (12.5f), t.wellText, juce::Justification::centredLeft);

    draw::text (g, "OCTAVE", octaveCaption, fonts::legend (10.5f, 0.08f), t.screenTextDim, juce::Justification::centredLeft);
    draw::text (g, arp ? "STEP" : "STEP FLAGS", flagCaption, fonts::legend (10.5f, 0.08f), t.screenTextDim, juce::Justification::centredLeft);

    // Keyboard, or the held-note index keys for arpeggiator patterns.
    const auto litKey = sel.state == seq::StepState::Note ? (arp ? static_cast<int> (sel.arpIndex) - 1 : sel.note % 12) : -1;
    bool black = false;

    const auto drawKey = [&] (int index)
    {
        const auto b = keyBounds (index, black);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 3.0f);
        g.setGradientFill (juce::ColourGradient (black ? t.keyBlack.brighter (0.12f) : t.keyWhite, 0.0f, b.getY(),
                                                 black ? t.keyBlack : t.keyWhite.darker (0.12f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (t.keyOutline);
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        const auto ledD = 9.0f;
        draw::led (g, juce::Rectangle<float> (b.getCentreX() - ledD * 0.5f, b.getBottom() - (black ? 18.0f : 34.0f), ledD, ledD),
                   t.accent, index == litKey, t);

        if (! black)
            draw::text (g, arp ? juce::String (index + 1) : juce::String (kNoteNames[index]), b.withTop (b.getBottom() - 20.0f),
                        fonts::legend (11.0f), t.keyBlack);
    };

    if (arp)
    {
        for (int i = 0; i < seq::kMaxArpIndex; ++i)
            drawKey (i);
    }
    else
    {
        for (auto pc : kWhitePc)  drawKey (pc);
        for (auto pc : kBlackPc)  drawKey (pc);
    }
}

void StepKeyboard::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (stepRow.contains (e.position))
    {
        for (int c = 0; c < kStepsPerPage; ++c)
            if (stepKey (c).contains (e.position))
            {
                const auto s = page * kStepsPerPage + c;

                if (s < static_cast<int> (model.getPattern().length))
                {
                    advanceFrom = -1;   // a step picked by hand takes the next note
                    model.setSelectedStep (s);
                }

                return;
            }

        return;
    }

    const auto key = keyAt (e.position);

    if (key >= 0)
        enterNote (key);
}

void StepKeyboard::mouseUp (const juce::MouseEvent&)
{
    if (previewHeld)
    {
        previewHeld = false;
        model.releasePreview();
    }
}

void StepKeyboard::previewSelected (bool held)
{
    const auto& p = model.getPattern();
    const auto& s = p.steps[static_cast<std::size_t> (juce::jlimit (0, seq::kMaxSteps - 1, model.getSelectedStep()))];

    if (! preview.getToggleState() || model.isArpPattern() || s.state != seq::StepState::Note)
        return;

    model.previewNote (seq::stepMidiNote (s), s.accent, held);
    previewHeld = held;
}

void StepKeyboard::editSelected (const std::function<void (seq::Step&)>& fn)
{
    auto edited = model.getPattern();
    fn (edited.steps[static_cast<std::size_t> (juce::jlimit (0, seq::kMaxSteps - 1, model.getSelectedStep()))]);
    model.applyPattern (edited);
}

void StepKeyboard::enterNote (int key)
{
    const auto arp = model.isArpPattern();

    // AUTO INC advances before the entry, not after it: the flags pressed between two notes
    // belong to the first one, which is still selected and shown in the readout.
    if (autoInc.getToggleState() && advanceFrom >= 0 && advanceFrom == model.getSelectedStep())
        moveSelection (1);

    advanceFrom = -1;

    editSelected ([key, arp] (seq::Step& s)
    {
        s.state = seq::StepState::Note;

        if (arp)
            s.arpIndex = static_cast<std::uint8_t> (key + 1);
        else
            s.note = static_cast<std::uint8_t> (key);
    });

    previewSelected (true);   // until the mouse is released

    if (autoInc.getToggleState())
        advanceFrom = model.getSelectedStep();
}

void StepKeyboard::moveSelection (int delta)
{
    advanceFrom = -1;
    const auto length = juce::jmax (1, static_cast<int> (model.getPattern().length));
    model.setSelectedStep (((model.getSelectedStep() + delta) % length + length) % length);
}

void StepKeyboard::refreshButtons()
{
    const auto& p = model.getPattern();
    const auto& s = p.steps[static_cast<std::size_t> (juce::jlimit (0, seq::kMaxSteps - 1, model.getSelectedStep()))];

    for (int i = 0; i < 4; ++i)
    {
        pageButtons[i].setToggleState (i == page, juce::dontSendNotification);
        pageButtons[i].setEnabled (i * kStepsPerPage < static_cast<int> (p.length));
    }

    for (int i = 0; i < 5; ++i)
        octaveButtons[i].setToggleState (s.octave == seq::kMinOctave + i, juce::dontSendNotification);

    accent.setToggleState (s.accent, juce::dontSendNotification);
    slide.setToggleState (s.slide, juce::dontSendNotification);
    vibrato.setToggleState (s.vibrato, juce::dontSendNotification);
    tie.setToggleState (s.state == seq::StepState::Tie, juce::dontSendNotification);
    rest.setToggleState (s.state == seq::StepState::Rest, juce::dontSendNotification);

    const auto hits = juce::jlimit (1, 4, static_cast<int> (s.ratchet));
    ratchet.setToggleState (hits > 1, juce::dontSendNotification);
    ratchet.setButtonText ("RATCHET " + juce::String (hits));
    gate.setToggleState (s.gate > 0, juce::dontSendNotification);
    gate.setButtonText (s.gate == 0 ? juce::String ("GATE PAT") : "GATE " + juce::String (juce::roundToInt (s.gate * 100.0f / 127.0f)) + "%");
}

void StepKeyboard::patternChanged()
{
    refreshButtons();
    repaint();
}

void StepKeyboard::playStepChanged (int, int)
{
    repaint (stepRow.getSmallestIntegerContainer().expanded (4, 6));
}

void StepKeyboard::selectedStepChanged (int, int now)
{
    page = juce::jlimit (0, 3, now / kStepsPerPage);
    refreshButtons();
    repaint();
}
}
