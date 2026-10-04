// Host transport scenarios checked in the audio: onsets detected from the engine's output
// land where the pattern says, whether the host rolls, is stopped with Run on, reports only
// changes, loops or relocates. Also External mode, MIDI out, and switching while running.
#include "EngineTestSupport.h"
#include "acid/common/Math.h"
#include "acid/seq/Player.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

using namespace etest;

namespace
{
/** Quarter-note pattern: a note every 4 sixteenths. */
const std::string kQuarters = "C . . . C . . . C . . . C . . .";

/** The voice opens its gate 1.9 to 5.4 ms after the event (turn-on delay) plus a short
    rise, and the onset detector adds a little. Every expected onset is checked within this. */
constexpr double kOnsetToleranceSeconds = 0.012;

bool onsetsMatch (const std::vector<std::int64_t>& found, const std::vector<std::int64_t>& expected, double sr, std::string& why)
{
    if (found.size() != expected.size())
    {
        why = "found " + std::to_string (found.size()) + " onsets, expected " + std::to_string (expected.size());
        for (const auto f : found) why += " " + std::to_string (f);
        return false;
    }
    const auto tol = static_cast<std::int64_t> (kOnsetToleranceSeconds * sr);
    for (std::size_t i = 0; i < found.size(); ++i)
    {
        const std::int64_t late = found[i] - expected[i];
        if (late < 0 || late > tol)
        {
            why = "onset " + std::to_string (i) + " at " + std::to_string (found[i]) + ", expected " + std::to_string (expected[i])
                + " (late by " + std::to_string (late) + ")";
            return false;
        }
    }
    return true;
}

std::vector<std::int64_t> expectedQuarterOnsets (double startQ, int count, double bpm, double sr, std::int64_t sampleOffset = 0)
{
    std::vector<std::int64_t> v;
    for (int i = 0; i < count; ++i)
        v.push_back (sampleOffset + sampleOfQuarter (startQ + static_cast<double> (i), bpm, sr));
    return v;
}
}

TEST_CASE ("transport: host playing from zero puts a note on every quarter", "[engine][transport]")
{
    Session s;
    s.params[kSeqRun] = 0.0f;       // host transport drives it
    s.setPattern (0, kQuarters);
    s.prepare();
    s.host.bpm = 120.0;
    s.host.play();
    const Rendered r = s.render (4 * 48000, 512);
    REQUIRE (allFinite (r));

    const auto onsets = detectOnsets (r.left, kFs);
    std::string why;
    CHECK (onsetsMatch (onsets, expectedQuarterOnsets (0.0, 8, 120.0, kFs), kFs, why));
    INFO (why);
    CHECK (r.status.back().hostRolling);
    CHECK (r.status.back().running);
    CHECK_FALSE (r.status.back().internalClock);
    CHECK (r.status.back().currentStep >= 0);
}

TEST_CASE ("transport: host stopped, Run on, the internal clock steps", "[engine][transport]")
{
    Session s;
    s.params[kSeqRun] = 1.0f;
    s.setPattern (0, kQuarters);
    s.prepare();
    s.host.bpm = 100.0;
    s.host.stop();
    const Rendered r = s.render (3 * 48000, 256);
    const auto onsets = detectOnsets (r.left, kFs);
    std::string why;
    CHECK (onsetsMatch (onsets, expectedQuarterOnsets (0.0, 5, 100.0, kFs), kFs, why));
    INFO (why);
    CHECK (r.status.back().internalClock);
    CHECK_FALSE (r.status.back().hostRolling);
    CHECK (r.status.back().running);

    // No transport at all (standalone): the same.
    Session t;
    t.params[kSeqRun] = 1.0f;
    t.setPattern (0, kQuarters);
    t.prepare();
    t.host.present = false;
    const Rendered r2 = t.render (3 * 48000, 256);
    CHECK (onsetsMatch (detectOnsets (r2.left, kFs), expectedQuarterOnsets (0.0, 6, 120.0, kFs), kFs, why));
    INFO (why);
}

