#include "acid/seq/MidiFile.h"
#include "acid/common/Utf8.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include "SeqInternal.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace acid::seq
{
using detail::clampInt;

namespace
{
// ---- Shared timing ---------------------------------------------------------------------

constexpr double kAuthenticGateExtraSeconds = 1.35e-3;

/** Step length in ticks for a ppq that is a multiple of 96 (exact for every division). */
std::int64_t stepTicks (StepDiv div, DivMod mod, int ppq) noexcept
{
    constexpr int kDenom[7] { 64, 32, 16, 8, 4, 2, 1 };
    const std::int64_t base = 4LL * ppq / kDenom[clampInt (div, 0, 6)];
    switch (mod)
    {
        case DivMod::Dotted: return base * 3 / 2;
        case DivMod::Triplet: return base * 2 / 3;
        case DivMod::Full: break;
    }
    return base;
}

double stepTicksD (StepDiv div, DivMod mod, double ppq) noexcept
{
    constexpr double kDenom[7] { 64, 32, 16, 8, 4, 2, 1 };
    const double base = 4.0 * ppq / kDenom[clampInt (div, 0, 6)];
    return mod == DivMod::Dotted ? base * 1.5 : mod == DivMod::Triplet ? base * 2.0 / 3.0 : base;
}

double authenticExtraTicks (double bpm, double ppq) noexcept
{
    return kAuthenticGateExtraSeconds * bpm / 60.0 * ppq;
}

int normalisePpq (int ppq) noexcept
{
    const int p = clampInt (ppq, 96, 9600);
    return std::max (96, (p + 48) / 96 * 96);
}

// ---- Writing ---------------------------------------------------------------------------

enum Prio { kPrioSustainOn = 0, kPrioNoteOff = 1, kPrioRelease = 2, kPrioSet = 3, kPrioNoteOn = 4 };

struct RawEvent
{
    std::int64_t tick = 0;
    int prio = 0;
    int seq = 0;
    std::uint8_t bytes[3] {};
};

struct NoteSpan
{
    std::int64_t start = 0;
    std::int64_t end = 0;
    int note = 60;
    int vel = 100;
    bool vib = false;
};

/** Orders spans by start, ties kept in input order. std::sort with an explicit tie break
    rather than std::stable_sort, whose temporary buffer comes from nothrow operator new. */
void sortByStart (std::vector<NoteSpan>& spans)
{
    std::vector<std::pair<NoteSpan, std::size_t>> tagged;
    tagged.reserve (spans.size());
    for (std::size_t i = 0; i < spans.size(); ++i) tagged.emplace_back (spans[i], i);
    std::sort (tagged.begin(), tagged.end(), [] (const auto& a, const auto& b) {
        if (a.first.start != b.first.start) return a.first.start < b.first.start;
        return a.second < b.second;
    });
    for (std::size_t i = 0; i < spans.size(); ++i) spans[i] = tagged[i].first;
}

void putVlq (std::vector<std::uint8_t>& out, std::uint64_t v)
{
    std::uint8_t buf[10];
    int n = 0;
    buf[n++] = static_cast<std::uint8_t> (v & 0x7F);
    while ((v >>= 7) != 0 && n < 10) buf[n++] = static_cast<std::uint8_t> (0x80 | (v & 0x7F));
    while (n > 0) out.push_back (buf[--n]);
}

void putU32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back (static_cast<std::uint8_t> ((v >> s) & 0xFF));
}

void putU16 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xFF));
    out.push_back (static_cast<std::uint8_t> (v & 0xFF));
}

void putMeta (std::vector<std::uint8_t>& trk, std::uint8_t type, std::string_view data)
{
    trk.push_back (0x00);   // delta
    trk.push_back (0xFF);
    trk.push_back (type);
    putVlq (trk, data.size());
    trk.insert (trk.end(), data.begin(), data.end());
}

/** Note spans plus extra controller events to sorted raw events, with CC1 for vibrato. */
std::vector<RawEvent> buildEvents (std::vector<NoteSpan> spans, std::vector<RawEvent> extra, int channel, bool vibratoCC)
{
    sortByStart (spans);
    const auto ch = static_cast<std::uint8_t> (clampInt (channel, 0, 15));
    std::vector<RawEvent> ev = std::move (extra);

    auto add = [&ev, ch] (std::int64_t tick, int prio, std::uint8_t status, int d1, int d2) {
        RawEvent e;
        e.tick = tick;
        e.prio = prio;
        e.bytes[0] = static_cast<std::uint8_t> (status | ch);
        e.bytes[1] = static_cast<std::uint8_t> (clampInt (d1, 0, 127));
        e.bytes[2] = static_cast<std::uint8_t> (clampInt (d2, 0, 127));
        ev.push_back (e);
    };
    for (RawEvent& e : ev) e.bytes[0] = static_cast<std::uint8_t> ((e.bytes[0] & 0xF0) | ch);

    bool wheel = false;
    for (std::size_t k = 0; k < spans.size(); ++k)
    {
        const NoteSpan& s = spans[k];
        if (vibratoCC && s.vib != wheel)
        {
            add (s.start, kPrioSet, 0xB0, 1, s.vib ? 127 : 0);
            wheel = s.vib;
        }
        add (s.start, kPrioNoteOn, 0x90, s.note, clampInt (s.vel, 1, 127));
        add (s.end, kPrioNoteOff, 0x80, s.note, 0);
        if (vibratoCC && s.vib)
        {
            const std::int64_t nextStart = k + 1 < spans.size() ? spans[k + 1].start : std::numeric_limits<std::int64_t>::max();
            if (nextStart >= s.end)
            {
                add (s.end, kPrioRelease, 0xB0, 1, 0);
                wheel = false;
            }
        }
    }

    for (std::size_t i = 0; i < ev.size(); ++i) ev[i].seq = static_cast<int> (i);
    std::sort (ev.begin(), ev.end(), [] (const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.prio != b.prio) return a.prio < b.prio;
        return a.seq < b.seq;
    });
    return ev;
}

