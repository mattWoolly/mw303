#include "PresetManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace acid::plugin
{
namespace fs = std::filesystem;

namespace
{
std::string lowered (std::string_view s)
{
    std::string out (s);
    for (char& c : out)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return out;
}

std::string trimmed (std::string_view s)
{
    const auto notSpace = [] (unsigned char c) { return ! std::isspace (c); };
    while (! s.empty() && ! notSpace (static_cast<unsigned char> (s.front()))) s.remove_prefix (1);
    while (! s.empty() && ! notSpace (static_cast<unsigned char> (s.back()))) s.remove_suffix (1);
    return std::string (s);
}

std::vector<std::string> splitTags (std::string_view tags)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= tags.size())
    {
        const auto comma = tags.find (',', start);
        const auto piece = trimmed (tags.substr (start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (! piece.empty() && std::find (out.begin(), out.end(), piece) == out.end())
            out.push_back (piece);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

bool containsIgnoreCase (std::string_view hay, const std::string& loweredNeedle)
{
    return lowered (hay).find (loweredNeedle) != std::string::npos;
}

bool readFile (const fs::path& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile (const fs::path& path, std::string_view text, std::string& error)
{
    std::error_code ec;
    fs::create_directories (path.parent_path(), ec);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f || ! f.write (text.data(), static_cast<std::streamsize> (text.size())))
    {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool lessByName (const PresetEntry& a, const PresetEntry& b)
{
    const std::string la = lowered (a.name), lb = lowered (b.name);
    if (la != lb) return la < lb;
    return a.id < b.id;
}
}

// ---- PresetEntry / PresetFilter ------------------------------------------------------------

bool PresetEntry::hasTag (std::string_view tag) const noexcept
{
    const std::string want = lowered (tag);
    for (const auto& t : tags)
        if (lowered (t) == want)
            return true;
    return false;
}

bool PresetFilter::matches (const PresetEntry& e) const noexcept
{
    if (type.has_value() && e.type != *type) return false;
    if (e.factory && ! includeFactory) return false;
    if (! e.factory && ! includeUser) return false;
    if (favouritesOnly && ! e.favourite) return false;

    if (! tags.empty())
    {
        int found = 0;
        for (const auto& t : tags)
            if (e.hasTag (t)) ++found;
        if (tagsAll ? found != static_cast<int> (tags.size()) : found == 0)
            return false;
    }

    if (! search.empty())
    {
        const std::string needle = lowered (trimmed (search));
        if (needle.empty()) return true;
        bool hit = containsIgnoreCase (e.name, needle) || containsIgnoreCase (e.author, needle)
                || containsIgnoreCase (e.description, needle);
        for (const auto& t : e.tags)
            hit = hit || containsIgnoreCase (t, needle);
        if (! hit) return false;
    }
    return true;
}

// ---- PresetManager ---------------------------------------------------------------------------

PresetManager::PresetManager (std::span<const FactoryFile> factory, fs::path userRootIn, fs::path globalDir)
    : factoryTable (factory), root (std::move (userRootIn)), global (std::move (globalDir))
{
}

fs::path PresetManager::defaultUserRoot()
{
    fs::path base;
    if (const char* xdg = std::getenv ("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
        base = xdg;
    else if (const char* home = std::getenv ("HOME"); home != nullptr && home[0] != '\0')
        base = fs::path (home) / ".config";
    else
        base = fs::path (".") / ".config";
    return base / "mwAudio" / "mw12F";
}

std::string PresetManager::typeDirectoryName (PresetType type)
{
    std::string name = presetTypeName (type);
    if (! name.empty())
        name[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (name[0])));
    return name;
}

std::string PresetManager::safeFileName (std::string_view name)
{
    std::string out;
    for (const char c : name)
    {
        const auto u = static_cast<unsigned char> (c);
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || u < 32)
            out += '_';
        else
            out += c;
    }
    out = trimmed (out);
    while (! out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty() || out == "..") out = "Untitled";
    if (out.size() > 120) out.resize (120);
    return out;
}

fs::path PresetManager::userDirectory (PresetType type) const
{
    return root / typeDirectoryName (type);
}

fs::path PresetManager::userFileFor (PresetType type, std::string_view name) const
{
    return userDirectory (type) / (safeFileName (name) + kPresetFileExtension);
}

std::string PresetManager::userId (const fs::path& rootDir, const fs::path& file)
{
    std::error_code ec;
    const fs::path rel = fs::relative (file, rootDir, ec);
    return "user:" + (ec || rel.empty() ? file.filename() : rel).generic_string();
}

bool PresetManager::readHeader (std::string_view text, PresetEntry& into)
{
    // The header is a few hundred bytes at most; a parse of the whole document is what we
    // have, and factory files are small, so parse it.
    PresetData d;
    const PresetParseResult r = parsePreset (text, d);
    if (! r.ok) return false;
    into.type = d.type;
    into.name = d.name;
    into.author = d.author;
    into.description = d.description;
    into.tags = splitTags (d.tags);
    return true;
}

void PresetManager::addFactory (std::vector<std::unique_ptr<Stored>>& into) const
{
    for (const FactoryFile& f : factoryTable)
    {
        const std::string path = f.path;
        if (path.size() < 6 || path.substr (path.size() - 6) != kPresetFileExtension) continue;
        auto s = std::make_unique<Stored>();
        s->factoryFile = &f;
        s->entry.factory = true;
        s->entry.id = "factory:" + path;
        if (! readHeader (f.text(), s->entry)) continue;
        if (s->entry.type == PresetType::State) continue;    // a session state is not a preset
        if (s->entry.name.empty())
            s->entry.name = fs::path (path).stem().string();
        into.push_back (std::move (s));
    }
}

void PresetManager::addUser (std::vector<std::unique_ptr<Stored>>& into) const
{
    std::error_code ec;
    fs::create_directories (root, ec);
    for (int t = 0; t < kNumPresetTypes; ++t)
    {
        const auto type = static_cast<PresetType> (t);
        if (type == PresetType::State) continue;
        const fs::path dir = userDirectory (type);
        fs::create_directories (dir, ec);
        if (! fs::is_directory (dir, ec)) continue;

        for (const auto& item : fs::directory_iterator (dir, fs::directory_options::skip_permission_denied, ec))
        {
            if (! item.is_regular_file (ec)) continue;
            if (lowered (item.path().extension().string()) != kPresetFileExtension) continue;
            std::string text;
            if (! readFile (item.path(), text)) continue;
            auto s = std::make_unique<Stored>();
            s->file = item.path();
            s->entry.id = userId (root, item.path());
            if (! readHeader (text, s->entry)) continue;
            if (s->entry.type == PresetType::State) continue;
            if (s->entry.name.empty())
                s->entry.name = item.path().stem().string();
            into.push_back (std::move (s));
        }
    }
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    std::string text;
    if (! readFile (global / "favourites.txt", text)) return;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        line = trimmed (line);
        if (! line.empty() && std::find (favourites.begin(), favourites.end(), line) == favourites.end())
            favourites.push_back (line);
    }
}

bool PresetManager::saveFavourites() const
{
    std::string text;
    for (const auto& id : favourites)
        text += id + "\n";
    std::string error;
    return writeFile (global / "favourites.txt", text, error);
}

void PresetManager::applyFavourites()
{
    for (auto& s : entries)
        s->entry.favourite = std::find (favourites.begin(), favourites.end(), s->entry.id) != favourites.end();
}

void PresetManager::rescan()
{
    std::vector<std::unique_ptr<Stored>> fresh;
    addFactory (fresh);
    addUser (fresh);

    // Keep the address of every entry that still exists: callers hold PresetEntry pointers.
    for (auto& n : fresh)
        for (auto& o : entries)
            if (o != nullptr && o->entry.id == n->entry.id)
            {
                o->entry = n->entry;
                o->file = n->file;
                o->factoryFile = n->factoryFile;
                n = std::move (o);
                break;
            }
    entries = std::move (fresh);

    std::stable_sort (entries.begin(), entries.end(), [] (const std::unique_ptr<Stored>& a, const std::unique_ptr<Stored>& b) {
        if (a->entry.factory != b->entry.factory) return a->entry.factory;
        return lessByName (a->entry, b->entry);
    });
    loadFavourites();
    applyFavourites();
    isScanned = true;
}

std::vector<const PresetEntry*> PresetManager::list (const PresetFilter& filter) const
{
    std::vector<const PresetEntry*> out;
    for (const auto& s : entries)
        if (filter.matches (s->entry))
            out.push_back (&s->entry);
    return out;
}

const PresetEntry* PresetManager::find (std::string_view id) const
{
    for (const auto& s : entries)
        if (s->entry.id == id)
            return &s->entry;
    return nullptr;
}

const PresetManager::Stored* PresetManager::stored (const PresetEntry& e) const
{
    for (const auto& s : entries)
        if (&s->entry == &e || s->entry.id == e.id)
            return s.get();
    return nullptr;
}

std::vector<std::string> PresetManager::tags (std::optional<PresetType> type) const
{
    std::vector<std::string> out;
    for (const auto& s : entries)
    {
        if (type.has_value() && s->entry.type != *type) continue;
        for (const auto& t : s->entry.tags)
        {
            const auto it = std::find_if (out.begin(), out.end(), [&] (const std::string& x) { return lowered (x) == lowered (t); });
            if (it == out.end()) out.push_back (t);
        }
    }
    std::sort (out.begin(), out.end(), [] (const std::string& a, const std::string& b) { return lowered (a) < lowered (b); });
    return out;
}

const PresetEntry* PresetManager::step (const PresetEntry* current, const PresetFilter& filter, int direction) const
{
    const auto items = list (filter);
    if (items.empty()) return nullptr;
    if (current == nullptr) return items.front();
    const auto it = std::find_if (items.begin(), items.end(), [&] (const PresetEntry* e) { return e->id == current->id; });
    if (it == items.end()) return items.front();
    const auto n = static_cast<long> (items.size());
    const long index = (static_cast<long> (it - items.begin()) + (direction >= 0 ? 1 : -1) + n) % n;
    return items[static_cast<std::size_t> (index)];
}

bool PresetManager::loadText (const PresetEntry& e, std::string& text) const
{
    const Stored* s = stored (e);
    if (s == nullptr) return false;
    if (s->factoryFile != nullptr)
    {
        text.assign (s->factoryFile->text());
        return true;
    }
    return readFile (s->file, text);
}

bool PresetManager::load (const PresetEntry& e, PresetData& out, std::string& error) const
{
    std::string text;
    if (! loadText (e, text))
    {
        error = "cannot read " + e.id;
        return false;
    }
    const PresetParseResult r = parsePreset (text, out);
    if (! r.ok)
    {
        error = e.id + ": " + r.error;
        return false;
    }
    return true;
}

const PresetEntry* PresetManager::saveUser (const PresetData& data, bool overwrite, std::string& error)
{
    if (data.type == PresetType::State)
    {
        error = "a session state is not a preset";
        return nullptr;
    }
    const fs::path file = userFileFor (data.type, data.name);
    std::error_code ec;
    if (! overwrite && fs::exists (file, ec))
    {
        error = "a preset of that name exists: " + file.string();
        return nullptr;
    }
    if (! writeFile (file, writePreset (data), error))
        return nullptr;
    rescan();
    return find (userId (root, file));
}

bool PresetManager::renameUser (const PresetEntry& e, std::string_view newName, std::string& error)
{
    const Stored* s = stored (e);
    if (s == nullptr || s->factoryFile != nullptr)
    {
        error = "only user presets can be renamed";
        return false;
    }
    std::string text;
    if (! readFile (s->file, text))
    {
        error = "cannot read " + s->file.string();
        return false;
    }
    PresetData d;
    const PresetParseResult r = parsePreset (text, d);
    if (! r.ok)
    {
        error = s->file.string() + ": " + r.error;
        return false;
    }
    d.name = trimmed (newName);
    const fs::path target = userFileFor (d.type, d.name);
    std::error_code ec;
    if (target != s->file && fs::exists (target, ec))
    {
        error = "a preset of that name exists: " + target.string();
        return false;
    }
    if (! writeFile (target, writePreset (d), error))
        return false;
    if (target != s->file)
        fs::remove (s->file, ec);
    const std::string oldId = e.id, newId = userId (root, target);
    if (auto it = std::find (favourites.begin(), favourites.end(), oldId); it != favourites.end())
    {
        *it = newId;
        saveFavourites();
    }
    rescan();
    return true;
}

bool PresetManager::deleteUser (const PresetEntry& e, std::string& error)
{
    const Stored* s = stored (e);
    if (s == nullptr || s->factoryFile != nullptr)
    {
        error = "only user presets can be deleted";
        return false;
    }
    std::error_code ec;
    if (! fs::remove (s->file, ec))
    {
        error = "cannot delete " + s->file.string();
        return false;
    }
    rescan();
    return true;
}

bool PresetManager::setFavourite (std::string_view id, bool favourite)
{
    const auto it = std::find (favourites.begin(), favourites.end(), std::string (id));
    if (favourite && it == favourites.end())
        favourites.emplace_back (id);
    else if (! favourite && it != favourites.end())
        favourites.erase (it);
    else
        return true;
    applyFavourites();
    return saveFavourites();
}

bool PresetManager::isFavourite (std::string_view id) const
{
    return std::find (favourites.begin(), favourites.end(), std::string (id)) != favourites.end();
}

const PresetEntry* PresetManager::importFile (const fs::path& source, bool overwrite, std::string& error)
{
    std::string text;
    if (! readFile (source, text))
    {
        error = "cannot read " + source.string();
        return nullptr;
    }
    PresetData d;
    const PresetParseResult r = parsePreset (text, d);
    if (! r.ok)
    {
        error = source.string() + ": " + r.error;
        return nullptr;
    }
    if (d.type == PresetType::State)
    {
        error = "a session state is not a preset";
        return nullptr;
    }
    if (d.name.empty())
        d.name = source.stem().string();
    return saveUser (d, overwrite, error);
}

bool PresetManager::exportTo (const PresetEntry& e, const fs::path& destination, std::string& error) const
{
    std::string text;
    if (! loadText (e, text))
    {
        error = "cannot read " + e.id;
        return false;
    }
    return writeFile (destination, text, error);
}
}
