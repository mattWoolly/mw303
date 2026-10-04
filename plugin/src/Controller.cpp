#include "Controller.h"

#include "PluginProcessor.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include "acid/seq/Player.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace acid::plugin
{
namespace fs = std::filesystem;

namespace
{
bool onMessageThread()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm != nullptr && mm->isThisTheMessageThread();
}

bool validSlot (int slot) noexcept { return slot >= 0 && slot < seq::kNumSlots; }

bool readWholeFile (const fs::path& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeWholeFile (const fs::path& path, std::string_view text)
{
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories (path.parent_path(), ec);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    return static_cast<bool> (f) && static_cast<bool> (f.write (text.data(), static_cast<std::streamsize> (text.size())));
}

std::string floatToText (float v)
{
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof (buf), v);
    return std::string (buf, r.ptr);
}

float textToFloat (std::string_view text, float fallback)
{
    float v = 0.0f;
    const auto r = std::from_chars (text.data(), text.data() + text.size(), v);
    return r.ec == std::errc {} && r.ptr == text.data() + text.size() && std::isfinite (v) ? v : fallback;
}

bool sameDerivation (const seq::GenParams& a, const seq::GenParams& b) noexcept
{
    return a.useSlide == b.useSlide && a.useAccent == b.useAccent && a.useVibrato == b.useVibrato
        && a.amountNote == b.amountNote && a.amountSlide == b.amountSlide && a.amountAccent == b.amountAccent
        && a.amountVibrato == b.amountVibrato;
}

/** Generator.h, "Hand edits": after steps were written directly, release the generator's
    memory wherever the pattern no longer is what applyDensities would make of it, judged with
    the amounts it was last derived with (lastDerived). If the edit also brought new amounts,
    they are applied afterwards, to the lanes that still follow the knobs. */
void keepHandEdits (const seq::GenParams& lastDerived, seq::Pattern& p) noexcept
{
    const seq::GenParams wanted = p.gen;
    p.gen.useSlide = lastDerived.useSlide;
    p.gen.useAccent = lastDerived.useAccent;
    p.gen.useVibrato = lastDerived.useVibrato;
    p.gen.amountNote = lastDerived.amountNote;
    p.gen.amountSlide = lastDerived.amountSlide;
    p.gen.amountAccent = lastDerived.amountAccent;
    p.gen.amountVibrato = lastDerived.amountVibrato;
    seq::keepEdits (p);
    p.gen = wanted;
    if (! sameDerivation (lastDerived, wanted))
        seq::applyDensities (p);
}

/** Quarter notes per step of a pattern's division. */
double quartersPerStep (const seq::Pattern& p) noexcept
{
    double q = 0.25;
    switch (p.div)
    {
        case seq::StepDiv::d1_64: q = 1.0 / 16.0; break;
        case seq::StepDiv::d1_32: q = 1.0 / 8.0; break;
        case seq::StepDiv::d1_16: q = 1.0 / 4.0; break;
        case seq::StepDiv::d1_8: q = 1.0 / 2.0; break;
        case seq::StepDiv::d1_4: q = 1.0; break;
        case seq::StepDiv::d1_2: q = 2.0; break;
        case seq::StepDiv::d1_1: q = 4.0; break;
    }
    if (p.divMod == seq::DivMod::Dotted) q *= 1.5;
    else if (p.divMod == seq::DivMod::Triplet) q *= 2.0 / 3.0;
    return q;
}
}

//==============================================================================
Controller::Controller (mw12FProcessor& p)
    : processor (p)
{
    globalUi.name = "ui";
}

Controller::~Controller()
{
    stopPreview();
    cleanDragFile();
    if (! dragDir.empty())
    {
        std::error_code ec;
        fs::remove (dragDir, ec);    // only when empty: another file in it is not ours to delete
    }
}

void Controller::addListener (Listener* l) { listeners.add (l); }
void Controller::removeListener (Listener* l) { listeners.remove (l); }

std::uint32_t Controller::version() const noexcept
{
    return processor.modelData.version.load (std::memory_order_relaxed);
}

void Controller::notify (std::uint32_t flags)
{
    if (flags == 0) return;
    if (! onMessageThread())
    {
        notifyLater (flags);
        return;
    }
    listeners.call ([flags] (Listener& l) { l.controllerChanged (flags); });
}

void Controller::notifyLater (std::uint32_t flags)
{
    pendingFlags.fetch_or (flags, std::memory_order_relaxed);
}

void Controller::pump()
{
    const std::uint32_t flags = pendingFlags.exchange (0, std::memory_order_relaxed);
    if (flags != 0)
        listeners.call ([flags] (Listener& l) { l.controllerChanged (flags); });
}

//==============================================================================
juce::AudioProcessorValueTreeState& Controller::apvts() noexcept { return processor.apvts; }

juce::RangedAudioParameter& Controller::parameter (ParamId id) noexcept
{
    return *processor.params.byId[static_cast<std::size_t> (id)];
}

float Controller::getParam (ParamId id) const noexcept
{
    return processor.params.byId[static_cast<std::size_t> (id)]->natural();
}

