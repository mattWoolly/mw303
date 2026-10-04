// UI gallery: renders the kit, the step editors and the real editor (mw12FEditor over a real
// mw12FProcessor) to PNG files in both themes at real zoom steps, times the editor's paint
// against the repaint budget, drives interactions through the controls to check the bindings,
// and sends real mouse events through a desktop window's peer. Run under `xvfb-run -a`. The
// exit code is non-zero when a check fails.
//
//   mwacid_ui_gallery [output directory]     (default: <build dir>/gallery)
//
// The processor writes user files under $XDG_CONFIG_HOME, which is pointed at a scratch
// directory inside the output directory so no run touches the real configuration.
#include "mw12FEditor.h"
#include "editor/SettingsMenu.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>

using namespace acid::ui;
namespace seq = acid::seq;

namespace
{
juce::File outputDir;
int failures = 0;

juce::String themeTag (ThemeId id) { return juce::String (theme (id).name).toLowerCase(); }

void savePng (const juce::Image& image, const juce::String& name)
{
    const auto file = outputDir.getChildFile (name + ".png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;

    if (out.openedOk() && png.writeImageToStream (image, out))
        std::cout << file.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
    else
        std::cerr << "FAILED " << file.getFullPathName() << "\n";
}

void check (bool condition, const char* what)
{
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << "\n";

    if (! condition)
        ++failures;
}

/** A plain plate to lay components out on, with small captions naming each group. */
class Sheet final : public Faceplate
{
public:
    void caption (const juce::String& text, int x, int y) { captions.add ({ text, x, y }); }

    void paintPlate (juce::Graphics& g, const Theme& t) override
    {
        g.setGradientFill (juce::ColourGradient (t.plateTop, 0.0f, 0.0f, t.plateBottom, 0.0f, static_cast<float> (getHeight()), false));
        g.fillAll();

        for (auto& c : captions)
            draw::text (g, c.text, juce::Rectangle<float> (static_cast<float> (c.x), static_cast<float> (c.y), 400.0f, 12.0f),
                        fonts::text (10.5f), t.legendFaint, juce::Justification::centredLeft);
    }

private:
    struct Caption { juce::String text; int x, y; };
    juce::Array<Caption> captions;
};

template <typename T> T& place (Sheet& sheet, T& component, int x, int y, int w, int h)
{
    component.setBounds (x, y, w, h);
    sheet.addAndMakeVisible (component);
    return component;
}

seq::Pattern demoPattern (int length)
{
    seq::Pattern p;
    p.length = static_cast<std::uint8_t> (length);
    static constexpr int kNotes[16] = { 0, 0, -1, 3, 7, 0, -2, -1, 10, 0, 0, -1, 5, 7, 8, 0 };
    static constexpr int kOct[16] = { 0, 0, 0, 0, 0, 1, 1, 0, -1, 0, 1, 0, 0, 0, 0, 0 };

    for (int i = 0; i < length; ++i)
    {
        auto& s = p.steps[static_cast<std::size_t> (i)];
        const auto n = kNotes[i % 16];
        s.state = n == -1 ? seq::StepState::Rest : (n == -2 ? seq::StepState::Tie : seq::StepState::Note);
        s.note = static_cast<std::uint8_t> (juce::jmax (0, n));
        s.arpIndex = static_cast<std::uint8_t> (1 + (i * 3) % 5);
        s.octave = static_cast<std::int8_t> (kOct[i % 16]);
        s.accent = i % 5 == 0;
        s.slide = i % 16 == 3 || i % 16 == 9 || i % 16 == 13;
        s.vibrato = i % 16 == 10;
        s.probability = static_cast<std::uint8_t> (i % 16 == 4 ? 70 : (i % 16 == 12 ? 45 : 100));
    }

    return p;
}

//==============================================================================
void renderKitSheet (ThemeId themeId)
{
    mw12FLookAndFeel lf (themeId);
    Sheet sheet;
    sheet.setLookAndFeel (&lf);
    sheet.setSize (980, 640);
    ControlSet c;

    sheet.caption ("Knob: large, small, mini, bipolar, disabled", 16, 8);
    place (sheet, c.knob (acid::kCutoff, Knob::Size::Large, "CUTOFF"), 16, 24, 80, 86);
    auto& reso = place (sheet, c.knob (acid::kResonance, Knob::Size::Large, "RESONANCE"), 100, 24, 80, 86);
    place (sheet, c.knob (acid::kDecay, Knob::Size::Small, "DECAY"), 190, 42, 62, 68);
    place (sheet, c.knob (acid::kVibDepth, Knob::Size::Mini, "FINE"), 260, 56, 46, 54);
    auto& bip = place (sheet, c.knob (acid::kDistColor, Knob::Size::Small, "COLOUR"), 314, 42, 62, 68);
    bip.setValue (-45.0);
    auto& bipZero = place (sheet, c.knob (acid::kTune, Knob::Size::Small, "TUNING"), 380, 42, 62, 68);
    bipZero.setValue (0.0);
    auto& off = place (sheet, c.knob (acid::kSweepAmt, Knob::Size::Small, "SWEEP"), 446, 42, 62, 68);
    off.setEnabled (false);

    sheet.caption ("Value tooltip (hover or drag)", 100, 114);

    sheet.caption ("Switches: 2 and 3 position", 530, 8);
    auto& wave = place (sheet, c.toggleSwitch (acid::kWave, "WAVE"), 530, 54, 62, 55);
    wave.setPositionIcon (0, draw::Icon::Saw);
    wave.setPositionIcon (1, draw::Icon::Square);
    auto& order = place (sheet, c.toggleSwitch (acid::kDistDynPost, "DYN", { "PRE", "POST" }), 596, 54, 56, 55);
    order.setValue (1.0);
    auto& muffler = place (sheet, c.toggleSwitch (acid::kModMuffler, "MUFFLER"), 656, 39, 60, 70);
    muffler.setValue (1.0);

    sheet.caption ("Segmented selector", 740, 8);
    place (sheet, c.selector (acid::kPlayMode), 740, 24, 168, 24);
    SegmentedSelector two ({ "BASIC", "FULL" });
    place (sheet, two, 740, 56, 104, 24);

    sheet.caption ("Numeric drag display", 740, 90);
    NumericDisplay length ("LENGTH");
    length.setRange (1.0, 64.0, 1.0);
    length.setValue (16.0);
    place (sheet, length, 740, 104, 60, 34);
    place (sheet, c.numeric (acid::kEqLoMidFreq, "FREQ"), 806, 104, 74, 34);
    auto& gate = place (sheet, c.numeric (acid::kLimThreshold, "THRESH", NumericDisplay::CaptionPlacement::Left), 740, 144, 140, 20);
    gate.setValue (-12.5);

    sheet.caption ("LED toggle: lens, lens + label, cap; five lamp colours, off and on", 16, 150);
    static constexpr LedColour kColours[5] = { LedColour::Accent, LedColour::Red, LedColour::Blue, LedColour::Amber, LedColour::Turquoise };
    juce::OwnedArray<LedButton> leds;

    for (int i = 0; i < 5; ++i)
        for (int on = 0; on < 2; ++on)
        {
            auto* b = leds.add (new LedButton ({}, kColours[i], LedButton::Style::Lens));
            b->setToggleState (on == 1, juce::dontSendNotification);
            place (sheet, *b, 16 + i * 44 + on * 20, 166, 18, 18);
        }

    auto* lensLabel = leds.add (new LedButton ("HI RESO", LedColour::Accent, LedButton::Style::LensLabel));
    lensLabel->setToggleState (true, juce::dontSendNotification);
    place (sheet, *lensLabel, 250, 166, 80, 18);
    auto* lensLabelOff = leds.add (new LedButton ("EXT RANGE", LedColour::Accent, LedButton::Style::LensLabel));
    place (sheet, *lensLabelOff, 330, 166, 84, 18);
    auto* capOn = leds.add (new LedButton ("ACCENT", LedColour::Accent, LedButton::Style::Cap));
    capOn->setToggleState (true, juce::dontSendNotification);
    place (sheet, *capOn, 16, 194, 86, 24);
    auto* capOff = leds.add (new LedButton ("SLIDE", LedColour::Accent, LedButton::Style::Cap));
    place (sheet, *capOff, 106, 194, 86, 24);
    auto* capRun = leds.add (new LedButton ("RUN", LedColour::Red, LedButton::Style::Cap));
    capRun->setToggleState (true, juce::dontSendNotification);
    place (sheet, *capRun, 196, 194, 76, 24);

    sheet.caption ("Push button: text, icon, latched, prominent, disabled", 16, 226);
    PushButton copy ("COPY"), prev ({}, draw::Icon::Prev), next ({}, draw::Icon::Next), cog ({}, draw::Icon::Cog);
    PushButton exportButton ("EXPORT", draw::Icon::Export), latched ("1-16"), drawButton ("DRAW", draw::Icon::Dice), disabled ("PASTE");
    latched.setToggleState (true, juce::dontSendNotification);
    drawButton.setProminent (true);
    disabled.setEnabled (false);
    place (sheet, copy, 16, 242, 60, 24);
    place (sheet, prev, 80, 242, 24, 24);
    place (sheet, next, 106, 242, 24, 24);
    place (sheet, cog, 134, 242, 28, 24);
    place (sheet, exportButton, 166, 242, 84, 24);
    place (sheet, latched, 254, 242, 58, 24);
    place (sheet, drawButton, 316, 242, 76, 24);
    place (sheet, disabled, 396, 242, 60, 24);

    sheet.caption ("Dropdown", 530, 150);
    Dropdown dropdown;
    dropdown.setItems ({ "Forward", "Reverse", "Ping-Pong" });
    dropdown.setSelectedItemIndex (0, juce::dontSendNotification);
    place (sheet, dropdown, 530, 166, 180, 22);

    sheet.caption ("Tab bar: LED per tab, drag to reorder (CHORUS mid-drag)", 16, 280);
    TabBar tabs;
    static constexpr const char* kTabNames[6] = { "DIST", "EQ", "DELAY", "REVERB", "CHORUS", "LIMIT" };

    for (int i = 0; i < 6; ++i)
        tabs.addTab (i, kTabNames[i]);

    tabs.getLedButton (0).setToggleState (true, juce::dontSendNotification);
    tabs.getLedButton (2).setToggleState (true, juce::dontSendNotification);
    tabs.getLedButton (3).setToggleState (true, juce::dontSendNotification);
    tabs.setSelectedTab (2, juce::dontSendNotification);
    place (sheet, tabs, 16, 296, 464, 25);

    TabBar tabsDragged;

    for (int i = 0; i < 6; ++i)
        tabsDragged.addTab (i, kTabNames[i]);

    tabsDragged.getLedButton (4).setToggleState (true, juce::dontSendNotification);
    tabsDragged.setSelectedTab (4, juce::dontSendNotification);
    place (sheet, tabsDragged, 500, 296, 464, 25);
    tabsDragged.setDragPreview (4, 132.0f);

    sheet.caption ("Meters: stereo LED ladder, gain reduction, accent charge", 740, 176);
    LevelMeter meter;
    meter.setLevels (1.0f, -7.0f);
    place (sheet, meter, 740, 192, 46, 96);
    GainReductionMeter gr;
    gr.setReductionDb (6.5f);
    place (sheet, gr, 800, 192, 124, 14);
    ChargeMeter charge;
    charge.setCharge (0.6f);
    place (sheet, charge, 800, 212, 64, 12);

    sheet.caption ("Section frame: panel, screen, bay", 16, 334);
    SectionFrame panel ("FILTER"), screen ({}, SectionFrame::Style::Screen), bay ("CALIBRATION", SectionFrame::Style::Bay);
    place (sheet, panel, 16, 350, 200, 90);
    place (sheet, screen, 226, 350, 120, 90);
    place (sheet, bay, 356, 350, 150, 90);

    sheet.caption ("Calibration cover: closed, sliding, open over the trimmers", 16, 452);
    juce::OwnedArray<CalibrationPanel> bays;

    for (int i = 0; i < 2; ++i)
    {
        auto* b = bays.add (new CalibrationPanel (c));
        place (sheet, *b, 16 + i * 484, 468, 470, CalibrationPanel::kHeight);
        b->cover.setOpenAmount (i == 0 ? 0.0f : 1.0f);
    }

    CalibrationPanel midBay (c);
    place (sheet, midBay, 530, 350, 434, CalibrationPanel::kHeight);
    midBay.cover.setOpenAmount (0.45f);

    sheet.caption ("Trimmer (on the bay)", 530, 232);
    SectionFrame trimBay ({}, SectionFrame::Style::Bay);
    place (sheet, trimBay, 530, 248, 190, 44);
    place (sheet, c.trimmer (acid::kCalTm3, "TM3 CUTOFF"), 538, 256, 86, 27);
    place (sheet, c.trimmer (acid::kCalC13, "C13 ACCENT"), 628, 256, 86, 27);

    sheet.showValueTip (reso, "Resonance", reso.getValueText());

    savePng (sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 1.5f), "kit-" + themeTag (themeId));

    ParamHooks hooks;
    hooks.name = "Resonance";
    hooks.midiAssignment = [] { return juce::String ("CC 71"); };
    hooks.onMidiLearn = [] {};
    hooks.onMidiClear = [] {};
    ParamMenuRequest request;
    request.target = &reso;
    request.hooks = &hooks;
    request.valueText = reso.getValueText();
    request.enterValue = [] {};
    request.resetToDefault = [] {};
    sheet.hideValueTip (nullptr);
    buildParamMenu (request).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&reso).withParentComponent (&sheet));
    savePng (sheet.createComponentSnapshot ({ 0, 0, 520, 290 }, true, 1.5f), "kit-param-menu-" + themeTag (themeId));
    juce::PopupMenu::dismissAllActiveMenus();

    sheet.beginTextEntry (reso, reso.getValueText(), [] (const juce::String&) {});
    savePng (sheet.createComponentSnapshot ({ 0, 0, 520, 150 }, true, 1.5f), "kit-text-entry-" + themeTag (themeId));
    sheet.endTextEntry();
    sheet.setLookAndFeel (nullptr);
}