std::vector<std::uint8_t> assembleFile (int ppq, double bpm, std::string_view name, std::string_view sidecar,
                                        const std::vector<RawEvent>& ev, std::int64_t endTick)
{
    std::vector<std::uint8_t> trk;
    putMeta (trk, 0x03, name);
    {
        const auto uspq = static_cast<std::uint32_t> (std::llround (60.0e6 / std::clamp (bpm, 20.0, 999.0)));
        const char tempo[3] { static_cast<char> ((uspq >> 16) & 0xFF), static_cast<char> ((uspq >> 8) & 0xFF),
                              static_cast<char> (uspq & 0xFF) };
        putMeta (trk, 0x51, std::string_view (tempo, 3));
        const char sig[4] { 4, 2, 24, 8 };
        putMeta (trk, 0x58, std::string_view (sig, 4));
    }
    if (! sidecar.empty()) putMeta (trk, 0x01, sidecar);

    std::int64_t now = 0;
    for (const RawEvent& e : ev)
    {
        const std::int64_t t = std::max (now, e.tick);
        putVlq (trk, static_cast<std::uint64_t> (t - now));
        now = t;
        trk.push_back (e.bytes[0]);
        trk.push_back (e.bytes[1]);
        trk.push_back (e.bytes[2]);
    }
    putVlq (trk, static_cast<std::uint64_t> (std::max (now, endTick) - now));
    trk.push_back (0xFF);
    trk.push_back (0x2F);
    trk.push_back (0x00);

    std::vector<std::uint8_t> out;
    out.reserve (trk.size() + 22);
    out.insert (out.end(), { 'M', 'T', 'h', 'd' });
    putU32 (out, 6);
    putU16 (out, 0);   // format 0
    putU16 (out, 1);   // one track
    putU16 (out, static_cast<std::uint32_t> (ppq));
    out.insert (out.end(), { 'M', 'T', 'r', 'k' });
    putU32 (out, static_cast<std::uint32_t> (trk.size()));
    out.insert (out.end(), trk.begin(), trk.end());
    return out;
}

std::string_view patternName (const Pattern& p) noexcept
{
    std::size_t n = 0;
    while (n < static_cast<std::size_t> (kPatternNameLength) && p.name[n] != '\0') ++n;
    return std::string_view (p.name, n);
}
}

