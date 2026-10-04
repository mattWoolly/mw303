#include "mw12FEditor.h"
#include "editor/SettingsMenu.h"
#include "editor/VoicingNames.h"
#include "kit/SafeText.h"
#include "editor/ParamHelp.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

using namespace acid;
using namespace acid::ui;

namespace
{
constexpr int kDesignW = EditorPlate::kDesignWidth;
constexpr int kDesignH = EditorPlate::kDesignHeight;

float toDb (float linear) noexcept
{
    return 20.0f * std::log10 (juce::jmax (1.0e-5f, linear));
}

juce::String slotName (int slot)
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot / seq::kSlotsPerBank)) + "-"
           + juce::String (slot % seq::kSlotsPerBank + 1).paddedLeft ('0', 2);
}

bool patternHasNotes (const seq::Pattern& p) noexcept
{
    for (int i = 0; i < static_cast<int> (p.length); ++i)
        if (p.steps[static_cast<std::size_t> (i)].state != seq::StepState::Rest)
            return true;

    return false;
}

/** The scale at which the plate fits a window: the smaller of the two axis ratios, so a host
    that ignores the aspect constraint shows the whole plate. */
float zoomFor (int width, int height) noexcept
{
    const auto byWidth = static_cast<float> (width) / static_cast<float> (kDesignW);
    const auto byHeight = static_cast<float> (height) / static_cast<float> (kDesignH);
    return juce::jmin (byWidth, byHeight);
}

/** Every editor in the process gets its own drag-out directory, so two instances dragging
    patterns of the same name never hand the host one path and delete each other's file. */
std::atomic<int> dragDirectoryCounter { 0 };

int countNotes (const seq::Pattern& p) noexcept
{
    int n = 0;

    for (int i = 0; i < static_cast<int> (p.length); ++i)
        n += p.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note ? 1 : 0;

    return n;
}

/** Largest pattern or MIDI file a drop reads (a pattern file is a few kilobytes). */
constexpr std::int64_t kMaxDroppedFileBytes = 4 * 1024 * 1024;
}

//==============================================================================
struct mw12FEditor::LearnClickWatcher final : juce::MouseListener
{
    explicit LearnClickWatcher (mw12FEditor& e) : editor (e) {}

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != nullptr)
            editor.handlePlateClick (*e.eventComponent);
    }

    mw12FEditor& editor;
};

struct mw12FEditor::TempoGesture final : juce::Slider::Listener
{
    explicit TempoGesture (plugin::Controller& c) : controller (c) {}
    void sliderValueChanged (juce::Slider*) override {}
    void sliderDragStarted (juce::Slider*) override { controller.beginGesture(); }
    void sliderDragEnded (juce::Slider*) override { controller.endGesture(); }
    plugin::Controller& controller;
};

//==============================================================================
mw12FEditor::mw12FEditor (mw12FProcessor& p)
    : juce::AudioProcessorEditor (p), controller (p.controller()), patternModel (controller), plate (patternModel), browser (controller)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        const std::string_view group = paramInfo (id).group;
        const auto rowsOf = [&] (PresetType type) { typeRows[static_cast<std::size_t> (type)].push_back (id); };

        // What each preset type carries (SPEC 6.7): a scene every row, a synth preset every
        // group but Performance, the rest their own group.
        rowsOf (PresetType::Scene);
        if (group != "Performance")                                   rowsOf (PresetType::Synth);
        if (group == "Calibration")                                   rowsOf (PresetType::Calibration);
        if (group == "Distortion" || group == "Distortion Voicing")   rowsOf (PresetType::Distortion);
        if (group == "EQ")                                            rowsOf (PresetType::Eq);
        if (group == "Delay")                                         rowsOf (PresetType::Delay);
        if (group == "Reverb")                                        rowsOf (PresetType::Reverb);
        if (group == "Chorus")                                        rowsOf (PresetType::Chorus);
        if (group == "Limiter")                                       rowsOf (PresetType::Limiter);
    }

    plate.setThemeId (controller.theme() == "silver" ? ThemeId::Silver : ThemeId::Night);
    addAndMakeVisible (plate);
    plate.setOverlay (&browser);
    plate.sequencer.run.setLit (false);   // the RUN lamp shows the engine's state, the cap the switch

    // The editor's own children (the MIDI and map file dialogs) are drawn in the house style.
    setLookAndFeel (&plate.getLookAndFeel());

    plate.addChildComponent (learnIndicator);
    learnIndicator.onCancel = [this] { cancelMidiLearn(); };
    learnClickWatcher = std::make_unique<LearnClickWatcher> (*this);
    plate.addMouseListener (learnClickWatcher.get(), true);

    // Help text on hover, drawn inside the plate (no extra window), after the usual delay.
    tooltipWindow = std::make_unique<juce::TooltipWindow> (&plate, 800);

    attachControls();
    bindPanels();
    restoreUiState();

    controller.addListener (this);
    lastVersion = controller.version();
    knownZoom = controller.zoom();
    refreshAll();

    // One size rule for the life of the window: the plate's aspect, 75 to 200 %. Only the
    // user's zoom choice or a host or user drag resize changes the size, never a parameter
    // (external play mode swaps the sequencer screen in place).
    setResizable (true, true);

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignW) / static_cast<double> (kDesignH));

    setResizeLimits (juce::roundToInt (kDesignW * metrics::kMinZoom), juce::roundToInt (kDesignH * metrics::kMinZoom),
                     juce::roundToInt (kDesignW * metrics::kMaxZoom), juce::roundToInt (kDesignH * metrics::kMaxZoom));
    setZoom (controller.zoom());
    startTimerHz (30);
}

mw12FEditor::~mw12FEditor()
{
    stopTimer();
    releaseHeldOverrides();
    controller.stopPreview();   // a preview key held while the window closes is released

    // Nothing would show an armed learn once the window is gone: it ends with the window.
    if (controller.midiLearning())
        controller.cancelMidiLearn();

    plate.removeMouseListener (learnClickWatcher.get());
    plate.sequencer.tempoBox.removeListener (tempoGesture.get());
    tooltipWindow.reset();
    controller.removeListener (this);
    browser.close (true);
    plate.setOverlay (nullptr);
    chooser.reset();   // the dialog, if one is up, goes with the window
    cleanDragDirectory();
    setLookAndFeel (nullptr);
}

//==============================================================================
ParamHooks mw12FEditor::hooksFor (ParamId id)
{
    ParamHooks h;
    h.name = paramInfo (id).name;
    h.midiAssignment = [this, id]
    {
        const auto cc = controller.midiControllerFor (id);
        return cc >= 0 ? "CC " + juce::String (cc) : juce::String();
    };
    h.onMidiLearn = [this, id] { controller.startMidiLearn (id); };
    h.onMidiClear = [this, id] { controller.clearMidiMapping (id); };
    h.isLearning = [this, id] { return controller.midiLearning() && controller.midiLearnTarget() == id; };
    h.onCancelLearn = [this] { cancelMidiLearn(); };
    return h;
}

