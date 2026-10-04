#pragma once
// The editor's sections. Each panel builds its controls through a shared ControlSet (so
// they are registered by ParamId for attachment), lays them out in design units, and draws
// its frame and fixed legends as static art for the Faceplate cache.
//
// The panels hold no parameter state and know nothing about the processor: a real editor
// reuses them unchanged and attaches the ControlSet. Controls that are not host parameters
// (preset boxes, zoom, pattern properties, view switches) are public members with plain
// callbacks.
//
// Inside a Faceplate each top-level panel is opaque and buffered, with its fixed artwork in a
// tile of its own (Faceplate.h). Artwork that depends on a view, tab or play mode is drawn in
// the panel's paint() as cheap text, never into the tile, so those switches repaint a panel
// without rebuilding anything.
#include "ParamControls.h"
#include "kit/SectionFrame.h"
#include "kit/TabBar.h"
#include "kit/Meters.h"
#include "kit/CoverPanel.h"
#include "seq/PianoRoll.h"
#include "seq/StepKeyboard.h"
#include "seq/PatternSelector.h"
#include "seq/GeneratorPanel.h"
#include "seq/MidiDragHandle.h"
#include "acid/engine/Settings.h"

namespace acid::ui
{
class Panel : public juce::Component, public StaticArt
{
public:
    Panel() { setWantsKeyboardFocus (false); }
    void paint (juce::Graphics& g) override { paintStaticArtIfUncached (*this, g); }
};

//==============================================================================
/** Name plate, preset display, play mode, zoom and settings. Height 48. */
class TopBar final : public Panel
{
public:
    explicit TopBar (ControlSet&);

    static constexpr int kHeight = 48;

    Dropdown presetBox, zoomBox;
    PushButton prevPreset { {}, draw::Icon::Prev }, nextPreset { {}, draw::Icon::Next }, savePreset { "SAVE" };
    PushButton settingsButton { {}, draw::Icon::Cog };
    SegmentedSelector& playMode;

    /** Zoom factor chosen in the zoom box (0.75 .. 2.0). */
    std::function<void (float)> onZoomChanged;
    void setZoom (float zoom);

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    juce::Rectangle<int> namePlate;
};

//==============================================================================
/** The thirteen panel knobs, waveform switch, couple LEDs and tuning. Height 134. */
class SynthPanel final : public Panel
{
public:
    explicit SynthPanel (ControlSet&);

    static constexpr int kHeight = 134;

    /** Swaps the tuning knob for the transpose readout and fine knob. Called from the EXT button
        in the mock; the editor calls it from the tune_ext parameter. */
    void setExtendedTuning (bool extended);

    /** Dims the knobs a couple switch takes over. */
    void setCouples (bool sweepCoupled, bool accentVolumeCoupled);

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

    ToggleSwitch& wave;
    LedButton& tuneExt;
    Knob &tune, &tuneFine, &cutoff, &resonance, &envMod, &decay, &envAttack;
    Knob &accent, &accDecay, &accVol, &sweepAmt, &slideTime, &vibSpeed, &vibDepth;
    NumericDisplay& tuneCoarse;
    LedButton &sweepCoupleA, &sweepCoupleB, &accCoupleA, &accCoupleB;

    /** Status lamps and the accent-capacitor meter, fed from EngineStatus by the editor's timer. */
    LedButton gateLamp { "GATE", LedColour::Accent, LedButton::Style::LensLabel };
    LedButton accentLamp { "ACC", LedColour::Red, LedButton::Style::LensLabel };
    ChargeMeter chargeMeter;

private:
    juce::Rectangle<int> frames[4];
};

//==============================================================================
/** Circuit mods: enable, seven amounts, five switches. Height 110. */
class ModsPanel final : public Panel
{
public:
    explicit ModsPanel (ControlSet&);

    static constexpr int kHeight = 110;
    static constexpr int kWidth = 656;

    /** Dims everything but the enable switch while the mods are off (the voice is stock then). */
    void setModsEnabled (bool enabled);

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

    LedButton& enable;

private:
    juce::Array<juce::Component*> gated;
    Knob* knobs[7] {};
    ToggleSwitch *sweepSpeed, *muffler, *subOct;
    LedButton *hiRes, *extRange;
};

//==============================================================================
/** The calibration bay with its sliding cover. Height 110. */
class CalibrationPanel final : public Panel
{
public:
    explicit CalibrationPanel (ControlSet&);

    static constexpr int kHeight = 110;

    CoverPanel cover { "CALIBRATION", "SERVICE TRIMMERS UNDER THIS COVER" };
    Dropdown presetBox;

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    juce::Array<juce::Component*> cells;
};

//==============================================================================
/** Distortion front panel: on, model, dynamics, order, drive, colour. Height 140. */
class DistortionPanel final : public Panel
{
public:
    explicit DistortionPanel (ControlSet&);