std::vector<std::uint8_t> writeMidiFile (const Pattern& p, const MidiExportOptions& opt)
{
    const int ppq = normalisePpq (opt.ppq);
    const double bpm = std::clamp (opt.bpm, 20.0, 999.0);
    const int patLen = clampInt (p.length, 1, kMaxSteps);
    const std::int64_t stepT = stepTicks (p.div, p.divMod, ppq);
    const bool shuffleOn = opt.applyShuffle && p.divMod == DivMod::Full && p.shuffle > 0;
    // The player delays odd steps of the absolute grid, so an odd-length pattern swaps its
    // shuffle parity every pass: two passes make a clip that loops the way it plays.
    const int len = shuffleOn && opt.twoPassesForOddShuffle && (patLen & 1) != 0 ? 2 * patLen : patLen;
    const std::int64_t clipEnd = len * stepT;
    const std::int64_t shuf = shuffleOn ? (static_cast<std::int64_t> (clampInt (p.shuffle, 0, 100)) * stepT + 100) / 200 : 0;
    const std::int64_t overlap = std::max<std::int64_t> (1, stepT / 64);
    const auto extra = static_cast<std::int64_t> (std::llround (authenticExtraTicks (bpm, ppq)));

    auto onset = [&] (int i) -> std::int64_t {
        if (i >= len) return clipEnd;
        return i * stepT + ((i & 1) != 0 ? shuf : 0);
    };
    auto gateT = [&] (const Step& s) -> std::int64_t {
        std::int64_t g = 0;
        if (s.gate > 0) g = (clampInt (s.gate, 1, 127) * stepT + 63) / 127;
        else if (p.gateLength > 0) g = (clampInt (p.gateLength, 1, 100) * stepT + 50) / 100;
        else g = stepT / 2 + extra;
        return std::max<std::int64_t> (1, g);
    };
    auto pitchOf = [&] (const Step& s) {
        return clampInt (kBaseNote + 12 * s.octave + s.note + (opt.applyTranspose ? p.transpose : 0), 0, 127);
    };
    auto velOf = [&] (const Step& s) {
        return clampInt (s.accent ? opt.accentVelocity : opt.normalVelocity, 1, 127);
    };
    auto sounds = [] (const Step& s) { return s.state == StepState::Note && s.probability > 0; };

    std::vector<NoteSpan> spans;
    std::vector<RawEvent> extraEv;
    int cur = -1;
    std::int64_t curGateEnd = 0;
    int curLastStep = 0;
    bool pendingSlide = false;
    bool sustainChain = false;

    auto addCC = [&extraEv] (std::int64_t tick, int prio, int cc, int value) {
        RawEvent e;
        e.tick = tick;
        e.prio = prio;
        e.bytes[0] = 0xB0;
        e.bytes[1] = static_cast<std::uint8_t> (cc);
        e.bytes[2] = static_cast<std::uint8_t> (value);
        extraEv.push_back (e);
    };
    auto closeAt = [&] (std::int64_t end) {
        NoteSpan& s = spans[static_cast<std::size_t> (cur)];
        s.end = std::max (s.start + 1, end);
        if (sustainChain)
        {
            addCC (s.end, kPrioRelease, 64, 0);
            sustainChain = false;
        }
        cur = -1;
        pendingSlide = false;
    };
    auto closeNonLegato = [&] () {
        closeAt (std::min (curGateEnd, onset (curLastStep + 1) - 1));
    };

    for (int i = 0; i < len; ++i)
    {
        const Step& s = p.steps[static_cast<std::size_t> (i % patLen)];
        const std::int64_t on = onset (i);

        if (sounds (s))
        {
            const int pitch = pitchOf (s);
            const int vel = velOf (s);
            bool merged = false;

            if (cur >= 0 && pendingSlide)
            {
                NoteSpan& c = spans[static_cast<std::size_t> (cur)];
                if (c.note == pitch)
                {
                    if (c.vel == vel) merged = true;
                    else
                    {
                        addCC (on, kPrioSustainOn, 64, 127);
                        c.end = std::max (c.start + 1, on);
                        cur = -1;
                        sustainChain = true;
                    }
                }
                else
                    closeAt (on + overlap);
            }
            else if (cur >= 0)
                closeNonLegato();

            if (! merged)
            {
                spans.push_back (NoteSpan { on, on + 1, pitch, vel, s.vibrato });
                cur = static_cast<int> (spans.size()) - 1;
            }

            const int r = clampInt (s.ratchet, 1, 4);
            const std::int64_t g = gateT (s);
            if (r > 1)
            {
                const std::int64_t sub = std::max<std::int64_t> (1, g / r);
                const std::int64_t subStep = std::max<std::int64_t> (1, stepT / r);
                for (int k = 1; k < r; ++k)
                {
                    const std::int64_t hit = on + k * subStep;
                    NoteSpan& c = spans[static_cast<std::size_t> (cur)];
                    c.end = std::max (c.start + 1, std::min (hit - subStep + sub, hit - 1));
                    if (sustainChain)
                    {
                        addCC (c.end, kPrioRelease, 64, 0);
                        sustainChain = false;
                    }
                    spans.push_back (NoteSpan { hit, hit + 1, pitch, vel, s.vibrato });
                    cur = static_cast<int> (spans.size()) - 1;
                }
                curGateEnd = on + (r - 1) * subStep + sub;
            }
            else
                curGateEnd = on + g;

            curLastStep = i;
            pendingSlide = s.slide;
        }
        else if (s.state == StepState::Tie && s.probability > 0)
        {
            if (cur >= 0)
            {
                curGateEnd = on + gateT (s);
                curLastStep = i;
            }
        }
        else if (cur >= 0)
        {
            if (! pendingSlide)
                closeNonLegato();
            else
                switch (opt.slideIntoRest)
                {
                    case SlideIntoRest::Release:
                        closeNonLegato();
                        break;
                    case SlideIntoRest::HoldThroughRest:
                        curGateEnd = on + gateT (s);
                        curLastStep = i;
                        closeNonLegato();
                        break;
                    case SlideIntoRest::SustainToNextNote:
                        curLastStep = i;
                        break;
                }
        }
    }

    if (cur >= 0)
    {
        const bool loopGlide = pendingSlide
            && (sounds (p.steps[0]) || opt.slideIntoRest == SlideIntoRest::SustainToNextNote);
        if (loopGlide) closeAt (clipEnd);
        else closeNonLegato();
    }

    std::int64_t endTick = clipEnd;
    if (opt.padToBar)
    {
        const std::int64_t bar = 4LL * ppq;
        endTick = (clipEnd + bar - 1) / bar * bar;
    }
    for (const NoteSpan& s : spans) endTick = std::max (endTick, s.end);

    const auto ev = buildEvents (std::move (spans), std::move (extraEv), opt.channel, opt.vibratoCC);
    const std::string sidecar = opt.embedPattern ? encodePattern (p) : std::string();
    return assembleFile (ppq, bpm, patternName (p), sidecar, ev, endTick);
}