void renderSequencerSheets (ThemeId themeId)
{
    mw12FLookAndFeel lf (themeId);
    const auto tag = themeTag (themeId);

    {
        Sheet sheet;
        sheet.setLookAndFeel (&lf);
        sheet.setSize (772, 760);
        LocalPatternModel seqModel, arpModel;
        seqModel.applyPattern (demoPattern (24));
        seqModel.setPlayStep (5);
        seqModel.setSelectedStep (3);
        arpModel.setArpPattern (true);
        arpModel.applyPattern (demoPattern (16));
        arpModel.setPlayStep (9);

        PianoRoll roll (seqModel), arpRoll (arpModel);
        place (sheet, roll, 8, 8, 756, 368);
        place (sheet, arpRoll, 8, 384, 756, 368);
        savePng (sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 1.5f), "seq-pianoroll-" + tag);

        roll.setPage (1);
        roll.setGeneratorHighlight (true, 0b010010101101, 0b00110);
        savePng (roll.createComponentSnapshot (roll.getLocalBounds(), true, 1.5f), "seq-pianoroll-page2-generator-" + tag);
        sheet.setLookAndFeel (nullptr);
    }

    {
        Sheet sheet;
        sheet.setLookAndFeel (&lf);
        sheet.setSize (772, 760);
        LocalPatternModel seqModel, arpModel;
        seqModel.applyPattern (demoPattern (16));
        seqModel.setPlayStep (5);
        seqModel.setSelectedStep (3);
        arpModel.setArpPattern (true);
        arpModel.applyPattern (demoPattern (12));
        arpModel.setSelectedStep (2);

        StepKeyboard keys (seqModel), arpKeys (arpModel);
        place (sheet, keys, 8, 8, 756, 368);
        place (sheet, arpKeys, 8, 384, 756, 368);
        savePng (sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 1.5f), "seq-keyboard-" + tag);
        sheet.setLookAndFeel (nullptr);
    }
}

