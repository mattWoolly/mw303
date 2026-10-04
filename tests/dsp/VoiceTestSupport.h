#pragma once
// Shared helpers for the voice tests: a traced render that records the control-path taps
// per sample, a small step sequencer with the hardware's gate and slide rules, pitch and
// spectrum measurements, and CSV output for tools/analysis.
#include "acid/common/Math.h"
#include "acid/dsp/Voice.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

namespace acid::test
{
inline std::string outputPath (const std::string& name) { return std::string (ACID_TEST_OUTPUT_DIR) + "/" + name; }

/** Default parameters with the slow pitch drift off, so that pitch and timing checks are exact. */
inline dsp::VoiceParams quietParams()
{
    dsp::VoiceParams p;
    p.calDrift = 0.0;
    return p;
}

//==============================================================================
/** Audio plus the control-path taps of every base-rate sample. */
struct Trace
{
    double fs = 48000.0;
    std::vector<float> audio;
    std::vector<dsp::VoiceDebugTaps> taps;

    std::size_t size() const { return audio.size(); }
    std::size_t at (double seconds) const { return static_cast<std::size_t> (seconds * fs + 0.5); }
    double seconds (std::size_t index) const { return static_cast<double> (index) / fs; }
};

/** Runs the voice one sample at a time for `seconds`, appending to the trace. */
inline void run (dsp::Voice& voice, Trace& trace, double seconds)
{
    trace.fs = voice.getSampleRate();
    const auto n = static_cast<std::size_t> (seconds * trace.fs + 0.5);
    for (std::size_t i = 0; i < n; ++i)
    {
        float s = 0.0f;
        voice.process (&s, 1);
        trace.audio.push_back (s);
        trace.taps.push_back (voice.debugTaps());
    }
}

/** Runs without recording. */
inline void skip (dsp::Voice& voice, double seconds)
{
    std::vector<float> buffer (512);
    auto n = static_cast<long long> (seconds * voice.getSampleRate() + 0.5);
    while (n > 0)
    {
        const int chunk = static_cast<int> (std::min<long long> (n, 512));
        voice.process (buffer.data(), chunk);
        n -= chunk;
    }
}

/** A prepared voice with the given parameters applied and settled. */
inline void start (dsp::Voice& voice, const dsp::VoiceParams& params, double fs = 48000.0, int oversampling = 4)
{
    voice.prepare (fs, 4096, oversampling);
    voice.setParams (params);
    voice.reset();
}

//==============================================================================
// Step sequencer with the hardware's rules: the slide flag sits on the note slid FROM,
// holds its gate into the next note and makes that note legato; a plain gate lasts half a
// step plus 1.35 ms; rests latch nothing.

struct SeqStep
{
    int note = 36;
    bool accent = false;
    bool slide = false;
    bool rest = false;
    bool vibrato = false;
};

struct SeqEvent
{
    long long sample = 0;
    bool on = false;
    int note = 0;
    bool accent = false, legato = false, vibrato = false;
};

inline double stepSeconds (double bpm) { return 60.0 / bpm / 4.0; }

inline std::vector<SeqEvent> makeEvents (const std::vector<SeqStep>& pattern, double bpm, double fs, int loops,
                                         double startSeconds = 0.0)
{
    std::vector<SeqEvent> events;
    const double step = stepSeconds (bpm);
    const int n = static_cast<int> (pattern.size());
    bool held = false;      // the previous step slid into this one
    for (int k = 0; k < n * loops; ++k)
    {
        const SeqStep& s = pattern[static_cast<std::size_t> (k % n)];
        const double t0 = startSeconds + step * k;
        if (s.rest)
        {
            held = false;
            continue;
        }
        SeqEvent on;
        on.sample = static_cast<long long> (std::llround (t0 * fs));
        on.on = true;
        on.note = s.note;
        on.accent = s.accent;
        on.legato = held;
        on.vibrato = s.vibrato;
        events.push_back (on);

        const bool last = k == n * loops - 1;
        const SeqStep& next = pattern[static_cast<std::size_t> ((k + 1) % n)];
        held = s.slide && ! next.rest && ! last;
        if (! held)
        {
            SeqEvent off;
            off.sample = static_cast<long long> (std::llround ((t0 + 0.5 * step + 1.35e-3) * fs));
            events.push_back (off);
        }
    }
    return events;
}

/** Renders totalSamples in host blocks of blockSize, splitting at event offsets. */
inline std::vector<float> render (dsp::Voice& voice, const std::vector<SeqEvent>& events,
                                  long long totalSamples, int blockSize)
{
    std::vector<float> out (static_cast<std::size_t> (totalSamples), 0.0f);
    std::size_t next = 0;
    long long pos = 0;
    while (pos < totalSamples)
    {
        const long long blockEnd = std::min<long long> (pos + blockSize, totalSamples);
        while (pos < blockEnd)
        {
            while (next < events.size() && events[next].sample <= pos)
            {
                const SeqEvent& e = events[next++];
                if (e.on) voice.noteOn (e.note, e.accent, e.legato, e.vibrato);
                else      voice.noteOff();
            }
            long long until = blockEnd;
            if (next < events.size() && events[next].sample < until)
                until = events[next].sample;
            voice.process (out.data() + pos, static_cast<int> (until - pos));
            pos = until;
        }
    }
    return out;
}

/** Same, one sample at a time, recording the taps. */
inline Trace renderTraced (dsp::Voice& voice, const std::vector<SeqEvent>& events, long long totalSamples)
{
    Trace trace;
    trace.fs = voice.getSampleRate();
    trace.audio.reserve (static_cast<std::size_t> (totalSamples));
    trace.taps.reserve (static_cast<std::size_t> (totalSamples));
    std::size_t next = 0;
    for (long long pos = 0; pos < totalSamples; ++pos)
    {
        while (next < events.size() && events[next].sample <= pos)
        {
            const SeqEvent& e = events[next++];
            if (e.on) voice.noteOn (e.note, e.accent, e.legato, e.vibrato);
            else      voice.noteOff();
        }
        float s = 0.0f;
        voice.process (&s, 1);
        trace.audio.push_back (s);
        trace.taps.push_back (voice.debugTaps());
    }
    return trace;
}

//==============================================================================
// Measurements.

inline double peakAbs (const std::vector<float>& x, std::size_t from = 0, std::size_t to = static_cast<std::size_t> (-1))
{
    double pk = 0.0;
    to = std::min (to, x.size());
    for (std::size_t i = from; i < to; ++i)
        pk = std::max (pk, static_cast<double> (std::abs (x[i])));
    return pk;
}

inline double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double acc = 0.0;
    to = std::min (to, x.size());
    for (std::size_t i = from; i < to; ++i)
        acc += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return to > from ? std::sqrt (acc / static_cast<double> (to - from)) : 0.0;
}

