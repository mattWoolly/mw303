#pragma once
// The cog menu: every Settings field, the GUI options (theme, zoom, sequencer view, cover
// animation), the quality tiers, the MIDI map file operations, the default state for new
// instances, undo and redo. It is a
// non-modal PopupMenu drawn inside the plate; numeric fields that are not a short list open
// the plate's typed entry over the cog.
#include "kit/Faceplate.h"
#include "Theme.h"
#include "Controller.h"
#include <vector>

namespace acid::ui
{
struct SettingsMenuHooks
{
    std::function<void (ThemeId)> setTheme;
    std::function<void (float)> setZoom;
    std::function<void (bool keyboardView)> setSequencerView;
    std::function<bool()> isKeyboardView;
    std::function<void (bool)> setCoverOpen;
    std::function<bool()> isCoverOpen;
    std::function<void (bool)> setCoverAnimated;
    std::function<bool()> isCoverAnimated;
    std::function<void (const juce::String&)> notice;
    /** The MIDI map file dialogs. The owner launches them so it also owns the chooser (and
        the dialog dies with the editor); the menu never keeps one alive by itself. */
    std::function<void()> saveMidiMap, loadMidiMap;
    /** When set, every chosen item runs through this, so the owner can bracket the action
        (the editor marks it as its own change). */
    std::function<void (const std::function<void()>&)> runAction;
    /** Default state for new instances (Controller::saveDefaultState, resetDefaultState).
        The restore item is enabled only while a user default exists. */
    std::function<void()> saveDefaultState, resetDefaultState;
    bool hasUserDefaultState = false;
    /** LV2: a quality tier change waits for the host's next activation (the plugin cannot ask
        for one), so the menu says so. */
    bool qualityAppliesAtActivation = false;
    ThemeId currentTheme = ThemeId::Silver;
    float currentZoom = 1.0f;
};

/** The menu and what each item does: item id n runs actions[n - 1]. Items without an action
    (headers, notes) have ids below 1. */
struct BuiltSettingsMenu
{
    juce::PopupMenu menu;
    std::vector<std::function<void()>> actions;
};

/** Builds the menu without showing it (tests read it). */
BuiltSettingsMenu buildSettingsMenu (plugin::Controller&, Faceplate& plate, juce::Component& anchor, const SettingsMenuHooks&);

/** Builds and shows the menu next to the anchor (the cog button). */
void showSettingsMenu (plugin::Controller&, Faceplate& plate, juce::Component& anchor, const SettingsMenuHooks&);
}
