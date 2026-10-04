// Independent review: step onsets and gate-offs against a closed-form model computed here
// in long double from the spec (section 6.4) and document 06 section 2, for random
// patterns, tempi, sample rates, start positions (negative included) and block sizes.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

using namespace ptest;

namespace
{
long double stepQOf (StepDiv d, DivMod m)
{
    long double q = 0.25L;
    switch (d)
    {
        case StepDiv::d1_64: q = 1.0L / 16; break;
        case StepDiv::d1_32: q = 1.0L / 8; break;
        case StepDiv::d1_16: q = 1.0L / 4; break;
        case StepDiv::d1_8:  q = 1.0L / 2; break;
        case StepDiv::d1_4:  q = 1.0L; break;
        case StepDiv::d1_2:  q = 2.0L; break;
        case StepDiv::d1_1:  q = 4.0L; break;
    }
    if (m == DivMod::Dotted) q *= 1.5L;
    if (m == DivMod::Triplet) q *= 2.0L / 3.0L;
    return q;
}

struct Expected
{
    std::int64_t onSample;
    std::int64_t offSample;
    int note;
};

int pitchFor (int pos) { return kC + pos; }   // every position has its own pitch
}

TEST_CASE ("Review: onsets and gate-offs match the closed form to the sample", "[review][closedform]")
{
    int checkedOns = 0, badOns = 0, badOffs = 0, badPitch = 0;

    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        acid::Rng rng (seed * 31u + 7u);
        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
        const double sr = rates[rng.nextInt (4)];
        Session s (sr, 4096);

        Pattern& p = s.seq();
        p.length = static_cast<std::uint8_t> (1 + rng.nextInt (48));
        p.div = static_cast<StepDiv> (rng.nextInt (5));          // 1/64 .. 1/4
        p.divMod = static_cast<DivMod> (rng.nextInt (3));
        p.shuffle = static_cast<std::uint8_t> (rng.chance (0.3) ? 0 : rng.nextInt (101));
        p.gateLength = static_cast<std::uint8_t> (rng.chance (0.5) ? 0 : 1 + rng.nextInt (100));

        for (int i = 0; i < p.length; ++i)
        {
            Step& st = p.steps[static_cast<std::size_t> (i)];
            st.state = StepState::Note;
            st.note = static_cast<std::uint8_t> (i % 12);
            st.octave = static_cast<std::int8_t> (i / 12);
            st.gate = static_cast<std::uint8_t> (rng.chance (0.7) ? 0 : 1 + rng.nextInt (127));
        }

        const double bpm = 40.0 + rng.nextInt (261);
        const bool internal = rng.chance (0.25);
        double startQ = 0.0;

        if (internal)
        {
            s.host.present = false;
            s.ctl.internalBpm = bpm;
            s.ctl.run = true;
        }
        else
        {
            s.host.bpm = bpm;
            // A start that is never inside the chase window of a step.
            const long double sq = stepQOf (p.div, p.divMod);
            const long double base = std::floor (static_cast<long double> (rng.nextDouble() * 40.0 - 8.0) / sq) * sq;
            const long double lo = 1.0L / 64 + 0.002L;
            startQ = static_cast<double> (base + lo + (0.9L * sq - lo) * static_cast<long double> (rng.nextDouble()));
            s.host.locate (startQ);
            s.host.newsOnlyOnChange = rng.chance (0.3);
            s.host.play();
        }

        const int blockMax = rng.chance (0.5) ? 64 : 4096;
        s.blockSizer = [&rng, blockMax] { return 1 + rng.nextInt (blockMax); };

        const std::int64_t total = static_cast<std::int64_t> (sr * 8.0);
        s.run (total);

        // Closed form.
        const long double qps = static_cast<long double> (bpm) / (60.0L * static_cast<long double> (sr));
        const long double sq = stepQOf (p.div, p.divMod);
        const long double swing = p.divMod == DivMod::Full ? 0.5L * (p.shuffle / 100.0L) * sq : 0.0L;
        auto onset = [&] (std::int64_t g) { return static_cast<long double> (g) * sq + ((((g % 2) + 2) % 2) == 1 ? swing : 0.0L); };
        auto sampleAt = [&] (long double q) { return static_cast<std::int64_t> (std::ceil ((q - startQ) / qps - 1.0e-3L)); };

        std::int64_t g = static_cast<std::int64_t> (std::floor (startQ / sq)) - 2;
        while (onset (g) < startQ - (internal ? 0.0L : 1.0L / 64)) ++g;

        std::vector<Expected> exp;
        for (;; ++g)
        {
            const long double on = onset (g);
            const std::int64_t onS = std::max<std::int64_t> (0, sampleAt (on));
            if (onS >= total) break;

            const int pos = static_cast<int> (((g % p.length) + p.length) % p.length);
            const Step& st = p.steps[static_cast<std::size_t> (pos)];
            long double gate = st.gate != 0 ? sq * st.gate / 127.0L
                             : p.gateLength != 0 ? sq * p.gateLength / 100.0L
                             : 0.5L * sq + 1.35e-3L * bpm / 60.0L;
            const long double dur = onset (g + 1) - on;
            if (gate > dur - qps) gate = dur - qps;
            if (gate < qps) gate = qps;
            exp.push_back ({ onS, std::max<std::int64_t> (onS + 1, sampleAt (on + gate)), pitchFor (pos) });
        }

        // Compare with what was played.
        std::vector<Ev> ons, offs;
        for (const Ev& e : s.voice)
        {
            if (e.type == VoiceEvent::Type::NoteOn) ons.push_back (e);
            if (e.type == VoiceEvent::Type::NoteOff) offs.push_back (e);
        }

        INFO ("seed " << seed << " sr " << sr << " bpm " << bpm << " startQ " << startQ << " div " << static_cast<int> (p.div)
              << " mod " << static_cast<int> (p.divMod) << " shuffle " << static_cast<int> (p.shuffle) << " internal " << internal);
        REQUIRE (ons.size() == exp.size());

        for (std::size_t i = 0; i < exp.size(); ++i)
        {
            ++checkedOns;
            if (ons[i].sample != exp[i].onSample) { ++badOns; if (badOns < 6) std::printf ("seed %llu on %zu: got %lld want %lld\n", (unsigned long long) seed, i, (long long) ons[i].sample, (long long) exp[i].onSample); }
            if (ons[i].note != exp[i].note) ++badPitch;
            if (i < offs.size() && exp[i].offSample < total && offs[i].sample != exp[i].offSample)
            {
                ++badOffs;
                if (badOffs < 6) std::printf ("seed %llu off %zu: got %lld want %lld\n", (unsigned long long) seed, i, (long long) offs[i].sample, (long long) exp[i].offSample);
            }
            if (i + 1 < exp.size()) CHECK (offs[i].sample < ons[i + 1].sample);   // a plain gate never reaches the next onset
        }
    }

    INFO ("checked " << checkedOns << " onsets");
    CHECK (checkedOns > 10000);
    CHECK (badOns == 0);
    CHECK (badOffs == 0);
    CHECK (badPitch == 0);
}