inline bool allFinite (const std::vector<float>& x)
{
    for (float s : x)
        if (! std::isfinite (s)) return false;
    return true;
}

/** Complex amplitude of the component at hz in x[from, from + length), Hann weighted;
    magnitude is the sine's peak amplitude. */
inline std::complex<double> hannBin (const std::vector<float>& x, std::size_t from, std::size_t length, double hz, double fs)
{
    std::complex<double> acc (0.0, 0.0);
    double weight = 0.0;
    const double w = kTwoPi * hz / fs;
    for (std::size_t i = 0; i < length && from + i < x.size(); ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (kTwoPi * (static_cast<double> (i) + 0.5) / static_cast<double> (length));
        acc += static_cast<double> (x[from + i]) * win * std::polar (1.0, -w * static_cast<double> (i));
        weight += win;
    }
    return 2.0 * acc / weight;
}

/** Frequency of the strongest component between lo and hi (Hann window, golden-section
    refinement of a coarse scan). */
inline double peakFrequency (const std::vector<float>& x, std::size_t from, std::size_t length, double fs,
                             double lo, double hi, int scanPoints = 200)
{
    const auto mag = [&] (double hz) { return std::abs (hannBin (x, from, length, hz, fs)); };
    const double ratio = std::pow (hi / lo, 1.0 / static_cast<double> (scanPoints - 1));
    double best = lo, bestMag = -1.0;
    double f = lo;
    for (int i = 0; i < scanPoints; ++i, f *= ratio)
    {
        const double m = mag (f);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    double a = best / ratio, b = best * ratio;
    const double phi = 0.6180339887498949;
    double c = b - phi * (b - a), d = a + phi * (b - a);
    double fc = mag (c), fd = mag (d);
    for (int i = 0; i < 40; ++i)
    {
        if (fc > fd) { b = d; d = c; fd = fc; c = b - phi * (b - a); fc = mag (c); }
        else         { a = c; c = d; fc = fd; d = a + phi * (b - a); fd = mag (d); }
    }
    return 0.5 * (a + b);
}

/** Fundamental frequency from the mean period between rising zero crossings of the
    signal band-passed around guessHz (a crude two-pole, enough to strip harmonics). */
inline double fundamentalHz (const std::vector<float>& x, std::size_t from, std::size_t to, double fs, double guessHz)
{
    // State-variable band-pass, Q 4.
    const double g = std::tan (kPi * guessHz / fs), k = 1.0 / 4.0;
    double s1 = 0.0, s2 = 0.0, prev = 0.0;
    double first = -1.0, last = -1.0;
    int crossings = 0;
    to = std::min (to, x.size());
    for (std::size_t i = from; i < to; ++i)
    {
        const double v = static_cast<double> (x[i]);
        const double hp = (v - (k + g) * s1 - s2) / (1.0 + g * (g + k));
        const double bp = g * hp + s1;
        s1 = g * hp + bp;
        const double lp = g * bp + s2;
        s2 = g * bp + lp;
        if (i > from + static_cast<std::size_t> (4.0 * fs / guessHz) && prev < 0.0 && bp >= 0.0)
        {
            const double t = static_cast<double> (i) - bp / (bp - prev);
            if (first < 0.0) first = t;
            else { last = t; ++crossings; }
        }
        prev = bp;
    }
    return crossings > 0 ? fs * static_cast<double> (crossings) / (last - first) : 0.0;
}

inline double cents (double hz, double referenceHz) { return 1200.0 * std::log2 (hz / referenceHz); }

/** First index at or after `from` where pred holds; size() when it never does. */
template <typename Pred>
std::size_t firstIndex (const Trace& t, std::size_t from, Pred pred)
{
    for (std::size_t i = from; i < t.size(); ++i)
        if (pred (t.taps[i], t.audio[i])) return i;
    return t.size();
}

//==============================================================================
/** Writes columns of equal length as CSV with a header row. */
inline bool writeCsv (const std::string& path, const std::vector<std::string>& names,
                      const std::vector<std::vector<double>>& columns)
{
    std::FILE* f = std::fopen (path.c_str(), "w");
    if (f == nullptr) return false;
    for (std::size_t c = 0; c < names.size(); ++c)
        std::fprintf (f, "%s%s", names[c].c_str(), c + 1 < names.size() ? "," : "\n");
    const std::size_t rows = columns.empty() ? 0 : columns[0].size();
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < columns.size(); ++c)
            std::fprintf (f, "%.9g%s", columns[c][r], c + 1 < columns.size() ? "," : "\n");
    return std::fclose (f) == 0;
}
}