void Controller::setParam (ParamId id, float natural)
{
    processor.params.byId[static_cast<std::size_t> (id)]->setNatural (natural);
}

ParamValues Controller::paramValues() const noexcept
{
    ParamValues v;
    processor.params.read (v);
    return v;
}

int Controller::selectedSlot() const noexcept
{
    const int bank = std::clamp (static_cast<int> (std::lround (getParam (kPatternBank))), 0, seq::kNumBanks - 1);
    const int slot = std::clamp (static_cast<int> (std::lround (getParam (kPatternSlot))) - 1, 0, seq::kSlotsPerBank - 1);
    return seq::slotIndex (bank, slot);
}

void Controller::selectSlot (int slot)
{
    if (! validSlot (slot)) return;
    setParam (kPatternBank, static_cast<float> (slot / seq::kSlotsPerBank));
    setParam (kPatternSlot, static_cast<float> (slot % seq::kSlotsPerBank + 1));
}

seq::PlayMode Controller::playMode() const noexcept
{
    return static_cast<seq::PlayMode> (std::clamp (static_cast<int> (std::lround (getParam (kPlayMode))), 0, 2));
}

void Controller::setPlayMode (seq::PlayMode mode)
{
    setParam (kPlayMode, static_cast<float> (static_cast<int> (mode)));
}

//==============================================================================
seq::Pattern Controller::pattern (bool arp, int slot) const
{
    if (! validSlot (slot)) return {};
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    const auto& bank = *processor.modelData.bank;
    return (arp ? bank.arp : bank.seq)[static_cast<std::size_t> (slot)];
}

void Controller::editPattern (bool arp, int slot, std::string_view label, const std::function<void (seq::Pattern&)>& edit,
                              bool handEdit)
{
    if (! validSlot (slot)) return;
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        seq::Pattern& target = (arp ? m.bank->arp : m.bank->seq)[static_cast<std::size_t> (slot)];
        const seq::Pattern before = target;
        seq::Pattern after = before;
        edit (after);
        seq::sanitizePattern (after);
        if (handEdit)
            keepHandEdits (before.gen, after);
        if (seq::samePattern (before, after))
            return;
        target = after;
        UndoEntry entry;
        entry.kind = UndoEntry::Kind::Pattern;
        entry.arp = arp;
        entry.slot = slot;
        entry.before = before;
        entry.after = after;
        entry.label = std::string (label);
        m.pushUndo (std::move (entry));
        processor.publishModelLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedPatterns | kChangedUndo);
}

void Controller::commitPattern (bool arp, int slot, const seq::Pattern& p, std::string_view undoLabel)
{
    editPattern (arp, slot, undoLabel, [&] (seq::Pattern& target) { target = p; });
}

void Controller::beginGesture()
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    processor.modelData.beginGesture();
}

void Controller::endGesture()
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    processor.modelData.endGesture();
}

void Controller::shiftPattern (bool arp, int slot, int steps)
{
    editPattern (arp, slot, steps >= 0 ? "Shift right" : "Shift left", [steps] (seq::Pattern& p) { seq::rotate (p, steps); });
}

void Controller::transposePattern (bool arp, int slot, int semitones)
{
    editPattern (arp, slot, "Transpose", [semitones] (seq::Pattern& p) { seq::transpose (p, semitones); });
}

void Controller::reversePattern (bool arp, int slot)
{
    editPattern (arp, slot, "Reverse", [] (seq::Pattern& p) { seq::reverse (p); });
}

void Controller::clearPattern (bool arp, int slot)
{
    editPattern (arp, slot, "Clear", [] (seq::Pattern& p) { seq::clearSteps (p); });
}

void Controller::mutePattern (bool arp, int slot)
{
    editPattern (arp, slot, "Mute steps", [] (seq::Pattern& p) { seq::muteSteps (p); });
}

void Controller::setPatternLength (bool arp, int slot, int length)
{
    editPattern (arp, slot, "Length", [length] (seq::Pattern& p) { seq::setLength (p, length); });
}

void Controller::duplicatePattern (bool arp, int slot)
{
    editPattern (arp, slot, "Duplicate", [] (seq::Pattern& p) { seq::duplicate (p); });
}

void Controller::invertArpPattern (int slot, int n)
{
    editPattern (true, slot, "Invert", [n] (seq::Pattern& p) { seq::invertArp (p, n); });
}

std::string Controller::patternAsText (bool arp, int slot) const
{
    return seq::encodePattern (pattern (arp, slot));
}

bool Controller::patternFromText (bool arp, int slot, std::string_view text, std::string* error)
{
    seq::Pattern p = pattern (arp, slot);
    if (seq::looksLikeEncodedPattern (text))
    {
        const seq::DecodeResult r = seq::decodePattern (text, p);
        if (! r.ok)
        {
            if (error != nullptr) *error = r.error;
            return false;
        }
    }
    else if (! seq::fromStepText (text, p, error))
    {
        return false;
    }
    commitPattern (arp, slot, p, "Paste");
    return true;
}

void Controller::copyPattern (bool arp, int slot)
{
    juce::SystemClipboard::copyTextToClipboard (juce::String (patternAsText (arp, slot)));
}

