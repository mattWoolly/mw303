// Pattern names are always valid UTF-8 (QA finding: names from session files, presets and
// MIDI import were stored with invalid bytes, and an editor drawing them overflowed a heap
// buffer). Every entry point repairs them: PatternCodec (both forms), sanitizePattern (which
// commit, load and import call) and MIDI import; truncation to kPatternNameLength - 1 bytes
// never cuts a character, and the field stays NUL terminated and padded.
#include "SeqTestSupport.h"
#include "acid/common/Rng.h"
#include "acid/common/Utf8.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace acid;
using namespace acid::seq;

namespace
{
std::string_view nameOf (const Pattern& p)
{
    std::size_t n = 0;
    while (n < static_cast<std::size_t> (kPatternNameLength) && p.name[n] != '\0') ++n;
    return { p.name, n };
}

/** Valid UTF-8, terminated inside the field, zero after the terminator. */
bool wellFormedName (const Pattern& p)
{
    const std::string_view n = nameOf (p);
    if (n.size() >= static_cast<std::size_t> (kPatternNameLength)) return false;
    for (std::size_t i = n.size(); i < static_cast<std::size_t> (kPatternNameLength); ++i)
        if (p.name[i] != '\0') return false;
    return utf8::isValid (n);
}

/** A type-0 SMF with one track: the given track name, then two notes. */
std::vector<std::uint8_t> smfWithTrackName (const std::string& name)
{
    std::vector<std::uint8_t> trk;
    const auto varlen = [&trk] (std::size_t v) {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = static_cast<std::uint8_t> (v & 0x7F);
        while ((v >>= 7) != 0) buf[n++] = static_cast<std::uint8_t> (0x80 | (v & 0x7F));
        while (n > 0) trk.push_back (buf[--n]);
    };
    trk.push_back (0); trk.push_back (0xFF); trk.push_back (0x03);
    varlen (name.size());
    for (const char c : name) trk.push_back (static_cast<std::uint8_t> (c));
    const std::uint8_t body[] = { 0x00, 0x90, 48, 100,  0x60, 0x80, 48, 0,  0x60, 0x90, 50, 100,  0x60, 0x80, 50, 0,
                                  0x00, 0xFF, 0x2F, 0x00 };
    trk.insert (trk.end(), std::begin (body), std::end (body));

    std::vector<std::uint8_t> f = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k' };
    const auto len = static_cast<std::uint32_t> (trk.size());
    for (int s = 24; s >= 0; s -= 8) f.push_back (static_cast<std::uint8_t> (len >> s));
    f.insert (f.end(), trk.begin(), trk.end());
    return f;
}
}

TEST_CASE ("utf8: validity follows RFC 3629", "[seq][utf8]")
{
    CHECK (utf8::isValid (""));
    CHECK (utf8::isValid ("plain ascii"));
    CHECK (utf8::isValid ("Caf\xC3\xA9 \xE2\x80\x94 \xF0\x9F\x8E\xB9"));     // e acute, em dash, a keyboard
    CHECK (utf8::isValid ("\xF4\x8F\xBF\xBF"));                               // U+10FFFF

    CHECK_FALSE (utf8::isValid ("\x80"));                     // stray continuation
    CHECK_FALSE (utf8::isValid ("\xC0\x80"));                 // overlong NUL
    CHECK_FALSE (utf8::isValid ("\xC1\xBF"));                 // overlong
    CHECK_FALSE (utf8::isValid ("\xE0\x80\x80"));             // overlong
    CHECK_FALSE (utf8::isValid ("\xED\xA0\x80"));             // surrogate U+D800
    CHECK_FALSE (utf8::isValid ("\xF0\x80\x80\x80"));         // overlong
    CHECK_FALSE (utf8::isValid ("\xF4\x90\x80\x80"));         // above U+10FFFF
    CHECK_FALSE (utf8::isValid ("\xF5\x80\x80\x80"));
    CHECK_FALSE (utf8::isValid ("\xFF"));
    CHECK_FALSE (utf8::isValid ("\xE2\x82"));                 // truncated
    CHECK_FALSE (utf8::isValid ("Caf\xE9"));                  // Latin-1
    CHECK_FALSE (utf8::isValid ("\x8F\xC0\x8F\xBD" "1"));     // what the QA fuzz case found
}

TEST_CASE ("utf8: repair replaces maximal invalid subparts and keeps valid text", "[seq][utf8]")
{
    CHECK (utf8::repaired ("Caf\xC3\xA9") == "Caf\xC3\xA9");
    CHECK (utf8::repaired ("Caf\xE9") == "Caf\xEF\xBF\xBD");
    CHECK (utf8::repaired ("\xE2\x82" "x") == "\xEF\xBF\xBD" "x");          // one subpart, not two
    CHECK (utf8::repaired ("\x8F\xC0\x8F\xBD" "1") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "1");
    CHECK (utf8::fromLatin1 ("Caf\xE9 Bass") == "Caf\xC3\xA9 Bass");

    // Fuzz: whatever the bytes, the repair is valid and repairing again changes nothing.
    Rng r (7);
    for (int k = 0; k < 20000; ++k)
    {
        std::string s (static_cast<std::size_t> (r.nextInt (40)), '\0');
        for (char& c : s) c = static_cast<char> (r.nextInt (256));
        const std::string fixed = utf8::repaired (s);
        REQUIRE (utf8::isValid (fixed));
        REQUIRE (utf8::repaired (fixed) == fixed);
        if (utf8::isValid (s)) REQUIRE (fixed == s);
    }
}