    static constexpr int kHeight = 140;
    static constexpr int kWidth = 290;

    /** The voicing list. Not registered with the ControlSet: dist_model is an Int row whose
        range outruns the list, so the editor binds the box with its own ParameterAttachment. */
    Dropdown modelBox;
    PushButton prevModel { {}, draw::Icon::Prev }, nextModel { {}, draw::Icon::Next };

    /** Fills the box from acid::fx::distortionVoicings() (index 0 is the custom voicing). */
    void setModelNames (const juce::StringArray& names);

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    LedButton& on;
    Knob &dynamics, &drive, &color;
    ToggleSwitch& order;
};

//==============================================================================
/** One effect's page in the rack: a row of knobs (basic) or a grid of numeric fields (full). */
class FxPage final : public Panel
{
public:
    FxPage (ControlSet&, std::vector<ParamId> basicParams, std::vector<ParamId> fullParams);

    void setFullView (bool full);
    bool isFullView() const noexcept { return fullView; }

    /** Extra component shown at the right of the basic view (the limiter's gain-reduction meter). */
    void setBasicExtra (juce::Component* extra, int width);

    void resized() override;
    /** The captions of the view shown are cheap text, drawn here rather than into the rack's
        static art, so a tab or view switch repaints the rack without rebuilding its tile. */
    void paint (juce::Graphics&) override;
    void paintStaticArt (juce::Graphics&, const Theme&) override {}

private:
    struct Item
    {
        juce::Component* component = nullptr;
        juce::String caption;      // drawn by the page (dropdowns and buttons have no caption of their own)
        bool captioned = false;
    };

    Item makeItem (ControlSet&, ParamId, bool full);

    std::vector<Item> basic, full;
    juce::Component* basicExtra = nullptr;
    int basicExtraWidth = 0;
    bool fullView = false;
};

/** The six-tab effects rack. Tabs reorder by drag; each tab's LED is the effect's on switch. */
class FxRackPanel final : public Panel
{
public:
    explicit FxRackPanel (ControlSet&);

    static constexpr int kHeight = 140;

    TabBar tabs;
    Dropdown presetBox;
    SegmentedSelector viewSelector { { "BASIC", "FULL" } };
    GainReductionMeter gainReduction;

    void setFullView (bool full);
    bool isFullView() const noexcept { return fullView; }
    void showEffect (FxSlot slot);
    FxSlot currentEffect() const noexcept { return static_cast<FxSlot> (current); }

    /** Order as Settings::fxOrder stores it. */
    void setOrder (const std::array<std::uint8_t, kNumFxSlots>& order);
    std::function<void (const std::array<std::uint8_t, kNumFxSlots>&)> onOrderChanged;

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    std::unique_ptr<FxPage> pages[kNumFxSlots];
    int current = 0;
    bool fullView = false;
};

//==============================================================================
/** Output volume, effects bypass and the output meter. Height 140, width 150. */
class MasterPanel final : public Panel
{
public:
    explicit MasterPanel (ControlSet&);

    static constexpr int kHeight = 140;
    static constexpr int kWidth = 150;

    LevelMeter meter;

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    Knob& volume;
    LedButton& bypass;
};

//==============================================================================
/** The sequencer screen: both step editors, pattern tools, generator, transport, selector. */
class SequencerPanel final : public Panel
{
public:
    SequencerPanel (ControlSet&, PatternEditModel&);
    ~SequencerPanel() override;

    static constexpr int kHeight = 416;

    enum class View { PianoRoll, Keyboard };
    enum class ToolsTab { Edit, Generate };

    void setView (View);
    View getView() const noexcept { return view; }
    void setToolsTab (ToolsTab);
    ToolsTab getToolsTab() const noexcept { return toolsTab; }

    /** Relabels the tools for an arpeggiator pattern (Inversion instead of Transpose, no generator, no export). */
    void setArpMode (bool arp);

    /** Refreshes the pattern property readouts from the model. Called on patternChanged. */
    void refreshFromPattern();

    PianoRoll roll;
    StepKeyboard keys;
    GeneratorPanel generator;
    PatternSelector selector;
    MidiDragHandle midiDrag;

