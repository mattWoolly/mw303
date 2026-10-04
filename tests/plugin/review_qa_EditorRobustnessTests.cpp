// QA robustness review (independent), editor side. A failing case is a finding.
//
// Pattern names are raw bytes: PatternCodec decodes %XX escapes into any byte and MIDI import
// copies the track name's bytes, both cut at 31 bytes. The editor turns them into text with
// juce::String::fromUTF8, and JUCE's UTF-8 to UTF-32 conversion (Unicode analysis while the
// pattern-name label is drawn) overruns its heap buffer on invalid UTF-8. ASan report from the
// host simulation (build-qa-asan): heap-buffer-overflow WRITE in
// juce::CharPointer_UTF32::writeNull <- StringEncodingConverter<UTF8,UTF32>::convert
// <- Unicode::analysisCallback <- ... <- LookAndFeel_V2::drawLabel. In Release it shows up as
// glibc "corrupted size vs. prev_size" / "free(): invalid pointer" aborts.
//
// Run under the ASan tree to see the overflow; in any build the UTF-8 validity checks fail.
#include <juce_audio_utils/juce_audio_utils.h>
#include "mw12FEditor.h"
#include "PluginTestSupport.h"

#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
bool validUtf8 (const char* s, int maxBytes)
{
    const auto n = static_cast<int> (strnlen (s, static_cast<std::size_t> (maxBytes)));
    std::string copy (s, static_cast<std::size_t> (n));
    return juce::CharPointer_UTF8::isValidString (copy.c_str(), n);
}

void paintEditor (mw12FProcessor& p)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
    REQUIRE (ed != nullptr);
    auto* e = dynamic_cast<mw12FEditor*> (ed.get());
    REQUIRE (e != nullptr);
    p.handlePendingMessages();
    e->tick();
    for (int i = 0; i < 3; ++i)
        (void) ed->createComponentSnapshot (ed->getLocalBounds());
    p.editorBeingDeleted (ed.get());
}

/** A type-0 SMF: one track named `name`, four 16th notes. */
std::vector<std::uint8_t> smfWithTrackName (const std::string& name)
{
    std::vector<std::uint8_t> trk;
    trk.insert (trk.end(), { 0x00, 0xFF, 0x03, static_cast<std::uint8_t> (name.size()) });
    trk.insert (trk.end(), name.begin(), name.end());
    for (int i = 0; i < 4; ++i)
        trk.insert (trk.end(), { 0x00, 0x90, static_cast<std::uint8_t> (36 + i), 100, 0x30, 0x80, static_cast<std::uint8_t> (36 + i), 0 });
    trk.insert (trk.end(), { 0x00, 0xFF, 0x2F, 0x00 });
    std::vector<std::uint8_t> f = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 0x60, 'M', 'T', 'r', 'k' };
    const auto len = static_cast<std::uint32_t> (trk.size());
    f.insert (f.end(), { static_cast<std::uint8_t> (len >> 24), static_cast<std::uint8_t> (len >> 16), static_cast<std::uint8_t> (len >> 8), static_cast<std::uint8_t> (len) });
    f.insert (f.end(), trk.begin(), trk.end());
    return f;
}
}

TEST_CASE ("review qa: a pattern name from a session file is valid text, and the editor draws it", "[review][qa][editor][utf8]")
{
    ScratchConfigDir scratch;
    std::string state;
    {
        Driver d;
        seq::Pattern p = d.patternFromText ("C D E F");
        std::strcpy (p.name, "QAMARKER");
        d.processor.controller().commitPattern (false, 0, p);
        state = d.stateText();
    }

    // The session file carries the bytes the fuzz case found (stray continuation, overlong
    // lead) as PatternCodec escapes. Written into the text directly: the controller cleans a
    // name it is handed, so a file is the way such bytes still arrive.
    const auto at = state.find ("QAMARKER");
    REQUIRE (at != std::string::npos);
    state.replace (at, 8, "%8F%C0%8F%BD1");
    INFO ("state carries: " << state.substr (at, 24));

    Driver d;
    d.setStateText (state);
    const auto loaded = d.processor.controller().pattern (false, 0);
    CHECK (validUtf8 (loaded.name, seq::kPatternNameLength));
    paintEditor (d.processor);                            // ASan: heap-buffer-overflow here
}

TEST_CASE ("review qa: a scene name from a session file is valid text, and the editor draws it", "[review][qa][editor][utf8]")
{
    ScratchConfigDir scratch;
    Driver d;
    std::string state = d.stateText();
    const auto at = state.find (" name=\"");
    REQUIRE (at != std::string::npos);
    const auto end = state.find ('"', at + 7);
    state.replace (at + 7, end - at - 7, "\x8F\xC0\x8F\xBD" "1");
    d.setStateText (state);
    const std::string name = d.processor.controller().sceneName();
    INFO ("scene name bytes: " << name.size());
    CHECK (juce::CharPointer_UTF8::isValidString (name.c_str(), static_cast<int> (name.size())));
    paintEditor (d.processor);
}

TEST_CASE ("review qa: MIDI import keeps the pattern name valid text (Latin-1 and cut UTF-8 track names)", "[review][qa][editor][utf8]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();

    // A Latin-1 track name, as many sequencers write them.
    REQUIRE (c.importMidiData (smfWithTrackName ("Caf\xE9 Bass"), false, 0));
    CHECK (validUtf8 (c.pattern (false, 0).name, seq::kPatternNameLength));

    // A valid UTF-8 name longer than 31 bytes, cut inside a multi-byte character.
    REQUIRE (c.importMidiData (smfWithTrackName ("Acid line \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 \xE2\x80\x94 x"), false, 1));
    CHECK (validUtf8 (c.pattern (false, 1).name, seq::kPatternNameLength));

    paintEditor (d.processor);
}

// Finds the byte shapes that make the editor overrun the heap (run under ASan; the last
// "qa-name" line printed before the report is the trigger). Hidden: run it by its tag.
TEST_CASE ("review qa: fuzz pattern and scene names through the editor", "[.][review][qa][editor][utf8][fuzz]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    std::unique_ptr<juce::AudioProcessorEditor> ed (d.processor.createEditorAndMakeActive());
    auto* e = dynamic_cast<mw12FEditor*> (ed.get());
    REQUIRE (e != nullptr);
    acid::Rng rng (12345);
    for (int it = 0; it < 600; ++it)
    {
        seq::Pattern p = c.pattern (false, 0);
        std::memset (p.name, 0, sizeof (p.name));
        const int len = 1 + rng.nextInt (seq::kPatternNameLength - 1);
        for (int i = 0; i < len; ++i)
            p.name[i] = static_cast<char> (rng.chance (0.5) ? 0x80 + rng.nextInt (128) : 0x20 + rng.nextInt (96));
        std::string hex;
        for (int i = 0; i < len; ++i) { char b[4]; std::snprintf (b, sizeof (b), "%02x", static_cast<unsigned char> (p.name[i])); hex += b; }
        std::fprintf (stderr, "qa-name %d pattern %s\n", it, hex.c_str());
        c.commitPattern (false, 0, p);
        c.setSceneName (std::string (p.name, static_cast<std::size_t> (len)));
        d.processor.handlePendingMessages();
        e->tick();
        (void) ed->createComponentSnapshot (ed->getLocalBounds());
    }
    d.processor.editorBeingDeleted (ed.get());
}
