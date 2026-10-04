#pragma once
// Text the editor shows, made safe whatever bytes it came from.
//
// Names, tags, authors, descriptions and error messages reach the editor as raw bytes: from a
// host session, a preset or pattern file, a MIDI track name or a file name. JUCE stores a
// juce::String's bytes as given, and its UTF-8 to UTF-32 conversion (run while text is shaped
// for drawing) writes past its buffer when the bytes are not valid UTF-8. So every piece of
// external text becomes a juce::String through these functions, never through
// juce::String (std::string) or juce::String::fromUTF8 on unchecked bytes, and the kit's text
// drawing passes what it draws through sanitise() as a second line of defence.
//
// The decoder is strict (RFC 3629): a stray continuation byte, a truncated sequence, an
// overlong form, a UTF-16 surrogate or a code point above U+10FFFF each become one U+FFFD.
// Control characters (C0, DEL, C1) become a space, so a name cannot break a one-line layout.
// Decoding stops at the first NUL or at maxBytes, whichever comes first.
#include <juce_core/juce_core.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace acid::ui::text
{
/** Valid UTF-8 with no control characters (the fast path's test). */
bool isClean (const char* bytes, std::size_t numBytes) noexcept;

/** `bytes`, at most `maxBytes` of them and up to the first NUL, as safe text. */
juce::String fromBytes (const char* bytes, std::size_t maxBytes);

/** A std::string or string_view of unknown provenance as safe text. */
juce::String from (std::string_view bytes);
inline juce::String from (const std::string& bytes) { return from (std::string_view (bytes)); }
inline juce::String from (const char* bytes) { return bytes != nullptr ? from (std::string_view (bytes)) : juce::String(); }

/** A fixed-size char array (seq::Pattern::name and the like), which need not be terminated. */
template <std::size_t N>
juce::String fromArray (const char (&bytes)[N]) { return fromBytes (bytes, N); }

/** The same string when its bytes are already clean, otherwise a cleaned copy. Cheap for the
    clean case (one scan, no allocation): the kit runs every string it draws through it. */
juce::String sanitise (const juce::String&);
}
