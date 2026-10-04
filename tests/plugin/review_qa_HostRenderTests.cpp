// QA (host integration): failures seen in REAPER 7.79 offline renders, reduced to the
// processor. See docs/host-matrix.md for the host evidence.
//
// 1. CLAP's render extension (clap_plugin_render.set) and LV2's freewheel port tell the
//    processor it is offline only after activate/prepareToPlay. The processor then re-prepares
//    from its 30 Hz message-thread timer (followQualityTier) while the host keeps processing:
//    blocks go silent, the sequencer restarts, the reported latency moves mid-render, and the
//    result depends on when the timer fired. A REAPER CLAP or LV2 render of the factory default
//    drops notes and differs from run to run.
// 2. CLAP: the latency reported before the first activate is 0 and prepareToPlay raises it,
//    which makes clap-juce-extensions call host->request_restart(). REAPER serves the restart
//    on its main thread, sometimes in the middle of an offline render (a silent gap of about
//    15 blocks and a sequencer reset in 1 of 5 renders with equal tiers).
#include "PluginTestSupport.h"
#include "FactoryContent.h"

#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
std::string factoryDefaultText()
{
    const FactoryFile* f = findFactoryFile ("default.mw12f");
    REQUIRE (f != nullptr);
    return std::string (f->text());
}

struct OfflineRun
{
    Rendered out;
    std::vector<int> latencies;
};

enum class Order { vst3, clapLv2, realtime };

/** Renders 4 bars of the factory default at 125 BPM with the host playing from ppq 0, the
    way a REAPER render does. Order::vst3: setupProcessing(kOffline) before setActive.
    Order::clapLv2: prepare first, then the offline flag. Order::realtime: no offline flag at
    all. The message thread runs every third block, as the 30 Hz timer does during a 3x render. */
OfflineRun renderOffline (Order order, acid::Quality* preparedTier = nullptr)
{
    const bool offlineBeforePrepare = order == Order::vst3;
    Driver d;
    d.processor.loadStateText (factoryDefaultText());
    d.processor.handlePendingMessages();
    d.playHead.present = true;
    d.playHead.playing = true;
    d.playHead.bpm = 125.0;
    d.playHead.ppq = 0.0;

    if (offlineBeforePrepare)
        d.processor.setNonRealtime (true);
    d.prepare (kFs, 512);
    if (order == Order::clapLv2)
        d.processor.setNonRealtime (true);
    if (preparedTier != nullptr)
        *preparedTier = d.processor.preparedQualityTier();

    OfflineRun run;
    run.latencies.push_back (d.processor.getLatencySamples());
    const std::int64_t total = static_cast<std::int64_t> (4 * 4 * 60.0 / 125.0 * kFs);
    int block = 0;
    run.out = d.render (total, 512, [&] (std::int64_t) {
        if (block++ % 3 == 2)
            d.processor.handlePendingMessages();
        run.latencies.push_back (d.processor.getLatencySamples());
    });
    return run;
}
}

// Contract (architect decision for this finding): the processor never re-prepares mid-stream.
// The tier is read in prepareToPlay only, so an offline flag that arrives after prepare takes
// effect at the next prepare, and the render runs on at the tier prepare chose. The case as
// first written compared the CLAP/LV2 order against the VST3 order bit for bit, which only a
// tier switch inside the render could satisfy (the fix this decision rules out). What it pins
// now: the latency never moves, no step is lost, the render is exactly what a render at the
// prepared tier gives (bit-identical, run to run), and the VST3 order still gets the offline tier.
TEST_CASE ("review_qa: an offline flag set after prepare renders like offline from the start", "[review_qa][host][quality]")
{
    ScratchConfigDir scratch;
    acid::Quality vst3Tier {}, clapTier {}, realtimeTier {};
    const OfflineRun vst3Order = renderOffline (Order::vst3, &vst3Tier);
    const OfflineRun clapOrder = renderOffline (Order::clapLv2, &clapTier);
    const OfflineRun clapAgain = renderOffline (Order::clapLv2);
    const OfflineRun realtime = renderOffline (Order::realtime, &realtimeTier);

    INFO ("latency first/last, VST3 order: " << vst3Order.latencies.front() << "/" << vst3Order.latencies.back()
          << ", CLAP/LV2 order: " << clapOrder.latencies.front() << "/" << clapOrder.latencies.back());

    // The latency a host compensates must not move while it renders.
    CHECK (std::adjacent_find (clapOrder.latencies.begin(), clapOrder.latencies.end(), std::not_equal_to<>()) == clapOrder.latencies.end());
    CHECK (std::adjacent_find (vst3Order.latencies.begin(), vst3Order.latencies.end(), std::not_equal_to<>()) == vst3Order.latencies.end());

    // Every step the sequencer plays in one order it plays in the other.
    CHECK (countNoteOns (clapOrder.out.midiOut) == countNoteOns (vst3Order.out.midiOut));

    // The tiers: the VST3 order prepares offline, the CLAP/LV2 order keeps the tier it was
    // prepared with (the realtime one) for the whole render.
    CHECK (vst3Tier != realtimeTier);
    CHECK (clapTier == realtimeTier);

    // Same project, same prepared tier: the same audio, every time.
    const auto firstDiff = [] (const Rendered& a, const Rendered& b) {
        for (std::size_t i = 0; i < a.left.size(); ++i)
            if (a.left[i] != b.left[i] || a.right[i] != b.right[i]) return i;
        return a.left.size();
    };
    INFO ("first sample that differs from the realtime-tier render: " << firstDiff (clapOrder.out, realtime.out));
    CHECK (firstDiff (clapOrder.out, realtime.out) == clapOrder.out.left.size());
    CHECK (firstDiff (clapOrder.out, clapAgain.out) == clapOrder.out.left.size());
}

TEST_CASE ("review_qa: the latency known before the first prepare is the latency prepare reports", "[review_qa][host][clap]")
{
    // clap-juce-extensions turns any latency change it hears about into host->request_restart().
    // A value that only appears inside the first activate is such a change.
    ScratchConfigDir scratch;
    mw12FProcessor p;
    const int before = p.getLatencySamples();
    p.setPlayConfigDetails (0, 2, kFs, 512);
    p.prepareToPlay (kFs, 512);
    const int after = p.getLatencySamples();
    INFO ("latency before the first prepare " << before << ", after " << after);
    CHECK (before == after);
    p.releaseResources();
}
