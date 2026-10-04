#include "editor/PresetBrowser.h"
#include "kit/Draw.h"
#include "kit/SafeText.h"

namespace acid::ui
{
namespace
{
constexpr int kPad = 12, kGap = 8, kLeftW = 150, kMidW = 236, kRowH = 22, kButtonH = 24;

struct TypeInfo { PresetType type; const char* label; };
constexpr TypeInfo kTypes[12] = {
    { PresetType::Scene, "SCENE" },           { PresetType::Synth, "SYNTH" },     { PresetType::Calibration, "CALIBRATION" },
    { PresetType::Distortion, "DISTORTION" }, { PresetType::Eq, "EQ" },           { PresetType::Delay, "DELAY" },
    { PresetType::Reverb, "REVERB" },         { PresetType::Chorus, "CHORUS" },   { PresetType::Limiter, "LIMITER" },
    { PresetType::Pattern, "PATTERN" },       { PresetType::Arp, "ARP PATTERN" }, { PresetType::Template, "TEMPLATE" }
};

juce::String joinTags (const std::vector<std::string>& tags)
{
    juce::String out;

    for (const auto& t : tags)
        out << (out.isEmpty() ? "" : ", ") << text::from (t);

    return out;
}

// The factory tag vocabulary (assets/factory/README.md, "Tags"), group by group, with the
// preset types each group is used on. A tag in two groups would show under the first group
// its preset type uses; the factory vocabulary keeps them apart (the families Deep Bass and
// Liquid Line, the characters Deep and Liquid).
using T = PresetType;
struct VocabularyGroup
{
    const char* caption;
    std::vector<PresetType> types;      // vectors, not initializer_lists: these outlive the braces
    std::vector<const char*> tags;
};

const std::vector<VocabularyGroup>& vocabulary()
{
    static const std::vector<VocabularyGroup> v = {
        { "PLAY MODE", { T::Scene, T::Arp }, { "Seq", "Arp" } },
        { "STYLE", { T::Scene, T::Pattern, T::Template },
          { "Showcase", "Chicago", "Anthem", "Rolling", "Melodic", "Minimal", "Hard", "Trance", "Dub", "Breaks", "Experimental", "Polymeter",
            "Teaching" } },
        { "FAMILY", { T::Synth },
          { "Stock", "Squelch", "Drive", "Deep Bass", "Scream", "Whistle", "Liquid Line", "Pluck", "Space", "Overdrive", "Filter FM", "Tracking", "Sub",
            "Sustain", "Utility" } },
        { "WAVEFORM", { T::Scene, T::Synth }, { "Saw", "Square" } },
        { "DISTORTION", { T::Scene, T::Synth }, { "None", "Light", "Medium", "Strong" } },
        { "CHARACTER", { T::Scene, T::Synth, T::Calibration },
          { "Squelchy", "Screaming", "Deep", "Whistling", "Plucky", "Liquid", "Dark", "Bright", "Gritty", "Rubbery", "Talking", "Percussive",
            "Droning", "Chaotic", "Warm", "Hollow", "Spacey", "Clean", "Noisy", "Vintage", "Aggressive", "Smooth" } },
        { "MODS", { T::Scene, T::Synth, T::Calibration, T::Distortion, T::Eq, T::Delay, T::Reverb, T::Chorus, T::Limiter, T::Pattern, T::Arp,
                    T::Template }, { "Mods" } },
        { "PATTERN", { T::Pattern, T::Arp },
          { "Main", "Variation", "Generated", "One Note", "Two Note", "Melodic Line", "All Accent", "Sparse Accent", "Slides", "Octave Jumps",
            "Ties", "Rests", "Shuffle", "Triplet", "Odd Length", "Long", "Half Time", "Vibrato", "Ratchet" } },
        { "ARP SHAPE", { T::Arp }, { "Up", "Down", "Up Down", "Chord", "Pedal", "Random Walk", "Octave Spread" } },
        { "UNIT", { T::Calibration }, { "Stock Unit", "Unit Variation", "Worn", "Extended Range" } },
        { "EFFECT", { T::Distortion, T::Eq, T::Delay, T::Reverb, T::Chorus, T::Limiter },
          { "Short", "Long", "Dotted", "Wide", "Filtered", "Tempo Sync", "Free Time", "Room", "Plate", "Hall", "Subtle", "Heavy", "Glue", "Safety",
            "Scoop", "Boost", "Cut", "Custom Voicing", "Pump", "Motion", "Clean Low" } },
        { "TEMPLATE", { T::Template }, { "Scale", "Pentatonic", "Chord", "Interval", "Acid", "Other" } },
    };
    return v;
}

constexpr const char* kUserGroup = "USER";

/** Position of a tag inside its vocabulary group (user tags sort after, by name). */
int vocabularyIndex (const juce::String& group, const juce::String& tag)
{
    for (const auto& g : vocabulary())
        if (group == g.caption)
        {
            int i = 0;

            for (const auto* t : g.tags)
            {
                if (tag.equalsIgnoreCase (t))
                    return i;

                ++i;
            }
        }

    return 1000;
}

bool containsTag (const std::vector<std::string>& tags, const std::string& tag)
{
    return std::find (tags.begin(), tags.end(), tag) != tags.end();
}
}

//==============================================================================
/** The tag groups: a caption, an ANY/ALL switch and the chips of each group, in a viewport. */
class PresetBrowser::TagPanel final : public juce::Component
{
public:
    explicit TagPanel (PresetBrowser& b) : owner (b)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void rebuild()
    {
        chips.clear();
        switches.clear();
        chipGroup.clear();

        for (std::size_t gi = 0; gi < owner.groups.size(); ++gi)
        {
            auto& group = owner.groups[gi];
            auto* sw = switches.add (new PushButton (group.all ? "ALL" : "ANY"));
            sw->setTooltip ("Within " + group.caption.toLowerCase() + ": ANY of its selected tags, or ALL of them. A preset must pass "
                            "every group that has a selection.");
            sw->onClick = [this, gi, sw]
            {
                auto& g = owner.groups[gi];
                g.all = ! g.all;
                sw->setButtonText (g.all ? "ALL" : "ANY");
                owner.rebuildList (owner.selectedEntry());
            };
            sw->setEnabled (group.tags.size() > 1);
            addAndMakeVisible (sw);

            for (const auto& tag : group.tags)
            {
                auto* chip = chips.add (new PushButton (tagDisplayName (group.caption, text::from (tag))));
                chipGroup.push_back (gi);
                chip->setClickingTogglesState (true);
                chip->setToggleState (containsTag (group.selected, tag), juce::dontSendNotification);
                chip->onClick = [this, gi, tag, chip]
                {
                    auto& sel = owner.groups[gi].selected;
                    const auto it = std::find (sel.begin(), sel.end(), tag);

                    if (chip->getToggleState() && it == sel.end())
                        sel.push_back (tag);
                    else if (! chip->getToggleState() && it != sel.end())
                        sel.erase (it);

                    owner.rebuildList (owner.selectedEntry());
                };
                addAndMakeVisible (chip);
            }
        }

        layout (getWidth());
    }

