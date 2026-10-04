// Event timing against closed-form expectations, at several tempos and sample rates, and
// block-size invariance of a whole song.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace ptest;

TEST_CASE ("Timing: onsets and authentic gates are exact to the sample at every tempo and rate", "[timing]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (double bpm : { 40.0, 91.3, 120.0, 137.5, 174.0, 300.0 })
        {
            Session s (sr);
            setSteps (s.seq(), "C D E F G A B C+ C D E F G A B C+");
            s.host.bpm = bpm;
            s.host.play();

            const std::int64_t total = sampleOf (16.0L * 0.25L, bpm, sr);     // one pass: every step fires once
            s.run (total, 333);
            s.host.stop();
            s.run (64, 64);

            const auto notes = s.notes();
            REQUIRE (notes.size() == 32);

            for (int k = 0; k < 16; ++k)
            {
                const long double onQ = 0.25L * k;
                const long double offQ = onQ + 0.125L + 1.35e-3L * static_cast<long double> (bpm) / 60.0L;

                INFO ("sr " << sr << " bpm " << bpm << " step " << k);
                REQUIRE (notes[static_cast<std::size_t> (2 * k)].type == VoiceEvent::Type::NoteOn);
                REQUIRE (notes[static_cast<std::size_t> (2 * k)].sample == sampleOf (onQ, bpm, sr));
                REQUIRE (notes[static_cast<std::size_t> (2 * k + 1)].type == VoiceEvent::Type::NoteOff);
                REQUIRE (notes[static_cast<std::size_t> (2 * k + 1)].sample == sampleOf (offQ, bpm, sr));
            }

            // The authentic gate is half a step plus 1.35 ms in real time, at any tempo.
            const double gateSeconds = static_cast<double> (notes[1].sample - notes[0].sample) / sr;
            REQUIRE (std::abs (gateSeconds - (0.5 * 0.25 * 60.0 / bpm + 1.35e-3)) < 1.01 / sr);

            REQUIRE (s.allocations == 0);
            REQUIRE (s.orderViolations == 0);
        }
    }
}

TEST_CASE ("Timing: divisions, shuffle and percentage gates against the closed form", "[timing]")
{
    struct Case { StepDiv div; DivMod mod; int shuffle; int gate; };

    const Case cases[] = {
        { StepDiv::d1_16, DivMod::Full, 0, 50 },
        { StepDiv::d1_16, DivMod::Full, 60, 25 },
        { StepDiv::d1_16, DivMod::Full, 100, 80 },
        { StepDiv::d1_32, DivMod::Full, 33, 70 },
        { StepDiv::d1_64, DivMod::Full, 100, 40 },
        { StepDiv::d1_8, DivMod::Triplet, 75, 60 },      // shuffle ignored
        { StepDiv::d1_16, DivMod::Dotted, 75, 90 },      // shuffle ignored
        { StepDiv::d1_4, DivMod::Full, 20, 10 },
        { StepDiv::d1_2, DivMod::Triplet, 0, 35 },
        { StepDiv::d1_1, DivMod::Full, 50, 5 },
    };

    for (double sr : { 44100.0, 96000.0 })
    {
        for (double bpm : { 97.0, 126.0, 181.5 })
        {
            for (const Case& c : cases)
            {
                Session s (sr);
                setSteps (s.seq(), "C D E F G A B");          // seven steps: odd, so the parity matters
                s.seq().div = c.div;
                s.seq().divMod = c.mod;
                s.seq().shuffle = static_cast<std::uint8_t> (c.shuffle);
                s.seq().gateLength = static_cast<std::uint8_t> (c.gate);
                s.host.bpm = bpm;
                s.host.play();

                const long double stepQ = stepLengthQ (c.div, c.mod);
                const long double swingQ = c.mod == DivMod::Full ? 0.5L * c.shuffle / 100.0L * stepQ : 0.0L;
                const long double sampleQ = static_cast<long double> (bpm) / (60.0L * sr);
                auto onsetQ = [&] (int g) { return g * stepQ + (g % 2 == 1 ? swingQ : 0.0L); };

                const int steps = 20;
                s.run (sampleOf (onsetQ (steps), bpm, sr), 1000);
                s.host.stop();
                s.run (64, 64);

                const auto notes = s.notes();
                REQUIRE (notes.size() == static_cast<std::size_t> (2 * steps));

                for (int g = 0; g < steps; ++g)
                {
                    const long double window = onsetQ (g + 1) - onsetQ (g);
                    const long double gateQ = std::min (c.gate / 100.0L * stepQ, window - sampleQ);

                    INFO ("sr " << sr << " bpm " << bpm << " div " << static_cast<int> (c.div) << " mod " << static_cast<int> (c.mod)
                                << " shuffle " << c.shuffle << " gate " << c.gate << " step " << g);
                    REQUIRE (notes[static_cast<std::size_t> (2 * g)].sample == sampleOf (onsetQ (g), bpm, sr));
                    REQUIRE (notes[static_cast<std::size_t> (2 * g)].note == kC + std::vector<int> { 0, 2, 4, 5, 7, 9, 11 }[static_cast<std::size_t> (g % 7)]);
                    REQUIRE (notes[static_cast<std::size_t> (2 * g + 1)].sample == sampleOf (onsetQ (g) + gateQ, bpm, sr));
                    REQUIRE (notes[static_cast<std::size_t> (2 * g + 1)].sample < sampleOf (onsetQ (g + 1), bpm, sr));
                }
            }
        }
    }
}