void mw12FEditor::attachControls()
{
    auto& controls = plate.getControls();

    controls.forEachSlider ([&] (ParamId id, juce::Slider& s)
    {
        sliderAttachments.push_back (std::make_unique<PolledSliderAttachment> (controller.parameter (id), s));
        s.setTooltip (paramTooltip (id));

        if (auto* d = dynamic_cast<DragSlider*> (&s))
        {
            applyValueText (*d, id);
            d->param = hooksFor (id);
        }
    });

    controls.forEachButton ([&] (ParamId id, juce::Button& b)
    {
        b.setTooltip (paramTooltip (id));

        if (auto* push = dynamic_cast<PushButton*> (&b))
        {
            // Momentary (the live overrides): down = 1, up = 0, and the cap lights while the
            // parameter is high, however it got there.
            push->param = hooksFor (id);
            auto attachment = std::make_unique<PolledParameterAttachment> (controller.parameter (id), [push] (float v)
            {
                push->setToggleState (v >= 0.5f, juce::dontSendNotification);
            });
            auto* raw = attachment.get();
            auto held = std::make_shared<bool> (false);
            push->onStateChange = [push, raw, held]
            {
                const auto down = push->isDown();

                if (down != *held)
                {
                    *held = down;
                    raw->setValueAsCompleteGesture (down ? 1.0f : 0.0f);
                }
            };
            attachment->sendInitialUpdate();
            momentaries.push_back ({ push, raw, held });
            watches.push_back (std::move (attachment));
            return;
        }

        if (auto* led = dynamic_cast<LedButton*> (&b))
            led->param = hooksFor (id);

        buttonAttachments.push_back (std::make_unique<PolledButtonAttachment> (controller.parameter (id), b));
    });

    controls.forEachComboBox ([&] (ParamId id, juce::ComboBox& box)
    {
        box.setTooltip (paramTooltip (id));

        if (auto* d = dynamic_cast<Dropdown*> (&box))
            d->param = hooksFor (id);

        comboAttachments.push_back (std::make_unique<PolledComboBoxAttachment> (controller.parameter (id), box));
    });

    // The distortion model box: an Int row whose list is the voicing table.
    auto& modelBox = plate.distortion.modelBox;
    plate.distortion.setModelNames (distortionVoicingNames());
    modelBox.param = hooksFor (kDistModel);
    modelBox.setTooltip (paramTooltip (kDistModel));
    modelAttachment = std::make_unique<PolledParameterAttachment> (controller.parameter (kDistModel), [this, &modelBox] (float v)
    {
        const juce::ScopedValueSetter<bool> guard (syncing, true);
        const auto index = juce::roundToInt (v);

        if (index >= 0 && index < modelBox.getNumItems())
            modelBox.setSelectedItemIndex (index, juce::dontSendNotification);
        else
            modelBox.setText ("Model " + juce::String (index), juce::dontSendNotification);
    });
    modelBox.onChange = [this, &modelBox]
    {
        if (syncing || modelBox.getSelectedItemIndex() < 0)
            return;

        modelAttachment->setValueAsCompleteGesture (static_cast<float> (modelBox.getSelectedItemIndex()));
    };
    modelAttachment->sendInitialUpdate();
}

void mw12FEditor::releaseHeldOverrides()
{
    for (auto& m : momentaries)
    {
        if (*m.held || m.button->isDown())
        {
            *m.held = false;
            m.attachment->setValueAsCompleteGesture (0.0f);
        }
    }
}

void mw12FEditor::watch (ParamId id, std::function<void (float)> fn)
{
    watches.push_back (std::make_unique<PolledParameterAttachment> (controller.parameter (id), std::move (fn)));
    watches.back()->sendInitialUpdate();
}

void mw12FEditor::runUiAction (const std::function<void()>& action)
{
    ++uiActions;
    action();
    --uiActions;
}