std::vector<std::uint8_t> writeMidiFile (std::span<const MidiNote> notes, double clipLengthQ,
                                         const MidiExportOptions& opt, std::string_view trackName)
{
    const int ppq = normalisePpq (opt.ppq);
    const double bpm = std::clamp (opt.bpm, 20.0, 999.0);
    static constexpr double kMaxQ = 1.0e6;
    auto toTicks = [ppq] (double q) {
        if (! std::isfinite (q)) q = 0.0;
        return static_cast<std::int64_t> (std::llround (std::clamp (q, 0.0, kMaxQ) * ppq));
    };

    std::vector<NoteSpan> spans;
    spans.reserve (notes.size());
    for (const MidiNote& n : notes)
    {
        NoteSpan s;
        s.start = toTicks (n.startQ);
        s.end = std::max (s.start + 1, toTicks (n.endQ));
        s.note = clampInt (n.note, 0, 127);
        s.vel = n.velocity > 0 ? clampInt (n.velocity, 1, 127) : clampInt (n.accent ? opt.accentVelocity : opt.normalVelocity, 1, 127);
        s.vib = n.vibrato;
        spans.push_back (s);
    }
    sortByStart (spans);

    // One key cannot overlap itself in MIDI: cut the earlier note where the later one starts.
    for (std::size_t i = 0; i < spans.size(); ++i)
        for (std::size_t j = i + 1; j < spans.size() && spans[j].start < spans[i].end; ++j)
            if (spans[j].note == spans[i].note) spans[i].end = std::max (spans[i].start + 1, std::min (spans[i].end, spans[j].start));

    std::int64_t endTick = toTicks (clipLengthQ);
    if (opt.padToBar)
    {
        const std::int64_t bar = 4LL * ppq;
        endTick = (endTick + bar - 1) / bar * bar;
    }
    for (const NoteSpan& s : spans) endTick = std::max (endTick, s.end);

    const auto ev = buildEvents (std::move (spans), {}, opt.channel, opt.vibratoCC);
    const std::string name = utf8::repaired (trackName);
    return assembleFile (ppq, bpm, std::string_view (name).substr (0, utf8::prefixLength (name, 127)), {}, ev, endTick);
}

// ---- Parsing ---------------------------------------------------------------------------

namespace
{
struct Reader
{
    std::span<const std::uint8_t> b;
    std::size_t pos = 0;

    bool have (std::size_t n) const noexcept { return pos <= b.size() && b.size() - pos >= n; }
    std::uint8_t u8() noexcept { return b[pos++]; }
    std::uint32_t u32() noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | b[pos++];
        return v;
    }
    std::uint16_t u16() noexcept
    {
        const auto hi = static_cast<std::uint16_t> (b[pos++]);
        return static_cast<std::uint16_t> ((hi << 8) | b[pos++]);
    }
    bool vlq (std::uint32_t& out) noexcept
    {
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (! have (1)) return false;
            const std::uint8_t c = u8();
            out = (out << 7) | (c & 0x7Fu);
            if ((c & 0x80) == 0) return true;
        }
        return false;   // longer than four bytes
    }
};

bool parseTrack (std::span<const std::uint8_t> data, SmfTrack& trk, std::string& err)
{
    Reader r { data };
    std::int64_t tick = 0;
    std::uint8_t running = 0;
    constexpr std::int64_t kMaxTick = std::int64_t (1) << 40;

    while (r.have (1))
    {
        std::uint32_t delta = 0;
        if (! r.vlq (delta)) { err = "bad delta time"; return false; }
        tick += delta;
        if (tick > kMaxTick) { err = "track too long"; return false; }
        if (! r.have (1)) { err = "truncated event"; return false; }

        SmfEvent e;
        e.tick = tick;
        std::uint8_t status = r.b[r.pos];
        if (status < 0x80)
        {
            if (running == 0) { err = "running status without a status byte"; return false; }
            status = running;
        }
        else
            ++r.pos;

        if (status >= 0x80 && status <= 0xEF)
        {
            running = status;
            const int n = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
            if (! r.have (static_cast<std::size_t> (n))) { err = "truncated channel event"; return false; }
            e.status = status;
            e.data1 = r.u8();
            if (n == 2) e.data2 = r.u8();
            if ((e.data1 & 0x80) != 0 || (e.data2 & 0x80) != 0) { err = "data byte with the top bit set"; return false; }
            trk.events.push_back (std::move (e));
        }
        else if (status == 0xFF)
        {
            running = 0;
            if (! r.have (1)) { err = "truncated meta event"; return false; }
            e.status = 0xFF;
            e.data1 = r.u8();
            std::uint32_t n = 0;
            if (! r.vlq (n) || ! r.have (n)) { err = "bad meta length"; return false; }
            e.payload.assign (r.b.begin() + static_cast<std::ptrdiff_t> (r.pos), r.b.begin() + static_cast<std::ptrdiff_t> (r.pos + n));
            r.pos += n;
            const bool eot = e.data1 == 0x2F;
            trk.events.push_back (std::move (e));
            if (eot)
            {
                trk.hasEndOfTrack = true;
                trk.endTick = tick;
                return true;
            }
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            running = 0;
            std::uint32_t n = 0;
            if (! r.vlq (n) || ! r.have (n)) { err = "bad sysex length"; return false; }
            e.status = status;
            e.payload.assign (r.b.begin() + static_cast<std::ptrdiff_t> (r.pos), r.b.begin() + static_cast<std::ptrdiff_t> (r.pos + n));
            r.pos += n;
            trk.events.push_back (std::move (e));
        }
        else
        {
            err = "illegal status byte in a file";
            return false;
        }
    }
    trk.endTick = tick;   // no End of Track: tolerated, ends at the last event
    return true;
}
}