    /** Lays the groups out at `width` and sets the height they need. */
    void layout (int width)
    {
        const auto chipFont = fonts::legend (11.0f);
        captions.clear();
        int y = 0, chipIndex = 0;

        for (std::size_t gi = 0; gi < owner.groups.size(); ++gi)
        {
            const juce::Rectangle<int> captionRow (0, y, width, 18);
            captions.push_back (captionRow);
            switches[static_cast<int> (gi)]->setBounds (captionRow.withLeft (width - 40).reduced (0, 1));
            y += 21;
            int x = 0;

            for (std::size_t t = 0; t < owner.groups[gi].tags.size(); ++t, ++chipIndex)
            {
                auto* chip = chips[chipIndex];
                const auto w = juce::jmin (width, juce::roundToInt (juce::GlyphArrangement::getStringWidth (chipFont, chip->getButtonText())) + 18);

                if (x > 0 && x + w > width)
                {
                    x = 0;
                    y += 24;
                }

                chip->setBounds (x, y, w, 21);
                x += w + 4;
            }

            y += 24 + 8;
        }

        setSize (width, juce::jmax (1, y));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = themeFor (*this);
        const auto font = fonts::legend (10.0f, 0.08f);

        for (std::size_t gi = 0; gi < captions.size() && gi < owner.groups.size(); ++gi)
        {
            const auto row = captions[gi].toFloat();
            const auto& group = owner.groups[gi];
            const auto caption = group.caption + (group.selected.empty() ? juce::String() : "  " + juce::String (static_cast<int> (group.selected.size())));
            draw::text (g, caption, row.withTrimmedRight (46.0f), font, group.selected.empty() ? t.screenTextDim : t.accent,
                        juce::Justification::centredLeft);
            g.setColour (t.screenGridBar.withAlpha (0.6f));
            g.fillRect (row.getX(), row.getBottom() + 1.0f, row.getWidth() - 46.0f, 1.0f);
        }
    }

private:
    PresetBrowser& owner;
    juce::OwnedArray<PushButton> chips, switches;
    std::vector<std::size_t> chipGroup;
    std::vector<juce::Rectangle<int>> captions;
};

//==============================================================================
juce::String PresetBrowser::tagGroupFor (PresetType type, const juce::String& tag)
{
    for (const auto& g : vocabulary())
        if (std::find (g.types.begin(), g.types.end(), type) != g.types.end())
            for (const auto* t : g.tags)
                if (tag.equalsIgnoreCase (t))
                    return g.caption;

    return kUserGroup;
}

juce::String PresetBrowser::tagDisplayName (const juce::String& group, const juce::String& tag)
{
    // "None" alone reads as "no tag": under DISTORTION it is the absence of drive.
    if (group == "DISTORTION" && tag.equalsIgnoreCase ("None"))
        return "Off";

    return tag;
}

//==============================================================================
class PresetBrowser::List final : public juce::Component
{
public:
    explicit List (PresetBrowser& b) : owner (b)
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void refresh()
    {
        setSize (juce::jmax (getParentWidth(), 10), juce::jmax (1, static_cast<int> (owner.entries.size())) * kRowH);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = themeFor (*this);
        g.fillAll (t.wellFill);
        const auto clip = g.getClipBounds();
        const auto first = juce::jmax (0, clip.getY() / kRowH);
        const auto last = juce::jmin (static_cast<int> (owner.entries.size()) - 1, clip.getBottom() / kRowH);
        const auto nameFont = fonts::text (13.0f);
        const auto small = fonts::legend (9.5f, 0.06f);

        if (owner.entries.empty())
        {
            draw::text (g, "No presets match", getLocalBounds().toFloat().withHeight (static_cast<float> (kRowH)).reduced (8.0f, 0.0f), nameFont,
                        t.wellTextDim, juce::Justification::centredLeft);
            return;
        }

        for (int i = first; i <= last; ++i)
        {
            const auto* e = owner.entries[static_cast<std::size_t> (i)];
            auto row = juce::Rectangle<float> (0.0f, static_cast<float> (i * kRowH), static_cast<float> (getWidth()), static_cast<float> (kRowH));
            const auto isSelected = i == owner.selected;

            if (isSelected)
            {
                g.setColour (t.accent);
                g.fillRect (row);
            }
            else if (i % 2 == 1)
            {
                g.setColour (t.screenRow.withAlpha (0.35f));
                g.fillRect (row);
            }

            const auto ink = isSelected ? t.accentInk : t.wellText;
            const auto dim = isSelected ? t.accentInk.withAlpha (0.7f) : t.wellTextDim;
            row.reduce (8.0f, 0.0f);

            // Favourite mark, source badge, name, author.
            auto left = row.removeFromLeft (14.0f);

            if (e->favourite)
            {
                g.setColour (isSelected ? t.accentInk : t.ledAmber);
                g.fillEllipse (left.withSizeKeepingCentre (7.0f, 7.0f));
            }

            draw::text (g, e->factory ? "F" : "U", row.removeFromLeft (16.0f), small, dim, juce::Justification::centredLeft);
            const auto author = text::from (e->author);
            const auto authorW = author.isEmpty() ? 0.0f : juce::jmin (160.0f, juce::GlyphArrangement::getStringWidth (small, author) + 6.0f);
            draw::text (g, author, row.removeFromRight (authorW), small, dim, juce::Justification::centredRight);
            draw::text (g, text::from (e->name), row, nameFont, ink, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        const auto row = static_cast<int> (e.position.y) / kRowH;

        if (row >= 0 && row < static_cast<int> (owner.entries.size()))
            owner.selectRow (row, true);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        const auto row = static_cast<int> (e.position.y) / kRowH;

        if (row >= 0 && row < static_cast<int> (owner.entries.size()))
        {
            owner.selectRow (row, true);
            owner.close (true);
        }
    }

private:
    PresetBrowser& owner;
};

//==============================================================================
PresetBrowser::PresetBrowser (plugin::Controller& c) : controller (c)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setVisible (false);

    for (int i = 0; i < kNumTypes; ++i)
    {
        auto* b = typeButtons.add (new PushButton (kTypes[i].label));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { setType (kTypes[i].type); };
        addAndMakeVisible (b);
    }

    for (auto* toggle : { &factoryToggle, &userToggle, &favouritesToggle })
    {
        toggle->setSurface (Surface::Dark);
        addAndMakeVisible (toggle);
    }

    factoryToggle.setToggleState (true, juce::dontSendNotification);
    userToggle.setToggleState (true, juce::dontSendNotification);
    factoryToggle.onClick = [this] { filter.includeFactory = factoryToggle.getToggleState(); rebuildList (selectedEntry()); };
    userToggle.onClick = [this] { filter.includeUser = userToggle.getToggleState(); rebuildList (selectedEntry()); };
    favouritesToggle.onClick = [this] { filter.favouritesOnly = favouritesToggle.getToggleState(); rebuildList (selectedEntry()); };

    styleField (search);
    search.onTextChange = [this] { filter.search = search.getText().trim().toStdString(); rebuildList (selectedEntry()); };
    search.onEscapeKey = [this] { search.setText ({}); juce::Component::unfocusAllComponents(); };
    search.onReturnKey = [] { juce::Component::unfocusAllComponents(); };
    addAndMakeVisible (search);

    clearSearch.setButtonText ("X");
    clearSearch.onClick = [this] { search.setText ({}); };
    addAndMakeVisible (clearSearch);

    clearTagsButton.onClick = [this] { clearTags(); };
    clearTagsButton.setTooltip ("Clear every tag selection.");
    addAndMakeVisible (clearTagsButton);

    tagPanel = std::make_unique<TagPanel> (*this);
    tagView.setViewedComponent (tagPanel.get(), false);
    tagView.setScrollBarsShown (true, false);
    tagView.setScrollBarThickness (8);
    tagView.setWantsKeyboardFocus (false);
    addAndMakeVisible (tagView);

    list = std::make_unique<List> (*this);
    listView.setViewedComponent (list.get(), false);
    listView.setScrollBarsShown (true, false);
    listView.setScrollBarThickness (10);
    listView.setWantsKeyboardFocus (false);
    addAndMakeVisible (listView);

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    okButton.setProminent (true);
    okButton.onClick = [this] { close (true); };
    cancelButton.onClick = [this] { close (false); };
    favButton.setClickingTogglesState (true);
    favButton.onClick = [this]
    {
        if (const auto* e = selectedEntry())
        {
            controller.presets().setFavourite (e->id, favButton.getToggleState());
            rebuildList (e);
        }
    };
    saveButton.onClick = [this] { showStrip (strip == Strip::Save ? Strip::None : Strip::Save); };
    renameButton.onClick = [this] { showStrip (strip == Strip::Rename ? Strip::None : Strip::Rename); };
    deleteButton.onClick = [this] { showStrip (strip == Strip::Delete ? Strip::None : Strip::Delete); };
    importButton.onClick = [this] { importFile(); };
    importButton.setTooltip ("Import a preset file into the user library (or drop .mw12f files onto the browser).");
    exportButton.setTooltip ("Export the selected preset as a file.");
    stripCancel.setTooltip ("Close these fields without saving. The browser's CANCEL reverts what browsing loaded.");
    favButton.setTooltip ("Mark the selected preset as a favourite (shared by every instance).");
    exportButton.onClick = [this] { exportSelected(); };

    for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &okButton, &cancelButton, &favButton, &saveButton,
                                                             &renameButton, &deleteButton, &importButton, &exportButton })
        addAndMakeVisible (b);

