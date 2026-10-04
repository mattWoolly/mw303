#pragma once
// LED ladder meters. Feed them from the single 30 Hz UI timer; they repaint only when a
// segment changes state, and they are opaque so nothing behind them is redrawn.
#include "kit/Draw.h"

namespace acid::ui
{
/** Stereo output meter: two LED columns with a dB scale between them. */
class LevelMeter final : public juce::Component
{
public:
    LevelMeter();

    /** Peak levels in dBFS. Values below the bottom segment show nothing. */
    void setLevels (float leftDb, float rightDb);

    static constexpr int kNumSegments = 12;
    /** Threshold of segment i (0 is the top) in dB. */
    static float segmentDb (int index) noexcept;

    void paint (juce::Graphics&) override;

private:
    static int litCount (float db) noexcept;
    int litLeft = 0, litRight = 0;
};

/** Gain-reduction meter: a horizontal ladder that fills from the left as reduction grows. */
class GainReductionMeter final : public juce::Component
{
public:
    GainReductionMeter();

    /** Reduction in dB as a positive number (0 = none). */
    void setReductionDb (float db);

    static constexpr int kNumSegments = 10;
    static float segmentDb (int index) noexcept;

    void paint (juce::Graphics&) override;

private:
    int lit = 0;
};

/** Accent-capacitor meter: a short horizontal ladder for a 0..1 charge (the C13 charge from
    EngineStatus). Sits in a section title row, so it is small and quiet. */
class ChargeMeter final : public juce::Component
{
public:
    ChargeMeter();

    /** 0..1. */
    void setCharge (float charge01);

    /** Segment colour (Turquoise by default, as the accent's couple LEDs). */
    void setLedColour (LedColour c) { colour = c; repaint(); }

    static constexpr int kNumSegments = 8;

    void paint (juce::Graphics&) override;

private:
    int lit = 0;
    LedColour colour = LedColour::Turquoise;
};
}
