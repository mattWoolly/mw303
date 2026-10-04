// Offline reproducibility (Engine.h, setNonRealtime): in non-realtime mode the voice and the
// effects start from rest when the transport starts, when it is relocated, and when the
// engine enters non-realtime mode, so a render does not depend on what ran before it.
// QA finding: CLAP renders after idle realtime processing gave three different outputs in
// four renders (the oscillator phase, drift and filter state carried over).
#include "EngineTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace etest;

namespace
{
constexpr const char* kLine = "C:A C+:A C:AS C+ ~ . D#:S D#+:A C . G-:AS G C+:A C+:A A#-:S C";

/** The render: a pattern on the host transport from bar 1, distortion, delay and reverb on. */
void renderParams (Session& s)
{
    s.params = typicalParams();
    s.params[kSeqRun] = 1.0f;
    s.params[kDelayOn] = 1.0f;
    s.params[kCalDrift] = 30.0f;     // the free-running pitch drift is part of what must reset
}

/** What a host might have done before the render: realtime blocks with the transport stopped,
    the line played by hand in External mode with other knob settings, so the voice, the
    drift generator, the delay and the reverb all hold something. */
void dirty (Session& s, std::uint64_t seed)
{
    s.params = typicalParams();
    s.params[kPlayMode] = 0.0f;
    s.params[kCutoff] = 80.0f;
    s.params[kResonance] = 95.0f;
    s.params[kDelayOn] = 1.0f;
    s.midiIn.clear();
    Rng rng (seed);
    std::int64_t t = 0;
    for (int i = 0; i < 12; ++i)
    {
        const int note = 36 + rng.nextInt (24);
        s.midiIn.push_back (noteOn (t, note, 60 + rng.nextInt (60)));
        t += 2000 + rng.nextInt (4000);
        s.midiIn.push_back (noteOff (t, note));
    }
    s.render (t + 5000 + rng.nextInt (3000), 512);
    s.midiIn.clear();
}

Rendered renderFromTop (Session& s, std::int64_t n, int block = 512)
{
    renderParams (s);
    s.host.locate (0.0);
    s.host.play();
    return s.render (n, block);
}

/** Equal tiers: without a prepare, a different offline tier only takes effect at the next
    prepare() (that is the host wrapper's job, Engine.h), and this is about state, not tiers. */
Session& setUp (Session& s)
{
    s.settings.offlineQuality = s.settings.realtimeQuality;
    s.setPattern (0, kLine);
    s.host.bpm = 125.0;
    return s;
}
}

TEST_CASE ("offline: a render after idle realtime processing equals a fresh instance's", "[engine][offline]")
{
    const auto n = static_cast<std::int64_t> (3.0 * kFs);

    Session fresh;
    setUp (fresh);
    fresh.engine.setNonRealtime (true);
    fresh.prepare (kFs, 512);
    const Rendered reference = renderFromTop (fresh, n);
    REQUIRE (peakOf (reference.left) > 0.05f);

    // The CLAP and LV2 path: no prepare between the idle realtime blocks and the render.
    for (const std::uint64_t seed : { 1u, 2u, 3u })
    {
        Session s;
        setUp (s);
        s.prepare (kFs, 512);
        dirty (s, seed);
        s.engine.setNonRealtime (true);
        dirty (s, seed + 100);                // offline, transport still stopped
        s.host.stop();
        const Rendered r = renderFromTop (s, n);
        INFO ("seed " << seed << ": largest difference " << maxAbsDiff (r.left, reference.left));
        CHECK (hashOf (r) == hashOf (reference));
    }

    // The VST3 path: the host prepares again after switching to offline.
    {
        Session s;
        setUp (s);
        s.prepare (kFs, 512);
        dirty (s, 7);
        s.engine.setNonRealtime (true);
        s.engine.prepare (kFs, 512);
        const Rendered r = renderFromTop (s, n);
        INFO ("prepared again: largest difference " << maxAbsDiff (r.left, reference.left));
        CHECK (hashOf (r) == hashOf (reference));
    }

    // Rendering twice in a row, with nothing between them but stopping the transport.
    {
        Session s;
        setUp (s);
        s.engine.setNonRealtime (true);
        s.prepare (kFs, 512);
        const Rendered first = renderFromTop (s, n);
        s.host.stop();
        s.render (4096, 512);
        const Rendered second = renderFromTop (s, n);
        CHECK (hashOf (first) == hashOf (reference));
        CHECK (hashOf (second) == hashOf (reference));
    }
}

TEST_CASE ("offline: a seek restarts from rest, a loop wrap carries on", "[engine][offline]")
{
    const auto bar = static_cast<std::int64_t> (4.0 * 60.0 / 125.0 * kFs);

    // A render that starts at bar 2 after the transport was already rolling elsewhere equals
    // a fresh render started at bar 2.
    Session fresh;
    setUp (fresh);
    fresh.engine.setNonRealtime (true);
    fresh.prepare (kFs, 512);
    renderParams (fresh);
    fresh.host.locate (4.0);
    fresh.host.play();
    const Rendered reference = fresh.render (2 * bar, 512);

    Session s;
    setUp (s);
    s.engine.setNonRealtime (true);
    s.prepare (kFs, 512);
    renderFromTop (s, bar / 2);
    s.host.locate (4.0);                       // still playing: a jump
    const Rendered r = s.render (2 * bar, 512);
    INFO ("largest difference " << maxAbsDiff (r.left, reference.left));
    CHECK (hashOf (r) == hashOf (reference));

    // A loop wrap is not a seek: the reverb tail carries across the loop point.
    Session looped;
    setUp (looped);
    looped.engine.setNonRealtime (true);
    looped.prepare (kFs, 512);
    renderParams (looped);
    looped.host.looping = true;
    looped.host.loopStart = 0.0;
    looped.host.loopEnd = 4.0;
    looped.host.locate (0.0);
    looped.host.play();
    const Rendered l = looped.render (2 * bar, 512);
    // The first samples after the wrap are not those of a start from rest.
    const auto wrapAt = static_cast<std::size_t> (bar);
    double tail = 0.0;
    for (std::size_t i = wrapAt; i < wrapAt + 64; ++i)
        tail = std::max (tail, static_cast<double> (std::abs (l.left[i] - reference.left[0])));
    CHECK (tail > 1.0e-4);
}

TEST_CASE ("offline: nothing resets in realtime mode", "[engine][offline]")
{
    // A live performance is never cut: a transport start in realtime mode leaves the voice
    // and the effects running, so the render differs from a fresh instance's.
    const auto n = static_cast<std::int64_t> (1.0 * kFs);
    Session fresh;
    setUp (fresh);
    fresh.prepare (kFs, 512);
    const Rendered reference = renderFromTop (fresh, n);

    Session s;
    setUp (s);
    s.prepare (kFs, 512);
    dirty (s, 1);
    const Rendered r = renderFromTop (s, n);
    CHECK (hashOf (r) != hashOf (reference));
}