    for (auto* f : { &nameField, &authorField, &tagsField, &descriptionField })
    {
        styleField (*f);
        f->onReturnKey = [this] { commitStrip(); };
        f->onEscapeKey = [this] { showStrip (Strip::None); };
        addChildComponent (f);
    }

    stripOk.setProminent (true);
    stripOk.onClick = [this] { commitStrip(); };
    stripCancel.onClick = [this] { showStrip (Strip::None); };
    addChildComponent (stripOk);
    addChildComponent (stripCancel);
}

PresetBrowser::~PresetBrowser()
{
    listView.setViewedComponent (nullptr, false);
    tagView.setViewedComponent (nullptr, false);
}

void PresetBrowser::styleField (juce::TextEditor& f)
{
    f.setFont (fonts::text (13.0f));
    f.setMultiLine (false);
    f.setReturnKeyStartsNewLine (false);
    f.setSelectAllWhenFocused (true);
    f.setIndents (6, 4);
    f.setBorder ({ 1, 1, 1, 1 });
}

void PresetBrowser::open (PresetType type, bool saveMode)
{
    if (! isVisible())
    {
        controller.beginAudition();
        applyTheme();
        setVisible (true);
    }

    setType (type);
    showStrip (saveMode ? Strip::Save : Strip::None);
}

