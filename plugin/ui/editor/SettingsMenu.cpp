#include "editor/SettingsMenu.h"
#include "kit/SafeText.h"
#include <memory>
#include <vector>

namespace acid::ui
{
namespace
{
/** Collects actions by menu id so one async callback can dispatch them. */
struct MenuBuilder
{
    std::vector<std::function<void()>> actions;

    int add (juce::PopupMenu& m, const juce::String& text, std::function<void()> action, bool enabled = true, bool ticked = false)
    {
        actions.push_back (std::move (action));
        const auto id = static_cast<int> (actions.size());
        m.addItem (id, text, enabled, ticked);
        return id;
    }
};

juce::String noteName (int midiNote)
{
    static constexpr const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (kNames[midiNote % 12]) + juce::String (midiNote / 12 - 1);
}

template <typename Setter>
void settingsEdit (plugin::Controller& c, const char* label, Setter&& set)
{
    auto s = c.settings();
    set (s);
    c.setSettings (s, label);
}

template <typename Setter>
void typedEntry (plugin::Controller& c, Faceplate& plate, juce::Component& anchor, const juce::String& current, const char* label, Setter&& set)
{
    plate.beginTextEntry (anchor, current, [&c, label, set] (const juce::String& text)
    {
        if (text.trim().isEmpty())
            return;

        settingsEdit (c, label, [&] (Settings& s) { set (s, text.trim()); });
    });
}
}

BuiltSettingsMenu buildSettingsMenu (plugin::Controller& c, Faceplate& plate, juce::Component& anchor, const SettingsMenuHooks& hooks)
{
    MenuBuilder b;
    const auto settings = c.settings();
    juce::PopupMenu menu;
    menu.addSectionHeader ("mw12F");

    const auto undoText = c.undoLabel();
    const auto redoText = c.redoLabel();
    b.add (menu, "Undo" + (undoText.empty() ? juce::String() : " " + text::from (undoText)), [&c] { c.undo(); }, c.canUndo());
    b.add (menu, "Redo" + (redoText.empty() ? juce::String() : " " + text::from (redoText)), [&c] { c.redo(); }, c.canRedo());
    b.add (menu, "Init scene", [&c] { c.initScene(); });
    b.add (menu, "Save as default", [hooks] { if (hooks.saveDefaultState) hooks.saveDefaultState(); }, hooks.saveDefaultState != nullptr);
    b.add (menu, "Restore factory default", [hooks] { if (hooks.resetDefaultState) hooks.resetDefaultState(); },
           hooks.resetDefaultState != nullptr && hooks.hasUserDefaultState);
    menu.addSeparator();

    // ---- GUI ----
    {
        juce::PopupMenu gui;
        juce::PopupMenu theme;
        b.add (theme, "Silver", [hooks] { if (hooks.setTheme) hooks.setTheme (ThemeId::Silver); }, true, hooks.currentTheme == ThemeId::Silver);
        b.add (theme, "Night", [hooks] { if (hooks.setTheme) hooks.setTheme (ThemeId::Night); }, true, hooks.currentTheme == ThemeId::Night);
        gui.addSubMenu ("Theme", theme);

        juce::PopupMenu zoom;

        for (int pct = 75; pct <= 200; pct += 25)
        {
            const auto z = static_cast<float> (pct) / 100.0f;
            b.add (zoom, juce::String (pct) + " %", [hooks, z] { if (hooks.setZoom) hooks.setZoom (z); }, true,
                   std::abs (hooks.currentZoom - z) < 0.01f);
        }

        gui.addSubMenu ("Zoom", zoom);

        juce::PopupMenu view;
        const auto keys = hooks.isKeyboardView ? hooks.isKeyboardView() : false;
        b.add (view, "Piano roll", [hooks] { if (hooks.setSequencerView) hooks.setSequencerView (false); }, true, ! keys);
        b.add (view, "Keyboard", [hooks] { if (hooks.setSequencerView) hooks.setSequencerView (true); }, true, keys);
        gui.addSubMenu ("Sequencer view", view);

        const auto coverOpen = hooks.isCoverOpen ? hooks.isCoverOpen() : false;
        const auto coverAnimated = hooks.isCoverAnimated ? hooks.isCoverAnimated() : true;
        b.add (gui, "Calibration cover open", [hooks, coverOpen] { if (hooks.setCoverOpen) hooks.setCoverOpen (! coverOpen); }, true, coverOpen);
        b.add (gui, "Animate calibration cover", [hooks, coverAnimated] { if (hooks.setCoverAnimated) hooks.setCoverAnimated (! coverAnimated); }, true,
               coverAnimated);
        menu.addSubMenu ("GUI", gui);
    }

    // ---- Tempo (internal clock: standalone, or a host without tempo) ----
    {
        juce::PopupMenu tempo;
        tempo.addItem (-1, "Internal tempo: " + juce::String (settings.internalBpm, 1) + " BPM", false, false);
        b.add (tempo, "Set...", [&c, &plate, &anchor, settings]
        {
            typedEntry (c, plate, anchor, juce::String (settings.internalBpm, 1), "Internal tempo",
                        [] (Settings& s, const juce::String& text) { s.internalBpm = juce::jlimit (20.0f, 300.0f, text.getFloatValue()); });
        });
        tempo.addSeparator();

        for (auto bpm : { 90, 100, 110, 120, 125, 128, 130, 135, 140, 150, 160, 170, 174 })
            b.add (tempo, juce::String (bpm) + " BPM", [&c, bpm] { settingsEdit (c, "Internal tempo", [bpm] (Settings& s) { s.internalBpm = static_cast<float> (bpm); }); },
                   true, std::abs (settings.internalBpm - static_cast<float> (bpm)) < 0.05f);

        menu.addSubMenu ("Tempo", tempo);
    }

    // ---- Sequencer ----
    {
        juce::PopupMenu sequencer;

        juce::PopupMenu trigger;
        b.add (trigger, "Live", [&c] { settingsEdit (c, "Trigger mode", [] (Settings& s) { s.triggerMode = seq::TriggerMode::Live; }); }, true,
               settings.triggerMode == seq::TriggerMode::Live);
        b.add (trigger, "Host note", [&c] { settingsEdit (c, "Trigger mode", [] (Settings& s) { s.triggerMode = seq::TriggerMode::HostNote; }); }, true,
               settings.triggerMode == seq::TriggerMode::HostNote);
        sequencer.addSubMenu ("Trigger", trigger);

        juce::PopupMenu launch;
        static constexpr const char* kLaunch[3] = { "Pattern end", "Next bar", "At once" };

        for (int i = 0; i < 3; ++i)
            b.add (launch, kLaunch[i], [&c, i] { settingsEdit (c, "Launch quantise", [i] (Settings& s) { s.launchQuant = static_cast<seq::LaunchQuant> (i); }); },
                   true, static_cast<int> (settings.launchQuant) == i);

        sequencer.addSubMenu ("Launch", launch);

        juce::PopupMenu slide;
        static constexpr const char* kSlide[3] = { "Release", "Hold through the rest", "Sustain to the next note" };

        for (int i = 0; i < 3; ++i)
            b.add (slide, kSlide[i], [&c, i] { settingsEdit (c, "Slide into rest", [i] (Settings& s) { s.slideIntoRest = static_cast<seq::SlideIntoRest> (i); }); },
                   true, static_cast<int> (settings.slideIntoRest) == i);

        sequencer.addSubMenu ("Slide into rest", slide);

        juce::PopupMenu priority;
        static constexpr const char* kPriority[3] = { "Last", "Low", "High" };

        for (int i = 0; i < 3; ++i)
            b.add (priority, kPriority[i], [&c, i] { settingsEdit (c, "Note priority", [i] (Settings& s) { s.notePriority = static_cast<seq::NotePriority> (i); }); },
                   true, static_cast<int> (settings.notePriority) == i);

        sequencer.addSubMenu ("Note priority (external)", priority);

        juce::PopupMenu base;

        for (int note = 0; note <= 72; note += 12)
            b.add (base, noteName (note) + " (" + juce::String (note) + ")",
                   [&c, note] { settingsEdit (c, "Host note base", [note] (Settings& s) { s.hostNoteBase = static_cast<std::uint8_t> (note); }); }, true,
                   settings.hostNoteBase == note);

        sequencer.addSubMenu ("Host note base", base);
        sequencer.addSeparator();
        b.add (sequencer, "Follow host transport", [&c] { settingsEdit (c, "Follow host transport", [] (Settings& s) { s.followHostTransport = ! s.followHostTransport; }); },
               true, settings.followHostTransport);
        b.add (sequencer, "Host link (lock steps to the song position)", [&c] { settingsEdit (c, "Host link", [] (Settings& s) { s.hostLink = ! s.hostLink; }); },
               true, settings.hostLink);
        b.add (sequencer, "Restart pattern on trigger note", [&c] { settingsEdit (c, "Restart on trigger", [] (Settings& s) { s.restartOnTrigger = ! s.restartOnTrigger; }); },
               true, settings.restartOnTrigger);
        menu.addSubMenu ("Sequencer", sequencer);
    }

    // ---- MIDI ----
    {
        juce::PopupMenu midi;
        b.add (midi, "MIDI out", [&c] { settingsEdit (c, "MIDI out", [] (Settings& s) { s.midiOut = ! s.midiOut; }); }, true, settings.midiOut);
        b.add (midi, "MIDI thru (external mode)", [&c] { settingsEdit (c, "MIDI thru", [] (Settings& s) { s.midiThru = ! s.midiThru; }); }, true, settings.midiThru);
        midi.addSeparator();

        const auto velocityMenu = [&] (const char* title, const char* label, int current, int lo, int hi, auto&& assign)
        {
            juce::PopupMenu sub;
            sub.addItem (-1, juce::String (title) + ": " + juce::String (current), false, false);
            b.add (sub, "Set...", [&c, &plate, &anchor, current, label, lo, hi, assign]
            {
                typedEntry (c, plate, anchor, juce::String (current), label,
                            [lo, hi, assign] (Settings& s, const juce::String& text) { assign (s, juce::jlimit (lo, hi, text.getIntValue())); });
            });
            sub.addSeparator();

            for (auto v : { 1, 16, 32, 48, 64, 80, 96, 100, 112, 127 })
                if (v >= lo && v <= hi)
                    b.add (sub, juce::String (v), [&c, v, label, assign] { settingsEdit (c, label, [v, assign] (Settings& s) { assign (s, v); }); }, true, current == v);

            midi.addSubMenu (title, sub);
        };

        velocityMenu ("Accent velocity out", "Accent velocity", settings.outAccentVelocity, 2, 127,
                      [] (Settings& s, int v) { s.outAccentVelocity = static_cast<std::uint8_t> (v); });
        velocityMenu ("Normal velocity out", "Normal velocity", settings.outNormalVelocity, 1, 126,
                      [] (Settings& s, int v) { s.outNormalVelocity = static_cast<std::uint8_t> (v); });
        velocityMenu ("Accent threshold in", "Accent threshold", settings.accentThreshold, 1, 126,
                      [] (Settings& s, int v) { s.accentThreshold = static_cast<std::uint8_t> (v); });

        juce::PopupMenu bend;

        for (int st = 0; st <= 12; ++st)
            b.add (bend, juce::String (st) + " st", [&c, st] { settingsEdit (c, "Bend range", [st] (Settings& s) { s.bendRange = static_cast<std::uint8_t> (st); }); },
                   true, settings.bendRange == st);

        midi.addSubMenu ("Pitch bend range", bend);
        midi.addSeparator();

        if (c.midiLearning())
            b.add (midi, "Cancel MIDI learn (" + juce::String (paramInfo (c.midiLearnTarget()).name) + ")", [&c] { c.cancelMidiLearn(); });

        b.add (midi, "Save MIDI map...", [hooks] { if (hooks.saveMidiMap) hooks.saveMidiMap(); }, hooks.saveMidiMap != nullptr);
        b.add (midi, "Load MIDI map...", [hooks] { if (hooks.loadMidiMap) hooks.loadMidiMap(); }, hooks.loadMidiMap != nullptr);
        b.add (midi, "Clear all MIDI mappings", [&c] { c.clearAllMidiMappings(); });
        menu.addSubMenu ("MIDI", midi);
    }

    // ---- Quality ----
    {
        juce::PopupMenu quality;
        static constexpr const char* kTiers[4] = { "Draft", "Normal", "High", "Ultra" };
        juce::PopupMenu realtime, offline;

        if (hooks.qualityAppliesAtActivation)
        {
            quality.addItem (-3, "A change applies at the host's next activation", false, false);
            quality.addSeparator();
        }

        for (int i = 0; i < 4; ++i)
        {
            b.add (realtime, kTiers[i], [&c, i] { settingsEdit (c, "Real-time quality", [i] (Settings& s) { s.realtimeQuality = static_cast<Quality> (i); }); },
                   true, static_cast<int> (settings.realtimeQuality) == i);
            b.add (offline, kTiers[i], [&c, i] { settingsEdit (c, "Offline quality", [i] (Settings& s) { s.offlineQuality = static_cast<Quality> (i); }); },
                   true, static_cast<int> (settings.offlineQuality) == i);
        }

        quality.addSubMenu ("Real-time", realtime);
        quality.addSubMenu ("Offline render", offline);
        menu.addSubMenu ("Quality", quality);
    }

    menu.addSeparator();
    menu.addItem (-2, juce::String (MW_PRODUCT_NAME) + "  by mwAudio", false, false);
    menu.setLookAndFeel (&anchor.getLookAndFeel());   // drawn in the house style, inside the plate
    return { std::move (menu), std::move (b.actions) };
}

void showSettingsMenu (plugin::Controller& c, Faceplate& plate, juce::Component& anchor, const SettingsMenuHooks& hooks)
{
    auto built = std::make_shared<BuiltSettingsMenu> (buildSettingsMenu (c, plate, anchor, hooks));
    const auto runAction = hooks.runAction;
    built->menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor), [built, runAction] (int result)
    {
        if (result > 0 && result <= static_cast<int> (built->actions.size()))
            if (auto& action = built->actions[static_cast<std::size_t> (result - 1)])
                runAction != nullptr ? runAction (action) : action();
    });
}
}
