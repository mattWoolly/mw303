#pragma once
// Controller: everything the editor needs from the plugin, in one place, with no access to
// processor internals. The editor (mw12FEditor) holds a reference to it and nothing else.
//
// Threading: every function here is for the message thread, unless its comment says
// otherwise. engineStatus() and version() may be called from anywhere. Nothing here blocks on
// the audio thread. Edits are applied to the model, published to the audio thread at once, and
// reported through Listener::controllerChanged (on the message thread) and the version counter.
//
// Patterns are edited by value: read a copy with pattern(), change it, hand it back with
// commitPattern(). The editor's PatternEditModel implementation is expected to do exactly that
// (plugin/ui/seq/PatternEditModel.h). Every edit is undoable; wrap a mouse drag in
// beginGesture()/endGesture() so the drag is one undo step.
//
// Slots are 0..47 (acid::seq::slotIndex (bank, slotInBank)). `arp` selects the arpeggiator bank.
#include "Model.h"
#include "PresetManager.h"
#include "acid/engine/Engine.h"
#include "acid/engine/ParamTable.h"
#include "acid/engine/Settings.h"
#include "acid/seq/Generator.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/Pattern.h"
#include "acid/seq/Scales.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class mw12FProcessor;

namespace acid::plugin
{
/** What changed, for Listener::controllerChanged. Several bits may be set at once. */
enum ChangeFlags : std::uint32_t
{
    kChangedPatterns = 1u << 0,     // any pattern of either bank
    kChangedSettings = 1u << 1,     // Settings (menu items)
    kChangedParams = 1u << 2,       // many parameters at once (preset or state load; single moves go through the APVTS)
    kChangedPresets = 1u << 3,      // the preset library (a save, rename, delete, import, favourite) or the scene name
    kChangedMidiMap = 1u << 4,      // a controller mapping, or learn mode started or ended
    kChangedUi = 1u << 5,           // zoom, theme or another UI value
    kChangedUndo = 1u << 6,         // the undo or redo stack
    kChangedEverything = 0xFFFFFFFFu
};

class Controller
{
public:
    explicit Controller (mw12FProcessor&);
    ~Controller();
    Controller (const Controller&) = delete;
    Controller& operator= (const Controller&) = delete;

    // ---- Change notification ----------------------------------------------------------------
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /** Message thread. flags: a ChangeFlags mask. */
        virtual void controllerChanged (std::uint32_t flags) = 0;
    };
    void addListener (Listener*);
    void removeListener (Listener*);

    /** Any thread. Bumped on every change (also those made from state loads on other threads),
        so a 30 Hz editor timer can compare it with the last value it saw. */
    std::uint32_t version() const noexcept;

    // ---- Parameters -------------------------------------------------------------------------
    /** The host parameters, for SliderAttachment, ButtonAttachment and ComboBoxAttachment by
        string id (acid::paramInfo (id).id). Every parameter is a RangedAudioParameter whose
        range is the table's natural-unit range (log mapped for Log rows). */
    juce::AudioProcessorValueTreeState& apvts() noexcept;
    juce::RangedAudioParameter& parameter (ParamId) noexcept;

    /** Natural units, as in ParamTable.h. */
    float getParam (ParamId) const noexcept;
    /** Sets a parameter in natural units and tells the host (not undoable; hosts own automation). */
    void setParam (ParamId, float natural);
    ParamValues paramValues() const noexcept;

    /** The slot the Performance parameters select (pattern_bank, pattern_slot): 0..47. */
    int selectedSlot() const noexcept;
    /** Sets pattern_bank and pattern_slot from a 0..47 slot. */
    void selectSlot (int slot);
    seq::PlayMode playMode() const noexcept;
    void setPlayMode (seq::PlayMode);

    // ---- Patterns ---------------------------------------------------------------------------
    seq::Pattern pattern (bool arp, int slot) const;
    /** Replaces a slot (sanitised), undoable, published. */
    void commitPattern (bool arp, int slot, const seq::Pattern&, std::string_view undoLabel = "Edit pattern");
    void beginGesture();
    void endGesture();

    // Pattern operations (each is one undo step; they call the acid::seq::PatternOps functions).
    void shiftPattern (bool arp, int slot, int steps);              // positive = right
    void transposePattern (bool arp, int slot, int semitones);
    void reversePattern (bool arp, int slot);
    void clearPattern (bool arp, int slot);                         // every step to a default Step
    void mutePattern (bool arp, int slot);                          // rests, pitches kept
    void setPatternLength (bool arp, int slot, int length);
    void duplicatePattern (bool arp, int slot);                     // first half into the second
    void invertArpPattern (int slot, int n);

    /** Whole-pattern clipboard, as PatternCodec text (lossless) through the system clipboard,
        so a pattern can be pasted into another instance. */
    void copyPattern (bool arp, int slot);
    bool pastePattern (bool arp, int slot);                         // false when the clipboard holds no pattern
    bool clipboardHasPattern() const;
    std::string patternAsText (bool arp, int slot) const;           // encodePattern
    bool patternFromText (bool arp, int slot, std::string_view text, std::string* error = nullptr);  // encoded or compact step text
    /** Copies a whole slot to another (either bank). */
    void copySlot (bool fromArp, int fromSlot, bool toArp, int toSlot);