TEST_CASE ("transport: a host that reports only changes sounds like one that reports every block", "[engine][transport]")
{
    const auto run = [] (bool newsOnly) {
        Session s;
        s.setPattern (0, "C D E F G A B C+ . . . . C:S D:A . .");
        s.prepare (kFs, 256);
        s.host.bpm = 133.0;
        s.host.newsOnlyOnChange = newsOnly;
        s.host.play();
        return s.render (3 * 48000, 256);
    };
    const Rendered every = run (false);
    const Rendered news = run (true);
    REQUIRE (peakOf (every.left) > 0.05f);
    CHECK (hashOf (every) == hashOf (news));
}

TEST_CASE ("transport: a host loop keeps the notes on the grid after each wrap", "[engine][transport][loop]")
{
    Session s;
    s.setPattern (0, kQuarters);
    s.prepare (kFs, 512);
    s.host.bpm = 120.0;
    s.host.looping = true;
    s.host.loopStart = 1.0;
    s.host.loopEnd = 3.0;       // two quarters, so the loop wraps every second
    s.host.locate (1.0);
    s.host.play();
    const Rendered r = s.render (5 * 48000, 512);

    // Notes at every quarter of host time; wrapping from 3.0 back to 1.0 lands on a note again.
    const auto onsets = detectOnsets (r.left, kFs);
    std::string why;
    CHECK (onsetsMatch (onsets, expectedQuarterOnsets (0.0, 10, 120.0, kFs), kFs, why));
    INFO (why);
    CHECK (allFinite (r));

    // The same loop with a host that does not cut its blocks at the loop point, in blocks far
    // larger than the prepared size: the engine splits them and wraps the position it hands to
    // each chunk, and the tracker wraps inside a chunk. Onsets stay on the quarter grid.
    Session u;
    u.setPattern (0, kQuarters);
    u.prepare (kFs, 128);
    u.host.bpm = 120.0;
    u.host.looping = true;
    u.host.loopStart = 1.0;
    u.host.loopEnd = 3.0;
    u.host.splitsAtLoop = false;
    u.host.locate (1.0);
    u.host.play();
    const Rendered r2 = u.render (5 * 48000, 4096);
    CHECK (onsetsMatch (detectOnsets (r2.left, kFs), expectedQuarterOnsets (0.0, 10, 120.0, kFs), kFs, why));
    INFO (why);
    CHECK (allFinite (r2));
}

TEST_CASE ("transport: relocation moves the playhead and no note is left hanging", "[engine][transport]")
{
    Session s;
    s.setPattern (0, "C . . . . . . . E . . . . . . .");    // notes on beats 1 and 3
    s.prepare (kFs, 512);
    s.host.bpm = 120.0;
    s.host.play();

    // After 1.2 s (position 2.4 quarters) jump to 7.9 quarters: the next note is at 8.0
    // (step 0 of bar 3), 0.1 quarter = 0.05 s after the jump.
    const std::int64_t jumpAt = 512 * 113;   // 57856 samples = 1.2053 s = 2.41 quarters
    const Rendered r = s.render (3 * 48000, 512, [&] (std::int64_t pos) {
        if (pos == jumpAt) s.host.locate (7.9);
    });
    REQUIRE (allFinite (r));

    const auto onsets = detectOnsets (r.left, kFs);
    std::vector<std::int64_t> expected;
    expected.push_back (sampleOfQuarter (0.0, 120.0, kFs));
    expected.push_back (sampleOfQuarter (2.0, 120.0, kFs));
    expected.push_back (jumpAt + sampleOfQuarter (0.1, 120.0, kFs));          // 8.0 after the jump
    expected.push_back (jumpAt + sampleOfQuarter (2.1, 120.0, kFs));          // 10.0
    std::string why;
    CHECK (onsetsMatch (onsets, expected, kFs, why));
    INFO (why);

    // Stop the host: the sounding note ends and nothing hangs.
    Session t;
    t.setPattern (0, "C ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~");   // one long tied note per bar
    t.prepare (kFs, 512);
    t.host.bpm = 120.0;
    t.host.play();
    const Rendered r2 = t.render (2 * 48000, 512, [&] (std::int64_t pos) {
        if (pos == 512 * 94) t.host.stop();
    });
    CHECK (peakOf (r2.left, 0, 47000) > 0.05f);
    CHECK (peakOf (r2.left, 60000, 96000) < 1.0e-3f);
    CHECK_FALSE (r2.status.back().gateOpen);
    CHECK_FALSE (r2.status.back().running);
}

