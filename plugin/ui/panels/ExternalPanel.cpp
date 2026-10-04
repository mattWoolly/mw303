#include "panels/Panels.h"

namespace acid::ui
{
namespace
{
constexpr int kPad = 8, kHeaderH = 26, kBodyY = 40, kColumnGap = 16;
constexpr int kMonitorW = 330, kSettingsW = 330;
constexpr int kTitleH = 14, kRowH = 22, kRowPitch = 40, kGuidePitch = 44, kCaptionW = 118, kNoteH = 64;

/** What the incoming MIDI does in external mode (SPEC 6.4), one row each. */
constexpr const char* kGuide[][2] = {
    { "NOTES", "One voice. The note priority picks which held key sounds." },
    { "OVERLAP", "A key pressed while another is held slides to it; letting go slides back." },
    { "VELOCITY", "Above the accent threshold the note plays accented." },
    { "PITCH BEND", "Bends by up to the bend range, up or down." },
    { "MOD WHEEL", "Scales the vibrato that VIB SPEED and VIB DEPTH set." },
    { "SUSTAIN", "The sustain pedal (CC 64) holds the gate." },
    { "ALL OFF", "All notes off and reset controllers end the note." },
};

/** A column title on a hairline rule, as LIVE OVERRIDE on the sequencer screen. */
void columnTitle (juce::Graphics& g, const juce::Rectangle<int>& column, const juce::String& title, const Theme& t)
{
    const auto o = column.toFloat();
    const auto font = fonts::bold (10.5f, 0.14f);
    const auto titleW = juce::GlyphArrangement::getStringWidth (font, title);
    draw::text (g, title, o.withHeight (static_cast<float> (kTitleH)), font, t.screenTextDim, juce::Justification::centredLeft);
    g.setColour (t.screenGridBar);
    g.fillRect (o.getX() + titleW + 8.0f, o.getY() + 7.0f, o.getWidth() - titleW - 8.0f, 1.0f);
}

void note (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, const Theme& t)
{
    g.setFont (fonts::text (12.0f));
    g.setColour (t.screenTextDim);
    g.drawFittedText (text, area, juce::Justification::bottomLeft, juce::jmax (1, area.getHeight() / 15), 1.0f);
}
}

struct ExternalPanel::GestureListener final : juce::Slider::Listener
{
    explicit GestureListener (ExternalPanel& p) : panel (p) {}
    void sliderValueChanged (juce::Slider*) override {}
    void sliderDragStarted (juce::Slider*) override { if (panel.onGestureBegin != nullptr) panel.onGestureBegin(); }
    void sliderDragEnded (juce::Slider*) override { if (panel.onGestureEnd != nullptr) panel.onGestureEnd(); }
    ExternalPanel& panel;
};

ExternalPanel::ExternalPanel()
{
    const auto edit = [this] (const char* label, std::function<void (Settings&)> fn)
    {
        if (onSettingsEdit != nullptr)
            onSettingsEdit (label, fn);
    };

    thresholdBox.setRange (1.0, 126.0, 1.0);
    thresholdBox.setDefaultValue (64.0);
    thresholdBox.textFromValueFunction = [] (double v) { return "VEL " + juce::String (juce::roundToInt (v)); };
    thresholdBox.valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("0123456789").getDoubleValue(); };
    thresholdBox.param.name = "Accent Threshold";
    thresholdBox.onValueChange = [this, edit]
    {
        const auto v = static_cast<std::uint8_t> (juce::jlimit (1, 126, juce::roundToInt (thresholdBox.getValue())));
        edit ("Accent threshold", [v] (Settings& s) { s.accentThreshold = v; });
    };