bool Controller::clipboardHasPattern() const
{
    const std::string text = juce::SystemClipboard::getTextFromClipboard().toStdString();
    return seq::looksLikeEncodedPattern (text);
}

bool Controller::pastePattern (bool arp, int slot)
{
    const std::string text = juce::SystemClipboard::getTextFromClipboard().toStdString();
    if (! seq::looksLikeEncodedPattern (text)) return false;
    return patternFromText (arp, slot, text);
}

void Controller::copySlot (bool fromArp, int fromSlot, bool toArp, int toSlot)
{
    if (! validSlot (fromSlot) || ! validSlot (toSlot)) return;
    const seq::Pattern source = pattern (fromArp, fromSlot);
    commitPattern (toArp, toSlot, source, "Copy pattern");
}

//==============================================================================
void Controller::previewNote (int midiNote, bool accent, bool on)
{
    if (midiNote < 0 || midiNote > 127) return;
    const auto key = static_cast<std::size_t> (midiNote);

    // External mode accents a velocity above the threshold: the plain note sits at it.
    const int threshold = settings().accentThreshold;
    const auto velocity = static_cast<std::uint8_t> (accent ? 127 : std::clamp (threshold, 1, 127));

    const auto send = [this] (std::uint8_t status, int note, std::uint8_t value) {
        seq::MidiMsg m;
        m.offset = 0;
        m.size = 3;
        m.data[0] = status;
        m.data[1] = static_cast<std::uint8_t> (note);
        m.data[2] = value;
        return processor.previewQueue.push (m);
    };

    if (on)
    {
        // One preview note at a time: the one that was down ends first.
        for (int n = 0; n < 128; ++n)
            if (previewDown[static_cast<std::size_t> (n)] && send (0x80, n, 0))
                previewDown[static_cast<std::size_t> (n)] = false;
        if (send (0x90, midiNote, velocity))
            previewDown[key] = true;
    }
    else if (previewDown[key] && send (0x80, midiNote, 0))
    {
        previewDown[key] = false;
    }
}

void Controller::stopPreview()
{
    for (int n = 0; n < 128; ++n)
        if (previewDown[static_cast<std::size_t> (n)])
            previewNote (n, false, false);
}

//==============================================================================
void Controller::generate (bool arp, int slot, std::uint64_t seed)
{
    editPattern (arp, slot, "Generate", [seed] (seq::Pattern& p) { seq::regenerate (p, seed); }, false);
}

void Controller::applyDensities (bool arp, int slot)
{
    editPattern (arp, slot, "Density", [] (seq::Pattern& p) { seq::applyDensities (p); }, false);
}

seq::GenParams Controller::genParams (bool arp, int slot) const
{
    return pattern (arp, slot).gen;
}

void Controller::setGenParams (bool arp, int slot, const seq::GenParams& gen)
{
    editPattern (arp, slot, "Generator", [&gen] (seq::Pattern& p) {
        p.gen = gen;
        seq::applyDensities (p);
    }, false);
}

std::span<const seq::ScaleTemplate> Controller::scaleTemplates() const noexcept
{
    return seq::scaleTemplates();
}

void Controller::applyScaleTemplate (bool arp, int slot, int templateIndex)
{
    const seq::ScaleTemplate* t = seq::scaleTemplate (templateIndex);
    if (t == nullptr) return;
    editPattern (arp, slot, "Template", [t] (seq::Pattern& p) { seq::applyScaleTemplate (*t, p.gen); }, false);
}

int Controller::matchingScaleTemplate (bool arp, int slot) const
{
    return seq::matchScaleTemplate (pattern (arp, slot).gen);
}

void Controller::flattenPattern (bool arp, int slot)
{
    editPattern (arp, slot, "Flatten", [] (seq::Pattern& p) { seq::flatten (p); }, false);
}

//==============================================================================
Settings Controller::settings() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.settings;
}

void Controller::setSettings (const Settings& s, std::string_view undoLabel)
{
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        if (std::memcmp (&m.settings, &s, sizeof (Settings)) == 0)
            return;
        UndoEntry entry;
        entry.kind = UndoEntry::Kind::Settings;
        entry.settingsBefore = m.settings;
        entry.settingsAfter = s;
        entry.label = std::string (undoLabel);
        m.settings = s;
        m.pushUndo (std::move (entry));
        processor.publishModelLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedSettings | kChangedUndo);
}

//==============================================================================
bool Controller::canUndo() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.canUndo();
}

bool Controller::canRedo() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.canRedo();
}

std::string Controller::undoLabel() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    const UndoEntry* e = processor.modelData.undoTop();
    return e != nullptr ? e->label : std::string();
}

std::string Controller::redoLabel() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    const UndoEntry* e = processor.modelData.redoTop();
    return e != nullptr ? e->label : std::string();
}