TEST_CASE ("transport: tempo change re-times the steps", "[engine][transport]")
{
    Session s;
    s.setPattern (0, kQuarters);
    s.prepare (kFs, 480);
    s.host.bpm = 120.0;
    s.host.play();
    const Rendered r = s.render (4 * 48000, 480, [&] (std::int64_t pos) {
        if (pos == 96000) { s.host.ppqBase = s.host.ppq(); s.host.samplesFromBase = 0; s.host.bpm = 60.0; s.host.changed = true; }
    });
    const auto onsets = detectOnsets (r.left, kFs);
    std::vector<std::int64_t> expected = expectedQuarterOnsets (0.0, 4, 120.0, kFs);      // 0, 0.5, 1.0, 1.5 s
    expected.push_back (96000);                                                             // 2.0 s, now at 60 BPM
    expected.push_back (96000 + 48000);                                                     // 3.0 s
    std::string why;
    CHECK (onsetsMatch (onsets, expected, kFs, why));
    INFO (why);
}

TEST_CASE ("external: MIDI notes play with slide and accent", "[engine][external]")
{
    Session s;
    s.params[kPlayMode] = 0.0f;
    s.params[kSlideTime] = 50.0f;
    s.settings.accentThreshold = 64;
    s.prepare (kFs, 256);

    // Note 1 (C4) plain, then an overlapping G4 (slide), then after a gap a loud C4 (accent).
    s.midiIn = { noteOn (4800, 60, 60), noteOn (24000, 67, 60), noteOff (24100, 60), noteOff (43200, 67),
                 noteOn (52800, 60, 110), noteOff (72000, 60) };
    const Rendered r = s.render (96000, 256);
    REQUIRE (allFinite (r));

    // Onsets: the slid note does not retrigger, so two onsets, not three.
    const auto onsets = detectOnsets (r.left, kFs);
    REQUIRE (onsets.size() == 2);
    CHECK (onsets[0] >= 4800);
    CHECK (onsets[0] < 4800 + 600);
    CHECK (onsets[1] >= 52800);
    CHECK (onsets[1] < 52800 + 600);

    // The slide: right after the second note-on the pitch is still near C4 and glides to G4.
    const double early = estimatePitchHz (r.left, 24000 + 100, 800, kFs, 150.0);
    const double late = estimatePitchHz (r.left, 24000 + 12000, 2400, kFs, 150.0);
    const double before = estimatePitchHz (r.left, 12000, 2400, kFs, 150.0);
    const double c4 = midiNoteToHz (60.0), g4 = midiNoteToHz (67.0);
    CHECK (std::abs (before - c4) / c4 < 0.03);
    CHECK (std::abs (late - g4) / g4 < 0.03);
    CHECK (early < 0.5 * (c4 + g4));
    CHECK (early > c4 * 0.9);

    // Without the overlap the same note jumps straight to G4.
    Session t;
    t.params[kPlayMode] = 0.0f;
    t.prepare (kFs, 256);
    t.midiIn = { noteOn (4800, 60, 60), noteOff (19200, 60), noteOn (24000, 67, 60), noteOff (43200, 67) };
    const Rendered r2 = t.render (48000, 256);
    const double jumped = estimatePitchHz (r2.left, 24000 + 400, 800, kFs, 150.0);
    CHECK (std::abs (jumped - g4) / g4 < 0.05);
    CHECK (detectOnsets (r2.left, kFs).size() == 2);

    // Accent: the loud note latches the accent and comes out louder than the plain note.
    bool sawAccent = false;
    for (std::size_t b = 0; b < r.status.size(); ++b)
        if (static_cast<std::int64_t> (b) * 256 > 52800 && static_cast<std::int64_t> (b) * 256 < 72000 && r.status[b].accentLatched)
            sawAccent = true;
    CHECK (sawAccent);
    CHECK (peakOf (r.left, 52800, 72000) > 1.3f * peakOf (r.left, 4800, 24000));
    CHECK (r.status.back().playMode == 0);
    CHECK (r.status.back().playingSlot == -1);

    // Nothing hangs after the last note-off.
    CHECK (peakOf (r.left, 80000, 96000) < 1.0e-3f);
    CHECK_FALSE (r.status.back().gateOpen);
}