//==============================================================================
/** A scripted host playhead so the sequencer runs while the gallery processes blocks. */
struct GalleryPlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setPpqPosition (ppq);
        p.setBpm (125.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

/** The rig: a processor with its editor, prepared and run for a few blocks so the status
    displays have something to show. */
struct Rig
{
    Rig()
    {
        processor.setPlayHead (&playHead);
        processor.prepareToPlay (48000.0, 512);
        editor.reset (dynamic_cast<mw12FEditor*> (processor.createEditor()));
        jassert (editor != nullptr);
    }

    ~Rig()
    {
        editor.reset();
        processor.releaseResources();
    }

    void run (int blocks)
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < blocks; ++i)
        {
            midi.clear();
            processor.processBlock (audio, midi);
            playHead.ppq += 512.0 * 125.0 / (60.0 * 48000.0);
        }

        processor.handlePendingMessages();
        editor->tick();
    }

    mw12FProcessor processor;
    GalleryPlayHead playHead;
    std::unique_ptr<mw12FEditor> editor;
};

struct EditorState
{
    const char* name;
    std::function<void (Rig&)> apply;
};

const std::vector<EditorState>& editorStates()
{
    static const std::vector<EditorState> states = {
        { "sequencer", [] (Rig&) {} },
        { "keyboard", [] (Rig& r) { r.editor->getPlate().sequencer.setView (SequencerPanel::View::Keyboard); } },
        { "generator", [] (Rig& r) { r.editor->getPlate().sequencer.setToolsTab (SequencerPanel::ToolsTab::Generate); } },
        { "calibration-open", [] (Rig& r) { r.editor->getPlate().calibration.cover.setOpen (true, false); r.editor->getController().setParam (acid::kModOn, 1.0f); } },
        { "external", [] (Rig& r)
          {
              // A held, accented note from the MIDI input, so the monitor has something to show.
              r.editor->getController().setPlayMode (seq::PlayMode::External);
              r.run (2);
              juce::AudioBuffer<float> audio (2, 512);
              juce::MidiBuffer midi;
              midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (120)), 0);
              r.processor.processBlock (audio, midi);
              r.run (6);
          } },
        { "arpeggiator", [] (Rig& r) { r.editor->getController().setPlayMode (seq::PlayMode::Arpeggiator); r.run (40); } },
        { "fx-full", [] (Rig& r) { r.editor->getPlate().rack.setFullView (true); r.editor->getPlate().rack.tabs.setSelectedTab (0);
                                  r.editor->getController().setParam (acid::kTuneExt, 1.0f); } },
        { "preset-browser", [] (Rig& r) { r.editor->openPresetBrowser (acid::PresetType::Scene); } },
        { "preset-save", [] (Rig& r) { r.editor->openPresetBrowser (acid::PresetType::Synth, true); } },
        { "midi-learn", [] (Rig& r) { r.editor->getController().startMidiLearn (acid::kResonance); r.editor->tick(); } },
    };
    return states;
}

