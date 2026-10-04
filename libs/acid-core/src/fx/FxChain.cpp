#include "acid/fx/FxChain.h"
#include <algorithm>
#include <cmath>

namespace acid::fx
{
namespace
{
constexpr double kSettleSeconds = 0.05;   // longer than any block's own on/off fade

inline double smoothstep (double t) noexcept { return t * t * (3.0 - 2.0 * t); }
}

//==============================================================================
FxParams fxParamsFrom (const ParamValues& v, const Settings& settings, double bpm) noexcept
{
    FxParams p;
    p.distortion = distortionParamsFrom (v);
    p.eq = eqParamsFrom (v);
    p.delay = delayParamsFrom (v, bpm);
    p.reverb = reverbParamsFrom (v);
    p.chorus = chorusParamsFrom (v);
    p.limiter = limiterParamsFrom (v);
    p.order = sanitiseFxOrder (settings.fxOrder);
    p.bypass = paramAsBool (v, kFxBypass);
    p.masterVolumeDb = v[static_cast<std::size_t> (kMasterVolume)];
    p.bpm = bpm;
    return p;
}

//==============================================================================
void FxChain::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = (std::isfinite (newSampleRate) && newSampleRate > 0.0) ? newSampleRate : 48000.0;
    const int block = std::max (1, maxBlockSize);

    distortion.prepare (sampleRate, block);
    eq.prepare (sampleRate, block);
    delay.prepare (sampleRate, block);
    reverb.prepare (sampleRate, block);
    chorus.prepare (sampleRate, block);
    limiter.prepare (sampleRate, block);

    gateStep = 1.0 / std::max (1.0, kReorderFadeSeconds * sampleRate);
    engageStep = 1.0 / std::max (1.0, kBypassFadeSeconds * sampleRate);
    settleSamples = static_cast<int> (std::ceil (kSettleSeconds * sampleRate));
    prepared = true;

    // Parameters set before prepare() were converted at the old rate: convert them again.
    applyParamsToBlocks (last);
    reset();
}

void FxChain::resetBlocks() noexcept
{
    distortion.reset();
    eq.reset();
    delay.reset();
    reverb.reset();
    chorus.reset();
    limiter.reset();
}

void FxChain::reset() noexcept
{
    resetBlocks();
    order = pendingOrder;
    slotGate.fill (1.0);
    slotGateDir.fill (0);
    slotGateNeeded.fill (false);
    outGate = 1.0;
    outGateDir = 0;
    outGateNeeded = false;
    engage = bypassTarget ? 0.0 : 1.0;
    blocksStale = false;

    for (auto& ring : dryRing)
        ring.fill (0.0f);
    dryPos = 0;
    dryLatency = std::min (latencySamples(), kDryRing - 1);

    for (int s = 0; s < kNumFxSlots; ++s)
        quietSamples[static_cast<std::size_t> (s)] = slotOn[static_cast<std::size_t> (s)] ? 0 : settleSamples;

    const bool clamped = meter.delayClamped;
    meter = {};
    meter.delayClamped = clamped;
    lastMeters = meter;
}

void FxChain::setDistortionOversampling (int factor) noexcept
{
    distortion.setOversampling (factor);
}

int FxChain::latencySamples() const noexcept
{
    return distortion.latencySamples();
}

void FxChain::setParams (const FxParams& p) noexcept
{
    last = p;
    applyParamsToBlocks (p);
}

void FxChain::applyParamsToBlocks (const FxParams& p) noexcept
{
    distortion.setParams (p.distortion);
    eq.setParams (p.eq);
    DelayParams d = p.delay;
    d.bpm = p.bpm;
    delay.setParams (d);
    reverb.setParams (p.reverb);
    chorus.setParams (p.chorus);
    limiter.setParams (p.limiter);

    slotOn[static_cast<std::size_t> (FxSlot::Distortion)] = p.distortion.on;
    slotOn[static_cast<std::size_t> (FxSlot::Eq)] = p.eq.on;
    slotOn[static_cast<std::size_t> (FxSlot::Delay)] = p.delay.on;
    slotOn[static_cast<std::size_t> (FxSlot::Reverb)] = p.reverb.on;
    slotOn[static_cast<std::size_t> (FxSlot::Chorus)] = p.chorus.on;
    slotOn[static_cast<std::size_t> (FxSlot::Limiter)] = p.limiter.on;

    pendingOrder = sanitiseFxOrder (p.order);
    bypassTarget = p.bypass;
    meter.delayClamped = delay.timeClamped();
    lastMeters.delayClamped = meter.delayClamped;
}

//==============================================================================
bool FxChain::slotRunning (int slot) const noexcept
{
    const auto s = static_cast<std::size_t> (slot);
    if (slotOn[s] || quietSamples[s] < settleSamples) return true;

    // An oversampled distortion that is off is still a delay line: moving it would replay
    // its stale contents, so its position counts.
    return static_cast<FxSlot> (slot) == FxSlot::Distortion && distortion.latencySamples() > 0;
}