void mw12FEditor::bindPanels()
{
    auto& top = plate.topBar;
    auto& synth = plate.synth;
    auto& seqPanel = plate.sequencer;
    auto& rack = plate.rack;

    // ---- Parameter side effects ----
    watch (kPlayMode, [this] (float v) { playModeChanged (static_cast<seq::PlayMode> (juce::jlimit (0, 2, juce::roundToInt (v)))); });
    watch (kTuneExt, [&synth] (float v) { synth.setExtendedTuning (v >= 0.5f); });
    watch (kSweepCouple, [this, &synth] (float v)
    {
        synth.setCouples (v >= 0.5f, controller.getParam (kAccVolCouple) >= 0.5f);
        updateCoupleTips (v >= 0.5f, controller.getParam (kAccVolCouple) >= 0.5f);
    });
    watch (kAccVolCouple, [this, &synth] (float v)
    {
        synth.setCouples (controller.getParam (kSweepCouple) >= 0.5f, v >= 0.5f);
        updateCoupleTips (controller.getParam (kSweepCouple) >= 0.5f, v >= 0.5f);
    });
    watch (kModOn, [this] (float v) { plate.mods.setModsEnabled (v >= 0.5f); });
    watch (kPatternBank, [this] (float) { slotChanged(); });
    watch (kPatternSlot, [this] (float) { slotChanged(); });

    // ---- Top bar ----
    const auto stepScene = [this] (int direction)
    {
        runUiAction ([this, direction]
        {
            plugin::PresetFilter f;
            f.type = PresetType::Scene;
            controller.loadNextPreset (f, direction);
        });
    };
    top.presetBox.onOpen = [this] { openPresetBrowser (PresetType::Scene); return true; };
    top.prevPreset.onClick = [stepScene] { stepScene (-1); };
    top.nextPreset.onClick = [stepScene] { stepScene (1); };
    top.savePreset.onClick = [this] { openPresetBrowser (PresetType::Scene, true); };
    top.presetBox.setTooltip ("Scene: the whole instrument, patterns included. Click to browse.");
    top.prevPreset.setTooltip ("Previous scene");
    top.nextPreset.setTooltip ("Next scene");
    top.savePreset.setTooltip ("Save the scene as a user preset.");
    top.settingsButton.setTooltip ("Settings: theme, zoom, tempo, sequencer, MIDI and quality options.");
    top.zoomBox.setTooltip ("Window size");
    plate.calibration.presetBox.setTooltip ("Calibration preset: one unit's service trims. Click to browse.");
    rack.presetBox.setTooltip ("Preset of the effect shown. Click to browse.");
    rack.viewSelector.setTooltip ("Basic: the main controls. Full: every control of the effect.");
    plate.distortion.prevModel.setTooltip ("Previous distortion voicing");
    plate.distortion.nextModel.setTooltip ("Next distortion voicing");
    top.onZoomChanged = [this] (float z) { setZoom (z); };
    top.settingsButton.onClick = [this] { showSettingsMenu (controller, plate, plate.topBar.settingsButton, settingsMenuHooks()); };

    // ---- Calibration and effects preset boxes ----
    plate.calibration.presetBox.onOpen = [this] { openPresetBrowser (PresetType::Calibration); return true; };
    rack.presetBox.onOpen = [this] { openPresetBrowser (presetTypeForEffect (plate.rack.currentEffect())); return true; };
    rack.onOrderChanged = [this] (const std::array<std::uint8_t, kNumFxSlots>& order)
    {
        auto s = controller.settings();

        if (s.fxOrder != order)
        {
            s.fxOrder = order;
            controller.setSettings (s, "Effects order");
        }
    };

    // ---- Sequencer: pattern operations ----
    const auto arp = [this] { return patternModel.isArp(); };
    const auto slot = [this] { return patternModel.getSlot(); };

    seqPanel.onShift = [this, arp, slot] (int direction) { controller.shiftPattern (arp(), slot(), direction); };
    seqPanel.onTranspose = [this, arp, slot] (int n)
    {
        if (arp())
            controller.invertArpPattern (slot(), n);
        else
            controller.transposePattern (false, slot(), n);
    };
    seqPanel.copyButton.onClick = [this, arp, slot] { controller.copyPattern (arp(), slot()); showNotice ("Pattern copied"); };
    seqPanel.pasteButton.onClick = [this, arp, slot]
    {
        if (! controller.pastePattern (arp(), slot()))
            showNotice ("No pattern on the clipboard");
    };
    seqPanel.clearButton.onClick = [this, arp, slot] { controller.clearPattern (arp(), slot()); };
    seqPanel.dupButton.onClick = [this, arp, slot] { controller.duplicatePattern (arp(), slot()); };
    seqPanel.undoButton.onClick = [this] { runUiAction ([this] { controller.undo(); }); };
    seqPanel.redoButton.onClick = [this] { runUiAction ([this] { controller.redo(); }); };
    seqPanel.exportButton.onClick = [this] { exportMidi(); };
    seqPanel.importButton.onClick = [this] { importMidi(); };
    seqPanel.midiDrag.onCreateFile = [this] { return writeDragMidiFile(); };
    seqPanel.midiDrag.onCreatePatternFile = [this] { return writeDragPatternFile (patternModel.getSlot()); };
    seqPanel.selector.onDragOut = [this] (int slotIndex) { return writeDragPatternFile (slotIndex); };
    seqPanel.selector.onFilesDropped = [this] (const juce::StringArray& files, int slotIndex) { loadPatternFiles (files, slotIndex); };

    tempoGesture = std::make_unique<TempoGesture> (controller);
    seqPanel.tempoBox.addListener (tempoGesture.get());
    seqPanel.tempoBox.onValueChange = [this]
    {
        if (syncing)
            return;

        auto s = controller.settings();
        s.internalBpm = static_cast<float> (plate.sequencer.tempoBox.getValue());
        controller.setSettings (s, "Internal tempo");
    };

    // The pattern name: a click browses, the right-click menu renames (a double-click too,
    // but the browser's overlay takes the second click, so the menu is the mouse path).
    seqPanel.patternName.onOpen = [this, arp] { openPresetBrowser (arp() ? PresetType::Arp : PresetType::Pattern); return true; };
    seqPanel.patternName.onDoubleClick = [this] { renamePattern(); };
    seqPanel.patternName.onContextMenu = [this] { showPatternNameMenu(); };
    const auto stepPattern = [this, arp] (int direction)
    {
        runUiAction ([this, arp, direction]
        {
            plugin::PresetFilter f;
            f.type = arp() ? PresetType::Arp : PresetType::Pattern;
            controller.loadNextPreset (f, direction);
        });
    };
    seqPanel.prevPattern.onClick = [stepPattern] { stepPattern (-1); };
    seqPanel.nextPattern.onClick = [stepPattern] { stepPattern (1); };

    seqPanel.selector.onSelect = [this] (int bank, int slotInBank) { controller.selectSlot (seq::slotIndex (bank, slotInBank)); };

    seqPanel.triggerBox.onChange = [this]
    {
        if (syncing) return;
        auto s = controller.settings();
        s.triggerMode = static_cast<seq::TriggerMode> (juce::jmax (0, plate.sequencer.triggerBox.getSelectedItemIndex()));
        controller.setSettings (s, "Trigger mode");
    };
    seqPanel.launchBox.onChange = [this]
    {
        if (syncing) return;
        auto s = controller.settings();
        s.launchQuant = static_cast<seq::LaunchQuant> (juce::jmax (0, plate.sequencer.launchBox.getSelectedItemIndex()));
        controller.setSettings (s, "Launch quantise");
    };

    // ---- External play mode: its settings, bound to Settings through the controller ----
    auto& ext = plate.external;
    ext.onSettingsEdit = [this] (const char* label, const std::function<void (Settings&)>& edit)
    {
        if (syncing)
            return;

        auto s = controller.settings();
        edit (s);
        controller.setSettings (s, label);
    };
    ext.onGestureBegin = [this] { controller.beginGesture(); };
    ext.onGestureEnd = [this] { controller.endGesture(); };

    // ---- Generator ----
    auto& gen = seqPanel.generator;
    juce::StringArray templateNames, templateCategories;
    static constexpr const char* kCategories[] = { "Scales", "Pentatonic", "Chords", "Intervals", "Acid", "Other" };

    for (const auto& t : controller.scaleTemplates())
    {
        templateNames.add (text::from (t.name));
        const auto c = static_cast<std::size_t> (t.category);
        templateCategories.add (c < std::size (kCategories) ? kCategories[c] : "Other");
    }

    gen.setTemplates (templateNames, templateCategories);
    gen.onDraw = [this, arp, slot]
    {
        // A 32-bit seed, which is all GenParams stores: the readout shows every digit, so
        // typing it back draws the same line.
        auto seed = static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt());
        controller.generate (arp(), slot(), seed == 0 ? 1u : seed);
    };
    gen.onSeedEntered = [this, arp, slot] (std::uint32_t seed) { controller.generate (arp(), slot(), seed); };
    gen.onParamsChanged = [this, arp, slot] (const seq::GenParams& gp) { controller.setGenParams (arp(), slot(), gp); };
    gen.onTemplateChosen = [this, arp, slot] (int index) { controller.applyScaleTemplate (arp(), slot(), index); };
    gen.onFlatten = [this, arp, slot] { controller.flattenPattern (arp(), slot()); };

    // ---- Browser ----
    browser.onNotice = [this] (const juce::String& text) { showNotice (text); };
    browser.onClose = [this] { refreshPresetBoxes(); };
}