void loadDemoScene (Rig& rig)
{
    auto& c = rig.editor->getController();
    acid::plugin::PresetFilter f;
    f.type = acid::PresetType::Scene;
    const auto scenes = c.presets().list (f);

    for (const auto* e : scenes)
        if (e->tags.size() > 1 || scenes.size() == 1)
        {
            c.loadPreset (*e);
            break;
        }

    c.setParam (acid::kSeqRun, 1.0f);
    c.setParam (acid::kDistOn, 1.0f);
    c.setParam (acid::kDelayOn, 1.0f);
    rig.run (64);
    rig.editor->getPatternModel().setSelectedStep (3);
}

void renderEditor (ThemeId themeId, const EditorState& state, float zoom)
{
    // Through the editor's own zoom (window size, plate transform, zoom box, per-zoom caches),
    // snapshotted at scale 1: what a user sees at that zoom step.
    Rig rig;
    rig.editor->setThemeId (themeId);
    rig.editor->setZoom (zoom);
    loadDemoScene (rig);
    state.apply (rig);
    rig.editor->tick();
    const auto image = rig.editor->createComponentSnapshot (rig.editor->getLocalBounds(), true, 1.0f);
    savePng (image, "editor-" + juce::String (state.name) + "-" + themeTag (themeId) + "-" + juce::String (juce::roundToInt (zoom * 100.0f)));
}

//==============================================================================
double microsecondsPer (int iterations, const std::function<void()>& fn)
{
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i)
        fn();

    const auto elapsed = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count();
    return elapsed / iterations;
}