TEST_CASE ("external: the mod wheel scales vibrato, and the wheel is silent in the pattern modes", "[engine][external][vibrato]")
{
    // External mode: with the wheel at 0 a held note has a steady pitch, with the wheel up it trills.
    const auto held = [] (int wheel) {
        Session s;
        s.params[kPlayMode] = 0.0f;
        s.params[kVibDepth] = 100.0f;
        s.params[kVibSpeed] = 50.0f;
        s.params[kCutoff] = 70.0f;
        s.prepare (kFs, 256);
        s.midiIn = { controller (0, 1, wheel), noteOn (100, 72, 60), noteOff (90000, 72) };
        return s.render (96000, 256);
    };
    const Rendered steady = held (0);
    const Rendered trill = held (127);
    const auto spread = [] (const Rendered& r) {
        double lo = 1.0e9, hi = 0.0;
        for (std::size_t from = 24000; from + 960 < 90000; from += 480)
        {
            const double f = estimatePitchHz (r.left, from, 960, kFs, 200.0);
            if (f > 0.0) { lo = std::min (lo, f); hi = std::max (hi, f); }
        }
        return hi / lo;
    };
    CHECK (spread (steady) < 1.01);
    CHECK (spread (trill) > 1.04);

    // Sequencer mode: a CC1 at 127 arriving must not add vibrato to unflagged steps.
    const auto seq = [] (bool wheel) {
        Session s;
        s.params[kSeqRun] = 1.0f;
        s.params[kVibDepth] = 100.0f;
        s.params[kCutoff] = 70.0f;
        s.setPattern (0, "C+++ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~");
        s.prepare (kFs, 256);
        if (wheel) s.midiIn = { controller (0, 1, 127) };
        return s.render (96000, 256);
    };
    CHECK (spread (seq (false)) < 1.01);
    CHECK (spread (seq (true)) < 1.01);

    // And a flagged step does trill in Sequencer mode.
    Session v;
    v.params[kSeqRun] = 1.0f;
    v.params[kVibDepth] = 100.0f;
    v.params[kCutoff] = 70.0f;
    v.setPattern (0, "C+++:V ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~");
    v.prepare (kFs, 256);
    CHECK (spread (v.render (96000, 256)) > 1.04);
}