void PresetBrowser::close (bool keep)
{
    if (! isVisible())
        return;

    showStrip (Strip::None);
    juce::Component::unfocusAllComponents();
    controller.endAudition (keep);
    setVisible (false);

    if (onClose != nullptr)
        onClose();
}

void PresetBrowser::setType (PresetType type)
{
    const auto* keep = currentType == type ? selectedEntry() : nullptr;

    if (keep == nullptr)
        keep = controller.lastLoaded (type);

    if (keep == nullptr && type == PresetType::Scene)
    {
        // The initial scene is loaded as state, not through the library: find it by name.
        plugin::PresetFilter byType;
        byType.type = type;
        const auto name = controller.sceneName();

        for (const auto* e : controller.presets().list (byType))
            if (e->name == name)
            {
                keep = e;
                break;
            }
    }

    if (currentType != type)
        groups.clear();   // a new type starts with no tag selected

    currentType = type;
    filter.type = type;
    filter.tags.clear();

    for (int i = 0; i < kNumTypes; ++i)
        typeButtons[i]->setToggleState (kTypes[i].type == type, juce::dontSendNotification);

    rebuildTags();
    rebuildList (keep);
}

void PresetBrowser::refresh()
{
    if (! isVisible())
        return;

    rebuildTags();
    rebuildList (selectedEntry());
}

void PresetBrowser::rebuildTags()
{
    // Group the type's tags by the vocabulary, keeping the selections and switches of groups
    // that survive (a library change keeps what the user picked).
    const auto previous = groups;
    groups.clear();

    for (const auto& tag : controller.presets().tags (currentType))
    {
        const auto caption = tagGroupFor (currentType, text::from (tag));
        auto it = std::find_if (groups.begin(), groups.end(), [&] (const TagGroup& g) { return g.caption == caption; });

        if (it == groups.end())
        {
            groups.push_back ({ caption, false, {}, {} });
            it = std::prev (groups.end());
        }

        it->tags.push_back (tag);
    }

    const auto groupOrder = [] (const juce::String& caption)
    {
        int i = 0;

        for (const auto& g : vocabulary())
        {
            if (caption == g.caption)
                return i;

            ++i;
        }

        return i;   // USER last
    };

    std::sort (groups.begin(), groups.end(), [&] (const TagGroup& a, const TagGroup& b) { return groupOrder (a.caption) < groupOrder (b.caption); });

    for (auto& g : groups)
    {
        std::stable_sort (g.tags.begin(), g.tags.end(), [&] (const std::string& a, const std::string& b)
        {
            const auto ia = vocabularyIndex (g.caption, text::from (a)), ib = vocabularyIndex (g.caption, text::from (b));
            return ia != ib ? ia < ib : text::from (a).compareIgnoreCase (text::from (b)) < 0;
        });

        for (const auto& old : previous)
            if (old.caption == g.caption)
            {
                g.all = old.all;

                for (const auto& s : old.selected)
                    if (containsTag (g.tags, s))
                        g.selected.push_back (s);
            }
    }

    tagPanel->rebuild();
    resized();
}