void timeEditor (float scale)
{
    Rig rig;
    rig.editor->setZoom (1.0f);
    loadDemoScene (rig);
    auto& plate = rig.editor->getPlate();
    const auto w = juce::roundToInt (static_cast<float> (plate.getWidth()) * scale);
    const auto h = juce::roundToInt (static_cast<float> (plate.getHeight()) * scale);
    juce::Image canvas (juce::Image::ARGB, w, h, true);

    const auto paintRegion = [&] (juce::Rectangle<int> designArea)
    {
        juce::Graphics g (canvas);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.reduceClipRegion (designArea);
        plate.paintEntireComponent (g, false);
    };

    const auto first = microsecondsPer (1, [&] { paintRegion (plate.getLocalBounds()); });
    const auto full = microsecondsPer (40, [&] { paintRegion (plate.getLocalBounds()); });

    auto& knob = plate.synth.cutoff;
    const auto knobArea = plate.getLocalArea (&knob, knob.getLocalBounds());
    double value = 0.0;
    const auto knobChange = microsecondsPer (4000, [&]
    {
        value = value >= 100.0 ? 0.0 : value + 0.37;
        knob.setValue (value, juce::dontSendNotification);
        paintRegion (knobArea);
    });

    juce::String perPanel;

    for (auto* child : plate.getChildren())
    {
        if (! child->isVisible() || child->getWidth() < 100)
            continue;

        const auto area = plate.getLocalArea (child, child->getLocalBounds());
        const auto us = microsecondsPer (30, [&] { paintRegion (area); });
        perPanel << "    " << juce::String (typeid (*child).name()).fromLastOccurrenceOf ("ui", false, false).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
                 << " " << juce::String (us / 1000.0, 2) << " ms\n";
    }

    auto& roll = plate.sequencer.roll;
    int step = 0;
    const auto cursorMove = microsecondsPer (2000, [&]
    {
        const auto previous = step;
        step = (step + 1) % 16;
        rig.editor->getPatternModel().setPlayStep (step);
        const auto colW = 43;

        for (auto s : { previous, step })
            paintRegion (plate.getLocalArea (&roll, juce::Rectangle<int> (5 + 46 + s * colW, 0, colW + 1, roll.getHeight() - 20)));
    });

    auto& meter = plate.master.meter;
    const auto meterArea = plate.getLocalArea (&meter, meter.getLocalBounds());
    float level = -40.0f;
    const auto meterUpdate = microsecondsPer (4000, [&]
    {
        level = level > 3.0f ? -40.0f : level + 1.7f;
        meter.setLevels (level, level - 2.0f);
        paintRegion (meterArea);
    });

    // A parameter change through the host path: what pluginval's editor automation costs.
    auto& cutoff = rig.editor->getController().parameter (acid::kCutoff);
    float norm = 0.0f;
    const auto hostChange = microsecondsPer (2000, [&]
    {
        norm = norm >= 1.0f ? 0.0f : norm + 0.0137f;
        cutoff.setValueNotifyingHost (norm);
        paintRegion (knobArea);
    });

    // Repaints that nothing invalidated must not render anything: tiles or panel buffers.
    const auto tilesBefore = plate.getStaticRenderCount();
    const auto buffersBefore = Faceplate::getBufferRenderCount();
    paintRegion (plate.getLocalBounds());
    paintRegion (plate.getLocalBounds());
    const auto idleTiles = plate.getStaticRenderCount() - tilesBefore;
    const auto idleBuffers = Faceplate::getBufferRenderCount() - buffersBefore;

    std::cout << "paint timing at " << juce::roundToInt (scale * 100.0f) << " % (" << w << "x" << h << ")\n"
              << "  first full paint, builds the static layer : " << juce::String (first / 1000.0, 2) << " ms\n"
              << "  full repaint, static layer cached          : " << juce::String (full / 1000.0, 2) << " ms\n"
              << "  one knob change (clip = the knob)          : " << juce::String (knobChange, 1) << " us\n"
              << "  host parameter change + knob repaint       : " << juce::String (hostChange, 1) << " us\n"
              << "  play cursor step (two columns)             : " << juce::String (cursorMove, 1) << " us\n"
              << "  meter update                               : " << juce::String (meterUpdate, 1) << " us\n"
              << "  static tile renders so far                 : " << plate.getStaticRenderCount() << "\n"
              << "  full repaint by panel:\n" << perPanel;
    check (idleTiles == 0 && idleBuffers == 0, "a repaint that nothing invalidated renders no tile and no panel buffer");
    check (full / 1000.0 < (scale >= 2.0f ? 8.0 : 4.0), "full repaint inside the budget (8 ms at 2x, 4 ms at 1x)");
}

