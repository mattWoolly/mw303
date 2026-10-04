#pragma once
// The preset browser: an overlay inside the plate (never a modal loop, never a separate
// window). Types down the left, sources and favourites under them, search and tag filters
// in the middle, results and the info pane on the right, with the actions along the foot.
//
// Tags are grouped as the factory vocabulary groups them (Play Mode, Style, Family, Waveform,
// Distortion, Character, Mods, Pattern, Arp Shape, Unit, Effect, Template; anything else is
// User), each group under its caption with its own ANY/ALL switch. A preset passes a group
// when it has any (or all) of that group's selected tags, and it must pass every group with a
// selection: "Saw or Square, and Strong" is Saw and Square in Waveform with Strong in
// Distortion. Tags whose bare word means nothing out of context are shown under a clearer name
// (Distortion "None" reads "Off").
//
// Browsing auditions: opening the browser snapshots the state (Controller::beginAudition),
// clicking a row loads it, OK keeps the result and CANCEL (or closing) reverts everything the
// browsing changed. A state that arrives from outside while the browser is open (a host
// project restore or program change) makes the snapshot stale: the editor then closes the
// browser keeping that state (see mw12FEditor::controllerChanged and isLoading()). Saving,
// renaming and deleting work on user files; factory files are read only. Import and export go
// through asynchronous file choosers the browser owns.
//
// The search box and the save fields are text editors, so they take keyboard focus when
// clicked; closing the browser gives it back.
#include "kit/Buttons.h"
#include "kit/Switches.h"
#include "Controller.h"
#include <memory>

namespace acid::ui
{
class PresetBrowser final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit PresetBrowser (plugin::Controller&);
    ~PresetBrowser() override;

    /** Opens on a type. saveMode starts with the save strip expanded. */
    void open (PresetType type, bool saveMode = false);
    void close (bool keep);
    bool isOpenFor (PresetType type) const noexcept { return isVisible() && currentType == type; }

    /** True while the browser itself is loading a preset through the controller. The owner
        uses it to tell the browser's own changes from state that arrived from outside (a host
        project restore), which makes the audition snapshot stale. */
    bool isLoading() const noexcept { return loading; }

    /** The library changed (a save, delete, import or favourite): rebuilds the list. */
    void refresh();

    // ---- Tag filter (also for tests and the gallery) ----
    /** Group captions shown for the current type, top to bottom. */
    juce::StringArray getTagGroups() const;
    /** Selects or clears a tag (its stored spelling). Returns false when the type has no such tag. */
    bool setTagSelected (const juce::String& tag, bool selected);
    /** A group's switch: true = ALL of its selected tags, false = ANY. */
    void setGroupMatchesAll (const juce::String& group, bool all);
    void clearTags();
    /** Rows in the list. */
    int getNumShown() const noexcept { return static_cast<int> (entries.size()); }
    /** The group a tag is shown under for a type, and its shown name. */
    static juce::String tagGroupFor (PresetType, const juce::String& tag);
    static juce::String tagDisplayName (const juce::String& group, const juce::String& tag);

    /** The owner hides the overlay. */
    std::function<void()> onClose;
    /** Short message for the owner's notice line. */
    std::function<void (const juce::String&)> onNotice;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void lookAndFeelChanged() override;

    /** .mw12f files dropped on the browser are imported into the user library of their type. */
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class List;
    class TagPanel;
    enum class Strip { None, Save, Rename, Delete };

    struct TagGroup
    {
        juce::String caption;
        bool all = false;
        std::vector<std::string> tags;       // stored spelling, in vocabulary order
        std::vector<std::string> selected;
    };

    bool passesTags (const plugin::PresetEntry&) const;

    void applyTheme();
    void setType (PresetType);
    void rebuildTags();
    void rebuildList (const plugin::PresetEntry* keepSelected);
    void selectRow (int row, bool load);
    void stepPreset (int direction);
    const plugin::PresetEntry* selectedEntry() const noexcept;
    void showStrip (Strip);
    void commitStrip();
    void updateButtons();
    void styleField (juce::TextEditor&);
    void importFile();
    void exportSelected();
    juce::Rectangle<int> infoArea, stripArea, headerArea, tagsCaption;

    plugin::Controller& controller;
    PresetType currentType = PresetType::Scene;
    plugin::PresetFilter filter;
    std::vector<const plugin::PresetEntry*> entries;
    std::vector<TagGroup> groups;
    int selected = -1;
    Strip strip = Strip::None;
    bool overwriteArmed = false, loading = false;

    static constexpr int kNumTypes = 12;
    juce::OwnedArray<PushButton> typeButtons;
    LedButton factoryToggle { "FACTORY", LedColour::Accent, LedButton::Style::LensLabel };
    LedButton userToggle { "USER", LedColour::Accent, LedButton::Style::LensLabel };
    LedButton favouritesToggle { "FAVOURITES", LedColour::Amber, LedButton::Style::LensLabel };
    juce::TextEditor search;
    PushButton clearSearch { {}, draw::Icon::None };
    PushButton clearTagsButton { "CLEAR TAGS" };
    std::unique_ptr<TagPanel> tagPanel;
    juce::Viewport tagView;
    std::unique_ptr<List> list;
    juce::Viewport listView;

    PushButton prevButton { {}, draw::Icon::Prev }, nextButton { {}, draw::Icon::Next };
    PushButton okButton { "OK" }, cancelButton { "CANCEL" };
    LedButton favButton { "FAVOURITE", LedColour::Amber, LedButton::Style::Cap };
    PushButton saveButton { "SAVE..." }, renameButton { "RENAME..." }, deleteButton { "DELETE..." };
    PushButton importButton { "IMPORT..." }, exportButton { "EXPORT..." };

    juce::TextEditor nameField, authorField, tagsField, descriptionField;
    // BACK closes the strip; the browser's own CANCEL (below) reverts the audition.
    PushButton stripOk { "SAVE" }, stripCancel { "BACK" };
    juce::String stripMessage;

    std::unique_ptr<juce::FileChooser> chooser;
};
}