std::uint32_t Controller::applyUndoEntry (const UndoEntry& e, bool useBefore)
{
    Model& m = processor.modelData;
    std::uint32_t flags = kChangedUndo;
    switch (e.kind)
    {
        case UndoEntry::Kind::Pattern:
            if (validSlot (e.slot))
                (e.arp ? m.bank->arp : m.bank->seq)[static_cast<std::size_t> (e.slot)] = useBefore ? e.before : e.after;
            flags |= kChangedPatterns;
            break;

        case UndoEntry::Kind::Settings:
            m.settings = useBefore ? e.settingsBefore : e.settingsAfter;
            flags |= kChangedSettings;
            break;

        case UndoEntry::Kind::Load:
            m.settings = useBefore ? e.settingsBefore : e.settingsAfter;
            flags |= kChangedSettings;
            if (const auto& bank = useBefore ? e.bankBefore : e.bankAfter; bank != nullptr)
            {
                *m.bank = *bank;
                flags |= kChangedPatterns;
            }
            break;
    }
    processor.publishModelLocked();
    m.version.fetch_add (1, std::memory_order_relaxed);
    return flags;
}

void Controller::undo()
{
    std::uint32_t flags = 0;
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        UndoEntry e;
        if (! m.popUndo (e)) return;
        flags = applyUndoEntry (e, true);
    }
    notify (flags);
}

void Controller::redo()
{
    std::uint32_t flags = 0;
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        UndoEntry e;
        if (! m.popRedo (e)) return;
        flags = applyUndoEntry (e, false);
    }
    notify (flags);
}

//==============================================================================
EngineStatus Controller::engineStatus() const noexcept
{
    return processor.eng.status();
}

//==============================================================================
PresetManager& Controller::presets() noexcept
{
    if (! processor.presets.scanned())
        processor.presets.rescan();
    return processor.presets;
}

bool Controller::loadPreset (const PresetEntry& entry, std::string* error)
{
    PresetData d;
    std::string err;
    if (! presets().load (entry, d, err))
    {
        if (error != nullptr) *error = err;
        return false;
    }
    if (d.type == PresetType::State)
    {
        if (error != nullptr) *error = "a session state is not a preset";
        return false;
    }
    processor.applyPresetData (d, false);
    if (auditionSnapshot.has_value())
        recordAuditionLoad (d);
    lastLoadedId[static_cast<std::size_t> (d.type)] = entry.id;
    pump();
    notify (kChangedPresets);
    return true;
}

const PresetEntry* Controller::savePreset (PresetType type, std::string_view name, std::string_view author,
                                           std::string_view tags, std::string_view description, bool overwrite, std::string* error)
{
    PresetData d;
    {
        const ParamValues v = paramValues();
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        const Model& m = processor.modelData;
        if (type == PresetType::Pattern)
            d = makePatternPreset (PresetType::Pattern, m.bank->seq[static_cast<std::size_t> (selectedSlot())]);
        else if (type == PresetType::Arp)
            d = makePatternPreset (PresetType::Arp, m.bank->arp[static_cast<std::size_t> (selectedSlot())]);
        else if (type == PresetType::Template)
            d = makeTemplatePreset (m.bank->seq[static_cast<std::size_t> (selectedSlot())].gen);
        else if (type == PresetType::State)
        {
            if (error != nullptr) *error = "a session state is not a preset";
            return nullptr;
        }
        else
            d = makePreset (type, v, m.settings, m.bank.get());
    }
    d.name = std::string (name);
    d.author = std::string (author);
    d.tags = std::string (tags);
    d.description = std::string (description);

    std::string err;
    const PresetEntry* saved = presets().saveUser (d, overwrite, err);
    if (saved == nullptr)
    {
        if (error != nullptr) *error = err;
        return nullptr;
    }
    if (type == PresetType::Scene)
    {
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        processor.modelData.sceneName = d.name;
        processor.modelData.sceneAuthor = d.author;
        processor.modelData.sceneTags = d.tags;
        processor.modelData.sceneDescription = d.description;
    }
    lastLoadedId[static_cast<std::size_t> (type)] = saved->id;
    notify (kChangedPresets);
    return saved;
}

std::string Controller::sceneName() const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.sceneName;
}

void Controller::setSceneName (std::string_view name)
{
    {
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        processor.modelData.sceneName = std::string (name);
        processor.modelData.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedPresets);
}

fs::path Controller::userDefaultStatePath() const
{
    return processor.presets.userRoot() / "default.mw12f";
}

bool Controller::hasUserDefaultState() const
{
    std::error_code ec;
    return fs::is_regular_file (userDefaultStatePath(), ec);
}

bool Controller::saveDefaultState (std::string* error)
{
    // Written next to the user library (not inside a type folder, so the browser never lists
    // it), through a temporary file so a half-written default can never be read.
    const fs::path path = userDefaultStatePath();
    const fs::path temp = fs::path (path).concat (".tmp");
    if (! writeWholeFile (temp, writePreset (processor.captureState())))
    {
        if (error != nullptr) *error = "cannot write " + temp.string();
        return false;
    }
    std::error_code ec;
    fs::rename (temp, path, ec);
    if (ec)
    {
        fs::remove (temp, ec);
        if (error != nullptr) *error = "cannot write " + path.string();
        return false;
    }
    notify (kChangedPresets);
    return true;
}