TEST_CASE ("midi out: the engine's MIDI output is the player's, with the configured velocities", "[engine][midiout]")
{
    Session s;
    s.params[kSeqRun] = 1.0f;
    s.settings.outAccentVelocity = 100;
    s.settings.outNormalVelocity = 40;
    s.setPattern (0, "C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    s.prepare (kFs, 300);
    s.host.bpm = 125.0;
    s.host.play();
    const Rendered r = s.render (4 * 48000, randomBlocks (5, 1, 2000));
    REQUIRE (! r.midiOut.empty());

    // The reference: the player on its own with the same inputs, in one block size.
    seq::Player player;
    player.prepare (kFs, 512);
    seq::PlayerOutput out;
    seq::PlayerControls ctl;
    ctl.playMode = seq::PlayMode::Sequencer;
    ctl.run = true;
    const seq::PlayerConfig cfg = seq::playerConfigFrom (s.settings);
    Host host;
    host.bpm = 125.0;
    host.play();
    std::vector<TimedMidi> reference;
    std::vector<std::pair<std::int64_t, seq::VoiceEvent>> voiceEvents;
    for (std::int64_t pos = 0; pos < 4 * 48000; pos += 512)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (512, 4 * 48000 - pos));
        player.process (host.info(), {}, *s.bank, ctl, cfg, n, out);
        for (const auto& m : out.midi()) reference.push_back ({ pos + m.offset, m });
        for (const auto& e : out.voice()) voiceEvents.emplace_back (pos + e.offset, e);
        host.advance (n);
    }

    REQUIRE (r.midiOut.size() == reference.size());
    for (std::size_t i = 0; i < reference.size(); ++i)
    {
        CHECK (r.midiOut[i].sample == reference[i].sample);
        CHECK (r.midiOut[i].msg.size == reference[i].msg.size);
        CHECK (std::memcmp (r.midiOut[i].msg.data, reference[i].msg.data, 3) == 0);
    }

    // Every voice NoteOn has a MIDI note-on at the same sample with the matching pitch and velocity.
    int matched = 0;
    for (const auto& [sample, e] : voiceEvents)
    {
        if (e.type != seq::VoiceEvent::Type::NoteOn) continue;
        bool found = false;
        for (const auto& m : r.midiOut)
            if (m.sample == sample && (m.msg.data[0] & 0xF0) == 0x90 && m.msg.data[1] == e.note && m.msg.data[2] > 0)
            {
                CHECK (m.msg.data[2] == (e.accent ? 100 : 40));
                found = true;
            }
        if (found) ++matched;
        CHECK (found);
    }
    CHECK (matched >= 16);

    // Balanced: as many note-offs as note-ons, once Run stops.
    Session t;
    t.params[kSeqRun] = 1.0f;
    t.setPattern (0, "C:S C+:S D# ~ ~ G-:AS C . C:S C+ . A# ~ ~ . C:A");
    t.prepare (kFs, 256);
    t.host.bpm = 125.0;
    t.host.play();
    const Rendered r2 = t.render (3 * 48000, 256, [&] (std::int64_t pos) { if (pos == 2 * 48000) t.params[kSeqRun] = 0.0f, t.host.stop(); });
    int ons = 0, offs = 0;
    for (const auto& m : r2.midiOut)
    {
        const int st = m.msg.data[0] & 0xF0;
        if (st == 0x90 && m.msg.data[2] > 0) ++ons;
        if (st == 0x80 || (st == 0x90 && m.msg.data[2] == 0)) ++offs;
    }
    CHECK (ons > 0);
    CHECK (ons == offs);
}