    bendBox.setRange (0.0, 12.0, 1.0);
    bendBox.setDefaultValue (2.0);
    bendBox.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };
    bendBox.valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("0123456789").getDoubleValue(); };
    bendBox.param.name = "Pitch Bend Range";
    bendBox.onValueChange = [this, edit]
    {
        const auto v = static_cast<std::uint8_t> (juce::jlimit (0, 12, juce::roundToInt (bendBox.getValue())));
        edit ("Bend range", [v] (Settings& s) { s.bendRange = v; });
    };

    priorityBox.onValueChange = [this, edit]
    {
        const auto v = static_cast<seq::NotePriority> (juce::jlimit (0, 2, priorityBox.getSelectedSegment()));
        edit ("Note priority", [v] (Settings& s) { s.notePriority = v; });
    };

    thruButton.onClick = [this, edit]
    {
        const auto on = thruButton.getToggleState();
        edit ("MIDI thru", [on] (Settings& s) { s.midiThru = on; });
    };

    for (auto* n : { &thresholdBox, &bendBox })
    {
        n->setSurface (Surface::Dark);
        n->setCaptionWidth (kCaptionW);
    }

    thresholdBox.setTooltip ("A note with a velocity above this plays accented (1 to 126). Drag, or right-click to type.");
    bendBox.setTooltip ("Pitch bend range in semitones, up and down (0 to 12).");
    priorityBox.setTooltip ("Which held key sounds: the last one pressed, the lowest or the highest.");
    thruButton.setTooltip ("MIDI thru: pass the notes arriving at the MIDI input on to the MIDI output.");

    envelopeMeter.setLedColour (LedColour::Accent);

    for (auto* lamp : { &gateLamp, &accentLamp, &overloadLamp })
    {
        lamp->setSurface (Surface::Dark);
        lamp->setInterceptsMouseClicks (false, false);
        lamp->setLit (false);
    }

    for (auto* comp : std::initializer_list<juce::Component*> { &gateLamp, &accentLamp, &overloadLamp, &envelopeMeter, &chargeMeter,
                                                                &thresholdBox, &bendBox, &priorityBox, &thruButton })
        addAndMakeVisible (comp);

    gestures = std::make_unique<GestureListener> (*this);

    for (auto* n : { &thresholdBox, &bendBox })
        n->addListener (gestures.get());

    showSettings (Settings {});
}

ExternalPanel::~ExternalPanel()
{
    for (auto* n : { &thresholdBox, &bendBox })
        n->removeListener (gestures.get());
}

void ExternalPanel::showSettings (const Settings& s)
{
    thresholdBox.setValue (s.accentThreshold, juce::dontSendNotification);
    bendBox.setValue (s.bendRange, juce::dontSendNotification);
    priorityBox.setValue (static_cast<double> (s.notePriority), juce::dontSendNotification);
    thruButton.setToggleState (s.midiThru, juce::dontSendNotification);
}

void ExternalPanel::setStatus (bool gateOpen, bool accentLatched, float envelope01, float accentCharge01, bool midiInDropped)
{
    if (gateLamp.isLit() != gateOpen)           gateLamp.setLit (gateOpen);
    if (accentLamp.isLit() != accentLatched)    accentLamp.setLit (accentLatched);
    if (overloadLamp.isLit() != midiInDropped)  overloadLamp.setLit (midiInDropped);
    envelopeMeter.setCharge (envelope01);
    chargeMeter.setCharge (accentCharge01);
}

void ExternalPanel::resized()
{
    const auto h = getHeight();
    const auto bodyH = h - kBodyY - kPad;
    const auto left = kPad + 4;
    monitorArea = { left, kBodyY + 4, kMonitorW, bodyH - 4 };
    settingsArea = { monitorArea.getRight() + 2 * kColumnGap, kBodyY + 4, kSettingsW, bodyH - 4 };
    guideArea = { settingsArea.getRight() + 2 * kColumnGap, kBodyY + 4, getWidth() - kPad - 4 - (settingsArea.getRight() + 2 * kColumnGap), bodyH - 4 };

    // Monitor: the lamps, then the two ladders.
    auto y = monitorArea.getY() + kTitleH + 14;
    const auto mx = monitorArea.getX();
    gateLamp.setBounds (mx, y, 78, 20);
    accentLamp.setBounds (mx + 104, y, 90, 20);
    overloadLamp.setBounds (mx + 214, y, kMonitorW - 214, 20);
    y += kRowPitch + 4;
    envelopeMeter.setBounds (mx + kCaptionW, y + 2, kMonitorW - kCaptionW, 18);
    y += kRowPitch;
    chargeMeter.setBounds (mx + kCaptionW, y + 2, kMonitorW - kCaptionW, 18);

    // Settings: two readouts, the priority selector and the thru switch.
    y = settingsArea.getY() + kTitleH + 12;
    const auto sx = settingsArea.getX();
    thresholdBox.setBounds (sx, y, kSettingsW, kRowH);
    y += kRowPitch;
    bendBox.setBounds (sx, y, kSettingsW, kRowH);
    y += kRowPitch;
    priorityBox.setBounds (sx + kCaptionW, y, kSettingsW - kCaptionW, kRowH);
    y += kRowPitch;
    thruButton.setBounds (sx + kCaptionW, y - 1, kSettingsW - kCaptionW, kRowH + 2);
}

void ExternalPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::screen (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), t);

    // Header: what this screen is, and how to get the sequencer back.
    const auto header = juce::Rectangle<float> (static_cast<float> (kPad + 4), static_cast<float> (kPad), static_cast<float> (getWidth() - 2 * kPad - 8),
                                                static_cast<float> (kHeaderH));
    const auto titleFont = fonts::bold (13.0f, 0.12f);
    const auto title = juce::String ("EXTERNAL MIDI");
    const auto titleW = juce::GlyphArrangement::getStringWidth (titleFont, title);
    draw::text (g, title, header, titleFont, t.accent, juce::Justification::centredLeft);
    draw::text (g, "The voice plays the notes at the MIDI input; the sequencer rests. MODE SEQ or ARP in the top bar brings it back.",
                header.withTrimmedLeft (titleW + 18.0f), fonts::text (12.5f), t.screenText, juce::Justification::centredLeft);
    g.setColour (t.screenGridBar.withAlpha (0.6f));
    g.fillRect (static_cast<float> (kPad), static_cast<float> (kBodyY) - 3.5f, static_cast<float> (getWidth() - 2 * kPad), 1.0f);

    // Hairlines between the three columns, as on the sequencer screen.
    for (const auto& column : { settingsArea, guideArea })
        g.fillRect (static_cast<float> (column.getX() - kColumnGap) - 0.5f, static_cast<float> (kBodyY + 4), 1.0f,
                    static_cast<float> (getHeight() - kBodyY - 2 * kPad - 4));

    columnTitle (g, monitorArea, "MONITOR", t);
    columnTitle (g, settingsArea, "EXTERNAL MODE", t);
    columnTitle (g, guideArea, "WHAT THE INPUT DOES", t);

    const auto caption = fonts::legend (10.5f);
    const auto captionLeft = [&] (int columnX, int y, int height, const juce::String& text)
    {
        draw::text (g, text, juce::Rectangle<float> (static_cast<float> (columnX), static_cast<float> (y), static_cast<float> (kCaptionW),
                                                     static_cast<float> (height)),
                    caption, t.screenText, juce::Justification::centredLeft);
    };

    // Monitor captions and a word on what the lamps show.
    const auto mx = monitorArea.getX();
    captionLeft (mx, envelopeMeter.getY() - 2, 22, "ENVELOPE");
    captionLeft (mx, chargeMeter.getY() - 2, 22, "C13 CHARGE");
    note (g, "GATE lights while a note sounds, ACCENT while the accent latch holds. The C13 charge builds up over a run of accents. "
             "INPUT FULL: more MIDI arrived in one block than the engine takes.",
          { mx, monitorArea.getBottom() - kNoteH - 6, kMonitorW, kNoteH }, t);

    // Settings captions (the readouts draw their own).
    const auto sx = settingsArea.getX();
    captionLeft (sx, priorityBox.getY(), priorityBox.getHeight(), "NOTE PRIORITY");
    captionLeft (sx, thruButton.getY(), thruButton.getHeight(), "MIDI THRU");
    note (g, "Saved with the session, not automated. The cog menu holds the same settings under Sequencer and MIDI.",
          { sx, settingsArea.getBottom() - kNoteH - 6, kSettingsW, kNoteH }, t);

    // The guide.
    auto y = guideArea.getY() + kTitleH + 12;
    const auto keyW = 104;

    for (const auto& row : kGuide)
    {
        draw::text (g, row[0], juce::Rectangle<float> (static_cast<float> (guideArea.getX()), static_cast<float> (y), static_cast<float> (keyW),
                                                       static_cast<float> (kRowH)),
                    caption, t.screenText, juce::Justification::centredLeft);
        draw::text (g, row[1], juce::Rectangle<float> (static_cast<float> (guideArea.getX() + keyW), static_cast<float> (y),
                                                       static_cast<float> (guideArea.getWidth() - keyW), static_cast<float> (kRowH)),
                    fonts::text (13.0f), t.screenText, juce::Justification::centredLeft);
        y += kGuidePitch;
    }
}
}