bool Controller::resetDefaultState (std::string* error)
{
    std::error_code ec;
    fs::remove (userDefaultStatePath(), ec);
    if (ec)
    {
        if (error != nullptr) *error = "cannot delete " + userDefaultStatePath().string();
        return false;
    }
    notify (kChangedPresets);
    return true;
}

void Controller::initScene()
{
    PresetData d;
    if (processor.userDefaultState (d))
    {
        d.type = PresetType::Scene;    // a scene load: the UI state and MIDI map stay
        restrictToType (d);
    }
    else if (const FactoryFile* f = findFactoryFile ("default.mw12f"); f != nullptr && parsePreset (f->text(), d).ok)
    {
        d.type = PresetType::Scene;    // a scene load: the UI state and MIDI map stay
        restrictToType (d);
    }
    else
    {
        d = makePreset (PresetType::Scene, defaultParamValues(), Settings {}, nullptr);
        d.hasBank = true;              // every slot empty
    }
    d.name = "Init";
    processor.applyPresetData (d, false);
    pump();
}

void Controller::beginAudition()
{
    auditionSnapshot = processor.captureState();
    audition = AuditionTrack {};
    audition.stateLoads = processor.stateLoads.load (std::memory_order_acquire);
}

void Controller::recordAuditionLoad (const PresetData& d)
{
    AuditionTrack& a = audition;
    const ParamValues now = paramValues();
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        if (d.hasParam[idx] && paramInType (d.type, static_cast<ParamId> (i)))
            a.params[idx] = true;
    }
    a.paramsLeft = now;

    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    const Model& m = processor.modelData;
    a.settingsMask |= d.settingsMask & settingsFieldsInType (d.type);
    a.settingsLeft = m.settings;
    if (bankInType (d.type) && d.hasBank)
        a.wholeBank = true;
    if (d.type == PresetType::Pattern || d.type == PresetType::Arp || d.type == PresetType::Template)
    {
        // applyPresetData puts these in the slot the Performance parameters select.
        const int bank = std::clamp (paramAsInt (now, kPatternBank), 0, seq::kNumBanks - 1);
        const int slotInBank = std::clamp (paramAsInt (now, kPatternSlot) - 1, 0, seq::kSlotsPerBank - 1);
        const std::pair<bool, int> where { d.type == PresetType::Arp, seq::slotIndex (bank, slotInBank) };
        if (std::find (a.slots.begin(), a.slots.end(), where) == a.slots.end())
            a.slots.push_back (where);
    }
    if (a.wholeBank || ! a.slots.empty())
        a.bankLeft = std::make_unique<seq::PatternBank> (*m.bank);
    if (d.type == PresetType::Scene)
        a.scene = true;
}

void Controller::endAudition (bool keep)
{
    if (! auditionSnapshot.has_value()) return;
    PresetData snapshot = std::move (*auditionSnapshot);
    auditionSnapshot.reset();
    AuditionTrack a = std::move (audition);
    audition = AuditionTrack {};

    // Keep, or a session the host loaded meanwhile replaced everything: nothing to revert.
    if (keep || processor.stateLoads.load (std::memory_order_acquire) != a.stateLoads)
        return;

    // Revert only what the audition changed and nobody changed since: as a scene document
    // that carries exactly those parameters, settings fields, patterns and scene fields.
    PresetData revert;
    revert.type = PresetType::Scene;
    const ParamValues now = paramValues();
    bool anything = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        if (a.params[idx] && juce::exactlyEqual (now[idx], a.paramsLeft[idx]) && paramInType (PresetType::Scene, static_cast<ParamId> (i)))
        {
            revert.setParam (static_cast<ParamId> (i), snapshot.params[idx]);
            anything = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        const Model& m = processor.modelData;
        if (a.settingsMask != 0 && std::memcmp (&m.settings, &a.settingsLeft, sizeof (Settings)) == 0)
        {
            revert.settings = snapshot.settings;
            revert.settingsMask = a.settingsMask;
            anything = true;
        }

        if (a.bankLeft != nullptr)
        {
            // The snapshot's bank as patterns, then the current bank with the audition's slots
            // (or all of them, after a scene) taken back from it, where they are still as the
            // audition left them.
            seq::PatternBank before;
            applyBank (snapshot, before);
            seq::PatternBank result = *m.bank;
            bool bankChanged = false;
            const auto takeBack = [&] (bool arp, int slot) {
                const auto k = static_cast<std::size_t> (slot);
                const seq::Pattern& current = (arp ? m.bank->arp : m.bank->seq)[k];
                const seq::Pattern& left = (arp ? a.bankLeft->arp : a.bankLeft->seq)[k];
                const seq::Pattern& old = (arp ? before.arp : before.seq)[k];
                if (seq::samePattern (current, left) && ! seq::samePattern (current, old))
                {
                    (arp ? result.arp : result.seq)[k] = old;
                    bankChanged = true;
                }
            };
            if (a.wholeBank)
            {
                for (int i = 0; i < seq::kNumSlots; ++i)
                {
                    takeBack (false, i);
                    takeBack (true, i);
                }
            }
            else
            {
                for (const auto& [arp, slot] : a.slots)
                    takeBack (arp, slot);
            }
            if (bankChanged)
            {
                const seq::Pattern empty {};
                revert.hasBank = true;
                for (int i = 0; i < seq::kNumSlots; ++i)
                {
                    const auto k = static_cast<std::size_t> (i);
                    for (const bool arp : { false, true })
                    {
                        const seq::Pattern& pattern = (arp ? result.arp : result.seq)[k];
                        if (seq::samePattern (pattern, empty)) continue;
                        SlotPattern sp;
                        sp.slot = i;
                        sp.pattern = pattern;
                        (arp ? revert.arpSlots : revert.seqSlots).push_back (std::move (sp));
                    }
                }
                anything = true;
            }
        }

        // applyPresetData sets the scene fields from every scene document: keep the current
        // ones unless the audition changed them.
        const bool sceneBack = a.scene;
        revert.name = sceneBack ? snapshot.name : m.sceneName;
        revert.author = sceneBack ? snapshot.author : m.sceneAuthor;
        revert.tags = sceneBack ? snapshot.tags : m.sceneTags;
        revert.description = sceneBack ? snapshot.description : m.sceneDescription;
        anything = anything || sceneBack;
    }

    if (! anything)
        return;
    processor.applyPresetData (revert, false);
    pump();
}