//==============================================================================
/** Drives the editor the way a user would, through the controls, and checks the controller saw it. */
void driveInteractions()
{
    std::cout << "interaction checks\n";
    Rig rig;
    rig.editor->setZoom (1.0f);
    loadDemoScene (rig);
    auto& c = rig.editor->getController();
    auto& plate = rig.editor->getPlate();

    // The engine ran while the scene loaded: the status displays have something to show.
    {
        const auto st = c.engineStatus();
        std::cout << "  status: running=" << st.running << " step=" << st.currentStep << " peakL=" << st.peakLeft
                  << " charge=" << st.accentCharge << " gate=" << st.gateOpen << "\n";
        check (st.running && st.currentStep >= 0, "sequencer runs under the gallery playhead");
        check (st.peakLeft > 0.001f || st.peakRight > 0.001f, "engine produces audio while running");
    }

    // A knob drag reaches the parameter, and a parameter change reaches the knob.
    const auto sceneCutoff = c.getParam (acid::kCutoff);
    const auto moved = std::abs (sceneCutoff - 61.0f) > 0.5f ? 61.0 : 40.0;
    plate.synth.cutoff.setValueAsGesture (moved);
    check (std::abs (c.getParam (acid::kCutoff) - static_cast<float> (moved)) < 0.01f, "cutoff knob writes the parameter");
    c.setParam (acid::kResonance, 42.0f);
    check (std::abs (plate.synth.resonance.getValue() - 42.0) < 0.01, "resonance parameter moves the knob");

    // Double-click returns to the value the scene loaded, not the table default and not the
    // hand-moved value.
    check (std::abs (plate.synth.cutoff.getDoubleClickReturnValue() - static_cast<double> (sceneCutoff)) < 0.01,
           "double-click default is the loaded scene's value");

    // A preset of another type leaves it alone.
    {
        acid::plugin::PresetFilter delayFilter;
        delayFilter.type = acid::PresetType::Delay;
        const auto delays = c.presets().list (delayFilter);

        if (! delays.empty())
            c.loadPreset (*delays.front());

        check (std::abs (plate.synth.cutoff.getDoubleClickReturnValue() - static_cast<double> (sceneCutoff)) < 0.01,
               "a delay preset does not move the cutoff knob's double-click value");
    }

    // Couple LEDs: both buttons share the parameter and the coupled knob dims.
    plate.synth.sweepCoupleA.setToggleState (! plate.synth.sweepCoupleA.getToggleState(), juce::sendNotificationSync);
    check (c.getParam (acid::kSweepCouple) < 0.5f, "couple LED toggles the parameter");
    check (! plate.synth.sweepCoupleB.getToggleState(), "the second couple LED follows");
    check (plate.synth.sweepAmt.isEnabled(), "sweep amount knob enabled when uncoupled");

    // Distortion model box and the voicing parameters following it.
    plate.distortion.modelBox.setSelectedItemIndex (3, juce::sendNotificationSync);
    check (std::abs (c.getParam (acid::kDistModel) - 3.0f) < 0.01f, "model box writes dist_model");
    rig.processor.handlePendingMessages();
    plate.distortion.nextModel.onClick();
    check (std::abs (c.getParam (acid::kDistModel) - 4.0f) < 0.01f, "next model button steps dist_model");

    // Pattern selector writes the Performance parameters and the editors follow.
    plate.sequencer.selector.onSelect (1, 4);
    check (c.selectedSlot() == seq::slotIndex (1, 4), "pattern selector selects bank B slot 5");
    check (rig.editor->getPatternModel().getSlot() == seq::slotIndex (1, 4), "pattern model follows the slot");

    // Piano-roll edit: place a note through the model, then undo it.
    auto& model = rig.editor->getPatternModel();
    auto p = model.getPattern();
    const auto before = p;
    p.steps[0].state = seq::StepState::Note;
    p.steps[0].note = 7;
    model.applyPattern (p);
    check (model.getPattern().steps[0].state == seq::StepState::Note, "pattern edit committed through the controller");
    check (c.canUndo(), "pattern edit is undoable");
    plate.sequencer.undoButton.onClick();
    check (seq::samePattern (model.getPattern(), before), "undo button restores the pattern");

    // Generator: draw, then the amount knob changes density without re-rolling the pitches.
    plate.sequencer.setToolsTab (SequencerPanel::ToolsTab::Generate);
    plate.sequencer.generator.getDrawButton().onClick();
    const auto drawn = model.getPattern();
    check (drawn.gen.seed != before.gen.seed || drawn.steps[0].magGate != 255, "draw generated a line");
    plate.sequencer.generator.getAmountKnob (0).setValueAsGesture (100.0);
    const auto dense = model.getPattern();
    int notesDrawn = 0, notesDense = 0;

    for (int i = 0; i < dense.length; ++i)
    {
        notesDrawn += drawn.steps[static_cast<std::size_t> (i)].state != seq::StepState::Rest;
        notesDense += dense.steps[static_cast<std::size_t> (i)].state != seq::StepState::Rest;
    }

    check (notesDense >= notesDrawn, "note amount at 100 % reveals every candidate");
    check (dense.gen.seed == drawn.gen.seed, "amount change keeps the seed");

    // Templates from the scale library.
    plate.sequencer.generator.getTemplateBox().setSelectedItemIndex (2, juce::sendNotificationSync);
    check (c.matchingScaleTemplate (false, model.getSlot()) == 2, "template box applies a scale template");

    // Effects order by tab drag writes Settings::fxOrder.
    std::array<std::uint8_t, acid::kNumFxSlots> order { 1, 0, 2, 3, 4, 5 };
    plate.rack.onOrderChanged (order);
    check (c.settings().fxOrder == order, "tab reorder writes the effects order");
    check (plate.rack.tabs.getOrder()[0] == 1, "tab bar shows the stored order");

    // Settings boxes.
    plate.sequencer.launchBox.setSelectedItemIndex (2, juce::sendNotificationSync);
    check (c.settings().launchQuant == seq::LaunchQuant::Immediate, "launch box writes the setting");

    // Live override is momentary.
    auto* set = dynamic_cast<PushButton*> (plate.getControls().findButton (acid::kOvrAccentSet));
    check (set != nullptr, "override button exists");

    if (set != nullptr)
    {
        set->setState (juce::Button::buttonDown);
        check (c.getParam (acid::kOvrAccentSet) >= 0.5f, "override down = 1");
        set->setState (juce::Button::buttonNormal);
        check (c.getParam (acid::kOvrAccentSet) < 0.5f, "override up = 0");
    }

    // Play mode: the external panel takes the sequencer's place; the window keeps its size.
    const auto sizeBefore = rig.editor->getBounds();
    c.setPlayMode (seq::PlayMode::External);
    rig.editor->tick();
    check (rig.editor->getBounds() == sizeBefore, "external mode keeps the window size");
    check (! plate.sequencer.isVisible(), "sequencer hidden in external mode");
    check (plate.external.isVisible() && plate.external.getBounds() == plate.sequencer.getBounds(), "external panel in the sequencer's place");
    plate.external.bendBox.setValue (7.0, juce::sendNotificationSync);
    check (c.settings().bendRange == 7, "external panel writes the bend range");
    plate.external.priorityBox.setValue (2.0, juce::sendNotificationSync);
    check (c.settings().notePriority == seq::NotePriority::High, "external panel writes the note priority");
    c.setPlayMode (seq::PlayMode::Sequencer);
    rig.editor->tick();
    check (rig.editor->getBounds() == sizeBefore, "sequencer mode keeps the window size");
    check (plate.sequencer.isVisible() && ! plate.external.isVisible(), "sequencer back in sequencer mode");

    // Zoom and theme persist through the controller.
    rig.editor->setZoom (1.5f);
    check (std::abs (c.zoom() - 1.5f) < 0.01f, "zoom stored in the UI state");
    check (rig.editor->getWidth() == 1770, "window width follows the zoom");
    rig.editor->setThemeId (ThemeId::Silver);
    check (c.theme() == "silver", "theme stored in the UI state");

    // Preset browser: audition and revert.
    const auto cutoffBefore = c.getParam (acid::kCutoff);
    rig.editor->openPresetBrowser (acid::PresetType::Synth);
    check (c.auditioning(), "opening the browser starts an audition");
    {
        // A click on the second row of the browser's list (its own load path; a load that
        // arrives from outside the browser ends the audition instead, see the editor tests).
        auto& browser = rig.editor->getPresetBrowser();
        juce::Component* list = nullptr;

        for (auto* child : browser.getChildren())
            if (auto* view = dynamic_cast<juce::Viewport*> (child))
                list = view->getViewedComponent();

        check (list != nullptr, "browser has a list");

        if (list != nullptr)
        {
            const juce::Point<float> row (10.0f, 22.0f + 11.0f);
            const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), row, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, list, list, juce::Time::getCurrentTime(), row, juce::Time::getCurrentTime(), 1, false);
            list->mouseDown (click);
        }

        check (c.lastLoaded (acid::PresetType::Synth) != nullptr && c.auditioning(), "a row click auditions a preset");
    }

    rig.editor->getPresetBrowser().close (false);
    check (! c.auditioning() && std::abs (c.getParam (acid::kCutoff) - cutoffBefore) < 0.01f, "cancel reverts the audition");

    // MIDI learn through the right-click hooks.
    plate.synth.cutoff.param.onMidiLearn();
    check (c.midiLearning() && c.midiLearnTarget() == acid::kCutoff, "learn hook arms the parameter");
    c.cancelMidiLearn();
    c.setMidiMapping (74, acid::kCutoff);
    check (plate.synth.cutoff.param.midiAssignment() == "CC 74", "menu shows the assignment");
    plate.synth.cutoff.param.onMidiClear();
    check (plate.synth.cutoff.param.midiAssignment().isEmpty(), "clear hook removes it");

    // Drag-out file.
    const auto dragFile = plate.sequencer.midiDrag.onCreateFile();
    check (dragFile.existsAsFile() && dragFile.getSize() > 0, "drag handle writes a MIDI file");

    std::cout << (failures == 0 ? "all interaction checks passed\n" : juce::String (failures) + " interaction check(s) FAILED\n");
}

