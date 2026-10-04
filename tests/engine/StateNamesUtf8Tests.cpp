// Names and other free text from a state or preset file are stored as valid UTF-8 (QA
// finding: a session whose scene or pattern name held bytes 8F C0 8F BD 31 was stored as is,
// and the editor overflowed a heap buffer drawing it). StateCodec repairs the root text
// attributes, the pattern codec and sanitizePattern repair pattern names, and the writer never
// writes invalid UTF-8 (whatever the PresetData holds).
#include "acid/common/Utf8.h"
#include "acid/engine/StateCodec.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace acid;

namespace
{
constexpr const char* kHostile = "\x8F\xC0\x8F\xBD" "1";

std::string nameOf (const seq::Pattern& p)
{
    std::size_t n = 0;
    while (n < static_cast<std::size_t> (seq::kPatternNameLength) && p.name[n] != '\0') ++n;
    return std::string (p.name, n);
}

std::string sceneWithHostileText()
{
    seq::PatternBank bank;
    seq::Pattern p;
    REQUIRE (seq::fromStepText ("C D E F", p));
    std::strcpy (p.name, "placeholder");
    bank.seq[0] = p;
    PresetData d = makePreset (PresetType::Scene, defaultParamValues(), Settings {}, &bank);
    d.name = "SCENE";
    d.author = "AUTHOR";
    d.tags = "TAGS";
    d.description = "DESC";
    std::string text = writePreset (d);

    const auto put = [&text] (const std::string& marker, const std::string& with) {
        const auto at = text.find (marker);
        REQUIRE (at != std::string::npos);
        text.replace (at, marker.size(), with);
    };
    put ("SCENE", kHostile);
    put ("AUTHOR", "Caf\xE9");
    put ("TAGS", "\xED\xA0\x80");
    put ("DESC", "\xE2\x82");
    put ("name=placeholder", "name=%8f%c0%8f%bd1");
    return text;
}
}

TEST_CASE ("state names: hostile bytes in a scene file are repaired on reading", "[state][utf8]")
{
    PresetData d;
    const PresetParseResult r = parsePreset (sceneWithHostileText(), d);
    REQUIRE (r.ok);
    CHECK (utf8::isValid (d.name));
    CHECK (utf8::isValid (d.author));
    CHECK (utf8::isValid (d.tags));
    CHECK (utf8::isValid (d.description));
    CHECK (d.name == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "1");
    CHECK (r.adjusted >= 5);

    seq::PatternBank bank;
    applyBank (d, bank);
    CHECK (utf8::isValid (nameOf (bank.seq[0])));
    CHECK (nameOf (bank.seq[0]).size() < static_cast<std::size_t> (seq::kPatternNameLength));

    // What is written back is valid UTF-8 and reads back unchanged with nothing to repair.
    const std::string again = writePreset (d);
    CHECK (utf8::isValid (again));
    PresetData d2;
    const PresetParseResult r2 = parsePreset (again, d2);
    REQUIRE (r2.ok);
    CHECK (r2.adjusted == 0);
    CHECK (d2.name == d.name);
    CHECK (writePreset (d2) == again);
}

TEST_CASE ("state names: the writer never writes invalid UTF-8", "[state][utf8]")
{
    PresetData d = makePreset (PresetType::Synth, defaultParamValues(), Settings {});
    d.name = kHostile;
    d.author = "Caf\xE9";
    d.description = std::string ("cut \xE2\x80");
    const std::string text = writePreset (d);
    CHECK (utf8::isValid (text));
    PresetData back;
    REQUIRE (parsePreset (text, back).ok);
    CHECK (utf8::isValid (back.name));
    CHECK (utf8::isValid (back.description));

    // A pattern preset made from a pattern with a hostile name.
    seq::Pattern p;
    REQUIRE (seq::fromStepText ("C", p));
    std::memcpy (p.name, kHostile, std::strlen (kHostile) + 1);
    const std::string pat = writePreset (makePatternPreset (PresetType::Pattern, p));
    CHECK (utf8::isValid (pat));
    PresetData pd;
    REQUIRE (parsePreset (pat, pd).ok);
    seq::Pattern loaded;
    REQUIRE (applyPattern (pd, loaded));
    CHECK (utf8::isValid (nameOf (loaded)));
}