bool FxChain::sameRunningSequence (const FxOrder& a, const FxOrder& b) const noexcept
{
    // Blocks that are off and settled pass audio through untouched, so only the relative
    // order of the running blocks matters.
    std::size_t i = 0, j = 0;
    for (;;)
    {
        while (i < a.size() && ! slotRunning (a[i])) ++i;
        while (j < b.size() && ! slotRunning (b[j])) ++j;
        if (i == a.size() || j == b.size()) return i == a.size() && j == b.size();
        if (a[i] != b[j]) return false;
        ++i;
        ++j;
    }
}

int FxChain::runningPredecessor (const FxOrder& o, int slot) const noexcept
{
    int previous = -1;   // the chain input
    for (const auto s : o)
    {
        if (s == slot) return previous;
        if (slotRunning (s)) previous = s;
    }
    return previous;
}

int FxChain::lastRunning (const FxOrder& o) const noexcept
{
    int found = -1;
    for (const auto s : o)
        if (slotRunning (s)) found = s;
    return found;
}

void FxChain::updateGate() noexcept
{
    const int latency = std::min (latencySamples(), kDryRing - 1);
    const bool dryAudible = engage < 1.0 || bypassTarget;
    if (! dryAudible) dryLatency = latency;   // nothing reads the dry path: retune it silently

    bool orderChange = pendingOrder != order;
    if (orderChange && (engage <= 0.0 || sameRunningSequence (order, pendingOrder)))
    {
        order = pendingOrder;
        orderChange = false;
    }
    const bool latencyChange = dryLatency != latency;

    // Which gates the change needs: the inputs whose feed changes, and the output when the
    // last running block changes (or the dry path's latency does).
    for (int slot = 0; slot < kNumFxSlots; ++slot)
        slotGateNeeded[static_cast<std::size_t> (slot)] =
            orderChange && slotRunning (slot) && runningPredecessor (order, slot) != runningPredecessor (pendingOrder, slot);
    outGateNeeded = latencyChange || (orderChange && lastRunning (order) != lastRunning (pendingOrder));

    // Apply the change once every gate it needs is shut.
    if (orderChange || latencyChange)
    {
        bool shut = ! outGateNeeded || outGate <= 0.0;
        for (int slot = 0; slot < kNumFxSlots; ++slot)
            if (slotGateNeeded[static_cast<std::size_t> (slot)] && slotGate[static_cast<std::size_t> (slot)] > 0.0)
                shut = false;
        if (shut)
        {
            order = pendingOrder;
            dryLatency = latency;
            slotGateNeeded.fill (false);
            outGateNeeded = false;
        }
    }

    for (int slot = 0; slot < kNumFxSlots; ++slot)
    {
        const auto s = static_cast<std::size_t> (slot);
        slotGateDir[s] = slotGateNeeded[s] ? -1 : (slotGate[s] < 1.0 ? 1 : 0);
    }
    outGateDir = outGateNeeded ? -1 : (outGate < 1.0 ? 1 : 0);
}

void FxChain::runSlot (int slot, float* left, float* right, int n) noexcept
{
    switch (static_cast<FxSlot> (slot))
    {
        case FxSlot::Distortion: distortion.process (left, right, n); break;
        case FxSlot::Eq:         eq.process (left, right, n); break;
        case FxSlot::Delay:      delay.process (left, right, n); break;
        case FxSlot::Reverb:     reverb.process (left, right, n); break;
        case FxSlot::Chorus:     chorus.process (left, right, n); break;
        case FxSlot::Limiter:
            limiter.process (left, right, n);
            meter.limiterReductionDb = std::max (meter.limiterReductionDb, limiter.gainReductionDb());
            break;
    }
}

//==============================================================================
void FxChain::process (float* left, float* right, int numSamples) noexcept
{
    if (! prepared || left == nullptr || right == nullptr || numSamples <= 0) return;

    meter.peakLeft = meter.peakRight = 0.0f;
    meter.limiterReductionDb = 0.0f;

    int pos = 0;
    while (pos < numSamples)
    {
        updateGate();
        int n = std::min (numSamples - pos, kChunk);
        // End the chunk where a closing gate shuts, so the swap lands on a sample boundary.
        const auto untilShut = [this] (double g) { return std::max (1, static_cast<int> (std::ceil (g / gateStep - 1.0e-9))); };
        if (outGateDir < 0) n = std::min (n, untilShut (outGate));
        for (int slot = 0; slot < kNumFxSlots; ++slot)
            if (slotGateDir[static_cast<std::size_t> (slot)] < 0) n = std::min (n, untilShut (slotGate[static_cast<std::size_t> (slot)]));

        processChunk (left + pos, right + pos, n);
        pos += n;
    }

    meter.dynamicsGainDb = engage > 0.0 ? static_cast<float> (distortion.dynamicsGainDb()) : 0.0f;
    lastMeters = meter;
}