const PresetEntry* Controller::lastLoaded (PresetType type) const
{
    const std::string& id = lastLoadedId[static_cast<std::size_t> (type)];
    return id.empty() ? nullptr : processor.presets.find (id);
}

const PresetEntry* Controller::loadNextPreset (const PresetFilter& filter, int direction)
{
    const PresetEntry* current = filter.type.has_value() ? lastLoaded (*filter.type) : nullptr;
    const PresetEntry* next = presets().step (current, filter, direction);
    if (next == nullptr) return nullptr;
    return loadPreset (*next) ? next : nullptr;
}

const PresetEntry* Controller::importPresetFile (const fs::path& path, std::string* error)
{
    std::string err;
    const PresetEntry* e = presets().importFile (path, false, err);
    if (e == nullptr && error != nullptr) *error = err;
    if (e != nullptr) notify (kChangedPresets);
    return e;
}

bool Controller::exportPreset (const PresetEntry& entry, const fs::path& destination, std::string* error)
{
    std::string err;
    const bool ok = presets().exportTo (entry, destination, err);
    if (! ok && error != nullptr) *error = err;
    return ok;
}

//==============================================================================
void Controller::startMidiLearn (ParamId id)
{
    processor.setLearnTarget (static_cast<int> (id));
    notify (kChangedMidiMap);
}

void Controller::cancelMidiLearn()
{
    processor.setLearnTarget (-1);
    notify (kChangedMidiMap);
}

bool Controller::midiLearning() const noexcept
{
    return processor.learnTargetIndex() >= 0;
}

ParamId Controller::midiLearnTarget() const noexcept
{
    const int t = processor.learnTargetIndex();
    return static_cast<ParamId> (std::clamp (t, 0, kNumParams - 1));
}

int Controller::midiControllerFor (ParamId id) const
{
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.midiMap.controllerFor (static_cast<int> (id));
}

int Controller::paramForMidiController (int cc) const
{
    if (cc < 0 || cc >= MidiMap::kNumControllers) return -1;
    std::lock_guard<std::mutex> lock (processor.modelData.mutex);
    return processor.modelData.midiMap.param[static_cast<std::size_t> (cc)];
}

void Controller::setMidiMapping (int cc, ParamId id)
{
    if (! MidiMap::bindable (cc)) return;    // channel-mode numbers (120..127) are never bound
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        m.midiMap.clearParam (static_cast<int> (id));
        m.midiMap.param[static_cast<std::size_t> (cc)] = static_cast<std::int16_t> (id);
        processor.publishMidiMapLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedMidiMap);
}

void Controller::clearMidiMapping (ParamId id)
{
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        m.midiMap.clearParam (static_cast<int> (id));
        processor.publishMidiMapLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    processor.setLearnTarget (-1);
    notify (kChangedMidiMap);
}

void Controller::clearAllMidiMappings()
{
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        m.midiMap.clear();
        processor.publishMidiMapLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    processor.setLearnTarget (-1);
    notify (kChangedMidiMap);
}