TEST_CASE ("Timing: shuffle and gate arithmetic in round numbers", "[timing]")
{
    // 120 BPM, 48 kHz: a step is 6000 samples.
    Session s;
    setSteps (s.seq(), "C D E F");
    s.seq().shuffle = 50;                 // odd steps 25 % of a step late: 62.5 : 37.5
    s.seq().gateLength = 25;              // 1500 samples
    s.seq().steps[2].gate = 64;           // 64/127 of a step: 3023.6 samples
    s.host.play();
    s.run (4 * kStep, 64);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC), off (1500),
        on (kStep + 1500, kC + 2), off (kStep + 3000),
        on (2 * kStep, kC + 4), off (2 * kStep + 3024),
        on (3 * kStep + 1500, kC + 5), off (3 * kStep + 3000) });

    // 100 %: 75 : 25.
    Session full;
    setSteps (full.seq(), "C D");
    full.seq().shuffle = 100;
    full.seq().gateLength = 10;
    full.host.play();
    full.run (4 * kStep, 64);
    const auto ons = full.noteOns();
    REQUIRE (ons.size() == 4);
    REQUIRE (ons[1].sample - ons[0].sample == 9000);
    REQUIRE (ons[2].sample - ons[1].sample == 3000);
    REQUIRE (ons[3].sample - ons[2].sample == 9000);
}

TEST_CASE ("Timing: a slid chain of n notes is gated for (n - 0.5) steps plus 1.35 ms", "[timing]")
{
    for (double bpm : { 100.0, 125.0, 147.0 })
    {
        for (int n : { 1, 2, 3, 5 })
        {
            Session s;
            Pattern& p = s.seq();
            setSteps (p, "C D E F G . . .");
            for (int i = 0; i < n - 1; ++i) p.steps[static_cast<std::size_t> (i)].slide = true;
            for (int i = n; i < 5; ++i) p.steps[static_cast<std::size_t> (i)].state = StepState::Rest;
            s.host.bpm = bpm;
            s.host.play();
            s.run (sampleOf (2.0L, bpm, 48000.0), 128);
            s.host.stop();
            s.run (64, 64);

            const auto notes = s.notes();
            REQUIRE (notes.size() == static_cast<std::size_t> (n + 1));
            for (int i = 1; i < n; ++i) REQUIRE (notes[static_cast<std::size_t> (i)].legato);

            const long double offQ = (n - 0.5L) * 0.25L + 1.35e-3L * static_cast<long double> (bpm) / 60.0L;
            REQUIRE (notes.back() == off (sampleOf (offQ, bpm, 48000.0)));
        }
    }
}

TEST_CASE ("Timing: no drift over twenty minutes, whichever clock is running", "[timing]")
{
    const double sr = 44100.0;
    const double bpm = 137.0;
    const std::int64_t total = static_cast<std::int64_t> (sr) * 60 * 20;

    for (int feed = 0; feed < 3; ++feed)
    {
        Session s (sr);
        setSteps (s.seq(), "C");
        s.seq().gateLength = 30;
        s.host.bpm = bpm;

        switch (feed)
        {
            case 0: s.host.play(); break;                                        // a position every block
            case 1: s.host.newsOnlyOnChange = true; s.host.play(); break;        // one position, then silence
            case 2: s.host.present = false; s.ctl.internalBpm = bpm; s.ctl.run = true; break;   // standalone
        }

        s.run (total, 4096);

        const auto ons = s.noteOns();
        const std::size_t expected = static_cast<std::size_t> (std::ceil (static_cast<double> (total) * bpm / (60.0 * sr) * 4.0));
        INFO ("feed " << feed);
        REQUIRE (ons.size() == expected);

        std::size_t wrong = 0;
        for (std::size_t k = 0; k < ons.size(); ++k)
            if (ons[k].sample != sampleOf (0.25L * k, bpm, sr)) ++wrong;

        REQUIRE (wrong == 0);
        REQUIRE (s.allocations == 0);
    }
}

