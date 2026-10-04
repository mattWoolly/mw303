#include "SeqTestSupport.h"
#include "acid/common/Rng.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

using namespace acid::seq;
using acid::seq::test::randomPattern;

namespace
{
const char* kDefaultEncoding =
    "acidpat 1|name=|len=16|div=1/16|mod=full|shuffle=0|gate=0|dir=fwd|transpose=0|seed=1|notemask=fff|root=0|"
    "octmask=06|useslide=1|useaccent=1|usevib=0|amtnote=50|amtslide=50|amtaccent=50|amtvib=50|steps=";

Pattern goldenPattern()
{
    Pattern p;
    std::strcpy (p.name, "Night Line|1%");
    p.length = 3;
    p.div = StepDiv::d1_8;
    p.divMod = DivMod::Triplet;
    p.shuffle = 25;
    p.gateLength = 60;
    p.direction = Direction::PingPong;
    p.transpose = -5;
    p.steps[0].state = StepState::Note;
    p.steps[0].note = 3;
    p.steps[0].octave = 1;
    p.steps[0].accent = true;
    p.steps[0].slide = true;
    p.steps[1].state = StepState::Tie;
    p.steps[2].note = 7;
    p.steps[2].octave = -1;
    p.steps[2].locks = 5;
    p.steps[2].magGate = 17;
    p.steps[3].state = StepState::Note;
    p.steps[3].probability = 50;
    p.gen.seed = 4000000000u;
    p.gen.noteMask = 0x0A5;
    p.gen.root = 9;
    p.gen.octaveMask = 0x1F;
    p.gen.useVibrato = true;
    p.gen.amountNote = 70;
    return p;
}

const char* kGoldenEncoding =
    "acidpat 1|name=Night Line%7c1%25|len=3|div=1/8|mod=tri|shuffle=25|gate=60|dir=pingpong|transpose=-5|"
    "seed=4000000000|notemask=0a5|root=9|octmask=1f|useslide=1|useaccent=1|usevib=1|amtnote=70|amtslide=50|"
    "amtaccent=50|amtvib=50|steps=N:D#:1:1:AS T:C:0 R:G:-1:1:-:100:1:0:5:17 N:C:0:1:-:50";
}

TEST_CASE ("codec golden strings are stable", "[codec]")
{
    REQUIRE (encodePattern (Pattern {}) == kDefaultEncoding);
    REQUIRE (encodePattern (goldenPattern()) == kGoldenEncoding);

    Pattern p;
    p.length = 9;
    const DecodeResult r = decodePattern (kGoldenEncoding, p);
    REQUIRE (r.ok);
    REQUIRE (r.version == 1);
    REQUIRE (r.adjusted == 0);
    REQUIRE (samePattern (p, goldenPattern()));

    Pattern d;
    d.length = 5;
    REQUIRE (decodePattern (kDefaultEncoding, d).ok);
    REQUIRE (samePattern (d, Pattern {}));
}

TEST_CASE ("codec round-trips thousands of random patterns losslessly", "[codec]")
{
    for (std::uint64_t seed = 1; seed <= 5000; ++seed)
    {
        const Pattern orig = randomPattern (seed);
        const std::string s = encodePattern (orig);
        Pattern back;
        const DecodeResult r = decodePattern (s, back);
        INFO (s);
        REQUIRE (r.ok);
        REQUIRE (r.adjusted == 0);
        REQUIRE (samePattern (back, orig));
        REQUIRE (encodePattern (back) == s);
        REQUIRE (s.find ('\n') == std::string::npos);
    }

    // Generated patterns (all generator memory in use).
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        Pattern p;
        p.length = 64;
        regenerate (p, seed);
        Pattern back;
        REQUIRE (decodePattern (encodePattern (p), back).ok);
        REQUIRE (samePattern (back, p));
    }
}