bool parseSmf (std::span<const std::uint8_t> bytes, SmfFile& out, std::string* error)
{
    std::string err;
    auto fail = [&] (const char* msg) {
        if (error != nullptr) *error = msg;
        return false;
    };

    Reader r { bytes };
    if (! r.have (14)) return fail ("too short for a MIDI file");
    if (! (r.b[0] == 'M' && r.b[1] == 'T' && r.b[2] == 'h' && r.b[3] == 'd')) return fail ("missing MThd");
    r.pos = 4;
    const std::uint32_t hlen = r.u32();
    if (hlen < 6 || ! r.have (hlen)) return fail ("bad header length");
    SmfFile f;
    f.format = r.u16();
    f.declaredTracks = r.u16();
    const std::uint16_t division = r.u16();
    r.pos += hlen - 6;
    if (f.format > 2) return fail ("unknown SMF format");
    if ((division & 0x8000) != 0) return fail ("SMPTE time division is not supported");
    if (division == 0) return fail ("zero ticks per quarter");
    f.ppq = division;

    while (r.have (8))
    {
        const bool isTrack = r.b[r.pos] == 'M' && r.b[r.pos + 1] == 'T' && r.b[r.pos + 2] == 'r' && r.b[r.pos + 3] == 'k';
        r.pos += 4;
        const std::uint32_t len = r.u32();
        if (! r.have (len)) return fail ("truncated chunk");
        if (isTrack)
        {
            SmfTrack t;
            if (! parseTrack (bytes.subspan (r.pos, len), t, err))
            {
                if (error != nullptr) *error = "track " + std::to_string (f.tracks.size()) + ": " + err;
                return false;
            }
            f.tracks.push_back (std::move (t));
            if (f.tracks.size() > 4096) return fail ("too many tracks");
        }
        r.pos += len;
    }
    if (f.tracks.empty()) return fail ("no tracks");
    out = std::move (f);
    return true;
}

// ---- Import ----------------------------------------------------------------------------

namespace
{
struct InNote
{
    std::int64_t on = 0;
    std::int64_t off = 0;
    int note = 0;
    int vel = 0;
    bool vib = false;
    bool sus = false;
};

std::vector<InNote> collectNotes (const SmfTrack& t)
{
    std::vector<InNote> notes;
    std::vector<std::size_t> open[16][128];
    int cc1[16] {}, cc64[16] {};

    for (const SmfEvent& e : t.events)
    {
        if (e.status < 0x80 || e.status > 0xEF) continue;
        const int ch = e.status & 0x0F;
        const int type = e.status & 0xF0;
        if (type == 0xB0)
        {
            if (e.data1 == 1) cc1[ch] = e.data2;
            else if (e.data1 == 64) cc64[ch] = e.data2;
            continue;
        }
        const bool isOn = type == 0x90 && e.data2 > 0;
        const bool isOff = type == 0x80 || (type == 0x90 && e.data2 == 0);
        auto& q = open[ch][e.data1 & 0x7F];
        if (isOn)
        {
            for (std::size_t idx : q) notes[idx].off = std::max (notes[idx].on, e.tick);   // re-strike
            q.clear();
            InNote n;
            n.on = e.tick;
            n.off = -1;
            n.note = e.data1;
            n.vel = e.data2;
            n.vib = cc1[ch] >= 64;
            n.sus = cc64[ch] >= 64;
            q.push_back (notes.size());
            notes.push_back (n);
        }
        else if (isOff && ! q.empty())
        {
            notes[q.front()].off = e.tick;
            q.erase (q.begin());
        }
    }
    for (InNote& n : notes)
        if (n.off < 0) n.off = std::max (n.on + 1, t.endTick);
    for (InNote& n : notes)
        if (n.off <= n.on) n.off = n.on + 1;
    // Stable by onset (notes are in file order), without std::stable_sort's nothrow buffer.
    std::vector<std::pair<InNote, std::size_t>> tagged;
    tagged.reserve (notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i) tagged.emplace_back (notes[i], i);
    std::sort (tagged.begin(), tagged.end(), [] (const auto& a, const auto& b) {
        if (a.first.on != b.first.on) return a.first.on < b.first.on;
        return a.second < b.second;
    });
    for (std::size_t i = 0; i < notes.size(); ++i) notes[i] = tagged[i].first;
    return notes;
}

/** The notes of the first track that has any (empty when none does). */
std::vector<InNote> firstTrackNotes (const SmfFile& f)
{
    for (const SmfTrack& t : f.tracks)
    {
        std::vector<InNote> notes = collectNotes (t);
        if (! notes.empty()) return notes;
    }
    return {};
}

/** True when rendering p gives the notes the file holds: the same onsets, keys and ends
    (to within one tick) and velocities that split into the same accent classes. The export
    options are not stored in the file, so every combination that changes the notes
    (shuffle, transpose, slide into rest, two passes, equal velocities) is tried, and so is
    every common writer resolution, scaled to the file's (a host may re-save at its own).
    Vibrato controllers are not compared. */
bool sidecarMatchesNotes (const Pattern& p, const SmfFile& f, double bpm)
{
    std::vector<InNote> have = firstTrackNotes (f);
    auto byTime = [] (const InNote& a, const InNote& b) {
        if (a.on != b.on) return a.on < b.on;
        if (a.note != b.note) return a.note < b.note;
        return a.off < b.off;
    };
    std::sort (have.begin(), have.end(), byTime);

    // Equal accent and normal velocities also change the notes (same-pitch slides merge).
    bool oneVelocity = true;
    for (const InNote& n : have) oneVelocity = oneVelocity && n.vel == have.front().vel;
    const bool shuffled = p.shuffle > 0 && p.divMod == DivMod::Full;
    const bool odd = (clampInt (p.length, 1, kMaxSteps) & 1) != 0;

    // The file's own resolution first, then the usual writer resolutions.
    const int filePpq = std::max (1, f.ppq);
    const int writerPpqs[] { normalisePpq (filePpq), 480, 960, 96, 192, 384, 1920 };

    for (std::size_t k = 0; k < std::size (writerPpqs); ++k)
    {
        const int ppq = writerPpqs[k];
        if (k > 0 && ppq == writerPpqs[0]) continue;
        const double toFile = static_cast<double> (filePpq) / static_cast<double> (ppq);
        auto close = [toFile] (std::int64_t fileTick, std::int64_t renderTick) {
            return std::abs (static_cast<double> (fileTick) - static_cast<double> (renderTick) * toFile) <= 1.0 + 1.0e-9;
        };

        // Every export option that changes the notes; variants that cannot differ are skipped.
        constexpr int kVariants = 2 * 3 * 2 * 3;
        for (int v = 0; v < kVariants; ++v)
        {
            const int velMode = v % 2;
            const int shuffleMode = (v / 2) % 3;          // 0 default, 1 no shuffle, 2 one pass
            const int transposeMode = (v / 6) % 2;
            const int policy = v / 12;
            if ((velMode == 1 && ! oneVelocity) || (shuffleMode > 0 && ! shuffled) || (shuffleMode == 2 && ! odd)
                || (transposeMode == 1 && p.transpose == 0))
                continue;

            MidiExportOptions o;
            o.bpm = bpm;
            o.ppq = ppq;
            o.accentVelocity = velMode == 0 ? 127 : 64;   // 127 marks the accent class
            o.normalVelocity = velMode == 0 ? 1 : 64;
            o.embedPattern = false;
            o.vibratoCC = false;
            o.applyShuffle = shuffleMode != 1;
            o.twoPassesForOddShuffle = shuffleMode == 0;
            o.applyTranspose = transposeMode == 0;
            o.slideIntoRest = static_cast<SlideIntoRest> (policy);

            SmfFile r;
            if (! parseSmf (writeMidiFile (p, o), r)) continue;
            std::vector<InNote> want = firstTrackNotes (r);
            if (want.size() != have.size()) continue;
            std::sort (want.begin(), want.end(), byTime);

            bool same = true;
            int accentVel = -1, plainVel = -1;
            for (std::size_t i = 0; i < want.size() && same; ++i)
            {
                const InNote& a = have[i];
                const InNote& b = want[i];
                int& cls = b.vel == 127 ? accentVel : plainVel;
                if (cls < 0) cls = a.vel;
                same = a.note == b.note && close (a.on, b.on) && close (a.off, b.off) && cls == a.vel;
            }
            if (same) return true;
        }
    }
    return false;
}

struct Grid
{
    double stepT = 120.0;
    double shuffleT = 0.0;      // delay of odd steps in ticks
    int shufflePct = 0;