bool PresetBrowser::passesTags (const plugin::PresetEntry& e) const
{
    for (const auto& g : groups)
    {
        if (g.selected.empty())
            continue;

        int hits = 0;

        for (const auto& tag : g.selected)
            hits += e.hasTag (tag) ? 1 : 0;

        if (g.all ? hits != static_cast<int> (g.selected.size()) : hits == 0)
            return false;
    }

    return true;
}

juce::StringArray PresetBrowser::getTagGroups() const
{
    juce::StringArray out;

    for (const auto& g : groups)
        out.add (g.caption);

    return out;
}

bool PresetBrowser::setTagSelected (const juce::String& tag, bool on)
{
    for (auto& g : groups)
        for (const auto& t : g.tags)
            if (tag.equalsIgnoreCase (text::from (t)))
            {
                const auto it = std::find (g.selected.begin(), g.selected.end(), t);

                if (on && it == g.selected.end())
                    g.selected.push_back (t);
                else if (! on && it != g.selected.end())
                    g.selected.erase (it);

                tagPanel->rebuild();
                rebuildList (selectedEntry());
                return true;
            }

    return false;
}

void PresetBrowser::setGroupMatchesAll (const juce::String& group, bool all)
{
    for (auto& g : groups)
        if (g.caption == group)
            g.all = all;

    tagPanel->rebuild();
    rebuildList (selectedEntry());
}

void PresetBrowser::clearTags()
{
    for (auto& g : groups)
        g.selected.clear();

    tagPanel->rebuild();
    rebuildList (selectedEntry());
}

void PresetBrowser::rebuildList (const plugin::PresetEntry* keepSelected)
{
    entries = controller.presets().list (filter);
    entries.erase (std::remove_if (entries.begin(), entries.end(), [this] (const plugin::PresetEntry* e) { return ! passesTags (*e); }),
                   entries.end());
    selected = -1;

    for (std::size_t i = 0; i < entries.size(); ++i)
        if (keepSelected != nullptr && entries[i]->id == keepSelected->id)
            selected = static_cast<int> (i);

    list->refresh();

    if (selected >= 0)
    {
        const auto rowTop = selected * kRowH;

        if (rowTop < listView.getViewPositionY() || rowTop + kRowH > listView.getViewPositionY() + listView.getViewHeight())
            listView.setViewPosition (0, juce::jmax (0, rowTop - listView.getViewHeight() / 2));
    }

    updateButtons();
    repaint (infoArea);
    repaint (headerArea);   // the count of rows shown
    tagPanel->repaint();    // the group captions count their selections
}

const plugin::PresetEntry* PresetBrowser::selectedEntry() const noexcept
{
    return selected >= 0 && selected < static_cast<int> (entries.size()) ? entries[static_cast<std::size_t> (selected)] : nullptr;
}

void PresetBrowser::selectRow (int row, bool load)
{
    if (row < 0 || row >= static_cast<int> (entries.size()))
        return;

    selected = row;
    list->repaint();

    if (load)
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        std::string error;

        if (! controller.loadPreset (*entries[static_cast<std::size_t> (row)], &error) && onNotice != nullptr)
            onNotice (text::from (error));
    }

    updateButtons();
    repaint (infoArea);
}

void PresetBrowser::stepPreset (int direction)
{
    if (entries.empty())
        return;

    const auto n = static_cast<int> (entries.size());
    const auto row = selected < 0 ? (direction > 0 ? 0 : n - 1) : ((selected + direction) % n + n) % n;
    selectRow (row, true);
    const auto rowTop = row * kRowH;

    if (rowTop < listView.getViewPositionY() || rowTop + kRowH > listView.getViewPositionY() + listView.getViewHeight())
        listView.setViewPosition (0, juce::jmax (0, rowTop - listView.getViewHeight() / 2));
}

void PresetBrowser::updateButtons()
{
    const auto* e = selectedEntry();
    const auto user = e != nullptr && ! e->factory;
    favButton.setEnabled (e != nullptr);
    favButton.setToggleState (e != nullptr && e->favourite, juce::dontSendNotification);
    renameButton.setEnabled (user);
    deleteButton.setEnabled (user);
    exportButton.setEnabled (e != nullptr);
    prevButton.setEnabled (! entries.empty());
    nextButton.setEnabled (! entries.empty());
}