TEST_CASE ("codec tolerates older and newer writers", "[codec]")
{
    // An older writer that knew fewer keys: everything missing keeps its default.
    Pattern p = randomPattern (3);
    DecodeResult r = decodePattern ("acidpat 1|len=4|steps=N:C:0 N:D:1:1:A", p);
    REQUIRE (r.ok);
    Pattern expect;
    expect.length = 4;
    expect.steps[0].state = StepState::Note;
    expect.steps[1].state = StepState::Note;
    expect.steps[1].note = 2;
    expect.steps[1].octave = 1;
    expect.steps[1].accent = true;
    REQUIRE (samePattern (p, expect));

    // Unknown keys anywhere, extra step subfields, unknown flags, a newer version number.
    r = decodePattern ("acidpat 3|future=xyz|len=2|junk|steps=N:E:0:1:AQ:100:1:0:0:-:-:-:-:C:0:extra:more R:C:0|later=1", p);
    REQUIRE (r.ok);
    REQUIRE (r.version == 3);
    REQUIRE (r.newerVersion);
    REQUIRE (r.adjusted == 1);   // the unknown flag Q
    REQUIRE (p.length == 2);
    REQUIRE (p.steps[0].note == 4);
    REQUIRE (p.steps[0].accent);

    // Unknown enum names (a newer division, say) fall back to the default and are counted.
    r = decodePattern ("acidpat 1|div=1/128|mod=quintuplet|dir=spiral", p);
    REQUIRE (r.ok);
    REQUIRE_FALSE (r.newerVersion);
    REQUIRE (r.adjusted == 3);
    REQUIRE (p.div == StepDiv::d1_16);
    REQUIRE (p.divMod == DivMod::Full);
    REQUIRE (p.direction == Direction::Forward);

    REQUIRE (looksLikeEncodedPattern (kDefaultEncoding));
    REQUIRE (looksLikeEncodedPattern ("  acidpat 1"));
    REQUIRE_FALSE (looksLikeEncodedPattern ("acidpattern"));
    REQUIRE_FALSE (looksLikeEncodedPattern (""));
}

TEST_CASE ("codec clamps out-of-range numbers and rejects malformed input", "[codec][hostile]")
{
    Pattern p;
    DecodeResult r = decodePattern ("acidpat 1|len=99999999999999999999999999|shuffle=-7|transpose=1000|root=12|"
                                    "seed=-1|amtnote=101|notemask=ffff|octmask=ff|useslide=7|"
                                    "steps=N:C:9:0:-:999:0:300:255:200:-5:-:-:C:-4", p);
    REQUIRE (r.ok);
    REQUIRE (r.adjusted > 10);
    REQUIRE (p.length == 64);
    REQUIRE (p.shuffle == 0);
    REQUIRE (p.transpose == 48);
    REQUIRE (p.gen.root == 11);
    REQUIRE (p.gen.seed == 0);
    REQUIRE (p.gen.amountNote == 100);
    REQUIRE (p.gen.noteMask == 0x0FFF);
    REQUIRE (p.gen.octaveMask == 0x1F);
    REQUIRE (p.gen.useSlide);
    const Step& s = p.steps[0];
    REQUIRE (s.octave == kMaxOctave);
    REQUIRE (s.arpIndex == 1);
    REQUIRE (s.probability == 100);
    REQUIRE (s.ratchet == 1);
    REQUIRE (s.gate == 127);
    REQUIRE (s.locks == 63);
    REQUIRE (s.magGate == 127);
    REQUIRE (s.magAccent == 0);
    REQUIRE (s.candOctave == kMinOctave);

    // More than 64 steps: the extra ones are dropped.
    std::string many = "acidpat 1|steps=";
    for (int i = 0; i < 70; ++i) many += "N:C:0 ";
    r = decodePattern (many, p);
    REQUIRE (r.ok);
    REQUIRE (r.adjusted == 1);

    // A long name is truncated to 31 bytes and stays terminated.
    r = decodePattern ("acidpat 1|name=" + std::string (100, 'n'), p);
    REQUIRE (r.ok);
    REQUIRE (std::strlen (p.name) == 31);

    // Rejections leave the output untouched.
    const Pattern keep = randomPattern (12);
    for (const char* bad : { "", "acidpat", "acidpat x", "acidpat 0", "acidpat -1", "hello 1", "acidpat 1|len=abc",
                             "acidpat 1|len=", "acidpat 1|name=%zz", "acidpat 1|name=abc%4", "acidpat 1|steps=X:C:0",
                             "acidpat 1|steps=N:H:0", "acidpat 1|steps=N:C:x", "acidpat 1|notemask=xyz",
                             "acidpat 1|steps=NN:C:0", "acidpat 1|useaccent=yes" })
    {
        Pattern q = keep;
        r = decodePattern (bad, q);
        INFO (bad);
        REQUIRE_FALSE (r.ok);
        REQUIRE_FALSE (r.error.empty());
        REQUIRE (samePattern (q, keep));
    }

    Pattern q = keep;
    REQUIRE_FALSE (decodePattern ("acidpat 1|name=" + std::string (70000, 'a'), q).ok);
    REQUIRE (samePattern (q, keep));
}