TEST_CASE ("switching: modes, slots and FX order while running never break the output or leave a note on", "[engine][switching]")
{
    Session s;
    s.params = typicalParams();
    s.params[kSeqRun] = 1.0f;
    s.params[kChoOn] = 1.0f;
    s.params[kDelayOn] = 1.0f;
    s.params[kEqOn] = 1.0f;
    s.params[kLimOn] = 1.0f;
    s.setPattern (0, "C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    s.setPattern (1, "C:S ~ ~ ~ C+:S ~ ~ ~ G:S ~ ~ ~ D#:S ~ ~ ~");
    s.setPattern (13, "A- . A- . A- . A- . A- . A- . A- . A-:S A-:S");
    s.bank->arp[0] = patternFromText ("1 2 3 1+ 2+ 3+ 1 3 2 1- . 2 3:A 1:S 2 1");
    s.settings.launchQuant = seq::LaunchQuant::Immediate;
    s.prepare (kFs, 256);
    s.host.bpm = 140.0;
    s.host.play();

    Rng rng (99);
    const std::array<std::uint8_t, 6> orders[] = { { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }, { 2, 0, 3, 1, 5, 4 }, { 1, 2, 0, 5, 3, 4 } };
    int orderIndex = 0;
    const Rendered r = s.render (8 * 48000, randomBlocks (21, 1, 1500), [&] (std::int64_t pos) {
        if (pos % 4800 < 1500 && rng.chance (0.3))
        {
            const int mode = rng.nextInt (3);
            s.params[kPlayMode] = static_cast<float> (mode);
            if (mode == 2 || mode == 0)
            {
                s.midiIn.push_back (noteOn (pos + 5, 36 + rng.nextInt (24), 100));
                s.midiIn.push_back (noteOn (pos + 7, 48 + rng.nextInt (12), 127));
                s.midiIn.push_back (noteOff (pos + 2000 + rng.nextInt (3000), 36));
            }
        }
        if (rng.chance (0.1))
        {
            s.params[kPatternSlot] = static_cast<float> (1 + rng.nextInt (3));
            s.params[kPatternBank] = static_cast<float> (rng.nextInt (2));
        }
        if (rng.chance (0.05))
        {
            orderIndex = (orderIndex + 1) % 4;
            s.settings.fxOrder = orders[orderIndex];
            s.publish();
        }
        if (rng.chance (0.05)) s.params[kFxBypass] = rng.chance (0.5) ? 1.0f : 0.0f;
        if (rng.chance (0.05)) s.params[kDistOn] = rng.chance (0.5) ? 1.0f : 0.0f;
        // Settle down at the end: Sequencer mode, host stopped, Run off, all keys off.
        if (pos >= 6 * 48000)
        {
            s.params[kPlayMode] = 1.0f;
            s.params[kSeqRun] = 0.0f;
            s.params[kFxBypass] = 0.0f;
            s.host.stop();
            if (pos < 6 * 48000 + 1500)
                for (int n = 0; n < 128; ++n) s.midiIn.push_back (noteOff (pos + 1, n));
        }
    });
    std::sort (s.midiIn.begin(), s.midiIn.end(), [] (const TimedMidi& a, const TimedMidi& b) { return a.sample < b.sample; });

    REQUIRE (allFinite (r));
    CHECK (peakOf (r.left) > 0.05f);
    CHECK (peakOf (r.left) < 4.0f);
    CHECK_FALSE (r.status.back().gateOpen);
    CHECK_FALSE (r.status.back().running);
    // The last half second: tails only (delay and reverb), decaying; nothing is held.
    CHECK (peakOf (r.left, 7 * 48000 + 24000, 8 * 48000) < peakOf (r.left, 6 * 48000, 6 * 48000 + 24000) + 1.0e-3f);
}

TEST_CASE ("reset: panic clears the voice, the accent capacitor and the effects", "[engine][reset]")
{
    Session s;
    s.params = typicalParams();
    s.params[kSeqRun] = 1.0f;
    s.params[kDelayOn] = 1.0f;
    s.setPattern (0, "C:A ~ ~ ~ ~ ~ ~ ~ C:A ~ ~ ~ ~ ~ ~ ~");
    s.prepare (kFs, 256);
    s.host.bpm = 120.0;
    s.host.play();
    const Rendered before = s.render (52800, 256);
    CHECK (before.status.back().gateOpen);
    CHECK (before.status.back().accentCharge > 0.0f);

    s.engine.reset();
    const EngineStatus st = s.engine.status();
    CHECK_FALSE (st.gateOpen);
    CHECK (st.accentCharge == 0.0f);
    CHECK (st.envelope == 0.0f);
    CHECK (st.peakLeft == 0.0f);
    CHECK_FALSE (st.running);

    // Render silence after the reset with Run off and the host stopped: the tails are gone too.
    s.params[kSeqRun] = 0.0f;
    s.host.stop();
    const Rendered after = s.render (4800, 256);
    CHECK (peakOf (after.left) < 1.0e-4f);
}