namespace
{
/** A session with several kinds of pattern, played through a scripted song. */
struct SongResult
{
    std::vector<Ev> voice;
    std::vector<MidiEv> midi;
};

void buildSongBank (Session& s)
{
    Pattern& a = s.seq (0);
    setSteps (a, "C:S D C+:A ~ . F:S G:AS A . C:V ~ ~ D#:S D#:A . G-");
    a.steps[2].ratchet = 3;
    a.steps[7].ratchet = 2;
    a.steps[5].probability = 60;
    a.steps[12].probability = 40;
    a.steps[15].gate = 120;
    a.gen.seed = 77;

    Pattern& b = s.seq (1);
    setSteps (b, "E:S F#:S A . B:A C+:S E+ . D ~ A-:S A-");
    b.shuffle = 66;
    b.gateLength = 85;
    b.transpose = -5;
    b.gen.seed = 5;

    Pattern& c = s.seq (2);
    setSteps (c, "C G:S C+ G:A D# A#:S D#+");
    c.div = StepDiv::d1_8;
    c.divMod = DivMod::Triplet;
    c.direction = Direction::PingPong;

    Pattern& d = s.seq (13);
    setSteps (d, "A:S . . C:A D . E:S ~ ~ . G A:S");
    d.div = StepDiv::d1_32;
    d.shuffle = 40;
    d.direction = Direction::Random;
    d.gen.seed = 991;
    for (auto& st : d.steps) st.probability = 80;

    Pattern& arp = s.arp (0);
    setSteps (arp, "C:S C C:A . C:S C+ C ~");
    arp.steps[1].arpIndex = 2;
    arp.steps[2].arpIndex = 3;
    arp.steps[4].arpIndex = 2;
    arp.steps[5].arpIndex = 4;
    arp.steps[6].ratchet = 2;
    arp.shuffle = 30;
}

/** The song: every kind of event the player reacts to, at fixed samples. blockSizer gives
    each block's size; blocks are cut at the action times, where a real host or engine would
    also start a new block (parameter changes are per block). */
SongResult renderSong (std::function<int()> blockSizer)
{
    Session s;
    buildSongBank (s);
    s.blockSizer = std::move (blockSizer);

    struct Action { std::int64_t at; std::function<void()> fn; };
    std::vector<Action> actions;
    auto at = [&] (std::int64_t sample, std::function<void()> fn) { actions.push_back ({ sample, std::move (fn) }); };

    const std::int64_t bar = 4 * 24000;     // at 120 BPM

    // Live mode with the host.
    at (0, [&] { s.host.play(); });
    at (bar + 777, [&] { s.ctl.slot = 1; });                                   // queued, end of pattern
    at (3 * bar + 5, [&] { s.ctl.ovrSlideSet = true; });
    at (3 * bar + 30011, [&] { s.ctl.ovrSlideSet = false; s.ctl.ovrAccentClr = true; });
    at (4 * bar, [&] { s.ctl.ovrAccentClr = false; s.ctl.liveTranspose = 7; s.cfg.launchQuant = LaunchQuant::NextBar; s.ctl.slot = 2; });
    at (5 * bar + 100, [&] { s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote; s.ctl.slot = 0; });
    at (6 * bar + 4321, [&] { s.seq (0).steps[4].state = StepState::Note; s.seq (0).length = 13; });
    at (7 * bar, [&] { s.host.setTempo (141.0); s.ctl.liveTranspose = 0; });
    at (8 * bar, [&] { s.host.locate (2.0); });                                // seek backwards
    at (9 * bar, [&] { s.cfg.launchQuant = LaunchQuant::Immediate; s.ctl.bank = 1; s.ctl.slot = 1; });   // slot 13
    at (9 * bar + 20000, [&] { s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest; s.ctl.ovrNoteSet = true; });
    at (9 * bar + 60000, [&] { s.ctl.ovrNoteSet = false; });
    at (10 * bar, [&] { s.host.setTempo (120.0); s.host.locate (16.0); s.host.looping = true; s.host.loopStart = 16.0;
                        s.host.loopEnd = 18.0; s.host.splitsAtLoop = false; s.ctl.bank = 0; s.ctl.slot = 0; });
    at (12 * bar, [&] { s.host.splitsAtLoop = true; s.host.changed = true; });
    at (13 * bar, [&] { s.host.looping = false; s.host.changed = true; s.cfg.hostLink = false; });
    at (13 * bar + 50000, [&] { s.host.locate (3.3); });                       // ignored: running free
    at (14 * bar, [&] { s.host.stop(); });
    at (14 * bar + 10000, [&] { s.ctl.run = true; });                          // the internal clock
    at (15 * bar, [&] { s.ctl.run = false; s.cfg.hostLink = true; });

    // Host Note mode.
    at (15 * bar + 1000, [&] { s.cfg.triggerMode = TriggerMode::HostNote; s.cfg.launchQuant = LaunchQuant::EndOfPattern; s.host.locate (0.0); s.host.play(); });
    s.keyDown (15 * bar + 24000, 24);
    s.keyUp (15 * bar + 70000, 24);
    s.keyDown (15 * bar + 70000, 25);
    s.keyDown (15 * bar + 99999, 26);
    s.keyUp (16 * bar + 30000, 25);
    s.keyUp (16 * bar + 50001, 26);

    // Arpeggiator.
    at (17 * bar, [&] { s.ctl.playMode = PlayMode::Arpeggiator; });
    s.keyDown (17 * bar + 500, 48);
    s.keyDown (17 * bar + 600, 55);
    s.keyDown (17 * bar + 30000, 52);
    s.keyUp (17 * bar + 60000, 48);
    s.keyDown (17 * bar + 61000, 64);
    s.keyUp (18 * bar, 55);
    s.keyUp (18 * bar + 3, 52);
    s.keyUp (18 * bar + 4000, 64);
    s.keyDown (18 * bar + 40000, 60);
    at (18 * bar + 50000, [&] { s.host.stop(); });
    s.keyUp (18 * bar + 80000, 60);

    // External.
    at (19 * bar, [&] { s.ctl.playMode = PlayMode::External; s.cfg.midiThru = true; });
    s.keyDown (19 * bar + 100, 40, 100);
    s.keyDown (19 * bar + 9000, 47, 30);
    s.midiAt (19 * bar + 12000, 0xE0, 0, 96);
    s.keyUp (19 * bar + 15000, 47);
    s.midiAt (19 * bar + 20000, 0xB0, 64, 127);
    s.keyUp (19 * bar + 21000, 40);
    s.midiAt (19 * bar + 22000, 0xB0, 1, 90);
    s.keyDown (19 * bar + 30000, 40, 127);
    s.keyUp (19 * bar + 40000, 40);
    s.midiAt (19 * bar + 50000, 0xB0, 64, 0);
    s.keyDown (19 * bar + 60000, 33, 70);
    at (20 * bar, [&] { s.ctl.playMode = PlayMode::Sequencer; s.cfg.triggerMode = TriggerMode::Live; s.host.play(); });
    s.keyUp (20 * bar + 10, 33);
    at (21 * bar, [&] { s.host.stop(); });

    const std::int64_t total = 21 * bar + 4096;
    std::size_t next = 0;

    while (s.now < total)
    {
        while (next < actions.size() && actions[next].at == s.now)
            actions[next++].fn();

        const std::int64_t until = next < actions.size() ? actions[next].at : total;
        s.run (until - s.now, 64);
    }

    REQUIRE (s.allocations == 0);
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.overflows == 0);
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    return { s.voice, s.midi };
}
}

