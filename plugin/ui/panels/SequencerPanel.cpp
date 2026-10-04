#include "panels/Panels.h"

namespace acid::ui
{
namespace
{
constexpr int kPad = 8, kHeaderH = 26, kBodyY = 40;
constexpr int kToolsW = 176, kRightW = 192;
constexpr int kRowH = 22, kRowPitch = 27, kCaptionW = 64;
constexpr ParamId kOverrideIds[8] = { kOvrNoteSet, kOvrAccentSet, kOvrSlideSet, kOvrVibSet, kOvrNoteClr, kOvrAccentClr, kOvrSlideClr, kOvrVibClr };
constexpr const char* kOverrideColumns[4] = { "NOTE", "ACC", "SLIDE", "VIB" };
}

struct SequencerPanel::ModelListener final : PatternEditModel::Listener
{
    explicit ModelListener (SequencerPanel& p) : panel (p) {}
    void patternChanged() override { panel.refreshFromPattern(); }
    SequencerPanel& panel;
};

struct SequencerPanel::GestureListener final : juce::Slider::Listener
{
    explicit GestureListener (PatternEditModel& m) : model (m) {}
    void sliderValueChanged (juce::Slider*) override {}
    void sliderDragStarted (juce::Slider*) override { model.beginEditGesture(); }
    void sliderDragEnded (juce::Slider*) override { model.endEditGesture(); }
    PatternEditModel& model;
};

SequencerPanel::SequencerPanel (ControlSet& c, PatternEditModel& m)
    : roll (m), keys (m), generator (m),
      run (c.ledButton (kSeqRun, "RUN", LedColour::Red, LedButton::Style::Cap)),
      liveTranspose (c.numeric (kLiveTranspose, "TRANSPOSE", NumericDisplay::CaptionPlacement::Left)),
      model (m)
{
    generator.setSurface (Surface::Dark);
    selector.setSurface (Surface::Dark);

    // The selector's two halves carry pattern_bank and pattern_slot, so they attach like any
    // control and their right-click menu offers MIDI learn of bank and pattern select.
    c.add (kPatternBank, selector.bankKeys);
    c.add (kPatternSlot, selector.slotKeys);
    run.setLatchedCap (true);

    tempoBox.setRange (20.0, 300.0, 0.1);
    tempoBox.setDefaultValue (120.0);
    tempoBox.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
    tempoBox.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
    tempoBox.param.name = "Internal Tempo";

    patternName.setTextWhenNothingSelected ("Pattern");

    rateBox.setItems ({ "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" });
    directionBox.setItems ({ "Forward", "Reverse", "Ping-Pong", "Random" });
    triggerBox.setItems ({ "Live", "Host Note" });
    triggerBox.setSelectedItemIndex (0, juce::dontSendNotification);
    launchBox.setItems ({ "Pattern end", "Next bar", "At once" });
    launchBox.setSelectedItemIndex (0, juce::dontSendNotification);

    lengthBox.setRange (1.0, static_cast<double> (seq::kMaxSteps), 1.0);
    lengthBox.setDefaultValue (16.0);
    shuffleBox.setRange (0.0, 100.0, 1.0);
    shuffleBox.setDefaultValue (0.0);
    shuffleBox.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    gateBox.setRange (0.0, 100.0, 1.0);
    gateBox.setDefaultValue (0.0);
    gateBox.setMinimumText ("STOCK");
    gateBox.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };

    for (auto* n : { &lengthBox, &shuffleBox, &gateBox, &liveTranspose, &tempoBox })
    {
        n->setSurface (Surface::Dark);
        n->setCaptionWidth (kCaptionW);
    }

    lengthBox.param.name = "Pattern Length";
    shuffleBox.param.name = "Shuffle";
    gateBox.param.name = "Gate Length";