TEST_CASE ("utf8: a name is cut at a character boundary, terminated and padded", "[seq][utf8]")
{
    char buf[kPatternNameLength];
    std::memset (buf, 'x', sizeof (buf));
    // 10 em dashes are 30 bytes, then a 4-byte character that does not fit in 31.
    std::string s;
    for (int i = 0; i < 10; ++i) s += "\xE2\x80\x94";
    s += "\xF0\x9F\x8E\xB9";
    CHECK (utf8::copyName (buf, sizeof (buf), s));
    CHECK (std::strlen (buf) == 30);
    CHECK (utf8::isValid (buf));
    for (std::size_t i = 30; i < sizeof (buf); ++i) CHECK (buf[i] == '\0');

    // Exactly 31 bytes fits; nothing is reported.
    std::string t (31, 'a');
    CHECK_FALSE (utf8::copyName (buf, sizeof (buf), t));
    CHECK (std::string (buf) == t);
}

TEST_CASE ("names: pattern text with any escaped bytes decodes to a valid name", "[seq][utf8]")
{
    Pattern p = test::randomPattern (11);
    std::string text = encodePattern (p);
    const auto at = text.find ("|name=");
    REQUIRE (at != std::string::npos);
    const auto end = text.find ('|', at + 6);

    // The QA case: bytes 8F C0 8F BD 31 stored as escapes.
    {
        std::string bad = text;
        bad.replace (at + 6, end - at - 6, "%8f%c0%8f%bd1");
        Pattern q;
        const DecodeResult r = decodePattern (bad, q);
        REQUIRE (r.ok);
        CHECK (wellFormedName (q));
        CHECK (r.adjusted >= 1);
    }
    // A valid name too long for the field, cut inside a 3-byte character.
    {
        std::string bad = text;
        bad.replace (at + 6, end - at - 6, "abcd%E2%80%94%E2%80%94%E2%80%94%E2%80%94%E2%80%94%E2%80%94%E2%80%94%E2%80%94%E2%80%94");
        Pattern q;
        const DecodeResult r = decodePattern (bad, q);
        REQUIRE (r.ok);
        CHECK (wellFormedName (q));
        CHECK (nameOf (q).size() == 4 + 9 * 3);
    }
    // Fuzz the escapes.
    Rng r (3);
    for (int k = 0; k < 5000; ++k)
    {
        std::string esc;
        const int n = r.nextInt (48);
        for (int i = 0; i < n; ++i)
        {
            char hex[4];
            std::snprintf (hex, sizeof (hex), "%%%02x", r.nextInt (256));
            esc += hex;
        }
        std::string bad = text;
        bad.replace (at + 6, end - at - 6, esc);
        Pattern q;
        const DecodeResult res = decodePattern (bad, q);
        REQUIRE (res.ok);
        REQUIRE (wellFormedName (q));
    }
}

TEST_CASE ("names: step text names are repaired too", "[seq][utf8]")
{
    Pattern p;
    REQUIRE (fromStepText ("name=\"\x8F\xC0\x8F\xBD" "1\" C D E", p));
    CHECK (wellFormedName (p));
    REQUIRE (fromStepText ("name=\"\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\" C", p));
    CHECK (wellFormedName (p));
    CHECK (nameOf (p).size() == 30);
}

TEST_CASE ("names: sanitizePattern repairs any name and counts it once", "[seq][utf8]")
{
    Rng r (5);
    for (int k = 0; k < 20000; ++k)
    {
        Pattern p;
        for (char& c : p.name) c = static_cast<char> (r.nextInt (256));
        if (r.nextInt (2) == 0) p.name[r.nextInt (kPatternNameLength)] = '\0';
        sanitizePattern (p);
        REQUIRE (wellFormedName (p));
        REQUIRE (sanitizePattern (p) == 0);
    }

    // A valid name with garbage after its terminator: cleared, not counted.
    Pattern p;
    std::memcpy (p.name, "ok\0zz", 5);
    CHECK (sanitizePattern (p) == 0);
    CHECK (p.name[3] == '\0');
}

TEST_CASE ("names: MIDI import stores valid UTF-8 (Latin-1 and cut UTF-8 track names)", "[seq][utf8]")
{
    Pattern p;
    REQUIRE (readMidiFile (smfWithTrackName ("Caf\xE9 Bass"), p));
    CHECK (wellFormedName (p));
    CHECK (nameOf (p) == "Caf\xC3\xA9 Bass");

    REQUIRE (readMidiFile (smfWithTrackName ("Acid line \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 x"), p));
    CHECK (wellFormedName (p));
    CHECK (nameOf (p).size() <= 31);

    REQUIRE (readMidiFile (smfWithTrackName ("\x8F\xC0\x8F\xBD" "1"), p));
    CHECK (wellFormedName (p));

    // Export writes valid text whatever the caller hands it, cut at a character boundary.
    const std::vector<MidiNote> notes { MidiNote {} };
    const auto bytes = writeMidiFile (notes, 1.0, {}, std::string (126, 'a') + "\xE2\x80\x94");
    SmfFile f;
    REQUIRE (parseSmf (bytes, f));
}