TEST_CASE ("codec survives truncation and garbage without out-of-range fields", "[codec][hostile]")
{
    const std::string good = encodePattern (randomPattern (99));
    for (std::size_t n = 0; n <= good.size(); ++n)
    {
        Pattern p;
        const DecodeResult r = decodePattern (std::string_view (good).substr (0, n), p);
        if (r.ok) REQUIRE (sanitizePattern (p) == 0);
    }

    acid::Rng rng (2024);
    const std::string alphabet = "acidpt 0123456789|=:-NRTCDEFGAB#%xf/\n\t";
    for (int iter = 0; iter < 20000; ++iter)
    {
        std::string s;
        if (iter % 2 == 0) s = "acidpat 1|";
        const int len = rng.nextInt (300);
        for (int i = 0; i < len; ++i)
            s.push_back (iter % 3 == 0 ? static_cast<char> (rng.nextInt (256)) : alphabet[static_cast<std::size_t> (rng.nextInt (static_cast<int> (alphabet.size())))]);
        Pattern p;
        const DecodeResult r = decodePattern (s, p);
        if (r.ok) REQUIRE (sanitizePattern (p) == 0);
    }

    // Mutations of a valid string: flip one character at a time.
    for (std::size_t i = 0; i < good.size(); i += 3)
        for (char c : { '9', '-', ':', '|', '=', 'Z', ' ' })
        {
            std::string s = good;
            s[i] = c;
            Pattern p;
            if (decodePattern (s, p).ok) REQUIRE (sanitizePattern (p) == 0);
        }
}