//==============================================================================
SettingsMenuHooks mw12FEditor::settingsMenuHooks()
{
    SettingsMenuHooks hooks;
    hooks.currentTheme = plate.getThemeId();
    hooks.currentZoom = getZoom();
    hooks.setTheme = [this] (ThemeId id) { setThemeId (id); };
    hooks.setZoom = [this] (float z) { setZoom (z); };
    hooks.setSequencerView = [this] (bool keys) { plate.sequencer.setView (keys ? SequencerPanel::View::Keyboard : SequencerPanel::View::PianoRoll); };
    hooks.isKeyboardView = [this] { return plate.sequencer.getView() == SequencerPanel::View::Keyboard; };
    hooks.setCoverOpen = [this] (bool open) { plate.calibration.cover.setOpen (open); };
    hooks.isCoverOpen = [this] { return plate.calibration.cover.isOpen(); };
    hooks.setCoverAnimated = [this] (bool animate) { plate.calibration.cover.setAnimated (animate); persisted.coverAnim = animate;
                                                    controller.setGlobalUiValue ("cover_anim", animate ? "1" : "0"); };
    hooks.isCoverAnimated = [this] { return persisted.coverAnim; };
    hooks.notice = [this] (const juce::String& text) { showNotice (text); };
    hooks.saveMidiMap = [this] { chooseMidiMapFile (true); };
    hooks.loadMidiMap = [this] { chooseMidiMapFile (false); };
    hooks.runAction = [this] (const std::function<void()>& action) { runUiAction (action); };
    hooks.saveDefaultState = [this]
    {
        std::string error;
        showNotice (controller.saveDefaultState (&error) ? juce::String ("Saved as the default for new instances") : text::from (error));
    };
    hooks.resetDefaultState = [this]
    {
        std::string error;
        showNotice (controller.resetDefaultState (&error) ? juce::String ("New instances start from the factory default") : text::from (error));
    };
    hooks.hasUserDefaultState = controller.hasUserDefaultState();
    hooks.qualityAppliesAtActivation = processor.wrapperType == juce::AudioProcessor::wrapperType_LV2;
    return hooks;
}

float mw12FEditor::getZoom() const noexcept
{
    return getWidth() > 0 ? juce::jlimit (metrics::kMinZoom, metrics::kMaxZoom, zoomFor (getWidth(), getHeight()))
                          : controller.zoom();
}

void mw12FEditor::setZoom (float zoom)
{
    const auto z = juce::jlimit (metrics::kMinZoom, metrics::kMaxZoom, zoom);
    zoomSettleTicks = 0;   // an explicit zoom supersedes any host resize still settling
    pendingZoom = -1.0f;
    {
        const juce::ScopedValueSetter<bool> guard (settingSize, true);
        setSize (juce::roundToInt (kDesignW * z), juce::roundToInt (kDesignH * z));
        resized();   // setSize is a no-op when the size is unchanged; the transform still has to follow
    }

    knownZoom = z;

    if (std::abs (z - controller.zoom()) > 0.002f)
        controller.setZoom (z);
}

void mw12FEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // The plate is scaled to fit both axes and centred; paint() fills any margin. A host
    // that honours the aspect constraint leaves no margin.
    const auto z = zoomFor (getWidth(), getHeight());
    const auto plateW = static_cast<float> (kDesignW) * z;
    const auto plateH = static_cast<float> (kDesignH) * z;
    const auto ox = std::floor ((static_cast<float> (getWidth()) - plateW) * 0.5f);
    const auto oy = std::floor ((static_cast<float> (getHeight()) - plateH) * 0.5f);
    plate.setTopLeftPosition (0, 0);
    plate.setTransform (juce::AffineTransform::scale (z).translated (ox, oy));
    plate.topBar.setZoom (z);

    // A host drag-resize arrives as a stream of sizes: persist the zoom (which also writes
    // the global default file) once it has settled, from the timer.
    if (! settingSize)
    {
        pendingZoom = juce::jlimit (metrics::kMinZoom, metrics::kMaxZoom, z);
        zoomSettleTicks = 10;
    }
}

void mw12FEditor::paint (juce::Graphics& g)
{
    // Only the margin around the plate (a window without the aspect ratio): the plate is
    // opaque, but JUCE does not exclude a transformed child from its parent's clip.
    g.excludeClipRegion (plate.getBoundsInParent());
    g.fillAll (juce::Colours::black);
}

void mw12FEditor::setThemeId (ThemeId id)
{
    plate.setThemeId (id);
    const auto name = id == ThemeId::Silver ? "silver" : "night";

    if (controller.theme() != name)
        controller.setTheme (name);
}

void mw12FEditor::openPresetBrowser (PresetType type, bool saveMode)
{
    if (plate.getOverlay() != &browser)
        plate.setOverlay (&browser);

    browser.open (type, saveMode);
}

void mw12FEditor::showNotice (const juce::String& text)
{
    plate.showValueTip (plate.topBar.presetBox, {}, text::sanitise (text));
    noticeTicks = 90;
}

//==============================================================================
void mw12FEditor::controllerChanged (std::uint32_t changed)
{
    using namespace acid::plugin;

    // A whole state (parameters with bank or settings) that neither the browser nor an action
    // of this editor loaded came from outside: a host project restore or program change. The
    // audition snapshot is stale then, so what the host loaded is kept and the browser closes
    // rather than letting CANCEL overwrite the host's state later.
    if ((changed & kChangedParams) && (changed & (kChangedPatterns | kChangedSettings)) && browser.isVisible() && ! browser.isLoading()
        && uiActions == 0)
    {
        browser.close (true);
        showNotice ("Browser closed: the host loaded a new state");
    }

    if (changed & kChangedPatterns)
    {
        patternModel.refresh();
        refreshUsedSlots();
        refreshPatternName();
    }

    if (changed & kChangedSettings)
        refreshSettingsBoxes();

    if (changed & kChangedParams)
        refreshPresetBoxes();

    if (changed & kChangedPresets)
    {
        refreshPresetBoxes();
        refreshPatternName();
        browser.refresh();
    }

    if (changed & kChangedMidiMap)
        refreshLearnIndicator();

    if (changed & kChangedUi)
    {
        const auto theme = controller.theme() == "silver" ? ThemeId::Silver : ThemeId::Night;

        if (theme != plate.getThemeId())
            plate.setThemeId (theme);

        // Follow the stored zoom only when something other than this editor changed it (a
        // state load); while a host resize is still settling the window is the truth.
        if (! settingSize && zoomSettleTicks == 0 && std::abs (controller.zoom() - knownZoom) > 0.002f)
            setZoom (controller.zoom());
    }

    if (changed & kChangedUndo)
        refreshUndo();

    lastVersion = controller.version();
}

void mw12FEditor::slotChanged()
{
    const auto slot = controller.selectedSlot();
    plate.sequencer.selector.setSelected (slot / seq::kSlotsPerBank, slot % seq::kSlotsPerBank);
    patternModel.setSlot (plate.getPlayMode() == seq::PlayMode::Arpeggiator, slot);
    refreshPatternName();
}