void reportXErrors();

/** Real mouse events through a desktop window's peer (the gallery runs under Xvfb), so the
    hit testing, the overlay and the double-click detection are the real thing. */
void driveMouse()
{
    std::cout << "mouse checks\n";
    Rig rig;
    rig.editor->setZoom (1.0f);
    loadDemoScene (rig);
    auto& c = rig.editor->getController();
    auto& plate = rig.editor->getPlate();
    rig.editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    rig.editor->setVisible (true);
    auto* peer = rig.editor->getPeer();
    check (peer != nullptr, "editor opens a desktop window");

    if (peer == nullptr)
        return;

    const auto clickAt = [&] (juce::Point<float> pos, juce::ModifierKeys button, int clicks)
    {
        for (int i = 0; i < clicks; ++i)
        {
            const auto now = juce::Time::currentTimeMillis();
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, button, juce::MouseInputSource::defaultPressure,
                                    juce::MouseInputSource::defaultOrientation, now);
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
                                    juce::MouseInputSource::defaultOrientation, now + 1);
        }
    };
    const auto click = [&] (juce::Component& target, juce::ModifierKeys button, int clicks = 1)
    {
        clickAt (rig.editor->getLocalPoint (&target, target.getLocalBounds().getCentre()).toFloat(), button, clicks);
    };
    const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);

    // The pattern name: right-click opens its menu (where rename lives), left click browses.
    int contextMenus = 0;
    auto& name = plate.sequencer.patternName;
    name.onContextMenu = [&contextMenus] { ++contextMenus; };
    click (name, right);
    check (contextMenus == 1, "right-click on the pattern name opens its menu");
    check (! rig.editor->getPresetBrowser().isVisible(), "right-click does not open the browser");
    click (name, left);
    check (rig.editor->getPresetBrowser().isVisible(), "left click on the pattern name opens the browser");
    click (name, left);   // the browser covers the box now: nothing more happens
    rig.editor->getPresetBrowser().close (false);
    check (! rig.editor->getPresetBrowser().isVisible(), "browser closes");

    // Double-click on a knob returns it to its default.
    const auto def = plate.synth.cutoff.getDoubleClickReturnValue();
    c.setParam (acid::kCutoff, def > 50.0 ? 20.0f : 80.0f);
    click (plate.synth.cutoff, left, 2);
    check (std::abs (c.getParam (acid::kCutoff) - static_cast<float> (def)) < 0.01f, "double-click on a knob returns it to its default");

    // An effects tab click switches the page.
    const auto order = plate.rack.tabs.getOrder();
    const auto tabW = static_cast<float> (plate.rack.tabs.getWidth()) / static_cast<float> (order.size());
    const auto secondTab = rig.editor->getLocalPoint (&plate.rack.tabs, juce::Point<float> (tabW * 1.5f, static_cast<float> (plate.rack.tabs.getHeight()) * 0.5f));
    clickAt (secondTab, left, 1);
    check (static_cast<int> (plate.rack.currentEffect()) == order[1], "clicking the second effects tab shows its page");

    // A live override is momentary under the mouse.
    auto* set = dynamic_cast<PushButton*> (plate.getControls().findButton (acid::kOvrAccentSet));

    if (set != nullptr)
    {
        const auto pos = rig.editor->getLocalPoint (set, set->getLocalBounds().getCentre()).toFloat();
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, left, juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        check (c.getParam (acid::kOvrAccentSet) >= 0.5f, "mouse down on ACC SET forces the accent");
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        check (c.getParam (acid::kOvrAccentSet) < 0.5f, "mouse up releases it");
    }

    rig.editor->removeFromDesktop();
    reportXErrors();
    std::cout << (failures == 0 ? "all mouse checks passed\n" : juce::String (failures) + " check(s) FAILED so far\n");
}