bool Controller::saveMidiMapFile (const fs::path& path, std::string* error) const
{
    xml::Element e;
    {
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        e = processor.modelData.midiMap.toXml();
    }
    if (! writeWholeFile (path, xml::write (e)))
    {
        if (error != nullptr) *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool Controller::loadMidiMapFile (const fs::path& path, std::string* error)
{
    std::string text;
    if (! readWholeFile (path, text))
    {
        if (error != nullptr) *error = "cannot read " + path.string();
        return false;
    }
    xml::Element root;
    const xml::ParseResult r = xml::parse (text, root);
    if (! r.ok || root.name != "midi_map")
    {
        if (error != nullptr) *error = r.ok ? "not a MIDI map file" : r.error;
        return false;
    }
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        m.midiMap.fromXml (root);
        processor.publishMidiMapLocked();
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedMidiMap);
    return true;
}

fs::path Controller::defaultMidiMapPath() const
{
    return processor.presets.userRoot() / "midi_map.xml";
}

//==============================================================================
std::vector<std::uint8_t> Controller::patternMidiFile (bool arp, int slot, std::span<const int> heldNotes, int cycles) const
{
    const seq::Pattern p = pattern (arp, slot);
    const Settings s = settings();

    seq::MidiExportOptions options;
    options.accentVelocity = s.outAccentVelocity;
    options.normalVelocity = s.outNormalVelocity;
    options.slideIntoRest = s.slideIntoRest;
    options.bpm = static_cast<double> (s.internalBpm);

    if (! arp)
        return seq::writeMidiFile (p, options);

    // An arpeggiator pattern needs held notes: record a take through a private Player.
    static const int kDefaultChord[] = { 48, 51, 55 };
    const std::span<const int> held = heldNotes.empty() ? std::span<const int> (kDefaultChord) : heldNotes;
    cycles = std::clamp (cycles, 1, 64);

    const double sampleRate = 48000.0;
    const double bpm = std::clamp (options.bpm, 20.0, 300.0);
    const double stepQ = quartersPerStep (p);
    const double clipQ = stepQ * static_cast<double> (std::clamp<int> (p.length, 1, seq::kMaxSteps)) * cycles;
    const auto totalSamples = static_cast<std::int64_t> (std::ceil (clipQ * 60.0 / bpm * sampleRate));
    const double quartersPerSample = bpm / (60.0 * sampleRate);

    auto player = std::make_unique<seq::Player>();
    auto bank = std::make_unique<seq::PatternBank>();
    bank->arp[static_cast<std::size_t> (slot)] = p;
    auto output = std::make_unique<seq::PlayerOutput>();

    seq::PlayerControls controls;
    controls.playMode = seq::PlayMode::Arpeggiator;
    controls.run = true;
    controls.bank = slot / seq::kSlotsPerBank;
    controls.slot = slot % seq::kSlotsPerBank;
    controls.internalBpm = bpm;
    seq::PlayerConfig config = seq::playerConfigFrom (s);
    config.midiOut = true;

    const int block = 256;
    player->prepare (sampleRate, block);

    std::vector<seq::MidiMsg> keys;
    for (const int n : held)
    {
        seq::MidiMsg m;
        m.offset = 0;
        m.size = 3;
        m.data[0] = 0x90;
        m.data[1] = static_cast<std::uint8_t> (std::clamp (n, 0, 127));
        m.data[2] = 100;
        keys.push_back (m);
    }

    struct Open { int note; std::int64_t start; bool accent; bool vibrato; };
    std::vector<Open> open;
    std::vector<seq::MidiNote> notes;
    bool cc1 = false;
    seq::TransportInfo noHost;

    for (std::int64_t pos = 0; pos < totalSamples; pos += block)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (block, totalSamples - pos));
        const std::span<const seq::MidiMsg> in = pos == 0 ? std::span<const seq::MidiMsg> (keys) : std::span<const seq::MidiMsg> {};
        output->clear();
        player->process (noHost, in, *bank, controls, config, n, *output);
        for (const seq::MidiMsg& m : output->midi())
        {
            if (m.size < 3) continue;
            const int status = m.data[0] & 0xF0;
            const std::int64_t at = pos + m.offset;
            if (status == 0xB0 && m.data[1] == 1) { cc1 = m.data[2] >= 64; continue; }
            const bool on = status == 0x90 && m.data[2] > 0;
            const bool off = status == 0x80 || (status == 0x90 && m.data[2] == 0);
            if (! on && ! off) continue;
            for (std::size_t k = 0; k < open.size(); ++k)
            {
                if (open[k].note != m.data[1]) continue;
                notes.push_back ({ static_cast<double> (open[k].start) * quartersPerSample, static_cast<double> (at) * quartersPerSample,
                                   static_cast<std::uint8_t> (m.data[1]), open[k].accent, open[k].vibrato, 0 });
                open.erase (open.begin() + static_cast<std::ptrdiff_t> (k));
                break;
            }
            if (on)
                open.push_back ({ m.data[1], at, m.data[2] >= s.outAccentVelocity, cc1 });
        }
    }
    for (const auto& o : open)
        notes.push_back ({ static_cast<double> (o.start) * quartersPerSample, clipQ, static_cast<std::uint8_t> (o.note), o.accent, o.vibrato, 0 });

    const std::string_view name (p.name, strnlen (p.name, seq::kPatternNameLength));
    return seq::writeMidiFile (notes, clipQ, options, name.empty() ? std::string_view ("arpeggio") : name);
}