    Dropdown patternName, rateBox, directionBox, triggerBox, launchBox;
    PushButton prevPattern { {}, draw::Icon::Prev }, nextPattern { {}, draw::Icon::Next };
    PushButton copyButton { "COPY" }, pasteButton { "PASTE" }, clearButton { "CLEAR" }, exportButton { "EXPORT", draw::Icon::Export };
    PushButton importButton { "IMPORT", draw::Icon::Note };
    PushButton dupButton { "DUP" }, undoButton { "UNDO" }, redoButton { "REDO" };
    PushButton shiftLeft { {}, draw::Icon::Prev }, shiftRight { {}, draw::Icon::Next };
    PushButton transposeDown { {}, draw::Icon::Down }, transposeUp { {}, draw::Icon::Up };
    SegmentedSelector viewSelector { { "ROLL", "KEYS" } }, toolsSelector { { "EDIT", "GENERATE" } };
    SegmentedSelector divModSelector { { "FULL", "DOT", "TRIP" } };
    NumericDisplay lengthBox { "LENGTH", NumericDisplay::CaptionPlacement::Left };
    NumericDisplay shuffleBox { "SHUFFLE", NumericDisplay::CaptionPlacement::Left };
    NumericDisplay gateBox { "GATE", NumericDisplay::CaptionPlacement::Left };
    /** The internal clock's tempo (Settings::internalBpm): standalone, or a host without a
        tempo or with its transport stopped. The owner binds it. */
    NumericDisplay tempoBox { "TEMPO", NumericDisplay::CaptionPlacement::Left };
    /** RUN: the cap shows the switch (latched in or out), the LED whether the line is playing,
        and the badge which clock drives it (INT or HOST). */
    LedButton& run;
    NumericDisplay& liveTranspose;

    /** Pattern operations are done by the owner (acid::seq::PatternOps in the plugin). */
    std::function<void (int direction)> onShift;        // -1 left, +1 right
    std::function<void (int semitones)> onTranspose;    // -1 or +1; index inversion for arpeggiator patterns

    void resized() override;
    /** The screen and the fixed captions are static art; the labels that depend on the tools
        tab and the play mode are drawn here, so a view switch or play_mode automation
        repaints the panel without rebuilding its tile. */
    void paint (juce::Graphics&) override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    void editPattern (const std::function<void (seq::Pattern&)>& fn);
    void updateVisibility();
    void setTooltips();

    PatternEditModel& model;
    View view = View::PianoRoll;
    ToolsTab toolsTab = ToolsTab::Edit;
    bool arpMode = false, refreshing = false;
    PushButton* overrides[8] {};
    juce::Rectangle<int> toolsArea, rightArea, overrideArea, editorArea;
    juce::Array<juce::Component*> editTools;

    struct ModelListener;
    std::unique_ptr<ModelListener> listener;

    /** Brackets a drag on a pattern property readout in one edit gesture (one undo step). */
    struct GestureListener;
    std::unique_ptr<GestureListener> gestures;
};

//==============================================================================
/** External play mode: takes the sequencer screen's place, at the same size, while the voice
    plays the notes arriving at the MIDI input. A monitor (gate, accent, envelope, the C13
    charge, MIDI input overload, all from EngineStatus), the external-mode settings, and a
    short guide to what the incoming MIDI does. The settings controls are not host
    parameters: the owner shows the Settings with showSettings and applies the edits that
    onSettingsEdit hands it. */
class ExternalPanel final : public Panel
{
public:
    ExternalPanel();
    ~ExternalPanel() override;

    /** Shows the external-mode fields (accent threshold, bend range, note priority, MIDI thru). */
    void showSettings (const Settings&);

    /** A control changed a field: the owner reads the settings, applies `edit` and stores them
        (`undoLabel` names the undo step). */
    std::function<void (const char* undoLabel, const std::function<void (Settings&)>& edit)> onSettingsEdit;

    /** A drag on a readout begins and ends: the owner brackets it in one undo step. */
    std::function<void()> onGestureBegin, onGestureEnd;

    /** The monitor, fed from EngineStatus by the owner's timer; repaints only what changed. */
    void setStatus (bool gateOpen, bool accentLatched, float envelope01, float accentCharge01, bool midiInDropped);

    LedButton gateLamp { "GATE", LedColour::Accent, LedButton::Style::LensLabel };
    LedButton accentLamp { "ACCENT", LedColour::Red, LedButton::Style::LensLabel };
    LedButton overloadLamp { "INPUT FULL", LedColour::Amber, LedButton::Style::LensLabel };
    ChargeMeter envelopeMeter, chargeMeter;

    NumericDisplay thresholdBox { "ACCENT ABOVE", NumericDisplay::CaptionPlacement::Left };
    NumericDisplay bendBox { "BEND RANGE", NumericDisplay::CaptionPlacement::Left };
    SegmentedSelector priorityBox { { "LAST", "LOW", "HIGH" } };
    LedButton thruButton { "NOTES TO OUT", LedColour::Accent, LedButton::Style::Cap };

    void resized() override;
    void paintStaticArt (juce::Graphics&, const Theme&) override;

private:
    struct GestureListener;
    std::unique_ptr<GestureListener> gestures;
    juce::Rectangle<int> monitorArea, settingsArea, guideArea;
};
}