void mw12FEditor::playModeChanged (seq::PlayMode mode)
{
    if (mode == seq::PlayMode::External)
        releaseHeldOverrides();   // the sequencer screen (and its override buttons) is about to hide

    plate.setPlayMode (mode);
    patternModel.setSlot (mode == seq::PlayMode::Arpeggiator, controller.selectedSlot());
    refreshUsedSlots();   // the selector's dots show the bank the mode edits
    refreshPatternName();
}

void mw12FEditor::refreshAll()
{
    patternModel.refresh();
    refreshUsedSlots();
    refreshSettingsBoxes();
    refreshPresetBoxes();
    refreshPatternName();
    refreshUndo();
    refreshLearnIndicator();
}

void mw12FEditor::refreshSettingsBoxes()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = controller.settings();
    plate.rack.setOrder (s.fxOrder);
    plate.sequencer.triggerBox.setSelectedItemIndex (static_cast<int> (s.triggerMode), juce::dontSendNotification);
    plate.sequencer.launchBox.setSelectedItemIndex (static_cast<int> (s.launchQuant), juce::dontSendNotification);
    plate.sequencer.tempoBox.setValue (s.internalBpm, juce::dontSendNotification);
    plate.external.showSettings (s);

    // Host Note: the keys show which MIDI note starts which pattern.
    plate.sequencer.selector.setHostNoteBase (s.triggerMode == seq::TriggerMode::HostNote ? static_cast<int> (s.hostNoteBase) : -1);
}

void mw12FEditor::updateCoupleTips (bool sweepCoupled, bool accentVolumeCoupled)
{
    auto& synth = plate.synth;
    synth.sweepAmt.setTooltip (sweepCoupled ? juce::String ("Sweep Amount: out of use while the amber couple LEDs are lit (RESONANCE sets the "
                                                            "accent sweep). Click an amber LED to uncouple and use this knob.")
                                            : paramTooltip (kSweepAmt));
    synth.accVol.setTooltip (accentVolumeCoupled ? juce::String ("Accent Volume: out of use while the cyan couple LEDs are lit (ACCENT sets the "
                                                                 "accent's loudness). Click a cyan LED to uncouple and use this knob.")
                                                 : paramTooltip (kAccVol));
    const auto sweepTip = paramTooltip (kSweepCouple) + (sweepCoupled ? " Now: coupled." : " Now: uncoupled.");
    const auto accTip = paramTooltip (kAccVolCouple) + (accentVolumeCoupled ? " Now: coupled." : " Now: uncoupled.");
    synth.sweepCoupleA.setTooltip (sweepTip);
    synth.sweepCoupleB.setTooltip (sweepTip);
    synth.accCoupleA.setTooltip (accTip);
    synth.accCoupleB.setTooltip (accTip);
}

PresetType mw12FEditor::presetTypeForEffect (FxSlot slot) const noexcept
{
    switch (slot)
    {
        case FxSlot::Distortion: return PresetType::Distortion;
        case FxSlot::Eq:         return PresetType::Eq;
        case FxSlot::Delay:      return PresetType::Delay;
        case FxSlot::Reverb:     return PresetType::Reverb;
        case FxSlot::Chorus:     return PresetType::Chorus;
        case FxSlot::Limiter:    break;
    }

    return PresetType::Limiter;
}

juce::String mw12FEditor::presetBoxText (PresetType type) const
{
    const auto* entry = controller.lastLoaded (type);

    if (entry == nullptr)
        return "- Custom -";

    const auto& snap = loaded[static_cast<std::size_t> (type)];
    auto edited = false;

    if (snap.id == entry->id)
    {
        const auto live = controller.paramValues();

        for (auto id : typeRows[static_cast<std::size_t> (type)])
            if (std::abs (live[static_cast<std::size_t> (id)] - snap.values[static_cast<std::size_t> (id)]) > 1.0e-4f)
            {
                edited = true;
                break;
            }
    }

    return text::from (entry->name) + (edited ? " *" : "");
}

void mw12FEditor::applyDefaults (PresetType type)
{
    // Double-click returns a control to the value the last preset of its type loaded, not
    // the table default. Only the rows that type carries move: a delay preset leaves the
    // cutoff knob's double-click where the last synth or scene put it.
    auto& controls = plate.getControls();

    for (auto id : typeRows[static_cast<std::size_t> (type)])
    {
        if (paramInfo (id).kind != ParamKind::Float && paramInfo (id).kind != ParamKind::Int)
            continue;

        const auto value = static_cast<double> (controller.getParam (id));
        controls.forEachSlider ([id, value] (ParamId rowId, juce::Slider& s)
        {
            if (rowId == id)
                if (auto* d = dynamic_cast<DragSlider*> (&s))
                    d->setDefaultValue (value);
        });
    }
}

void mw12FEditor::refreshPresetBoxes()
{
    // Snapshot the parameters of a freshly loaded preset per type, so the box can show when
    // the user has moved away from it, and move that type's double-click defaults.
    const auto live = controller.paramValues();

    for (int t = 0; t < kNumPresetTypes; ++t)
    {
        auto& snap = loaded[static_cast<std::size_t> (t)];
        const auto* entry = controller.lastLoaded (static_cast<PresetType> (t));
        const std::string id = entry != nullptr ? entry->id : std::string();

        if (id != snap.id)
        {
            snap.id = id;
            snap.values = live;

            if (! id.empty())
                applyDefaults (static_cast<PresetType> (t));
        }
    }

    const juce::ScopedValueSetter<bool> guard (syncing, true);
    plate.topBar.presetBox.setText (text::from (controller.sceneName()), juce::dontSendNotification);
    plate.calibration.presetBox.setText (presetBoxText (PresetType::Calibration), juce::dontSendNotification);
    plate.rack.presetBox.setText (presetBoxText (presetTypeForEffect (plate.rack.currentEffect())), juce::dontSendNotification);
}

void mw12FEditor::checkPresetEdits()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto cal = presetBoxText (PresetType::Calibration);
    const auto fx = presetBoxText (presetTypeForEffect (plate.rack.currentEffect()));

    if (plate.calibration.presetBox.getText() != cal)
        plate.calibration.presetBox.setText (cal, juce::dontSendNotification);

    if (plate.rack.presetBox.getText() != fx)
        plate.rack.presetBox.setText (fx, juce::dontSendNotification);
}