    lengthBox.onValueChange = [this] { editPattern ([this] (seq::Pattern& p) { p.length = static_cast<std::uint8_t> (juce::roundToInt (lengthBox.getValue())); }); };
    shuffleBox.onValueChange = [this] { editPattern ([this] (seq::Pattern& p) { p.shuffle = static_cast<std::uint8_t> (juce::roundToInt (shuffleBox.getValue())); }); };
    gateBox.onValueChange = [this] { editPattern ([this] (seq::Pattern& p) { p.gateLength = static_cast<std::uint8_t> (juce::roundToInt (gateBox.getValue())); }); };
    rateBox.onChange = [this] { editPattern ([this] (seq::Pattern& p) { p.div = static_cast<seq::StepDiv> (juce::jmax (0, rateBox.getSelectedItemIndex())); }); };
    directionBox.onChange = [this] { editPattern ([this] (seq::Pattern& p) { p.direction = static_cast<seq::Direction> (juce::jmax (0, directionBox.getSelectedItemIndex())); }); };
    divModSelector.onValueChange = [this] { editPattern ([this] (seq::Pattern& p) { p.divMod = static_cast<seq::DivMod> (divModSelector.getSelectedSegment()); }); };

    shiftLeft.onClick = [this] { if (onShift != nullptr) onShift (-1); };
    shiftRight.onClick = [this] { if (onShift != nullptr) onShift (1); };
    transposeDown.onClick = [this] { if (onTranspose != nullptr) onTranspose (-1); };
    transposeUp.onClick = [this] { if (onTranspose != nullptr) onTranspose (1); };

    viewSelector.onValueChange = [this] { setView (viewSelector.getSelectedSegment() == 1 ? View::Keyboard : View::PianoRoll); };
    toolsSelector.onValueChange = [this] { setToolsTab (toolsSelector.getSelectedSegment() == 1 ? ToolsTab::Generate : ToolsTab::Edit); };

    // The live overrides stay on screen in both tool tabs: a performer drawing patterns keeps them.
    for (int i = 0; i < 8; ++i)
    {
        overrides[i] = &c.pushButton (kOverrideIds[i], i < 4 ? "SET" : "CLR");
        addAndMakeVisible (overrides[i]);
    }

    for (auto* comp : std::initializer_list<juce::Component*> { &lengthBox, &rateBox, &divModSelector, &shuffleBox, &gateBox, &directionBox,
                                                                &shiftLeft, &shiftRight, &transposeDown, &transposeUp })
        editTools.add (comp);

    for (auto* comp : editTools)
        addAndMakeVisible (comp);

    for (auto* comp : std::initializer_list<juce::Component*> { &roll, &viewSelector, &toolsSelector, &prevPattern, &nextPattern, &patternName,
                                                                &copyButton, &pasteButton, &clearButton, &dupButton, &undoButton, &redoButton,
                                                                &midiDrag, &importButton, &exportButton, &run, &liveTranspose, &triggerBox,
                                                                &launchBox, &selector, &tempoBox })
        addAndMakeVisible (comp);

    setTooltips();

    addChildComponent (keys);
    addChildComponent (generator);

    gestures = std::make_unique<GestureListener> (model);

    for (auto* n : { &lengthBox, &shuffleBox, &gateBox })
        n->addListener (gestures.get());

    listener = std::make_unique<ModelListener> (*this);
    model.addListener (listener.get());
    refreshFromPattern();
}

SequencerPanel::~SequencerPanel()
{
    for (auto* n : { &lengthBox, &shuffleBox, &gateBox })
        n->removeListener (gestures.get());

    model.removeListener (listener.get());
}