TEST_CASE ("step notation: the document example and round trips", "[codec][steptext]")
{
    Pattern p;
    std::string err;
    const char* example = "acidsteps v1 len=16 div=1/16 mod=full shuffle=0 gate=auto dir=fwd transpose=0 name=\"example\"\n"
                          "C  C+:S  D#:A  ~  .  G-:AS  C  .  C:S  C+  .  A#  ~  ~  .  C:A";
    REQUIRE (fromStepText (example, p, &err));
    REQUIRE (p.length == 16);
    REQUIRE (std::string (p.name) == "example");
    REQUIRE (p.steps[1].octave == 1);
    REQUIRE (p.steps[1].slide);
    REQUIRE (p.steps[2].note == 3);
    REQUIRE (p.steps[2].accent);
    REQUIRE (p.steps[3].state == StepState::Tie);
    REQUIRE (p.steps[4].state == StepState::Rest);
    REQUIRE (p.steps[5].note == 7);
    REQUIRE (p.steps[5].octave == -1);
    REQUIRE (p.steps[5].accent);
    REQUIRE (p.steps[5].slide);

    StepTextOptions opt;
    opt.newlineEvery16 = true;
    REQUIRE (toStepText (p, opt) ==
             "acidsteps v1 len=16 div=1/16 mod=full shuffle=0 gate=auto dir=fwd transpose=0 name=\"example\"\n"
             "C C+:S D#:A ~ . G-:AS C . C:S C+ . A# ~ ~ . C:A");

    // Flats, enharmonics and extensions.
    REQUIRE (fromStepText ("Bb Cb B#- Db+:P50R3G64V", p, &err));
    REQUIRE (p.length == 4);
    REQUIRE ((p.steps[0].note == 10 && p.steps[0].octave == 0));
    REQUIRE ((p.steps[1].note == 11 && p.steps[1].octave == -1));
    REQUIRE ((p.steps[2].note == 0 && p.steps[2].octave == 0));
    REQUIRE ((p.steps[3].note == 1 && p.steps[3].octave == 1));
    REQUIRE (p.steps[3].probability == 50);
    REQUIRE (p.steps[3].ratchet == 3);
    REQUIRE (p.steps[3].gate == 64);
    REQUIRE (p.steps[3].vibrato);

    // Round trips of random playable patterns (what the notation carries).
    for (std::uint64_t seed = 1; seed <= 1000; ++seed)
    {
        Pattern r = randomPattern (seed);
        r.name[0] = '\0';
        Pattern back;
        StepTextOptions o;
        o.restPitches = true;
        const std::string s = toStepText (r, o);
        INFO (s);
        REQUIRE (fromStepText (s, back, &err));
        REQUIRE (back.length == r.length);
        REQUIRE (back.div == r.div);
        REQUIRE (back.divMod == r.divMod);
        REQUIRE (back.shuffle == r.shuffle);
        REQUIRE (back.gateLength == r.gateLength);
        REQUIRE (back.direction == r.direction);
        REQUIRE (back.transpose == r.transpose);
        for (int i = 0; i < r.length; ++i)
        {
            const Step& a = r.steps[static_cast<std::size_t> (i)];
            const Step& b = back.steps[static_cast<std::size_t> (i)];
            REQUIRE (a.state == b.state);
            if (a.state == StepState::Tie) continue;
            REQUIRE (a.note == b.note);
            REQUIRE (a.octave == b.octave);
            if (a.state != StepState::Note) continue;
            REQUIRE (a.accent == b.accent);
            REQUIRE (a.slide == b.slide);
            REQUIRE (a.vibrato == b.vibrato);
            REQUIRE (a.probability == b.probability);
            REQUIRE (a.ratchet == b.ratchet);
            REQUIRE (a.gate == b.gate);
        }
        REQUIRE (toStepText (back, o) == s);
    }

    // Arpeggiator patterns write indices.
    Pattern arp;
    arp.length = 3;
    arp.steps[0].state = StepState::Note;
    arp.steps[0].arpIndex = 10;
    arp.steps[0].octave = -1;
    arp.steps[1].state = StepState::Note;
    arp.steps[1].arpIndex = 2;
    arp.steps[1].accent = true;
    StepTextOptions ao;
    ao.header = false;
    ao.arpeggiator = true;
    REQUIRE (toStepText (arp, ao) == "10- 2:A .");
    Pattern arpBack;
    REQUIRE (fromStepText ("10- 2:A .", arpBack, &err));
    REQUIRE (arpBack.steps[0].arpIndex == 10);
    REQUIRE (arpBack.steps[0].octave == -1);
    REQUIRE (arpBack.steps[1].arpIndex == 2);
    REQUIRE (arpBack.steps[1].accent);
}