TEST_CASE ("Timing: a whole song gives the identical event list for every block size", "[timing][invariance]")
{
    const SongResult reference = renderSong ([] { return 64; });

    // Kept for inspection.
    {
        std::ofstream file (std::string (ACID_TEST_OUTPUT_DIR) + "/player-song-events.txt");
        for (const Ev& e : reference.voice) file << e << "\n";
    }

    // The song has to exercise the things it is meant to: notes, legato notes, MIDI out.
    std::size_t ons = 0, legato = 0, accents = 0;
    for (const Ev& e : reference.voice)
    {
        if (e.type != VoiceEvent::Type::NoteOn) continue;
        ++ons;
        if (e.legato) ++legato;
        if (e.accent) ++accents;
    }
    REQUIRE (ons > 200);
    REQUIRE (legato > 50);
    REQUIRE (accents > 40);
    REQUIRE (reference.midi.size() > 300);

    for (int blockSize : { 1, 17, 512, 4096 })
    {
        const SongResult r = renderSong ([blockSize] { return blockSize; });
        INFO ("block size " << blockSize);
        REQUIRE (r.voice.size() == reference.voice.size());
        REQUIRE (r.voice == reference.voice);
        REQUIRE (r.midi == reference.midi);
    }

    for (std::uint64_t seed : { 1ull, 2ull, 3ull })
    {
        acid::Rng rng (seed);
        const SongResult r = renderSong ([rng] () mutable { return 1 + rng.nextInt (rng.chance (0.2) ? 5000 : 300); });
        INFO ("random block sizes, seed " << seed);
        REQUIRE (r.voice == reference.voice);
        REQUIRE (r.midi == reference.midi);
    }
}