void SequencerPanel::setTooltips()
{
    viewSelector.setTooltip ("Step editor: piano roll or keyboard (step by step, as on the hardware).");
    toolsSelector.setTooltip ("Pattern properties and edit tools, or the pattern generator.");
    patternName.setTooltip ("The pattern in the edited slot. Click to browse pattern presets; right-click to rename.");
    prevPattern.setTooltip ("Load the previous pattern preset into this slot (undoable).");
    nextPattern.setTooltip ("Load the next pattern preset into this slot (undoable).");
    importButton.setTooltip ("Import a MIDI file into this slot.");
    exportButton.setTooltip ("Export this slot as a MIDI file.");
    midiDrag.setTooltip ("Drag to a DAW track or the desktop: a MIDI file of this pattern. Shift-drag: a pattern file (.mw12f) "
                         "for another instance.");
    copyButton.setTooltip ("Copy the pattern to the clipboard (paste it into any instance).");
    pasteButton.setTooltip ("Paste the clipboard's pattern into this slot.");
    clearButton.setTooltip ("Clear every step of this slot.");
    dupButton.setTooltip ("Duplicate: copy the first half of the steps over the second half.");
    undoButton.setTooltip ("Undo the last pattern or settings edit.");
    redoButton.setTooltip ("Redo");
    shiftLeft.setTooltip ("Shift every step one place left (wraps).");
    shiftRight.setTooltip ("Shift every step one place right (wraps).");
    transposeDown.setTooltip ("Transpose the pattern down a semitone (arpeggiator patterns: inversion down).");
    transposeUp.setTooltip ("Transpose the pattern up a semitone (arpeggiator patterns: inversion up).");
    lengthBox.setTooltip ("Pattern length in steps (1 to 64). Drag, or right-click to type.");
    rateBox.setTooltip ("Step length.");
    divModSelector.setTooltip ("Step timing: straight, dotted or triplet.");
    shuffleBox.setTooltip ("Shuffle: delays every second step.");
    gateBox.setTooltip ("Gate length of every step: STOCK is the original's half step; 1 to 100 % of the step.");
    directionBox.setTooltip ("Play direction.");
    triggerBox.setTooltip ("Live: RUN and the pattern keys play the line. Host Note: MIDI notes from the host start patterns "
                           "(the keys show which note starts which pattern).");
    launchBox.setTooltip ("When a newly selected pattern takes over: at the end of the pattern, at the next bar, or at once.");
    liveTranspose.setTooltip ("Transposes playback live, without changing the pattern.");
    tempoBox.setTooltip ("Internal clock tempo: used when no host transport drives the sequencer (standalone, or the host stopped). "
                         "Greyed while the host's clock is in charge.");
    run.setTooltip ("RUN: the cap shows the switch, the LED whether the line is playing. INT: the internal clock drives it, and it "
                    "keeps playing when the host stops. HOST: the host transport drives it.");

    static constexpr const char* kOverrideTips[8] = {
        "Hold: every step plays a note.", "Hold: every note is accented.", "Hold: every note slides.", "Hold: every note gets vibrato.",
        "Hold: mutes every step.", "Hold: no accents.", "Hold: no slides.", "Hold: no vibrato."
    };

    for (int i = 0; i < 8; ++i)
        overrides[i]->setTooltip (juce::String ("Live override. ") + kOverrideTips[i]);
}

void SequencerPanel::editPattern (const std::function<void (seq::Pattern&)>& fn)
{
    if (refreshing)
        return;

    auto edited = model.getPattern();
    fn (edited);
    model.applyPattern (edited);
}

void SequencerPanel::refreshFromPattern()
{
    const auto& p = model.getPattern();
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    lengthBox.setValue (p.length, juce::dontSendNotification);
    shuffleBox.setValue (p.shuffle, juce::dontSendNotification);
    gateBox.setValue (p.gateLength, juce::dontSendNotification);
    rateBox.setSelectedItemIndex (static_cast<int> (p.div), juce::dontSendNotification);
    directionBox.setSelectedItemIndex (static_cast<int> (p.direction), juce::dontSendNotification);
    divModSelector.setValue (static_cast<double> (p.divMod), juce::dontSendNotification);

    if (toolsTab == ToolsTab::Generate)
        roll.setGeneratorHighlight (true, generator.getAbsoluteNoteMask(), p.gen.octaveMask);
}

void SequencerPanel::setView (View v)
{
    view = v;
    viewSelector.setValue (v == View::Keyboard ? 1.0 : 0.0, juce::dontSendNotification);
    updateVisibility();
}

void SequencerPanel::setToolsTab (ToolsTab tab)
{
    toolsTab = (arpMode && tab == ToolsTab::Generate) ? ToolsTab::Edit : tab;
    toolsSelector.setValue (toolsTab == ToolsTab::Generate ? 1.0 : 0.0, juce::dontSendNotification);
    updateVisibility();
}