void mw12FEditor::refreshPatternName()
{
    const auto& p = patternModel.getPattern();
    const auto name = text::fromArray (p.name);
    const auto label = slotName (patternModel.getSlot()) + "  " + (name.isNotEmpty() ? name : juce::String (patternHasNotes (p) ? "" : "(empty)"));
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    if (plate.sequencer.patternName.getText() != label)
    {
        plate.sequencer.patternName.setText (label, juce::dontSendNotification);
        const auto where = slotName (patternModel.getSlot());
        const auto kind = patternModel.isArp() ? "arpeggiator pattern" : "pattern";
        plate.sequencer.prevPattern.setTooltip (juce::String ("Load the previous ") + kind + " preset into " + where + " (replaces it; undoable).");
        plate.sequencer.nextPattern.setTooltip (juce::String ("Load the next ") + kind + " preset into " + where + " (replaces it; undoable).");
    }

    plate.sequencer.generator.setTemplateIndex (controller.matchingScaleTemplate (patternModel.isArp(), patternModel.getSlot()));
}

void mw12FEditor::refreshUsedSlots()
{
    std::array<std::uint16_t, 4> used {};
    const auto arp = patternModel.isArp();

    for (int slot = 0; slot < seq::kNumSlots; ++slot)
        if (patternHasNotes (controller.pattern (arp, slot)))
            used[static_cast<std::size_t> (slot / seq::kSlotsPerBank)] |= static_cast<std::uint16_t> (1u << (slot % seq::kSlotsPerBank));

    plate.sequencer.selector.setUsedSlots (used);
}

void mw12FEditor::refreshUndo()
{
    plate.sequencer.undoButton.setEnabled (controller.canUndo());
    plate.sequencer.redoButton.setEnabled (controller.canRedo());
}

void mw12FEditor::refreshLearnIndicator()
{
    if (! controller.midiLearning())
    {
        learnIndicator.hide();

        // Escape cancels a learn: the editor takes the keyboard only while one is armed.
        if (getWantsKeyboardFocus())
        {
            const auto hadFocus = hasKeyboardFocus (false);
            setWantsKeyboardFocus (false);

            if (hadFocus)
                giveAwayKeyboardFocus();
        }

        return;
    }

    const auto id = controller.midiLearnTarget();
    auto& controls = plate.getControls();
    juce::Component* target = controls.findSlider (id);

    if (target == nullptr) target = controls.findButton (id);
    if (target == nullptr) target = controls.findComboBox (id);
    if (target == nullptr && id == kDistModel) target = &plate.distortion.modelBox;

    // A control on a hidden page (another effect tab, the full view) has nothing to ring:
    // the banner still names the parameter.
    const auto shownOnPlate = [this] (juce::Component* c)
    {
        for (; c != nullptr && c != &plate; c = c->getParentComponent())
            if (! c->isVisible())
                return false;

        return c == &plate;
    };

    if (! shownOnPlate (target))
        target = &plate.topBar;

    learnIndicator.show (*target, paramInfo (id).name);

    if (isShowing() && ! plate.isTextEntryActive())
    {
        setWantsKeyboardFocus (true);
        grabKeyboardFocus();
    }
}

void mw12FEditor::handlePlateClick (juce::Component& clicked)
{
    if (! controller.midiLearning())
        return;

    auto* target = learnIndicator.getTarget();

    // The target itself (its right-click menu offers Cancel learn) and the indicator (which
    // cancels on its own) are left alone; any other press ends the learn.
    if (&clicked == &learnIndicator)
        return;

    if (target != nullptr && (&clicked == target || target->isParentOf (&clicked)
                              || (&clicked == target->getParentComponent() && dynamic_cast<PatternSelector*> (&clicked) != nullptr)))
        return;

    cancelMidiLearn();
}

void mw12FEditor::cancelMidiLearn()
{
    if (controller.midiLearning())
        controller.cancelMidiLearn();

    refreshLearnIndicator();
}

bool mw12FEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && controller.midiLearning())
    {
        cancelMidiLearn();
        return true;
    }

    return false;
}

//==============================================================================
void mw12FEditor::restoreUiState()
{
    auto& s = plate.sequencer;
    s.setView (controller.uiValue ("view", "roll") == "keys" ? SequencerPanel::View::Keyboard : SequencerPanel::View::PianoRoll);
    s.setToolsTab (controller.uiValue ("tools", "edit") == "generate" ? SequencerPanel::ToolsTab::Generate : SequencerPanel::ToolsTab::Edit);
    const auto fxTab = juce::jlimit (0, kNumFxSlots - 1, text::from (controller.uiValue ("fx_tab", "2")).getIntValue());
    plate.rack.tabs.setSelectedTab (fxTab, juce::dontSendNotification);
    plate.rack.showEffect (static_cast<FxSlot> (fxTab));
    plate.rack.setFullView (controller.uiValue ("fx_full", "0") == "1");
    persisted.coverAnim = controller.globalUiValue ("cover_anim", "1") != "0";
    plate.calibration.cover.setAnimated (persisted.coverAnim);
    plate.calibration.cover.setOpen (controller.uiValue ("cover", "0") == "1", false);

    persisted.view = static_cast<int> (s.getView());
    persisted.tools = static_cast<int> (s.getToolsTab());
    persisted.fxTab = fxTab;
    persisted.fxFull = plate.rack.isFullView();
    persisted.coverOpen = plate.calibration.cover.isOpen();
}

void mw12FEditor::persistUiState()
{
    auto& s = plate.sequencer;
    const auto view = static_cast<int> (s.getView());
    const auto tools = static_cast<int> (s.getToolsTab());
    const auto fxTab = static_cast<int> (plate.rack.currentEffect());
    const auto fxFull = plate.rack.isFullView();
    const auto cover = plate.calibration.cover.isOpen();

    if (view != persisted.view)       { persisted.view = view;     controller.setUiValue ("view", view == 1 ? "keys" : "roll"); }
    if (tools != persisted.tools)     { persisted.tools = tools;   controller.setUiValue ("tools", tools == 1 ? "generate" : "edit"); }
    if (fxFull != persisted.fxFull)   { persisted.fxFull = fxFull; controller.setUiValue ("fx_full", fxFull ? "1" : "0"); }
    if (cover != persisted.coverOpen) { persisted.coverOpen = cover; controller.setUiValue ("cover", cover ? "1" : "0"); }

    if (fxTab != persisted.fxTab)
    {
        persisted.fxTab = fxTab;
        controller.setUiValue ("fx_tab", std::to_string (fxTab));
        refreshPresetBoxes();
    }
}

//==============================================================================
void mw12FEditor::tick()
{
    ++tickCount;
    pollAttachments();
    updateStatus();
    learnIndicator.tick();

    // Learn armed or ended from elsewhere (a host's state load, the settings menu, a CC).
    if (controller.midiLearning() != learnIndicator.isActive())
        refreshLearnIndicator();
    persistUiState();

    if (noticeTicks > 0 && --noticeTicks == 0)
        plate.hideValueTip (&plate.topBar.presetBox);

    if (zoomSettleTicks > 0 && --zoomSettleTicks == 0)
    {
        knownZoom = pendingZoom;

        if (std::abs (pendingZoom - controller.zoom()) > 0.002f)
            controller.setZoom (pendingZoom);
    }

    // Changes made from other threads (a host state load) raise the version; the processor's
    // own timer delivers the flags, this is the backstop when it has not run yet.
    if (const auto v = controller.version(); v != lastVersion)
    {
        lastVersion = v;
        patternModel.refresh();
        refreshSettingsBoxes();
        refreshUndo();
    }

    if (tickCount % 10 == 0)
        checkPresetEdits();
}

