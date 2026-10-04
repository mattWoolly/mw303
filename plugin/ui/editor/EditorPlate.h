#pragma once
// The faceplate: every panel laid out in design units (1180 wide, 880 high, in every play
// mode) with no processor behind it. mw12FEditor owns one,
// applies the zoom as a transform and binds the controls to the Controller; the gallery
// renders one through a real editor. The plate knows nothing about parameters beyond the
// ControlSet registry: every side effect (couples, extended tuning, mods gating, play mode)
// is a public call the owner makes from its parameter listeners.
#include "mw12FLookAndFeel.h"
#include "panels/Panels.h"
#include "acid/seq/PlayerTypes.h"

namespace acid::ui
{
class EditorPlate final : public Faceplate
{
private:
    // Declared first: the panels below build their controls into the ControlSet.
    mw12FLookAndFeel lookAndFeel;
    ControlSet controls;

public:
    explicit EditorPlate (PatternEditModel& patternModel);
    ~EditorPlate() override;

    static constexpr int kDesignWidth = metrics::kDesignWidth;
    static constexpr int kDesignHeight = metrics::kDesignHeight;
    void setThemeId (ThemeId);
    ThemeId getThemeId() const noexcept { return lookAndFeel.getThemeId(); }
    mw12FLookAndFeel& getLookAndFeel() noexcept { return lookAndFeel; }

    /** Shows the sequencer screen, or in External mode the external-MIDI panel in its place.
        The plate's size never changes: the window does not follow a parameter. */
    void setPlayMode (seq::PlayMode);
    seq::PlayMode getPlayMode() const noexcept { return playMode; }

    /** A full-plate overlay (the preset browser). It covers everything under the top bar; null hides it. */
    void setOverlay (juce::Component* overlay);
    juce::Component* getOverlay() const noexcept { return overlay; }

    ControlSet& getControls() noexcept { return controls; }

    TopBar topBar;
    SynthPanel synth;
    ModsPanel mods;
    CalibrationPanel calibration;
    DistortionPanel distortion;
    FxRackPanel rack;
    MasterPanel master;
    SequencerPanel sequencer;
    ExternalPanel external;

    void resized() override;

protected:
    void paintPlate (juce::Graphics&, const Theme&) override;

private:
    seq::PlayMode playMode = seq::PlayMode::Sequencer;
    juce::Component* overlay = nullptr;
};
}