void FxChain::processChunk (float* left, float* right, int n) noexcept
{
    float* io[2] = { left, right };
    constexpr int mask = kDryRing - 1;

    // The dry path: always written, read only while something can hear it. A non-finite
    // input sample becomes silence here (and a wild one is clamped), before the dry ring
    // and every block, so one bad sample from upstream cannot latch NaN into any filter,
    // line or detector.
    const bool needDry = engage < 1.0 || bypassTarget;
    for (int c = 0; c < 2; ++c)
    {
        auto& ring = dryRing[static_cast<std::size_t> (c)];
        auto& dry = dryBuf[static_cast<std::size_t> (c)];
        int p = dryPos;
        for (int i = 0; i < n; ++i)
        {
            io[c][i] = static_cast<float> (insert::finiteSample (io[c][i]));
            ring[static_cast<std::size_t> (p)] = io[c][i];
            if (needDry) dry[static_cast<std::size_t> (i)] = ring[static_cast<std::size_t> ((p - dryLatency) & mask)];
            p = (p + 1) & mask;
        }
    }
    dryPos = (dryPos + n) & mask;

    // Fully bypassed: the blocks do not run at all.
    const bool runChain = ! (bypassTarget && engage <= 0.0);

    // Gain curves for this chunk.
    const auto ramp = [this, n] (double& g, int dir, std::array<float, kChunk>& curve)
    {
        for (int i = 0; i < n; ++i)
        {
            g = clamp (g + static_cast<double> (dir) * gateStep, 0.0, 1.0);
            curve[static_cast<std::size_t> (i)] = static_cast<float> (smoothstep (g));
        }
    };
    std::array<bool, kNumFxSlots> slotGating {};
    for (int slot = 0; slot < kNumFxSlots; ++slot)
    {
        const auto s = static_cast<std::size_t> (slot);
        slotGating[s] = slotGateDir[s] != 0 || slotGate[s] < 1.0;
        if (slotGating[s]) ramp (slotGate[s], slotGateDir[s], slotCurve[s]);
    }
    const bool outGating = outGateDir != 0 || outGate < 1.0;
    if (outGating) ramp (outGate, outGateDir, outCurve);
    if (needDry)
    {
        const double delta = bypassTarget ? -engageStep : engageStep;
        for (int i = 0; i < n; ++i)
        {
            engage = clamp (engage + delta, 0.0, 1.0);
            engageCurve[static_cast<std::size_t> (i)] = static_cast<float> (smoothstep (engage));
        }
    }

    const auto applyCurve = [&] (const std::array<float, kChunk>& curve)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                io[c][i] *= curve[static_cast<std::size_t> (i)];
    };

    if (runChain)
    {
        if (blocksStale)
        {
            resetBlocks();
            blocksStale = false;
        }

        // While the bypass fades, the chain input fades with it, so a chain coming back from
        // reset starts smoothly and its delay lines never hold a step to replay later.
        if (needDry) applyCurve (engageCurve);

        for (const auto slot : order)
        {
            // A block whose feed changes at a reorder has its input faded, so at the swap
            // it does not see a jump (one would echo out of the delay-based blocks).
            if (slotGating[slot]) applyCurve (slotCurve[slot]);
            runSlot (slot, left, right, n);
        }
    }

    for (int s = 0; s < kNumFxSlots; ++s)
    {
        auto& q = quietSamples[static_cast<std::size_t> (s)];
        q = slotOn[static_cast<std::size_t> (s)] ? 0 : std::min (q + n, settleSamples);
    }

    // Bypass crossfade against the delayed dry signal.
    if (needDry)
    {
        for (int c = 0; c < 2; ++c)
        {
            const auto& dry = dryBuf[static_cast<std::size_t> (c)];
            for (int i = 0; i < n; ++i)
            {
                const double w = runChain ? static_cast<double> (engageCurve[static_cast<std::size_t> (i)]) : 0.0;
                io[c][i] = static_cast<float> (insert::OnOffFade::mix (static_cast<double> (dry[static_cast<std::size_t> (i)]),
                                                                      static_cast<double> (io[c][i]), w));
            }
        }
        if (bypassTarget && engage <= 0.0)
            blocksStale = true;
    }

    // Reorder gate on the output.
    if (outGating) applyCurve (outCurve);

    float pl = meter.peakLeft, pr = meter.peakRight;
    for (int i = 0; i < n; ++i)
    {
        pl = std::max (pl, std::abs (left[i]));
        pr = std::max (pr, std::abs (right[i]));
    }
    meter.peakLeft = pl;
    meter.peakRight = pr;
}
}