    double onset (int k) const noexcept { return k * stepT + ((k & 1) != 0 ? shuffleT : 0.0); }

    /** Nearest step and its distance in ticks. */
    int snap (double t, double& dist) const noexcept
    {
        const double pair = std::floor (t / (2.0 * stepT));
        const double pos = t - pair * 2.0 * stepT;
        const double targets[3] { 0.0, stepT + shuffleT, 2.0 * stepT };
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs (pos - targets[i]) < std::abs (pos - targets[best])) best = i;
        dist = std::abs (pos - targets[best]);
        const double k = pair * 2.0 + best;
        return k > 1.0e6 ? 1000000 : static_cast<int> (k);
    }
};

Grid makeGrid (const std::vector<InNote>& notes, double stepT, bool full, bool detectShuffle)
{
    Grid g;
    g.stepT = stepT;
    if (! full || ! detectShuffle || notes.empty()) return g;

    std::vector<double> delays;
    for (const InNote& n : notes)
    {
        const double t = static_cast<double> (n.on);
        const double pos = t - std::floor (t / (2.0 * stepT)) * 2.0 * stepT;
        if (pos >= 0.5 * stepT && pos <= 1.75 * stepT) delays.push_back (pos - stepT);
    }
    // Needs at least two late off-beats that agree with each other.
    if (delays.size() < 2) return g;
    std::sort (delays.begin(), delays.end());
    const double d = delays[delays.size() / 2];
    std::size_t agree = 0;
    for (double x : delays) agree += std::abs (x - d) <= 0.1 * stepT ? 1u : 0u;
    if (d > 0.1 * stepT && agree * 4 >= delays.size() * 3)
    {
        g.shufflePct = clampInt (std::lround (d * 200.0 / stepT), 0, 100);
        g.shuffleT = g.shufflePct * stepT / 200.0;
    }
    return g;
}

double offGrid (const std::vector<InNote>& notes, const Grid& g, double tol)
{
    if (notes.empty()) return 0.0;
    int off = 0;
    for (const InNote& n : notes)
    {
        double dist = 0.0;
        g.snap (static_cast<double> (n.on), dist);
        if (dist > tol * g.stepT) ++off;
    }
    return static_cast<double> (off) / static_cast<double> (notes.size());
}
}