/** The gallery is a console app, not a JUCEApplication, so JUCE leaves Xlib's default error
    handler in place, and that one prints the error and exits the process. A bare Xvfb server
    (no window manager, no other client) has none of the ICCCM and EWMH atoms interned, and
    JUCE writes some of them on every window it creates without checking, so the desktop
    window in driveMouse() would end the run. A reporting, non-fatal handler keeps it going,
    and interning the atoms first keeps the properties meaningful. libX11 is reached through
    dlopen, as JUCE itself does, so the tool links nothing new. */
/** Xlib's XErrorEvent, laid out as <X11/Xlib.h> declares it (the header is not included). */
struct XErrorEventLayout
{
    int type;
    void* display;
    unsigned long resourceId, serial;
    unsigned char errorCode, requestCode, minorCode;
};

std::map<std::pair<int, int>, int> xErrors;   // (error, request) -> count, reported once at the end

int reportXError (void*, void* event)
{
    const auto* e = static_cast<const XErrorEventLayout*> (event);
    ++xErrors[{ e->errorCode, e->requestCode }];
    return 0;
}

void reportXErrors()
{
    for (const auto& [kind, count] : xErrors)
        std::cout << "  (" << count << " X error(s) of code " << kind.first << " in request " << kind.second << " ignored: bare X server)\n";
}

void prepareBareXServer()
{
    static juce::DynamicLibrary x11 ("libX11.so.6");
    using OpenDisplay = void* (*) (const char*);
    using InternAtom = unsigned long (*) (void*, const char*, int);
    using CloseDisplay = int (*) (void*);
    using ErrorHandler = int (*) (void*, void*);
    using SetErrorHandler = ErrorHandler (*) (ErrorHandler);
    const auto open = reinterpret_cast<OpenDisplay> (x11.getFunction ("XOpenDisplay"));
    const auto intern = reinterpret_cast<InternAtom> (x11.getFunction ("XInternAtom"));
    const auto close = reinterpret_cast<CloseDisplay> (x11.getFunction ("XCloseDisplay"));
    const auto setHandler = reinterpret_cast<SetErrorHandler> (x11.getFunction ("XSetErrorHandler"));

    if (setHandler != nullptr)
        setHandler (reportXError);

    if (open == nullptr || intern == nullptr || close == nullptr)
        return;

    if (auto* display = open (nullptr))
    {
        for (auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                            "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_STATE",
                            "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT" })
            intern (display, name, 0);

        close (display);
    }
}
}

int main (int argc, char** argv)
{
    prepareBareXServer();
    juce::ScopedJuceInitialiser_GUI gui;
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (fonts::textTypeface());

    outputDir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String (MW_GALLERY_DEFAULT_OUT));
    outputDir.createDirectory();

    // Keep the processor's user files out of the real configuration.
    const auto config = outputDir.getChildFile ("config");
    config.deleteRecursively();
    config.createDirectory();
    ::setenv ("XDG_CONFIG_HOME", config.getFullPathName().toRawUTF8(), 1);

    for (auto themeId : { ThemeId::Silver, ThemeId::Night })
    {
        renderKitSheet (themeId);
        renderSequencerSheets (themeId);

        for (const auto& state : editorStates())
            for (auto scale : { 1.0f, 1.5f })
                renderEditor (themeId, state, scale);

        renderEditor (themeId, editorStates().front(), 0.75f);
        renderEditor (themeId, editorStates().front(), 2.0f);

        for (const auto& state : editorStates())
            if (juce::String (state.name) == "external")
                renderEditor (themeId, state, 0.75f);
    }

    timeEditor (1.0f);
    timeEditor (2.0f);
    driveInteractions();
    driveMouse();
    return failures == 0 ? 0 : 1;
}