void PresetBrowser::showStrip (Strip s)
{
    strip = s;
    overwriteArmed = false;
    stripMessage.clear();
    const auto* e = selectedEntry();

    if (s == Strip::Save)
    {
        if (currentType == PresetType::Scene)
        {
            nameField.setText (text::from (controller.sceneName()), false);
        }
        else
        {
            const auto* last = controller.lastLoaded (currentType);
            nameField.setText (last != nullptr ? text::from (last->name) : juce::String(), false);
        }

        if (e != nullptr && ! e->factory)
        {
            authorField.setText (text::from (e->author), false);
            tagsField.setText (joinTags (e->tags), false);
            descriptionField.setText (text::from (e->description), false);
        }

        stripOk.setButtonText ("SAVE");
    }
    else if (s == Strip::Rename && e != nullptr)
    {
        nameField.setText (text::from (e->name), false);
        stripOk.setButtonText ("RENAME");
    }
    else if (s == Strip::Delete && e != nullptr)
    {
        stripMessage = "Delete \"" + text::from (e->name) + "\" from the user library?";
        stripOk.setButtonText ("DELETE");
    }

    const auto fields = s == Strip::Save;
    nameField.setVisible (s == Strip::Save || s == Strip::Rename);
    authorField.setVisible (fields);
    tagsField.setVisible (fields);
    descriptionField.setVisible (fields);
    stripOk.setVisible (s != Strip::None);
    stripCancel.setVisible (s != Strip::None);
    saveButton.setToggleState (s == Strip::Save, juce::dontSendNotification);
    renameButton.setToggleState (s == Strip::Rename, juce::dontSendNotification);
    deleteButton.setToggleState (s == Strip::Delete, juce::dontSendNotification);

    if (s == Strip::None)
        juce::Component::unfocusAllComponents();

    resized();
    repaint();
}

void PresetBrowser::commitStrip()
{
    std::string error;
    const auto* e = selectedEntry();

    if (strip == Strip::Save)
    {
        const auto name = nameField.getText().trim();

        if (name.isEmpty())
        {
            stripMessage = "A name is needed";
            repaint (stripArea);
            return;
        }

        const auto existing = controller.presets().userFileFor (currentType, name.toStdString());

        if (std::filesystem::exists (existing) && ! overwriteArmed)
        {
            overwriteArmed = true;
            stripMessage = "\"" + name + "\" exists: SAVE again to replace it";
            repaint (stripArea);
            return;
        }

        const auto* saved = controller.savePreset (currentType, name.toStdString(), authorField.getText().trim().toStdString(),
                                                   tagsField.getText().trim().toStdString(), descriptionField.getText().trim().toStdString(),
                                                   overwriteArmed, &error);

        if (saved == nullptr)
        {
            stripMessage = text::from (error);
            overwriteArmed = false;
            repaint (stripArea);
            return;
        }

        showStrip (Strip::None);
        rebuildTags();
        rebuildList (saved);

        if (onNotice != nullptr)
            onNotice ("Saved " + name);
    }
    else if (strip == Strip::Rename && e != nullptr)
    {
        const auto name = nameField.getText().trim();

        if (name.isEmpty() || ! controller.presets().renameUser (*e, name.toStdString(), error))
        {
            stripMessage = name.isEmpty() ? juce::String ("A name is needed") : text::from (error);
            repaint (stripArea);
            return;
        }

        showStrip (Strip::None);
        const auto* renamed = controller.presets().find ("user:" + plugin::PresetManager::typeDirectoryName (currentType) + "/"
                                                         + plugin::PresetManager::safeFileName (name.toStdString()) + ".mw12f");
        rebuildList (renamed);
    }
    else if (strip == Strip::Delete && e != nullptr)
    {
        if (! controller.presets().deleteUser (*e, error))
        {
            stripMessage = text::from (error);
            repaint (stripArea);
            return;
        }

        showStrip (Strip::None);
        rebuildTags();
        rebuildList (nullptr);
    }
}

void PresetBrowser::importFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.mw12f", false);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        std::string error;
        const auto* imported = safe->controller.importPresetFile (fc.getResult().getFullPathName().toStdString(), &error);

        if (imported == nullptr)
        {
            if (safe->onNotice != nullptr)
                safe->onNotice (text::from (error));

            return;
        }

        safe->setType (imported->type);
        safe->rebuildList (imported);
    });
}

bool PresetBrowser::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("mw12f"))
            return true;

    return false;
}

void PresetBrowser::filesDropped (const juce::StringArray& files, int, int)
{
    const plugin::PresetEntry* last = nullptr;
    int count = 0;
    juce::String firstError;

    for (const auto& f : files)
    {
        if (! juce::File (f).hasFileExtension ("mw12f"))
            continue;

        std::string error;

        if (const auto* imported = controller.importPresetFile (f.toStdString(), &error))
        {
            last = imported;
            ++count;
        }
        else if (firstError.isEmpty())
        {
            firstError = text::from (error);
        }
    }

    if (last != nullptr)
    {
        setType (last->type);
        rebuildList (last);
    }

    if (onNotice != nullptr)
    {
        if (count > 0)
            onNotice ("Imported " + juce::String (count) + (count == 1 ? " preset" : " presets"));
        else if (firstError.isNotEmpty())
            onNotice (firstError);
    }
}