TEST_CASE ("step notation rejects bad input and leaves the pattern alone", "[codec][steptext][hostile]")
{
    const Pattern keep = randomPattern (31);
    std::string sixtyFive;
    for (int i = 0; i < 65; ++i) sixtyFive += "C ";
    for (const std::string& bad : { std::string (""), std::string ("H"), std::string ("C++++"), std::string ("C--"),
                                   std::string ("C:Q"), std::string ("C:P101"), std::string ("C:R0"), std::string ("C:R5"),
                                   std::string ("C:G128"), std::string ("C:P"), std::string ("11"), std::string ("0"),
                                   std::string ("C x+"), std::string ("acidsteps v1 len=0"), std::string ("acidsteps v1 len=65 C"),
                                   std::string ("acidsteps v1 name=\"open"), std::string ("C len=4"), std::string (".H"),
                                   std::string ("acidsteps v1 div=1/3 C"), sixtyFive })
    {
        Pattern p = keep;
        std::string err;
        INFO (bad);
        REQUIRE_FALSE (fromStepText (bad, p, &err));
        REQUIRE_FALSE (err.empty());
        REQUIRE (samePattern (p, keep));
    }

    acid::Rng rng (7);
    const std::string alphabet = "ABCDEFGH#b+-.~:ASVPRG0123456789 \n=\"acidsteps";
    for (int iter = 0; iter < 20000; ++iter)
    {
        std::string s;
        const int len = rng.nextInt (200);
        for (int i = 0; i < len; ++i)
            s.push_back (alphabet[static_cast<std::size_t> (rng.nextInt (static_cast<int> (alphabet.size())))]);
        Pattern p;
        if (fromStepText (s, p)) REQUIRE (sanitizePattern (p) == 0);
    }
}

TEST_CASE ("codec stores authentic-mode spare pitch entries on request", "[codec][authentic]")
{
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        const Pattern p = randomPattern (seed);
        acid::Rng r (seed + 17);
        std::vector<PitchEntry> spares;
        for (int k = 0; k < static_cast<int> (seed % 12); ++k)
            spares.push_back (PitchEntry { static_cast<std::uint8_t> (r.nextInt (12)),
                                           static_cast<std::int8_t> (kMinOctave + r.nextInt (5)),
                                           r.chance (0.5), r.chance (0.5), r.chance (0.5) });

        const std::string text = encodePattern (p, spares);
        Pattern back;
        std::vector<PitchEntry> sparesBack { PitchEntry { 3, 1 } };   // overwritten
        const DecodeResult res = decodePattern (text, back, &sparesBack);
        REQUIRE (res.ok);
        REQUIRE (samePattern (back, p));
        REQUIRE (sparesBack.size() == spares.size());
        for (std::size_t k = 0; k < spares.size(); ++k) REQUIRE (samePitchEntry (sparesBack[k], spares[k]));

        // Without spares the string is the plain encoding, and plain readers skip the key.
        if (spares.empty()) REQUIRE (text == encodePattern (p));
        Pattern plain;
        REQUIRE (decodePattern (text, plain).ok);
        REQUIRE (samePattern (plain, p));
    }

    // Hostile spares: bad tokens reject the whole string and leave both outputs alone; out of
    // range octaves are clamped and counted; more than 256 entries are dropped and counted.
    Pattern p;
    std::vector<PitchEntry> spares { PitchEntry { 5, 0 } };
    REQUIRE_FALSE (decodePattern ("acidpat 1|len=3|spares=H:0", p, &spares).ok);
    REQUIRE_FALSE (decodePattern ("acidpat 1|len=3|spares=C:x", p, &spares).ok);
    REQUIRE (p.length == 16);
    REQUIRE (spares.size() == 1);
    DecodeResult res = decodePattern ("acidpat 1|spares=C:9:AQ D#", p, &spares);
    REQUIRE (res.ok);
    REQUIRE (res.adjusted == 2);
    REQUIRE (spares.size() == 2);
    REQUIRE (spares[0].octave == kMaxOctave);
    REQUIRE (spares[0].accent);
    REQUIRE (spares[1].note == 3);
    REQUIRE (spares[1].octave == 0);
    std::string many = "acidpat 1|spares=";
    for (int i = 0; i < 300; ++i) many += "C:0 ";
    res = decodePattern (many, p, &spares);
    REQUIRE (res.ok);
    REQUIRE (spares.size() == 256);
    REQUIRE (res.adjusted == 1);
    REQUIRE (decodePattern ("acidpat 1|len=3", p, &spares).ok);
    REQUIRE (spares.empty());
}