void SequencerPanel::setArpMode (bool arp)
{
    arpMode = arp;

    if (arp && toolsTab == ToolsTab::Generate)
        setToolsTab (ToolsTab::Edit);

    toolsSelector.setEnabled (! arp);   // no generator for arpeggiator templates

    updateVisibility();
}

void SequencerPanel::updateVisibility()
{
    roll.setVisible (view == View::PianoRoll);
    keys.setVisible (view == View::Keyboard);

    const auto edit = toolsTab == ToolsTab::Edit;

    for (auto* comp : editTools)
        comp->setVisible (edit);

    generator.setVisible (! edit);
    roll.setGeneratorHighlight (! edit, generator.getAbsoluteNoteMask(), model.getPattern().gen.octaveMask);
    repaint();
}

void SequencerPanel::resized()
{
    const auto w = getWidth(), h = getHeight();
    const auto rightX = w - kPad - kRightW;
    const auto toolsX = rightX - kPad - kToolsW;
    editorArea = { kPad, kBodyY, toolsX - kPad - kPad, h - kBodyY - kPad };
    toolsArea = { toolsX, kBodyY, kToolsW, h - kBodyY - kPad };
    rightArea = { rightX, kBodyY, kRightW, h - kBodyY - kPad };

    roll.setBounds (editorArea);
    keys.setBounds (editorArea);

    // Header row over the editor.
    auto x = kPad;
    viewSelector.setBounds (x, kPad, 112, kHeaderH);
    x += 112 + 12;
    prevPattern.setBounds (x, kPad + 1, 24, kHeaderH - 2);
    nextPattern.setBounds (x + 26, kPad + 1, 24, kHeaderH - 2);
    patternName.setBounds (x + 54, kPad + 1, 250, kHeaderH - 2);
    exportButton.setBounds (editorArea.getRight() - 84, kPad + 1, 84, kHeaderH - 2);
    midiDrag.setBounds (exportButton.getX() - 6 - 104, kPad + 1, 104, kHeaderH - 2);
    importButton.setBounds (midiDrag.getX() - 6 - 84, kPad + 1, 84, kHeaderH - 2);

    // Tools column. The generator fills it down to the live override block.
    toolsSelector.setBounds (toolsX, kPad, kToolsW, kHeaderH);
    const auto overrideH = 14 + 20 + 2 * 28;
    overrideArea = { toolsX, toolsArea.getBottom() - overrideH, kToolsW, overrideH };
    generator.setBounds (toolsArea.withTrimmedTop (4).withBottom (overrideArea.getY() - 6));

    auto y = kBodyY + 4;
    const auto fieldX = toolsX + kCaptionW;
    const auto fieldW = kToolsW - kCaptionW;
    lengthBox.setBounds (toolsX, y, kToolsW, kRowH);
    y += kRowPitch;
    rateBox.setBounds (fieldX, y, fieldW, kRowH);
    y += kRowPitch;
    divModSelector.setBounds (fieldX, y, fieldW, kRowH);
    y += kRowPitch;
    shuffleBox.setBounds (toolsX, y, kToolsW, kRowH);
    y += kRowPitch;
    gateBox.setBounds (toolsX, y, kToolsW, kRowH);
    y += kRowPitch;
    directionBox.setBounds (fieldX, y, fieldW, kRowH);
    y += kRowPitch + 4;

    const auto half = (fieldW - 4) / 2;
    shiftLeft.setBounds (fieldX, y, half, kRowH + 2);
    shiftRight.setBounds (fieldX + half + 4, y, half, kRowH + 2);
    y += kRowPitch + 2;
    transposeDown.setBounds (fieldX, y, half, kRowH + 2);
    transposeUp.setBounds (fieldX + half + 4, y, half, kRowH + 2);
    y += kRowPitch + 10;

    // Live override sits at the foot of the column, apart from the pattern properties.
    const auto colW = (kToolsW + 4) / 4;
    const auto buttonsY = overrideArea.getY() + 34;

    for (int i = 0; i < 8; ++i)
        overrides[i]->setBounds (toolsX + colW * (i % 4), buttonsY + (i / 4) * 28, colW - 4, 24);

    // Right column: transport, then the selector and pattern tools.
    run.setBounds (rightX, kPad, 84, kHeaderH);
    triggerBox.setBounds (rightX + 90, kPad + 1, kRightW - 90, kHeaderH - 2);
    y = kBodyY + 4;
    liveTranspose.setBounds (rightX, y, kRightW, kRowH);
    y += kRowPitch;
    launchBox.setBounds (rightX + kCaptionW, y, kRightW - kCaptionW, kRowH);
    y += kRowPitch;
    tempoBox.setBounds (rightX, y, kRightW, kRowH);
    y += kRowPitch + 4;

    const auto third = (kRightW - 8) / 3;
    const auto toolsY = rightArea.getBottom() - 24;
    const auto historyY = toolsY - 28;
    copyButton.setBounds (rightX, toolsY, third, 24);
    pasteButton.setBounds (rightX + third + 4, toolsY, third, 24);
    clearButton.setBounds (rightX + 2 * (third + 4), toolsY, third, 24);
    dupButton.setBounds (rightX, historyY, third, 24);
    undoButton.setBounds (rightX + third + 4, historyY, third, 24);
    redoButton.setBounds (rightX + 2 * (third + 4), historyY, third, 24);
    selector.setBounds (rightX, y, kRightW, juce::jmin (238, historyY - 10 - y));
}

void SequencerPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);

    const auto& t = themeFor (*this);
    const auto caption = fonts::legend (10.5f);
    const auto ink = t.screenText;
    const auto label = [&] (const juce::Component& c, const juce::String& text)
    {
        draw::text (g, text, juce::Rectangle<float> (static_cast<float> (toolsArea.getX()), static_cast<float> (c.getY()), static_cast<float> (kCaptionW),
                                                     static_cast<float> (c.getHeight())),
                    caption, ink, juce::Justification::centredLeft);
    };

    if (toolsTab == ToolsTab::Edit)
    {
        label (rateBox, "RATE");
        label (divModSelector, "TIMING");
        label (directionBox, "DIRECTION");
        label (shiftLeft, "SHIFT");
        label (transposeDown, arpMode ? "INVERSION" : "TRANSPOSE");
    }

    {
        const auto o = overrideArea.toFloat();
        const auto title = fonts::bold (10.5f, 0.14f);
        const auto titleW = juce::GlyphArrangement::getStringWidth (title, "LIVE OVERRIDE");
        draw::text (g, "LIVE OVERRIDE", o.withHeight (14.0f), title, t.screenTextDim, juce::Justification::centredLeft);
        g.setColour (t.screenGridBar);
        g.fillRect (o.getX() + titleW + 8.0f, o.getY() + 7.0f, o.getWidth() - titleW - 8.0f, 1.0f);

        for (int i = 0; i < 4; ++i)
            draw::text (g, kOverrideColumns[i], overrides[i]->getBounds().toFloat().withHeight (12.0f).translated (0.0f, -14.0f),
                        fonts::legend (10.0f), ink);
    }
}

void SequencerPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::screen (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), t);

    draw::text (g, "LAUNCH", juce::Rectangle<float> (static_cast<float> (rightArea.getX()), static_cast<float> (launchBox.getY()),
                                                     static_cast<float> (kCaptionW), static_cast<float> (launchBox.getHeight())),
                fonts::legend (10.5f), t.screenText, juce::Justification::centredLeft);

    // Hairlines between the three columns of the screen.
    g.setColour (t.screenGridBar.withAlpha (0.6f));
    g.fillRect (static_cast<float> (toolsArea.getX()) - 4.5f, static_cast<float> (kPad), 1.0f, static_cast<float> (getHeight() - 2 * kPad));
    g.fillRect (static_cast<float> (rightArea.getX()) - 4.5f, static_cast<float> (kPad), 1.0f, static_cast<float> (getHeight() - 2 * kPad));
}
}
