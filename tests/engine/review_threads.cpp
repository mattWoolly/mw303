// Reviewer stress test: publishBank/publishSettings hammered from one thread, status() polled
// from another, process() on a third with MIDI input, oversized blocks and resets. Meant to
// run under ThreadSanitizer (setarch -R). Passing in a plain build only shows nothing breaks.
#include "EngineTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace etest;

TEST_CASE ("review: publish, status and process on three threads", "[.][review][review-threads]")
{
    auto engine = std::make_unique<Engine>();
    auto bank = std::make_unique<seq::PatternBank>();
    for (int i = 0; i < seq::kNumSlots; ++i)
        bank->seq[(std::size_t) i] = patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    Settings settings;
    settings.launchQuant = seq::LaunchQuant::Immediate;
    engine->publishSettings (settings);
    engine->publishBank (*bank);
    engine->prepare (48000.0, 256);

    std::atomic<bool> stop { false };
    std::atomic<long> published { 0 }, polled { 0 };

    std::thread publisher ([&] {
        auto b = std::make_unique<seq::PatternBank> (*bank);
        Settings s = settings;
        Rng rng (77);
        while (! stop.load (std::memory_order_relaxed))
        {
            auto& p = b->seq[(std::size_t) rng.nextInt (seq::kNumSlots)];
            for (auto& st : p.steps)
            {
                st.state = rng.chance (0.6) ? seq::StepState::Note : seq::StepState::Rest;
                st.note = (std::uint8_t) rng.nextInt (12);
                st.slide = rng.chance (0.3);
            }
            engine->publishBank (*b);
            s.fxOrder = rng.chance (0.5) ? std::array<std::uint8_t, 6> { 5, 4, 3, 2, 1, 0 } : std::array<std::uint8_t, 6> { 0, 1, 2, 3, 4, 5 };
            s.triggerMode = rng.chance (0.1) ? seq::TriggerMode::HostNote : seq::TriggerMode::Live;
            s.realtimeQuality = (Quality) rng.nextInt (4);
            engine->publishSettings (s);
            published.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::thread poller ([&] {
        while (! stop.load (std::memory_order_relaxed))
        {
            const EngineStatus st = engine->status();
            (void) engine->latencySamples();
            (void) engine->tailSeconds();
            if (st.peakLeft >= 0.0f) polled.fetch_add (1, std::memory_order_relaxed);
        }
    });

    std::size_t allocations = 0;
    std::thread audio ([&] {
        ParamValues p = typicalParams();
        p[kSeqRun] = 1.0f;
        p[kDelayOn] = 1.0f;
        std::vector<float> l (8192), r (8192);
        std::vector<seq::MidiMsg> out (4096);
        Rng rng (5);
        double ppq = 0.0;
        // 4000 blocks, then on until the publisher has made its count: under ASan or TSan the
        // threads run several times slower and not in proportion, so a fixed block count can
        // starve the publisher's counter although nothing is wrong (QA finding). Bounded.
        const auto started = std::chrono::steady_clock::now();
        for (int block = 0; block < 4000 || (published.load() <= 10 && std::chrono::steady_clock::now() - started < std::chrono::seconds (60)); ++block)
        {
            const int n = rng.chance (0.1) ? 8192 : 1 + rng.nextInt (600);
            seq::TransportInfo t;
            t.valid = rng.chance (0.8);
            t.playing = true;
            t.ppqValid = true;
            t.ppq = ppq;
            t.bpmValid = true;
            t.bpm = 140.0;
            p[kPlayMode] = (float) (block / 500 % 3);
            p[kPatternSlot] = (float) (1 + rng.nextInt (12));
            seq::MidiMsg msgs[2] = { noteOn (0, 48 + rng.nextInt (12)).msg, noteOff (n - 1, 48).msg };
            MidiOutBuffer mo { out.data(), (int) out.size(), 0 };
            {
                test::ScopedAllocGuard g;
                engine->process (p, t, { msgs, 2 }, mo, l.data(), r.data(), n);
                if (block % 997 == 0) engine->reset();
                allocations += g.count();
            }
            ppq += n * 140.0 / (60.0 * 48000.0);
        }
        stop.store (true);
    });

    audio.join();
    publisher.join();
    poller.join();
    INFO ("published " << published.load() << " polled " << polled.load());
    CHECK (allocations == 0);
    CHECK (published.load() > 10);
}