void PresetBrowser::exportSelected()
{
    const auto* e = selectedEntry();

    if (e == nullptr)
        return;

    const auto suggested = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                               .getChildFile (text::from (plugin::PresetManager::safeFileName (e->name)) + ".mw12f");
    chooser = std::make_unique<juce::FileChooser> ("Export preset", suggested, "*.mw12f", false);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    const std::string id = e->id;
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, id] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto* entry = safe->controller.presets().find (id);
        std::string error;

        if (entry == nullptr || ! safe->controller.exportPreset (*entry, fc.getResult().getFullPathName().toStdString(), &error))
        {
            if (safe->onNotice != nullptr)
                safe->onNotice (error.empty() ? juce::String ("Export failed") : text::from (error));
        }
        else if (safe->onNotice != nullptr)
        {
            safe->onNotice ("Exported " + fc.getResult().getFileName());
        }
    });
}

void PresetBrowser::resized()
{
    auto b = getLocalBounds().reduced (kPad);
    headerArea = b.removeFromTop (26);
    b.removeFromTop (kGap);

    // Left: types, then sources.
    auto left = b.removeFromLeft (kLeftW);
    b.removeFromLeft (kGap);

    for (auto* t : typeButtons)
    {
        t->setBounds (left.removeFromTop (kButtonH));
        left.removeFromTop (3);
    }

    // Sources go under the types (the overlay is always the full plate under the top bar).
    left.removeFromTop (10);

    for (auto* toggle : { &factoryToggle, &userToggle, &favouritesToggle })
    {
        toggle->setBounds (left.removeFromTop (20));
        left.removeFromTop (2);
    }

    // Middle: search and tags.
    auto mid = b.removeFromLeft (kMidW);
    b.removeFromLeft (kGap);
    auto searchRow = mid.removeFromTop (kButtonH);
    clearSearch.setBounds (searchRow.removeFromRight (24));
    searchRow.removeFromRight (3);
    search.setBounds (searchRow);
    mid.removeFromTop (kGap);
    tagsCaption = mid.removeFromTop (22);
    clearTagsButton.setBounds (tagsCaption.removeFromRight (92));
    mid.removeFromTop (6);

    // Tag groups scroll in their own view when they outgrow the column.
    tagView.setBounds (mid);
    tagPanel->layout (mid.getWidth() - 10);

    // Right: list, info, strip, actions.
    auto right = b;
    auto actions = right.removeFromBottom (kButtonH);
    right.removeFromBottom (kGap);
    auto actions2 = right.removeFromBottom (kButtonH);
    right.removeFromBottom (kGap);

    const auto stripH = strip == Strip::Save ? 88 : (strip == Strip::None ? 0 : 30);
    stripArea = right.removeFromBottom (stripH);

    if (stripH > 0)
        right.removeFromBottom (kGap);

    infoArea = right.removeFromBottom (juce::jmin (64, juce::jmax (40, right.getHeight() / 4)));
    right.removeFromBottom (kGap);
    listView.setBounds (right);
    list->refresh();

    // Strip layout.
    {
        auto s = stripArea;

        if (strip == Strip::Save)
        {
            auto row1 = s.removeFromTop (kButtonH);
            s.removeFromTop (4);
            auto row2 = s.removeFromTop (kButtonH);
            s.removeFromTop (4);
            auto row3 = s.removeFromTop (kButtonH);
            stripCancel.setBounds (row1.removeFromRight (70));
            row1.removeFromRight (4);
            stripOk.setBounds (row1.removeFromRight (70));
            row1.removeFromRight (kGap);
            nameField.setBounds (row1);
            authorField.setBounds (row2.removeFromLeft (row2.getWidth() / 3));
            row2.removeFromLeft (4);
            tagsField.setBounds (row2);
            descriptionField.setBounds (row3);
        }
        else if (strip == Strip::Rename)
        {
            auto row = s.removeFromTop (kButtonH);
            stripCancel.setBounds (row.removeFromRight (70));
            row.removeFromRight (4);
            stripOk.setBounds (row.removeFromRight (80));
            row.removeFromRight (kGap);
            nameField.setBounds (row);
        }
        else if (strip == Strip::Delete)
        {
            auto row = s.removeFromTop (kButtonH);
            stripCancel.setBounds (row.removeFromRight (70));
            row.removeFromRight (4);
            stripOk.setBounds (row.removeFromRight (80));
        }
    }

    // Action rows: navigation and keep/revert on the first, file operations on the second.
    prevButton.setBounds (actions2.removeFromLeft (28));
    actions2.removeFromLeft (3);
    nextButton.setBounds (actions2.removeFromLeft (28));
    actions2.removeFromLeft (kGap);
    favButton.setBounds (actions2.removeFromLeft (110));
    cancelButton.setBounds (actions2.removeFromRight (84));
    actions2.removeFromRight (4);
    okButton.setBounds (actions2.removeFromRight (84));

    saveButton.setBounds (actions.removeFromLeft (84));
    actions.removeFromLeft (4);
    renameButton.setBounds (actions.removeFromLeft (84));
    actions.removeFromLeft (4);
    deleteButton.setBounds (actions.removeFromLeft (84));
    exportButton.setBounds (actions.removeFromRight (84));
    actions.removeFromRight (4);
    importButton.setBounds (actions.removeFromRight (84));
}