void mw12FEditor::pollAttachments()
{
    for (auto& a : sliderAttachments) a->poll();
    for (auto& a : buttonAttachments) a->poll();
    for (auto& a : comboAttachments)  a->poll();
    for (auto& a : watches)           a->poll();

    if (modelAttachment != nullptr)
        modelAttachment->poll();
}

void mw12FEditor::updateStatus()
{
    const auto st = controller.engineStatus();

    // Output meter with a short hold so single-block peaks read at 30 Hz.
    holdLeft = juce::jmax (st.peakLeft, holdLeft * 0.82f);
    holdRight = juce::jmax (st.peakRight, holdRight * 0.82f);
    plate.master.meter.setLevels (toDb (holdLeft), toDb (holdRight));
    plate.rack.gainReduction.setReductionDb (st.limiterReductionDb);

    if (st.gateOpen != gateLit)        { gateLit = st.gateOpen;          plate.synth.gateLamp.setLit (gateLit); }
    if (st.accentLatched != accentLit) { accentLit = st.accentLatched;   plate.synth.accentLamp.setLit (accentLit); }
    plate.synth.chargeMeter.setCharge (st.accentCharge);

    // The external-MIDI monitor: only what EngineStatus already carries.
    if (plate.external.isVisible())
        plate.external.setStatus (st.gateOpen, st.accentLatched, st.envelope, st.accentCharge, st.midiInDropped);

    if (st.running != runLit)
    {
        runLit = st.running;
        plate.sequencer.run.setLit (runLit);
        plate.sequencer.selector.setRunning (runLit);
    }

    // Which clock drives the line: INT keeps playing when the host stops, HOST follows it.
    plate.sequencer.run.setBadge (! st.running ? juce::String() : (st.internalClock ? "INT" : "HOST"));

    if (st.internalClock != internalClockShown)
    {
        internalClockShown = st.internalClock;
        plate.sequencer.tempoBox.setEnabled (internalClockShown);
    }

    // A synced delay time longer than the line holds is clamped: the delay tab turns amber.
    if (st.delayClamped != delayClampLit)
    {
        delayClampLit = st.delayClamped;
        plate.rack.tabs.setTabWarning (static_cast<int> (FxSlot::Delay), delayClampLit);

        if (delayClampLit && controller.getParam (kDelayOn) >= 0.5f)
            showNotice ("Delay time clamped: the synced time is longer than the delay line");
    }

    // Pattern LEDs: the engine reports slots of the bank the play mode uses.
    const auto modeMatches = st.playMode == static_cast<int> (plate.getPlayMode()) && plate.getPlayMode() != seq::PlayMode::External;
    const auto playing = modeMatches ? st.playingSlot : -1;
    const auto queued = modeMatches ? st.queuedSlot : -1;
    const auto pb = playing >= 0 ? playing / seq::kSlotsPerBank : -1, ps = playing >= 0 ? playing % seq::kSlotsPerBank : -1;
    const auto qb = queued >= 0 ? queued / seq::kSlotsPerBank : -1, qs = queued >= 0 ? queued % seq::kSlotsPerBank : -1;

    if (pb != playingBank || ps != playingSlot) { playingBank = pb; playingSlot = ps; plate.sequencer.selector.setPlaying (pb, ps); }
    if (qb != queuedBank || qs != queuedSlot)   { queuedBank = qb;  queuedSlot = qs;  plate.sequencer.selector.setQueued (qb, qs); }

    patternModel.setPlayStep (playing >= 0 && playing == patternModel.getSlot() ? st.currentStep : -1);
}

//==============================================================================
void mw12FEditor::exportMidi()
{
    const auto arp = patternModel.isArp();
    const auto slot = patternModel.getSlot();
    const auto suggested = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                               .getChildFile (text::from (plugin::PresetManager::safeFileName ((slotName (slot) + " " + patternFileName (arp, slot)).toStdString()))
                                              + ".mid");
    chooser = std::make_unique<juce::FileChooser> ("Export pattern as MIDI file", suggested, "*.mid", false, false, this);
    juce::Component::SafePointer<mw12FEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, arp, slot] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        std::string error;

        if (safe->controller.exportPatternMidiFile (arp, slot, fc.getResult().getFullPathName().toStdString(), &error))
            safe->showNotice ("Exported " + fc.getResult().getFileName());
        else
            safe->showNotice (text::from (error));
    });
    fitDialog();
}

void mw12FEditor::importMidi()
{
    const auto arp = patternModel.isArp();
    const auto slot = patternModel.getSlot();
    chooser = std::make_unique<juce::FileChooser> ("Import MIDI file into " + slotName (slot),
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.mid;*.midi", false, false, this);
    juce::Component::SafePointer<mw12FEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, arp, slot] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        seq::MidiImportReport report;

        if (safe->controller.importMidiFile (fc.getResult().getFullPathName().toStdString(), arp, slot, &report))
        {
            if (report.usedEmbedded)
            {
                // A file this product wrote: the pattern came back exactly, name and all.
                const auto p = safe->controller.pattern (arp, slot);
                safe->showNotice ("Imported " + safe->patternFileName (arp, slot) + " (" + juce::String (countNotes (p)) + " notes, lossless)");
                return;
            }

            juce::String text = juce::String (report.notesRead) + " notes";
            if (report.notesDropped > 0) text << ", " << report.notesDropped << " dropped";
            if (report.suggestTriplet) text << ", looks triplet";
            if (report.octaveShift != 0) text << ", shifted " << report.octaveShift << " st";
            safe->showNotice ("Imported " + text);
        }
        else
        {
            safe->showNotice (report.error.empty() ? juce::String ("Import failed") : text::from (report.error));
        }
    });
    fitDialog();
}

void mw12FEditor::chooseMidiMapFile (bool save)
{
    chooser = std::make_unique<juce::FileChooser> (save ? "Save MIDI map" : "Load MIDI map", juce::File (controller.defaultMidiMapPath().string()),
                                                   "*.xml", false, false, this);
    juce::Component::SafePointer<mw12FEditor> safe (this);
    const auto mode = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                           : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (mode, [safe, save] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        std::string error;
        const auto path = fc.getResult().getFullPathName().toStdString();
        const auto ok = save ? safe->controller.saveMidiMapFile (path, &error) : safe->controller.loadMidiMapFile (path, &error);
        safe->showNotice (ok ? (save ? "Saved " : "Loaded ") + fc.getResult().getFileName() : text::from (error));
    });
    fitDialog();
}

