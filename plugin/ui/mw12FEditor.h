#pragma once
// mw12FEditor: the plugin's window. It owns an EditorPlate laid out in design units and
// scaled by the zoom transform, binds every control in the plate's ControlSet to the host
// parameters through polled attachments (editor/PolledAttachments.h: JUCE's behaviour, minus
// the message JUCE's attachments post from the audio thread when a host automates), drives
// the status displays from EngineStatus on one 30 Hz timer, and routes every edit through
// acid::plugin::Controller.
//
// Repaint budget: parameter changes repaint the control that moved (attachments), the timer
// repaints a meter, lamp or play cursor only when its state changed, and the plate caches its
// static artwork per panel and zoom (Faceplate). Nothing here grabs keyboard focus: typed
// entry goes through the Faceplate, and the preset browser's text fields take focus only when
// clicked.
//
// Every file dialog the editor opens (MIDI export and import, MIDI map save and load) is a
// FileChooser the editor owns, parented to the editor and reached through a SafePointer, so
// closing the window or removing the plugin takes the dialog with it.
#include "editor/EditorPlate.h"
#include "editor/ControllerPatternModel.h"
#include "editor/PresetBrowser.h"
#include "editor/LearnIndicator.h"
#include "editor/SettingsMenu.h"
#include "editor/PolledAttachments.h"
#include "PluginProcessor.h"

#include <memory>
#include <vector>