bool readMidiFile (std::span<const std::uint8_t> bytes, Pattern& out, const MidiImportOptions& opt, MidiImportReport* report)
{
    MidiImportReport rep;
    auto finish = [&] (bool ok) {
        rep.ok = ok;
        if (report != nullptr) *report = rep;
        return ok;
    };

    SmfFile f;
    if (! parseSmf (bytes, f, &rep.error)) return finish (false);
    rep.format = f.format;
    rep.ppq = f.ppq;

    // Tempo and names.
    std::string trackName;
    bool haveTempo = false;
    for (const SmfTrack& t : f.tracks)
        for (const SmfEvent& e : t.events)
            if (! haveTempo && e.status == 0xFF && e.data1 == 0x51 && e.payload.size() == 3)
            {
                const double uspq = (e.payload[0] << 16) | (e.payload[1] << 8) | e.payload[2];
                if (uspq > 0)
                {
                    rep.bpm = std::clamp (60.0e6 / uspq, 1.0, 1000.0);
                    haveTempo = true;
                }
            }

    // The lossless sidecar wins while it still describes the notes. Hosts keep text events
    // when a clip is edited, so a sidecar whose notes differ from the file's is stale.
    if (opt.useEmbeddedPattern)
        for (const SmfTrack& t : f.tracks)
            for (const SmfEvent& e : t.events)
                if (e.status == 0xFF && e.data1 == 0x01)
                {
                    const std::string_view text (reinterpret_cast<const char*> (e.payload.data()), e.payload.size());
                    if (! looksLikeEncodedPattern (text)) continue;
                    Pattern p;
                    if (! decodePattern (text, p).ok) continue;
                    if (sidecarMatchesNotes (p, f, rep.bpm))
                    {
                        out = p;
                        rep.usedEmbedded = true;
                        rep.sidecarStale = false;
                        // The report still describes the file: notes in the first track with notes.
                        for (const SmfTrack& nt : f.tracks)
                        {
                            const auto n = collectNotes (nt);
                            if (! n.empty()) { rep.notesRead = static_cast<int> (n.size()); break; }
                        }
                        return finish (true);
                    }
                    rep.sidecarStale = true;
                }

    // First track with notes.
    std::vector<InNote> notes;
    const SmfTrack* track = nullptr;
    for (const SmfTrack& t : f.tracks)
    {
        notes = collectNotes (t);
        if (! notes.empty()) { track = &t; break; }
    }
    if (track == nullptr) { rep.error = "no notes"; return finish (false); }
    rep.notesRead = static_cast<int> (notes.size());
    for (const SmfEvent& e : track->events)
        if (e.status == 0xFF && e.data1 == 0x03)
        {
            trackName.assign (reinterpret_cast<const char*> (e.payload.data()), e.payload.size());
            break;
        }

    // Grid.
    const double ppq = f.ppq;
    const double tol = std::clamp (opt.snapTolerance, 0.01, 0.5);
    StepDiv div = opt.div;
    DivMod mod = opt.divMod;
    Grid grid;
    if (opt.autoDivision)
    {
        div = StepDiv::d1_16;
        mod = DivMod::Full;
        grid = makeGrid (notes, stepTicksD (div, mod, ppq), true, opt.detectShuffle);
        const double f16 = offGrid (notes, grid, tol);
        rep.suggestTriplet = f16 > 0.2;
        rep.offGridFraction = f16;
        // A 2:1 shuffle on 1/16 puts notes where 1/8 triplets go; then the clip end decides.
        const bool tripletFeel = grid.shufflePct >= 64 && grid.shufflePct <= 70;
        if (f16 > 0.2 || tripletFeel)
        {
            const Grid tri = makeGrid (notes, stepTicksD (StepDiv::d1_8, DivMod::Triplet, ppq), false, false);
            const double ft = offGrid (notes, tri, tol);
            auto onGrid = [&] (double stepLen) {
                const double eot = static_cast<double> (track->endTick);
                return std::abs (eot - std::round (eot / stepLen) * stepLen) <= tol * stepLen;
            };
            if (ft < f16 || (ft == f16 && onGrid (tri.stepT) && ! onGrid (grid.stepT)))
            {
                div = StepDiv::d1_8;
                mod = DivMod::Triplet;
                grid = tri;
                rep.offGridFraction = ft;
            }
        }
    }
    else
    {
        grid = makeGrid (notes, stepTicksD (div, mod, ppq), mod == DivMod::Full, opt.detectShuffle);
        rep.offGridFraction = offGrid (notes, grid, tol);
        rep.suggestTriplet = mod == DivMod::Full && rep.offGridFraction > 0.2;
    }
    const double stepT = grid.stepT;

    // Snap and reduce to one note per step: latest start, then highest velocity.
    struct Chosen { int step; InNote n; };
    std::vector<Chosen> chosen;
    for (const InNote& n : notes)
    {
        double dist = 0.0;
        const int k = grid.snap (static_cast<double> (n.on), dist);
        if (k < 0 || k >= kMaxSteps) { ++rep.notesDropped; continue; }
        if (! chosen.empty() && chosen.back().step == k)
        {
            ++rep.notesDropped;
            const InNote& c = chosen.back().n;
            if (n.on > c.on || (n.on == c.on && n.vel > c.vel)) chosen.back().n = n;
            continue;
        }
        if (! chosen.empty() && chosen.back().step > k) { ++rep.notesDropped; continue; }
        chosen.push_back (Chosen { k, n });
    }
    if (chosen.empty()) { rep.error = "no notes inside 64 steps"; return finish (false); }

    // Length.
    const int lastStep = chosen.back().step;
    double lastEnd = 0.0;
    for (const Chosen& c : chosen) lastEnd = std::max (lastEnd, static_cast<double> (c.n.off));
    int length = 0;
    {
        const double eot = static_cast<double> (track->endTick);
        const double nEot = std::round (eot / stepT);
        if (std::abs (eot - nEot * stepT) <= tol * stepT && nEot >= lastStep + 1 && nEot <= kMaxSteps)
            length = static_cast<int> (nEot);
        else
            length = clampInt (static_cast<long long> (std::ceil (lastEnd / stepT - 1.0e-9)), lastStep + 1, kMaxSteps);
    }
    const double clipEnd = length * stepT;

    // Gate estimate from plain notes.
    const double authentic = stepT / 2.0 + authenticExtraTicks (rep.bpm, ppq);
    double gateFrac = 0.5;
    int gateLength = 0;
    {
        std::vector<double> durs;
        for (std::size_t i = 0; i < chosen.size(); ++i)
        {
            // Plain: ends before the next step's onset (a tied or slid note crosses it).
            const InNote& n = chosen[i].n;
            const double dur = static_cast<double> (n.off - n.on);
            const bool overlapsNext = i + 1 < chosen.size() && n.off > chosen[i + 1].n.on;
            const double nextOnset = grid.onset (chosen[i].step + 1);
            if (! overlapsNext && static_cast<double> (n.off) <= nextOnset + 0.02 * stepT && dur < stepT) durs.push_back (dur);
        }
        if (! durs.empty())
        {
            std::sort (durs.begin(), durs.end());
            const double med = durs[durs.size() / 2];
            gateFrac = med / stepT;
            if (std::abs (med - authentic) <= 1.5) gateLength = 0;
            else gateLength = clampInt (std::lround (gateFrac * 100.0), 1, 100);
        }
    }
    const double tieThreshold = std::max (1.0, 0.5 * gateFrac * stepT);

    // Octave fit.
    constexpr int kLo = 12 * kMinOctave;          // relative to kBaseNote
    constexpr int kHi = 12 * kMaxOctave + 11;
    int shift = 0;
    {
        int bestCount = -1;
        for (int s = 0; s <= 120; s += 12)
            for (int sign : { 1, -1 })
            {
                const int sh = s * sign;
                int count = 0;
                for (const Chosen& c : chosen)
                {
                    const int rel = c.n.note - kBaseNote + sh;
                    if (rel >= kLo && rel <= kHi) ++count;
                }
                if (count > bestCount) { bestCount = count; shift = sh; }
            }
    }
    rep.octaveShift = shift;

    Pattern p {};
    // A track name is free text in whatever encoding the writing software used: UTF-8 when it
    // is valid UTF-8, otherwise most likely Latin-1 (older software); either way the stored
    // name is valid UTF-8, cut at a character boundary.
    utf8::copyName (p.name, kPatternNameLength, utf8::isValid (trackName) ? std::string (trackName) : utf8::fromLatin1 (trackName));
    p.length = static_cast<std::uint8_t> (length);
    p.div = div;
    p.divMod = mod;
    p.shuffle = static_cast<std::uint8_t> (grid.shufflePct);
    p.gateLength = static_cast<std::uint8_t> (gateLength);
    p.transpose = static_cast<std::int8_t> (opt.keepPitch ? clampInt (-shift, -48, 48) : 0);

    const int threshold = clampInt (opt.accentThreshold, 0, 127);
    for (std::size_t i = 0; i < chosen.size(); ++i)
    {
        const int k = chosen[i].step;
        if (k >= length) { ++rep.notesDropped; continue; }
        const InNote& n = chosen[i].n;
        Step& s = p.steps[static_cast<std::size_t> (k)];
        int rel = n.note - kBaseNote + shift;
        while (rel < kLo) rel += 12;
        while (rel > kHi) rel -= 12;
        s.state = StepState::Note;
        s.octave = static_cast<std::int8_t> ((rel - kLo) / 12 + kMinOctave);
        s.note = static_cast<std::uint8_t> ((rel - kLo) % 12);
        s.accent = n.vel > threshold;
        s.vibrato = n.vib;

        const bool hasNext = i + 1 < chosen.size() && chosen[i + 1].step < length;
        bool slide = false;
        if (hasNext)
        {
            const InNote& m = chosen[i + 1].n;
            slide = n.off > m.on || (n.note == m.note && m.sus && static_cast<double> (n.off) + tol * stepT >= static_cast<double> (m.on));
        }
        else
            slide = static_cast<double> (n.off) >= clipEnd - 1.0e-9 && chosen[0].step == 0;
        s.slide = slide;

        const int limit = hasNext ? chosen[i + 1].step : length;
        for (int m = k + 1; m < limit; ++m)
        {
            const double need = slide ? grid.onset (m) : grid.onset (m) + tieThreshold;
            if (static_cast<double> (n.off) < need) break;
            p.steps[static_cast<std::size_t> (m)].state = StepState::Tie;
        }
    }

    sanitizePattern (p);
    out = p;
    return finish (true);
}
}