    // Hand edits on a generated pattern: every edit above (commitPattern and the operations)
    // calls acid::seq::keepEdits, with the density amounts the pattern was last derived with,
    // so the next density move does not write the generator's line back over the edit.

    // ---- Note preview -----------------------------------------------------------------------
    /** Sounds a note through the voice (the keyboard view's PREVIEW), lock-free into the
        processor's MIDI input. It sounds only while the sequencer is stopped; a note-off is
        always delivered. Starting a note while another preview note is down ends that one. */
    void previewNote (int midiNote, bool accent, bool on);
    /** Ends every preview note. Call it when the editor closes. */
    void stopPreview();

    // ---- Generator (acid::seq::Generator, Scales) -------------------------------------------
    /** Draws a new line with the slot's GenParams (seed stored in the pattern). */
    void generate (bool arp, int slot, std::uint64_t seed);
    /** Re-derives states and flags from the stored memory and the amounts: call after changing
        a density amount through setGenParams. */
    void applyDensities (bool arp, int slot);
    seq::GenParams genParams (bool arp, int slot) const;
    /** Replaces the generator fields and re-applies the densities (undoable). */
    void setGenParams (bool arp, int slot, const seq::GenParams&);
    /** Template library: acid::seq::scaleTemplates(). */
    std::span<const seq::ScaleTemplate> scaleTemplates() const noexcept;
    void applyScaleTemplate (bool arp, int slot, int templateIndex);
    int matchingScaleTemplate (bool arp, int slot) const;
    /** Commits the generated line and drops the generator memory. */
    void flattenPattern (bool arp, int slot);

    // ---- Settings ---------------------------------------------------------------------------
    Settings settings() const;
    /** Replaces the settings (undoable) and publishes. */
    void setSettings (const Settings&, std::string_view undoLabel = "Settings");

    // ---- Undo ---------------------------------------------------------------------------------
    bool canUndo() const;
    bool canRedo() const;
    std::string undoLabel() const;
    std::string redoLabel() const;
    void undo();
    void redo();

    // ---- Engine status ----------------------------------------------------------------------
    /** Any thread. Poll it from the editor's 30 Hz timer. */
    EngineStatus engineStatus() const noexcept;

    // ---- Presets ----------------------------------------------------------------------------
    PresetManager& presets() noexcept;
    /** Loads one, applying only what the file carries (SPEC 6.7). false with a message on error. */
    bool loadPreset (const PresetEntry&, std::string* error = nullptr);
    /** Saves the live model as a user preset of that type (name, author, tags comma separated). */
    const PresetEntry* savePreset (PresetType, std::string_view name, std::string_view author, std::string_view tags,
                                   std::string_view description, bool overwrite, std::string* error = nullptr);
    /** The current scene's name (the last scene or state loaded, "Init" after initScene). */
    std::string sceneName() const;
    void setSceneName (std::string_view);
    /** Everything back to the factory default scene (or the table defaults). */
    void initScene();

    /** Audition: load presets freely, then keep or revert. Reverting puts back only what the
        audition's loads changed: a parameter the host automated since, a setting or pattern
        edited since, are left as they are. A session state the host loads meanwhile ends the
        audition (there is nothing left to revert to). */
    void beginAudition();
    bool auditioning() const noexcept { return auditionSnapshot.has_value(); }
    void endAudition (bool keep);

    /** Default state (checklist 118). saveDefaultState writes the session as it is now to the
        user config directory (userDefaultStatePath()); a new instance and initScene() then
        start from it instead of the factory default. resetDefaultState deletes it: back to
        the factory default. */
    bool saveDefaultState (std::string* error = nullptr);
    bool resetDefaultState (std::string* error = nullptr);
    bool hasUserDefaultState() const;
    std::filesystem::path userDefaultStatePath() const;

    /** Previous and next inside a filtered list, relative to the last preset loaded of that
        type. Returns the entry loaded, or nullptr when the list is empty. */
    const PresetEntry* loadNextPreset (const PresetFilter&, int direction);
    /** The last preset loaded of that type (its id), or nullptr. */
    const PresetEntry* lastLoaded (PresetType) const;

    /** A .mw12f dropped from outside: imported into the user library of its own type. */
    const PresetEntry* importPresetFile (const std::filesystem::path&, std::string* error = nullptr);
    bool exportPreset (const PresetEntry&, const std::filesystem::path& destination, std::string* error = nullptr);