juce::String mw12FEditor::patternFileName (bool arp, int slot) const
{
    const auto p = controller.pattern (arp, slot);
    const auto name = text::fromArray (p.name).trim();
    return name.isNotEmpty() ? name : (arp ? "arp " : "pattern ") + slotName (slot);
}

juce::File mw12FEditor::writeDragPatternFile (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= seq::kNumSlots)
        return {};

    // The same per-editor directory as the MIDI drag (writeDragMidiFile makes it).
    const auto arp = patternModel.isArp();
    const auto name = patternFileName (arp, slotIndex);

    PresetData data;
    data.type = arp ? PresetType::Arp : PresetType::Pattern;
    data.name = name.toStdString();
    data.hasPattern = true;
    data.pattern = controller.pattern (arp, slotIndex);
    const auto text = writePreset (data);

    if (! makeDragDirectory())
        return {};

    const auto file = dragDirectory.getChildFile (text::from (plugin::PresetManager::safeFileName (data.name)) + ".mw12f");

    if (! file.replaceWithData (text.data(), text.size()))
        return {};

    return file;
}

int mw12FEditor::loadPatternFiles (const juce::StringArray& paths, int slotIndex)
{
    const auto editArp = patternModel.isArp();
    auto slot = slotIndex >= 0 ? slotIndex : patternModel.getSlot();
    int count = 0;
    juce::String firstError, lastName;

    for (const auto& path : paths)
    {
        if (slot >= seq::kNumSlots)
            break;

        const juce::File file (path);
        const auto ext = file.getFileExtension().toLowerCase();

        if (! file.existsAsFile() || file.getSize() > kMaxDroppedFileBytes)
        {
            if (firstError.isEmpty())
                firstError = "Cannot read " + text::sanitise (file.getFileName());

            continue;
        }

        if (ext == ".mid" || ext == ".midi")
        {
            seq::MidiImportReport report;

            if (controller.importMidiFile (file.getFullPathName().toStdString(), editArp, slot, &report))
            {
                ++count;
                lastName = slotName (slot);
                ++slot;
            }
            else if (firstError.isEmpty())
            {
                firstError = report.error.empty() ? "Import failed" : text::from (report.error);
            }

            continue;
        }

        if (ext != ".mw12f")
            continue;

        juce::MemoryBlock bytes;

        if (! file.loadFileAsData (bytes))
            continue;

        PresetData data;
        const auto result = parsePreset (std::string_view (static_cast<const char*> (bytes.getData()), bytes.getSize()), data);

        if (! result.ok || ! data.hasPattern || (data.type != PresetType::Pattern && data.type != PresetType::Arp))
        {
            if (firstError.isEmpty())
                firstError = text::sanitise (file.getFileName()) + " is not a pattern file";

            continue;
        }

        // A pattern file goes into its own bank (an arpeggiator pattern addresses held notes).
        auto p = data.pattern;
        std::memset (p.name, 0, sizeof (p.name));
        text::from (data.name).copyToUTF8 (p.name, sizeof (p.name));
        controller.commitPattern (data.type == PresetType::Arp, slot, p, "Drop pattern");
        ++count;
        lastName = slotName (slot) + " " + text::from (data.name);
        ++slot;
    }

    if (count == 1)
        showNotice ("Loaded " + lastName);
    else if (count > 1)
        showNotice ("Loaded " + juce::String (count) + " patterns");
    else if (firstError.isNotEmpty())
        showNotice (firstError);

    return count;
}

bool mw12FEditor::makeDragDirectory()
{
    cleanDragDirectory();

    const char* runtime = std::getenv ("XDG_RUNTIME_DIR");
    const auto root = runtime != nullptr && runtime[0] != '\0' ? juce::File (text::from (runtime))
                                                                : juce::File::getSpecialLocation (juce::File::tempDirectory);
    dragDirectory = root.getChildFile ("mw12F").getChildFile (juce::String (static_cast<int> (::getpid()))
                                                              + "-" + juce::String (dragDirectoryCounter.fetch_add (1) + 1));

    if (! dragDirectory.createDirectory())
    {
        dragDirectory = juce::File();
        return false;
    }

    return true;
}

juce::File mw12FEditor::writeDragMidiFile()
{
    if (! makeDragDirectory())
        return {};

    const auto arp = patternModel.isArp();
    const auto slot = patternModel.getSlot();
    const auto name = patternFileName (arp, slot);
    const auto file = dragDirectory.getChildFile (text::from (plugin::PresetManager::safeFileName (name.toStdString())) + ".mid");

    if (! controller.exportPatternMidiFile (arp, slot, file.getFullPathName().toStdString()))
        return {};

    return file;
}

void mw12FEditor::fitDialog()
{
    for (auto* child : getChildren())
        if (dynamic_cast<juce::FileChooserDialogBox*> (child) != nullptr && ! getLocalBounds().contains (child->getBounds()))
            child->setBounds (juce::Rectangle<int> (juce::jmin (child->getWidth(), getWidth()), juce::jmin (child->getHeight(), getHeight()))
                                  .withCentre (getLocalBounds().getCentre()));
}

void mw12FEditor::cleanDragDirectory()
{
    if (dragDirectory != juce::File())
    {
        dragDirectory.deleteRecursively();
        dragDirectory = juce::File();
    }
}

void mw12FEditor::renamePattern()
{
    const auto& p = patternModel.getPattern();
    const auto current = text::fromArray (p.name);
    juce::Component::SafePointer<mw12FEditor> safe (this);
    plate.beginTextEntry (plate.sequencer.patternName, current, [safe] (const juce::String& text)
    {
        if (safe == nullptr)
            return;

        auto edited = safe->patternModel.getPattern();
        std::memset (edited.name, 0, sizeof (edited.name));
        text.trim().copyToUTF8 (edited.name, sizeof (edited.name));
        safe->controller.commitPattern (safe->patternModel.isArp(), safe->patternModel.getSlot(), edited, "Rename pattern");
    });
}

void mw12FEditor::showPatternNameMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&plate.getLookAndFeel());   // in the house style, inside the plate
    menu.addItem (1, "Rename pattern...");
    menu.addItem (2, juce::String ("Browse ") + (patternModel.isArp() ? "arpeggiator patterns..." : "patterns..."));
    juce::Component::SafePointer<mw12FEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&plate.sequencer.patternName), [safe] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
            safe->renamePattern();
        else if (result == 2)
            safe->openPresetBrowser (safe->patternModel.isArp() ? PresetType::Arp : PresetType::Pattern);
    });
}

//==============================================================================
/** The processor's createEditor() calls this through a weak reference (see PluginProcessor.cpp). */
extern "C" juce::AudioProcessorEditor* mw12FCreateEditor (mw12FProcessor& p)
{
    return new mw12FEditor (p);
}