void PresetBrowser::applyTheme()
{
    const auto& t = themeFor (*this);

    struct Placeholder { juce::TextEditor* field; const char* text; };
    const Placeholder fields[] = { { &search, "Search name, author, tags" }, { &nameField, "Name" }, { &authorField, "Author" },
                                   { &tagsField, "Tags, comma separated" }, { &descriptionField, "Description" } };

    for (const auto& [f, placeholder] : fields)
    {
        f->setColour (juce::TextEditor::backgroundColourId, t.wellFill);
        f->setColour (juce::TextEditor::textColourId, t.wellText);
        f->setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
        f->setColour (juce::TextEditor::highlightedTextColourId, t.accentBright);
        f->setColour (juce::TextEditor::outlineColourId, t.wellOutline);
        f->setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        f->setColour (juce::CaretComponent::caretColourId, t.accentBright);
        f->setTextToShowWhenEmpty (placeholder, t.wellTextDim);
        f->applyColourToAllText (t.wellText, true);
    }
}

void PresetBrowser::lookAndFeelChanged()
{
    applyTheme();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    draw::screen (g, getLocalBounds().toFloat(), t);

    const auto title = fonts::bold (metrics::kTitleHeight, 0.14f);
    auto header = headerArea.toFloat();
    draw::text (g, "PRESETS", header.removeFromLeft (90.0f), title, t.screenText, juce::Justification::centredLeft);

    juce::String typeName;

    for (const auto& ti : kTypes)
        if (ti.type == currentType)
            typeName = ti.label;

    draw::text (g, typeName + "   " + juce::String (static_cast<int> (entries.size())) + " SHOWN", header, fonts::legend (10.5f, 0.08f),
                t.screenTextDim, juce::Justification::centredLeft);

    draw::text (g, "CLICK A PRESET TO AUDITION, OK KEEPS IT, CANCEL REVERTS", header, fonts::legend (10.0f, 0.08f), t.screenTextDim,
                juce::Justification::centredRight);

    // Tag caption left of the clear button.
    draw::text (g, groups.empty() ? "NO TAGS" : "TAGS BY GROUP", tagsCaption.toFloat(), fonts::legend (10.5f), t.screenText,
                juce::Justification::centredLeft);

    // Info pane.
    draw::well (g, infoArea.toFloat(), t);
    auto info = infoArea.toFloat().reduced (8.0f, 4.0f);

    if (const auto* e = selectedEntry())
    {
        auto line1 = info.removeFromTop (18.0f);
        draw::text (g, text::from (e->name), line1.removeFromLeft (info.getWidth() * 0.6f), fonts::bold (13.0f), t.wellText,
                    juce::Justification::centredLeft);
        draw::text (g, text::from (e->author) + (e->factory ? "   FACTORY" : "   USER"), line1, fonts::legend (10.0f, 0.06f), t.wellTextDim,
                    juce::Justification::centredRight);
        draw::text (g, joinTags (e->tags), info.removeFromTop (16.0f), fonts::legend (10.0f, 0.04f), t.accentBright, juce::Justification::centredLeft);
        g.setColour (t.wellText.withAlpha (0.85f));
        g.setFont (fonts::text (12.0f));
        g.drawFittedText (text::from (e->description), info.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
    else
    {
        draw::text (g, "Nothing selected", info, fonts::text (12.5f), t.wellTextDim, juce::Justification::centredLeft);
    }

    // Strip message (errors, confirmations).
    if (strip != Strip::None && stripMessage.isNotEmpty())
    {
        const auto area = strip == Strip::Delete
                              ? stripArea.toFloat().withTrimmedRight (160.0f).withHeight (static_cast<float> (kButtonH))
                              : stripArea.toFloat().withTrimmedTop (static_cast<float> (stripArea.getHeight()) - 16.0f).withTrimmedLeft (0.0f);
        draw::text (g, stripMessage, area, fonts::text (12.0f), t.ledAmber, juce::Justification::centredLeft);
    }

    const juce::Rectangle<float> sourcesCaption (static_cast<float> (factoryToggle.getX()), static_cast<float> (factoryToggle.getY()) - 16.0f, 100.0f, 14.0f);
    draw::text (g, "SOURCES", sourcesCaption, fonts::legend (10.0f, 0.08f), t.screenTextDim, juce::Justification::centredLeft);
}

void PresetBrowser::mouseDown (const juce::MouseEvent&)
{
    // A click on the plain surface gives the keyboard back to the host.
    juce::Component::unfocusAllComponents();
}
}