    // ---- MIDI learn -------------------------------------------------------------------------
    /** Right-click Learn: the next controller moved is bound to the parameter. */
    void startMidiLearn (ParamId);
    void cancelMidiLearn();
    bool midiLearning() const noexcept;
    ParamId midiLearnTarget() const noexcept;
    /** Controller number bound to a parameter, or -1. */
    int midiControllerFor (ParamId) const;
    /** Parameter bound to a controller number, or -1. */
    int paramForMidiController (int cc) const;
    void setMidiMapping (int cc, ParamId);            // explicit binding; 120..127 (channel mode) are refused
    void clearMidiMapping (ParamId);
    void clearAllMidiMappings();
    /** The map as a file, besides living in the state. Default path: the global settings dir. */
    bool saveMidiMapFile (const std::filesystem::path&, std::string* error = nullptr) const;
    bool loadMidiMapFile (const std::filesystem::path&, std::string* error = nullptr);
    std::filesystem::path defaultMidiMapPath() const;

    // ---- MIDI files -------------------------------------------------------------------------
    /** The pattern as a type-0 Standard MIDI File with the configured velocities and the
        slide-into-rest policy. For an arpeggiator pattern, heldNotes are the keys to play it
        with (low to high; empty = a C minor triad) and cycles the number of passes. */
    std::vector<std::uint8_t> patternMidiFile (bool arp, int slot, std::span<const int> heldNotes = {}, int cycles = 1) const;
    bool exportPatternMidiFile (bool arp, int slot, const std::filesystem::path&, std::string* error = nullptr) const;
    /** Writes a temp .mid for drag-out and keeps it until the next call or destruction
        (hosts read it after the drop callback). The directory is this instance's own, so two
        instances never write or delete each other's file. Empty path on failure. */
    std::filesystem::path writeDragMidiFile (bool arp, int slot);
    /** Imports a .mid into a slot (undoable). The report says how it was read. */
    bool importMidiFile (const std::filesystem::path&, bool arp, int slot, seq::MidiImportReport* report = nullptr);
    bool importMidiData (std::span<const std::uint8_t>, bool arp, int slot, seq::MidiImportReport* report = nullptr);

    // ---- UI persistence ---------------------------------------------------------------------
    // Per instance (saved in the session state as <ui .../>), plus a global defaults file
    // ($XDG_CONFIG_HOME/mwAudio/mw12F/ui.xml) that a new instance starts from.
    float zoom() const;                           // 0.75 .. 2.0
    void setZoom (float, bool alsoGlobalDefault = true);
    std::string theme() const;                    // "night", "silver", ... whatever the editor defines
    void setTheme (std::string_view, bool alsoGlobalDefault = true);
    /** Any other per-instance UI value (view, tab, cover state...). */
    std::string uiValue (std::string_view key, std::string_view fallback = {}) const;
    void setUiValue (std::string_view key, std::string_view value);
    std::string globalUiValue (std::string_view key, std::string_view fallback = {}) const;
    void setGlobalUiValue (std::string_view key, std::string_view value);

    // ---- Housekeeping -----------------------------------------------------------------------
    /** Runs the processor's message-thread work now (normally the 30 Hz timer does). */
    void pump();

private:
    friend class ::mw12FProcessor;

    /** handEdit: the edit writes steps directly (not a generator stage), so the generator's
        memory is reconciled with it afterwards (seq::keepEdits). */
    void editPattern (bool arp, int slot, std::string_view label, const std::function<void (seq::Pattern&)>& edit,
                      bool handEdit = true);
    void notify (std::uint32_t flags);
    void notifyLater (std::uint32_t flags);
    /** Applies one side of an entry and publishes; returns the ChangeFlags for it. Model::mutex held. */
    std::uint32_t applyUndoEntry (const UndoEntry&, bool useBefore);
    std::filesystem::path globalUiFile() const;
    void loadGlobalUi();
    bool saveGlobalUi() const;
    void applyGlobalUiDefaults();
    void cleanDragFile();

    /** Notes what an audition load changed (loadPreset while auditioning). */
    void recordAuditionLoad (const PresetData&);

    /** What the audition's loads changed, and what they left, so a cancel reverts exactly that. */
    struct AuditionTrack
    {
        std::array<bool, kNumParams> params {};      // a load wrote this parameter
        ParamValues paramsLeft {};                   // ...and left this value
        std::uint32_t settingsMask = 0;
        Settings settingsLeft {};
        bool wholeBank = false;                      // a scene load replaced the bank
        std::vector<std::pair<bool, int>> slots;     // (arp, slot) a pattern, arp or template load wrote
        std::unique_ptr<seq::PatternBank> bankLeft;  // the bank as the last load left it
        bool scene = false;                          // a scene load changed the scene name and info
        std::uint32_t stateLoads = 0;                // processor state loads seen at begin
    };

    mw12FProcessor& processor;
    juce::ListenerList<Listener> listeners;
    std::optional<PresetData> auditionSnapshot;
    AuditionTrack audition;
    std::array<std::string, kNumPresetTypes> lastLoadedId;
    xml::Element globalUi;
    bool globalUiLoaded = false;
    std::filesystem::path dragFile;
    std::filesystem::path dragDir;               // this instance's directory for drag-out files
    std::array<bool, 128> previewDown {};        // preview keys this Controller has sent down
    std::atomic<std::uint32_t> pendingFlags { 0 };
};
}
