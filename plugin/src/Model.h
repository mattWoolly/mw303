#pragma once
// The editable model: pattern bank, settings, per-instance UI state, MIDI map, undo history
// and whatever unknown content the last session state carried. Owned by the processor,
// edited on the message thread, read and replaced by state save and load on any non-audio
// thread. Every member below the mutex is guarded by it; the audio thread never takes the
// mutex and never reads the model, it reads the snapshots the processor publishes to the
// Engine (publishBank, publishSettings) after each change.
#include "acid/engine/Settings.h"
#include "acid/engine/StateCodec.h"
#include "acid/seq/Pattern.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace acid::plugin
{
/** CC number to parameter map. -1 = no parameter on that controller. */
struct MidiMap
{
    static constexpr int kNumControllers = 128;
    /** CC 120..127 are channel-mode messages (All Sound Off, All Notes Off, ...), which hosts
        send on stop. They can never be bound: setMidiMapping, fromXml and learn refuse them. */
    static constexpr int kFirstChannelMode = 120;
    static constexpr bool bindable (int cc) noexcept { return cc >= 0 && cc < kFirstChannelMode; }
    std::array<std::int16_t, kNumControllers> param;

    MidiMap() noexcept { param.fill (-1); }

    /** First controller mapped to that parameter, or -1. */
    int controllerFor (int paramIndex) const noexcept;
    void clearParam (int paramIndex) noexcept;
    void clear() noexcept { param.fill (-1); }
    bool empty() const noexcept;

    /** <midi_map><cc number="74" param="cutoff"/>...</midi_map>, controllers in ascending order. */
    xml::Element toXml() const;
    /** Replaces the map with the element's content. Unknown parameters and bad numbers are skipped. */
    void fromXml (const xml::Element&) noexcept;
};

/** Which voicing the Distortion Voicing parameters last followed, and what it wrote.

    The user's own voicing (dist_model 0, Custom) is kept apart in `custom`: stepping to a
    factory model writes that model's voicing over the rows, and selecting Custom again writes
    the saved one back. It is saved in the session state (<dist_custom>) while a factory model
    is selected; with Custom selected the rows themselves are the custom voicing. */
struct DistortionTracker
{
    int appliedModel = -1;                   // dist_model value the voicing values were last written for; -1 = unknown
    std::array<float, kNumParams> written {};   // the values written then (only the voicing rows are read)
    bool hasCustom = false;
    std::array<float, kNumParams> custom {};    // the custom voicing (only the voicing rows are used)
};

struct UndoEntry
{
    /** Load: a preset load, one step holding the settings and (when it changed) the whole
        bank before and after. The bank copies live on the heap and are shared with the redo
        list, so an entry stays small while it is not a load. */
    enum class Kind : std::uint8_t { Pattern, Settings, Load };
    Kind kind = Kind::Pattern;
    bool arp = false;
    int slot = 0;
    seq::Pattern before {}, after {};
    Settings settingsBefore {}, settingsAfter {};
    std::shared_ptr<const seq::PatternBank> bankBefore, bankAfter;   // Load only; null when the bank did not change
    std::string label;
};

class Model
{
public:
    static constexpr std::size_t kUndoDepth = 200;
    /** Load entries that carry bank copies (two per entry, about 110 KB each). Past this the
        oldest entries are dropped, as kUndoDepth drops them. */
    static constexpr std::size_t kBankUndoDepth = 16;

    Model();

    /** Taken by non-audio threads around every read and write of the members below. */
    mutable std::mutex mutex;

    std::unique_ptr<seq::PatternBank> bank;   // heap: 96 patterns is a large object
    Settings settings {};
    xml::Element ui;                          // <ui zoom="1" theme="night" .../>, free-form attributes
    MidiMap midiMap;
    DistortionTracker distortion;

    std::string sceneName;                    // the loaded scene preset's name ("Init" when none)
    std::string sceneAuthor;
    std::string sceneTags;
    std::string sceneDescription;
    int currentProgram = 0;

    /** Unknown content of the last `state` document, written back on save (SPEC 6.7). Only the
        extra* members are used; ui and midi_map are regenerated from the model. */
    PresetData carried;

    // ---- Undo (message thread) ----
    void pushUndo (UndoEntry);               // clears the redo list; coalesces inside a gesture
    bool canUndo() const noexcept { return ! undoList.empty(); }
    bool canRedo() const noexcept { return ! redoList.empty(); }
    const UndoEntry* undoTop() const noexcept { return undoList.empty() ? nullptr : &undoList.back(); }
    const UndoEntry* redoTop() const noexcept { return redoList.empty() ? nullptr : &redoList.back(); }
    /** Moves the top undo entry to the redo list and returns it (apply its `before`). */
    bool popUndo (UndoEntry& out);
    bool popRedo (UndoEntry& out);
    void clearHistory();
    void beginGesture() noexcept { ++gestureDepth; }
    void endGesture() noexcept { if (gestureDepth > 0) --gestureDepth; if (gestureDepth == 0) gestureOpen = false; }
    bool inGesture() const noexcept { return gestureDepth > 0; }

    /** Bumped on every change the editor could care about. Any thread may read it. */
    std::atomic<std::uint32_t> version { 0 };

private:
    std::deque<UndoEntry> undoList, redoList;
    int gestureDepth = 0;
    bool gestureOpen = false;                // the top undo entry belongs to the current gesture
};

/** The ParamIds of the Distortion Voicing group, in table order. */
std::span<const ParamId> distortionVoicingParams() noexcept;
}
