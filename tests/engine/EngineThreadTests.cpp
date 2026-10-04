// Publishing banks and settings from another thread while process() runs. Built and run
// under ThreadSanitizer in build-engine-tsan (on this machine under 'setarch -R'); in the
// normal build it still checks that nothing breaks and that the audio thread never allocates.
#include "EngineTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace etest;

TEST_CASE ("threads: banks and settings published while process() runs", "[engine][threads]")
{
    Session s;
    s.params = typicalParams();
    s.params[kSeqRun] = 1.0f;
    s.params[kChoOn] = 1.0f;
    s.params[kDelayOn] = 1.0f;
    s.setPattern (0, "C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    s.settings.launchQuant = seq::LaunchQuant::Immediate;
    s.prepare (kFs, 256);
    s.host.bpm = 140.0;
    s.host.play();

    std::atomic<bool> stop { false };
    std::atomic<int> published { 0 };

    // The message thread: edits its own copies of the model and publishes them, as the
    // plugin does behind its model mutex. It never touches what the audio thread reads.
    std::thread publisher ([&] {
        auto bank = std::make_unique<seq::PatternBank> (*s.bank);
        Settings settings = s.settings;
        Rng rng (1234);
        const std::array<std::uint8_t, 6> orders[] = { { 0, 1, 2, 3, 4, 5 }, { 5, 4, 3, 2, 1, 0 }, { 2, 0, 3, 1, 5, 4 } };
        while (! stop.load (std::memory_order_relaxed))
        {
            for (int k = 0; k < 4; ++k)
            {
                seq::Pattern& p = bank->seq[static_cast<std::size_t> (rng.nextInt (3))];
                for (int i = 0; i < 16; ++i)
                {
                    seq::Step& st = p.steps[static_cast<std::size_t> (i)];
                    st.state = rng.chance (0.7) ? seq::StepState::Note : seq::StepState::Rest;
                    st.note = static_cast<std::uint8_t> (rng.nextInt (12));
                    st.octave = static_cast<std::int8_t> (rng.nextInt (3) - 1);
                    st.accent = rng.chance (0.3);
                    st.slide = rng.chance (0.3);
                }
                p.length = static_cast<std::uint8_t> (8 + rng.nextInt (9));
            }
            s.engine.publishBank (*bank);

            settings.fxOrder = orders[rng.nextInt (3)];
            settings.slideIntoRest = static_cast<seq::SlideIntoRest> (rng.nextInt (3));
            settings.midiOut = rng.chance (0.5);
            s.engine.publishSettings (settings);

            published.fetch_add (1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // The audio thread: 6 seconds of blocks, with slot changes, under the allocation guard.
    s.guardAllocations = true;
    Rng rng (77);
    const auto perBlock = [&] (std::int64_t) {
        if (rng.chance (0.02)) s.params[kPatternSlot] = static_cast<float> (1 + rng.nextInt (3));
        (void) s.engine.status();
    };
    const Rendered r = s.render (6 * 48000, randomBlocks (8, 16, 1024), perBlock);

    // Under ASan or TSan the threads slow down by different factors, and a fixed length can
    // end before the publisher has made its count although nothing is wrong (QA finding): keep
    // processing (bounded) until it has.
    std::size_t moreAllocations = 0;
    const auto started = std::chrono::steady_clock::now();
    while (published.load() <= 10 && std::chrono::steady_clock::now() - started < std::chrono::seconds (60))
        moreAllocations += s.render (48000, randomBlocks (9, 16, 1024), perBlock).allocations;

    stop.store (true, std::memory_order_relaxed);
    publisher.join();

    CHECK (published.load() > 10);
    CHECK (r.allocations == 0);
    CHECK (moreAllocations == 0);
    CHECK (allFinite (r));
    CHECK (peakOf (r.left) > 0.02f);
    CHECK (peakOf (r.left) < 4.0f);
}

TEST_CASE ("threads: status() from another thread while processing", "[engine][threads]")
{
    Session s;
    s.params[kSeqRun] = 1.0f;
    s.setPattern (0, "C . E . G . B . C+ . . . A:S G . .");
    s.prepare (kFs, 128);
    s.host.play();

    std::atomic<bool> stop { false };
    std::atomic<int> reads { 0 };
    std::atomic<int> sawRunning { 0 };
    std::thread ui ([&] {
        while (! stop.load (std::memory_order_relaxed))
        {
            const EngineStatus st = s.engine.status();
            if (st.running) sawRunning.fetch_add (1, std::memory_order_relaxed);
            (void) s.engine.latencySamples();
            (void) s.engine.tailSeconds();
            reads.fetch_add (1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    const Rendered r = s.render (2 * 48000, 128);
    stop.store (true);
    ui.join();
    CHECK (reads.load() > 0);
    CHECK (sawRunning.load() > 0);
    CHECK (allFinite (r));
}