bool Controller::exportPatternMidiFile (bool arp, int slot, const fs::path& path, std::string* error) const
{
    const auto bytes = patternMidiFile (arp, slot);
    if (! writeWholeFile (path, std::string_view (reinterpret_cast<const char*> (bytes.data()), bytes.size())))
    {
        if (error != nullptr) *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

void Controller::cleanDragFile()
{
    if (dragFile.empty()) return;
    std::error_code ec;
    fs::remove (dragFile, ec);
    dragFile.clear();
}

fs::path Controller::writeDragMidiFile (bool arp, int slot)
{
    cleanDragFile();
    if (dragDir.empty())
    {
        // A directory of this instance's own: the file keeps the pattern's name (hosts show
        // it), and another instance dragging a pattern of the same name cannot overwrite it
        // or delete it while a host is still reading it.
        fs::path base;
        if (const char* runtime = std::getenv ("XDG_RUNTIME_DIR"); runtime != nullptr && runtime[0] != '\0')
            base = fs::path (runtime) / "mw12F";
        else
        {
            std::error_code ec;
            base = fs::temp_directory_path (ec) / "mw12F";
        }
        dragDir = base / ("drag-" + juce::Uuid().toString().toStdString());
    }
    const fs::path dir = dragDir;
    const seq::Pattern p = pattern (arp, slot);
    std::string name (p.name, strnlen (p.name, seq::kPatternNameLength));
    if (name.empty())
        name = (arp ? "arp " : "pattern ") + std::string (1, static_cast<char> ('A' + slot / seq::kSlotsPerBank)) + std::to_string (slot % seq::kSlotsPerBank + 1);
    const fs::path file = dir / (PresetManager::safeFileName (name) + ".mid");
    if (! exportPatternMidiFile (arp, slot, file))
        return {};
    dragFile = file;
    return file;
}

bool Controller::importMidiData (std::span<const std::uint8_t> bytes, bool arp, int slot, seq::MidiImportReport* report)
{
    seq::Pattern p = pattern (arp, slot);
    seq::MidiImportOptions options;
    options.accentThreshold = settings().accentThreshold;
    seq::MidiImportReport local;
    if (! seq::readMidiFile (bytes, p, options, report != nullptr ? report : &local))
        return false;
    commitPattern (arp, slot, p, "Import MIDI");
    return true;
}

bool Controller::importMidiFile (const fs::path& path, bool arp, int slot, seq::MidiImportReport* report)
{
    std::string text;
    if (! readWholeFile (path, text))
    {
        if (report != nullptr) { report->ok = false; report->error = "cannot read " + path.string(); }
        return false;
    }
    return importMidiData ({ reinterpret_cast<const std::uint8_t*> (text.data()), text.size() }, arp, slot, report);
}

//==============================================================================
fs::path Controller::globalUiFile() const
{
    return processor.presets.userRoot() / "ui.xml";
}

void Controller::loadGlobalUi()
{
    if (globalUiLoaded) return;
    globalUiLoaded = true;
    std::string text;
    if (! readWholeFile (globalUiFile(), text)) return;
    xml::Element root;
    if (xml::parse (text, root).ok && root.name == "ui")
    {
        globalUi = root;
        globalUi.children.clear();
    }
}

bool Controller::saveGlobalUi() const
{
    return writeWholeFile (globalUiFile(), xml::write (globalUi));
}

void Controller::applyGlobalUiDefaults()
{
    // Nothing to copy eagerly: uiValue() falls back to the global file when the instance has
    // no value of its own, so a new instance starts from the global defaults.
}

std::string Controller::uiValue (std::string_view key, std::string_view fallback) const
{
    {
        std::lock_guard<std::mutex> lock (processor.modelData.mutex);
        if (const std::string* v = processor.modelData.ui.attribute (key))
            return *v;
    }
    return globalUiValue (key, fallback);
}

void Controller::setUiValue (std::string_view key, std::string_view value)
{
    if (! xml::isValidName (key)) return;
    {
        Model& m = processor.modelData;
        std::lock_guard<std::mutex> lock (m.mutex);
        if (const std::string* v = m.ui.attribute (key); v != nullptr && *v == value)
            return;
        m.ui.setAttribute (key, value);
        m.version.fetch_add (1, std::memory_order_relaxed);
    }
    notify (kChangedUi);
}

std::string Controller::globalUiValue (std::string_view key, std::string_view fallback) const
{
    const_cast<Controller*> (this)->loadGlobalUi();
    if (const std::string* v = globalUi.attribute (key))
        return *v;
    return std::string (fallback);
}

void Controller::setGlobalUiValue (std::string_view key, std::string_view value)
{
    if (! xml::isValidName (key)) return;
    loadGlobalUi();
    globalUi.setAttribute (key, value);
    saveGlobalUi();
}

float Controller::zoom() const
{
    return std::clamp (textToFloat (uiValue ("zoom", "1"), 1.0f), 0.75f, 2.0f);
}

void Controller::setZoom (float z, bool alsoGlobalDefault)
{
    const std::string text = floatToText (std::clamp (z, 0.75f, 2.0f));
    setUiValue ("zoom", text);
    if (alsoGlobalDefault) setGlobalUiValue ("zoom", text);
}

std::string Controller::theme() const
{
    return uiValue ("theme", "night");
}

void Controller::setTheme (std::string_view theme, bool alsoGlobalDefault)
{
    setUiValue ("theme", theme);
    if (alsoGlobalDefault) setGlobalUiValue ("theme", theme);
}
}