class mw12FEditor final : public juce::AudioProcessorEditor,
                          private acid::plugin::Controller::Listener,
                          private juce::Timer
{
public:
    explicit mw12FEditor (mw12FProcessor&);
    ~mw12FEditor() override;

    acid::ui::EditorPlate& getPlate() noexcept { return plate; }
    acid::ui::PresetBrowser& getPresetBrowser() noexcept { return browser; }
    acid::ui::ControllerPatternModel& getPatternModel() noexcept { return patternModel; }
    acid::plugin::Controller& getController() noexcept { return controller; }

    /** Window zoom, 0.75 to 2.0: resizes the window and stores the value as the instance's and
        the global default. */
    void setZoom (float zoom);
    /** The zoom the window shows: the largest scale at which the plate fits the window. */
    float getZoom() const noexcept;

    void setThemeId (acid::ui::ThemeId);
    void openPresetBrowser (acid::PresetType, bool saveMode = false);

    /** A short message in the plate's tooltip, cleared after a few seconds. */
    void showNotice (const juce::String&);

    /** Typed entry over the pattern name box. Reached from the box's right-click menu and from
        a double-click on it. */
    void renamePattern();
    /** The pattern name's right-click menu (rename, browse). */
    void showPatternNameMenu();

    /** What the cog menu is built with (public so tests can build the menu without showing it). */
    acid::ui::SettingsMenuHooks settingsMenuHooks();

    /** The MIDI map file dialogs (Settings > MIDI). The chooser is owned here. */
    void chooseMidiMapFile (bool save);

    /** Writes the edited pattern as a .mid for drag-out into this instance's own temp
        directory (unique per process and editor), kept until the next drag or destruction.
        Empty on failure. */
    juce::File writeDragMidiFile();

    /** Writes a slot (0..47 of the bank the play mode edits) as a pattern file (.mw12f of type
        pattern or arp) for drag-out, in the same directory. Empty on failure. */
    juce::File writeDragPatternFile (int slotIndex);

    /** Loads dropped pattern files into consecutive slots from `slotIndex` (-1: the edited
        slot): .mw12f pattern and arp files into their own bank, .mid files into the bank the
        play mode edits. Each file is one undo step. Returns the number loaded. */
    int loadPatternFiles (const juce::StringArray& paths, int slotIndex);

    /** MIDI learn: the persistent indicator, and the cancel paths (Escape, a click elsewhere,
        the indicator, the target's menu). */
    acid::ui::LearnIndicator& getLearnIndicator() noexcept { return learnIndicator; }
    void cancelMidiLearn();
    /** What a mouse press anywhere on the plate does to an armed learn (public for tests):
        a press on the target, or on the indicator, keeps it; any other press cancels it. */
    void handlePlateClick (juce::Component& clicked);

    bool keyPressed (const juce::KeyPress&) override;

    /** Sends the momentary 0 for every live override this editor is holding high. Called when
        the window closes and when the sequencer screen is hidden. */
    void releaseHeldOverrides();

    /** What the 30 Hz timer does: status displays, UI persistence, housekeeping. Public so a
        test harness without a message loop can drive it. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:

    void controllerChanged (std::uint32_t changed) override;
    void timerCallback() override { tick(); }

    acid::ui::ParamHooks hooksFor (acid::ParamId);
    void attachControls();
    void bindPanels();
    void watch (acid::ParamId, std::function<void (float)>);
    /** Runs a controller edit the user asked for through the editor, so the change
        notification that follows is known to be the editor's own (see controllerChanged). */
    void runUiAction (const std::function<void()>&);
    void slotChanged();
    void playModeChanged (acid::seq::PlayMode);
    void refreshAll();
    void refreshSettingsBoxes();
    void refreshPresetBoxes();
    void refreshPatternName();
    void refreshUsedSlots();
    void refreshUndo();
    void refreshLearnIndicator();
    void updateCoupleTips (bool sweepCoupled, bool accentVolumeCoupled);
    /** Pattern name for files and notices: the stored name, or "pattern A-01". */
    juce::String patternFileName (bool arp, int slot) const;
    /** Double-click values of the rows a preset type covers become their current values. */
    void applyDefaults (acid::PresetType);
    void restoreUiState();
    void persistUiState();
    void updateStatus();
    void checkPresetEdits();
    void exportMidi();
    void importMidi();
    /** Keeps a just-launched file dialog (a child of the editor) inside the window, which a
        host that ignores the size limits can make smaller than the dialog's default size. */
    void fitDialog();
    void cleanDragDirectory();
    /** A fresh drag-out directory for this editor (the previous drag's files go). */
    bool makeDragDirectory();
    acid::PresetType presetTypeForEffect (acid::FxSlot) const noexcept;
    juce::String presetBoxText (acid::PresetType) const;

    acid::plugin::Controller& controller;
    acid::ui::ControllerPatternModel patternModel;
    acid::ui::EditorPlate plate;
    acid::ui::PresetBrowser browser;
    acid::ui::LearnIndicator learnIndicator;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    /** Cancels an armed MIDI learn on a click anywhere on the plate but the target. */
    struct LearnClickWatcher;
    std::unique_ptr<LearnClickWatcher> learnClickWatcher;
    /** Brackets a drag on the tempo readout in one undo step. */
    struct TempoGesture;
    std::unique_ptr<TempoGesture> tempoGesture;

    std::vector<std::unique_ptr<acid::ui::PolledSliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<acid::ui::PolledButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<acid::ui::PolledComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<acid::ui::PolledParameterAttachment>> watches;
    std::unique_ptr<acid::ui::PolledParameterAttachment> modelAttachment;
    /** Applies parameter changes other threads made since the last tick. */
    void pollAttachments();
    std::unique_ptr<juce::FileChooser> chooser;

    /** A live override button and its attachment; `held` is true while the editor has sent
        the 1 and not yet the 0. */
    struct Momentary
    {
        acid::ui::PushButton* button = nullptr;
        acid::ui::PolledParameterAttachment* attachment = nullptr;
        std::shared_ptr<bool> held;
    };
    std::vector<Momentary> momentaries;

    /** What the type's preset box compares the live parameters against ("name *" when edited). */
    struct LoadedSnapshot
    {
        std::string id;
        acid::ParamValues values {};
    };
    std::array<LoadedSnapshot, acid::kNumPresetTypes> loaded;
    std::array<std::vector<acid::ParamId>, acid::kNumPresetTypes> typeRows;

    bool syncing = false, settingSize = false;
    int uiActions = 0;
    float pendingZoom = -1.0f;   // host-driven resize, persisted once it settles
    float knownZoom = 1.0f;      // the stored zoom as the editor last wrote or applied it
    int zoomSettleTicks = 0;
    std::uint32_t lastVersion = 0;
    int tickCount = 0, noticeTicks = 0;
    float holdLeft = 0.0f, holdRight = 0.0f;
    bool gateLit = false, accentLit = false, runLit = false, delayClampLit = false, internalClockShown = true;
    int playingBank = -1, playingSlot = -1, queuedBank = -1, queuedSlot = -1;
    juce::File dragDirectory;

    struct UiState
    {
        int view = -1, tools = -1, fxTab = -1;
        bool fxFull = false, coverOpen = false, coverAnim = true;
    } persisted;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (mw12FEditor)
};
