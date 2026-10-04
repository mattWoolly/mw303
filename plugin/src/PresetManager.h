#pragma once
// The preset library: factory files embedded in the binary (read-only) and user files on disk,
// for every type in SPEC 6.7, with search, tag filtering, favourites and the file operations
// the browser needs. JUCE-free (std::filesystem), so it is testable without a message thread.
//
// Locations (docs/research/07 section 5.4; $XDG_CONFIG_HOME is honoured, JUCE's own lookup
// is not):
//   user files        <userRoot>/<Type>/<name>.mw12f    userRoot = $XDG_CONFIG_HOME/mwAudio/mw12F
//   favourites        <globalDir>/favourites.txt         one preset id per line, shared by all instances
//
// Preset ids are stable strings: "factory:<path under assets/factory>" and "user:<path under
// userRoot>", so a favourite made on one machine survives a move of the config directory.
//
// Message thread only. Nothing here throws: every failure comes back as false plus a message.
#include "FactoryContent.h"
#include "acid/engine/StateCodec.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace acid::plugin
{
struct PresetEntry
{
    std::string id;                    // "factory:scene/Rubber Floor.mw12f" or "user:Scene/My Line.mw12f"
    PresetType type = PresetType::Scene;
    std::string name;
    std::string author;
    std::string description;
    std::vector<std::string> tags;     // split on ',' and trimmed
    bool factory = false;
    bool favourite = false;

    bool hasTag (std::string_view tag) const noexcept;   // case-insensitive
};

struct PresetFilter
{
    std::optional<PresetType> type;    // no value: every type
    std::string search;                // case-insensitive substring of name, author, tags or description
    std::vector<std::string> tags;     // empty: no tag filter
    bool tagsAll = false;              // true: every listed tag must be present; false: any of them
    bool favouritesOnly = false;
    bool includeFactory = true;
    bool includeUser = true;

    bool matches (const PresetEntry&) const noexcept;
};

class PresetManager
{
public:
    /** factory: the embedded table. userRoot: where user files live (created on first use).
        globalDir: where the favourites file lives (normally the same directory). */
    PresetManager (std::span<const FactoryFile> factory, std::filesystem::path userRoot, std::filesystem::path globalDir);

    /** $XDG_CONFIG_HOME/mwAudio/mw12F, or ~/.config/mwAudio/mw12F. */
    static std::filesystem::path defaultUserRoot();

    /** Directory name of a type ("Scene", "Synth", ... first letter capitalised). */
    static std::string typeDirectoryName (PresetType);

    /** Builds the catalogue: factory headers, user directory scan, favourites. Cheap to call
        again; call it after changing files behind the manager's back. */
    void rescan();
    bool scanned() const noexcept { return isScanned; }

    /** Entries passing the filter: factory first, then user, each sorted by name. An entry's
        address stays valid for the manager's lifetime as long as the preset exists under the
        same id: rescans update entries in place, and only a delete or a rename (which changes
        the id) drops one. */
    std::vector<const PresetEntry*> list (const PresetFilter& = {}) const;
    const PresetEntry* find (std::string_view id) const;

    /** Every distinct tag of the entries of that type (or of all types), sorted case-insensitively. */
    std::vector<std::string> tags (std::optional<PresetType> type = {}) const;

    /** The next (direction +1) or previous (-1) entry after `current` in the filtered list,
        wrapping; the first entry when current is null or not in the list. Null when empty. */
    const PresetEntry* step (const PresetEntry* current, const PresetFilter&, int direction) const;

    /** Reads and parses the file. */
    bool load (const PresetEntry&, PresetData& out, std::string& error) const;
    bool loadText (const PresetEntry&, std::string& text) const;

    /** Writes a user file for the data's type, named after data.name. An existing file of that
        name is replaced only with overwrite. Returns the new entry or nullptr with an error. */
    const PresetEntry* saveUser (const PresetData& data, bool overwrite, std::string& error);

    bool renameUser (const PresetEntry&, std::string_view newName, std::string& error);
    bool deleteUser (const PresetEntry&, std::string& error);

    /** Favourites are global (one file for every instance) and persist at once. */
    bool setFavourite (std::string_view id, bool favourite);
    bool isFavourite (std::string_view id) const;

    /** Copies a .mw12f dropped from outside into the user directory of its own type (the file's
        type attribute decides, not where it came from). Returns the new entry. */
    const PresetEntry* importFile (const std::filesystem::path& source, bool overwrite, std::string& error);

    /** Writes the entry's bytes to that path (the preset's own text, factory or user). */
    bool exportTo (const PresetEntry&, const std::filesystem::path& destination, std::string& error) const;

    const std::filesystem::path& userRoot() const noexcept { return root; }
    std::filesystem::path userDirectory (PresetType) const;

    /** The user file a name would be saved to for that type (without creating it). */
    std::filesystem::path userFileFor (PresetType, std::string_view name) const;

    /** A file-system safe version of a preset name (what userFileFor uses). */
    static std::string safeFileName (std::string_view name);

private:
    struct Stored
    {
        PresetEntry entry;
        const FactoryFile* factoryFile = nullptr;   // factory entries
        std::filesystem::path file;                 // user entries
    };

    static bool readHeader (std::string_view text, PresetEntry& into);
    void addFactory (std::vector<std::unique_ptr<Stored>>&) const;
    void addUser (std::vector<std::unique_ptr<Stored>>&) const;
    void loadFavourites();
    bool saveFavourites() const;
    void applyFavourites();
    const Stored* stored (const PresetEntry&) const;
    static std::string userId (const std::filesystem::path& root, const std::filesystem::path& file);

    std::span<const FactoryFile> factoryTable;
    std::filesystem::path root;
    std::filesystem::path global;
    std::vector<std::unique_ptr<Stored>> entries;
    std::vector<std::string> favourites;
    bool isScanned = false;
};
}
