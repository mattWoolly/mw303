// Editor tests: a real mw12FEditor over a real mw12FProcessor, headless (nothing goes on the
// desktop). The timing cases enforce the repaint budget of docs/research/07 section 6.3; the
// others pin the behaviours the editor review found and this workstream fixed. Each case says
// which contract it checks.
//
// Built as mwacid_ui_tests from plugin/ui-gallery/CMakeLists.txt and run by ctest.
#include "mw12FEditor.h"
#include "PluginTestSupport.h"
#include "kit/SafeText.h"

#include <juce_events/juce_events.h>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

// JUCE's own message pump (juce_events, Linux), as the plugin client's message thread uses it:
// runDispatchLoopUntil needs modal loops, which this build forbids.
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}

using namespace ptest;
using namespace acid::ui;

namespace
{
/** Runs the message loop (timers included) for about `ms` milliseconds. */
void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (ms);

    while (juce::Time::getMillisecondCounter() < end)
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
}

struct EditorRig
{
    ScratchConfigDir config;
    Driver driver;
    std::unique_ptr<mw12FEditor> editor;

    EditorRig()
    {
        driver.prepare();
        editor.reset (dynamic_cast<mw12FEditor*> (driver.processor.createEditor()));
        REQUIRE (editor != nullptr);
        editor->setZoom (1.0f);
    }

    ~EditorRig() { editor.reset(); }

    acid::plugin::Controller& controller() { return editor->getController(); }

    /** What a host's message loop would do between two user actions. */
    void settle()
    {
        driver.processor.handlePendingMessages();
        editor->tick();
    }

    void paintAll (juce::Image& canvas)
    {
        juce::Graphics g (canvas);
        editor->paintEntireComponent (g, false);
    }

    juce::Image canvas() const { return juce::Image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true); }
};

std::uint16_t usedMask (acid::plugin::Controller& c, bool arp, int bank)
{
    std::uint16_t m = 0;

    for (int s = 0; s < seq::kSlotsPerBank; ++s)
    {
        const auto p = c.pattern (arp, bank * seq::kSlotsPerBank + s);

        for (int i = 0; i < p.length; ++i)
            if (p.steps[static_cast<std::size_t> (i)].state != seq::StepState::Rest)
            {
                m = static_cast<std::uint16_t> (m | (1u << s));
                break;
            }
    }

    return m;
}

void putNote (acid::plugin::Controller& c, bool arp, int slot)
{
    auto p = c.pattern (arp, slot);
    p.steps[0].state = seq::StepState::Note;
    c.commitPattern (arp, slot, p);
}

/** Median of `runs` timings of fn, each the mean over `perRun` calls, in milliseconds. The
    median keeps one scheduler hiccup from failing a budget case on a loaded machine. */
double medianMs (int runs, int perRun, const std::function<void()>& fn)
{
    std::vector<double> samples;

    for (int r = 0; r < runs; ++r)
    {
        const auto t0 = std::chrono::steady_clock::now();

        for (int i = 0; i < perRun; ++i)
            fn();

        samples.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / perRun);
    }

    std::sort (samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

juce::MouseEvent mouseEventOn (juce::Component& target, juce::ModifierKeys mods)
{
    const auto pos = target.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                             juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}
}

//==============================================================================
TEST_CASE ("editor: a full repaint stays inside the budget at 100 % and 200 %", "[ui][budget]")
{
    // docs/research/07 section 6.3 rule 5: 8 ms per full repaint at 2x. 1x gets half.
    EditorRig r;

    for (const auto& [zoom, budgetMs] : { std::pair { 1.0f, 4.0 }, std::pair { 2.0f, 8.0 } })
    {
        r.editor->setZoom (zoom);
        auto canvas = r.canvas();
        r.paintAll (canvas);   // builds the static layer and the panel buffers
        const auto ms = medianMs (5, 10, [&] { r.paintAll (canvas); });
        INFO ("full repaint at " << juce::roundToInt (zoom * 100.0f) << " %: " << ms << " ms");
        CHECK (ms < budgetMs);
    }
}

TEST_CASE ("editor: a knob change repaints one knob, not a panel, inside 1 ms at 200 %", "[ui][budget]")
{
    EditorRig r;
    r.editor->setZoom (2.0f);
    auto canvas = r.canvas();
    r.paintAll (canvas);

    auto& plate = r.editor->getPlate();
    auto& knob = plate.synth.cutoff;
    const auto knobArea = r.editor->getLocalArea (&knob, knob.getLocalBounds());
    double value = 0.0;
    const auto ms = medianMs (5, 200, [&]
    {
        value = value >= 100.0 ? 0.0 : value + 0.37;
        knob.setValue (value, juce::dontSendNotification);
        juce::Graphics g (canvas);
        g.reduceClipRegion (knobArea);
        r.editor->paintEntireComponent (g, false);
    });
    INFO ("knob repaint at 200 %: " << ms << " ms");
    CHECK (ms < 1.0);
}

TEST_CASE ("editor: local changes do not re-render the static layer", "[ui]")
{
    // docs/research/07 section 6.3 rule 2: a parameter change repaints the control that moved.
    EditorRig r;
    r.editor->setZoom (2.0f);
    auto canvas = r.canvas();
    r.paintAll (canvas);
    auto& plate = r.editor->getPlate();
    auto& c = r.controller();
    const auto renders = plate.getStaticRenderCount();
    REQUIRE (renders > 0);

    SECTION ("tune_ext automation swaps the tuning controls")
    {
        c.setParam (acid::kTuneExt, 1.0f);
        r.paintAll (canvas);
        CHECK (plate.getStaticRenderCount() == renders);
        CHECK (plate.synth.tuneFine.isVisible());
    }

    SECTION ("play_mode automation relabels the sequencer")
    {
        c.setPlayMode (seq::PlayMode::Arpeggiator);
        r.paintAll (canvas);
        CHECK (plate.getStaticRenderCount() == renders);
    }

    SECTION ("an effects tab click and the full view")
    {
        plate.rack.tabs.setSelectedTab (0);
        plate.rack.setFullView (true);
        r.paintAll (canvas);
        CHECK (plate.getStaticRenderCount() == renders);
    }

    SECTION ("the sequencer view and tools switches")
    {
        plate.sequencer.setView (SequencerPanel::View::Keyboard);
        plate.sequencer.setToolsTab (SequencerPanel::ToolsTab::Generate);
        r.paintAll (canvas);
        CHECK (plate.getStaticRenderCount() == renders);
    }

    SECTION ("a theme change rebuilds every tile once")
    {
        r.editor->setThemeId (plate.getThemeId() == ThemeId::Night ? ThemeId::Silver : ThemeId::Night);
        r.paintAll (canvas);
        CHECK (plate.getStaticRenderCount() == 2 * renders);
    }
}

//==============================================================================
TEST_CASE ("editor: the selector's used-slot dots follow the bank the play mode edits", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    putNote (c, false, 0);
    putNote (c, false, 1);
    putNote (c, true, 5);
    r.settle();

    auto& selector = r.editor->getPlate().sequencer.selector;
    REQUIRE (selector.getUsedSlots()[0] == usedMask (c, false, 0));

    c.setPlayMode (seq::PlayMode::Arpeggiator);
    r.settle();
    CHECK (r.editor->getPatternModel().isArp());
    CHECK (selector.getUsedSlots()[0] == usedMask (c, true, 0));

    c.setPlayMode (seq::PlayMode::Sequencer);
    r.settle();
    CHECK (selector.getUsedSlots()[0] == usedMask (c, false, 0));
}

TEST_CASE ("editor: a live override held while the editor closes or the screen hides is released", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    auto* set = dynamic_cast<PushButton*> (r.editor->getPlate().getControls().findButton (acid::kOvrAccentSet));
    REQUIRE (set != nullptr);

    SECTION ("external play mode hides the sequencer screen")
    {
        set->setState (juce::Button::buttonDown);
        REQUIRE (c.getParam (acid::kOvrAccentSet) >= 0.5f);
        c.setPlayMode (seq::PlayMode::External);
        CHECK (c.getParam (acid::kOvrAccentSet) < 0.5f);
    }

    SECTION ("the host closes the window with the mouse still down")
    {
        set->setState (juce::Button::buttonDown);
        REQUIRE (c.getParam (acid::kOvrAccentSet) >= 0.5f);
        r.editor.reset();
        CHECK (c.getParam (acid::kOvrAccentSet) < 0.5f);
    }
}

TEST_CASE ("editor: the preset browser's audition yields to a state the host loads", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    c.setParam (acid::kCutoff, 90.0f);
    juce::MemoryBlock project;
    r.driver.processor.getStateInformation (project);
    c.setParam (acid::kCutoff, 10.0f);

    SECTION ("CANCEL after a host project restore keeps the host's state")
    {
        r.editor->openPresetBrowser (acid::PresetType::Synth);
        REQUIRE (c.auditioning());
        r.driver.processor.setStateInformation (project.getData(), static_cast<int> (project.getSize()));
        r.settle();
        REQUIRE (c.getParam (acid::kCutoff) == Catch::Approx (90.0f));

        // The browser closed itself, keeping the loaded state, so CANCEL has nothing to revert.
        CHECK (! r.editor->getPresetBrowser().isVisible());
        CHECK (! c.auditioning());
        r.editor->getPresetBrowser().close (false);
        CHECK (c.getParam (acid::kCutoff) == Catch::Approx (90.0f));
    }

    SECTION ("CANCEL still reverts what the browser itself loaded")
    {
        r.editor->openPresetBrowser (acid::PresetType::Synth);
        acid::plugin::PresetFilter f;
        f.type = acid::PresetType::Synth;
        const auto synths = c.presets().list (f);
        REQUIRE (! synths.empty());

        // Through the browser's own path: a click on the first row of its list.
        auto& browser = r.editor->getPresetBrowser();
        juce::Component* list = nullptr;

        for (auto* child : browser.getChildren())
            if (auto* view = dynamic_cast<juce::Viewport*> (child))
                list = view->getViewedComponent();

        REQUIRE (list != nullptr);
        const auto click = mouseEventOn (*list, juce::ModifierKeys::leftButtonModifier);
        list->mouseDown (click.withNewPosition (juce::Point<float> (10.0f, 11.0f)));
        r.settle();
        REQUIRE (c.lastLoaded (acid::PresetType::Synth) != nullptr);
        CHECK (browser.isVisible());
        CHECK (c.auditioning());

        browser.close (false);
        CHECK (c.getParam (acid::kCutoff) == Catch::Approx (10.0f));
    }
}

TEST_CASE ("editor: a preset moves the double-click defaults of its own rows only", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& cutoff = r.editor->getPlate().synth.cutoff;
    const auto before = cutoff.getDoubleClickReturnValue();
    c.setParam (acid::kCutoff, 80.0f);

    acid::plugin::PresetFilter f;
    f.type = acid::PresetType::Delay;
    const auto delays = c.presets().list (f);
    REQUIRE (! delays.empty());
    REQUIRE (c.loadPreset (*delays.front()));
    r.settle();
    CHECK (cutoff.getDoubleClickReturnValue() == Catch::Approx (before));

    // A scene carries every row: the knob's double-click now returns to the scene's value.
    f.type = acid::PresetType::Scene;
    const auto scenes = c.presets().list (f);
    REQUIRE (! scenes.empty());
    REQUIRE (c.loadPreset (*scenes.front()));
    r.settle();
    CHECK (cutoff.getDoubleClickReturnValue() == Catch::Approx (static_cast<double> (c.getParam (acid::kCutoff))));
}

TEST_CASE ("editor: a UI change right after a host resize keeps the host's size", "[ui]")
{
    EditorRig r;
    r.editor->setSize (1500, 1119);   // the host drags the window larger (aspect kept)
    r.editor->tick();
    r.editor->getPlate().sequencer.setView (SequencerPanel::View::Keyboard);   // a click 1/30 s later
    r.editor->tick();
    CHECK (r.editor->getWidth() == 1500);

    for (int i = 0; i < 12; ++i)
        r.editor->tick();

    // Settled: the zoom is stored, and the stored value does not bounce the window.
    CHECK (r.controller().zoom() == Catch::Approx (1500.0f / 1180.0f).margin (0.002));
    CHECK (r.editor->getWidth() == 1500);

    // A zoom restored with a state still reaches the window.
    r.controller().setZoom (1.0f);
    CHECK (r.editor->getWidth() == 1180);
}

TEST_CASE ("editor: the plate fits and centres in a window the host sized without the aspect ratio", "[ui]")
{
    EditorRig r;
    auto& plate = r.editor->getPlate();

    r.editor->setSize (1500, 900);
    auto area = r.editor->getLocalArea (&plate, plate.getLocalBounds());
    CHECK (area.getBottom() <= 900);
    CHECK (area.getRight() <= 1500);
    CHECK (std::abs (area.getX() - (1500 - area.getRight())) <= 1);
    CHECK (r.editor->getZoom() == Catch::Approx (900.0f / 880.0f).margin (0.002));

    r.editor->setSize (1000, 1400);
    area = r.editor->getLocalArea (&plate, plate.getLocalBounds());
    CHECK (area.getRight() <= 1000);
    CHECK (area.getBottom() <= 1400);
    CHECK (std::abs (area.getY() - (1400 - area.getBottom())) <= 1);
}

TEST_CASE ("editor: pattern rename is reachable with the mouse", "[ui]")
{
    // docs/research/07 section 6.5: every function reachable by keyboard is reachable by mouse.
    // A left click on the name opens the browser, whose overlay takes the second click of a
    // double-click; the right-click menu is the mouse path to rename.
    EditorRig r;
    auto& plate = r.editor->getPlate();
    auto& name = plate.sequencer.patternName;
    REQUIRE (name.onContextMenu != nullptr);

    int contextMenus = 0;
    name.onContextMenu = [&contextMenus] { ++contextMenus; };
    name.mouseDown (mouseEventOn (name, juce::ModifierKeys::rightButtonModifier));
    CHECK (contextMenus == 1);
    CHECK (! r.editor->getPresetBrowser().isVisible());

    r.editor->renamePattern();
    CHECK (plate.isTextEntryActive());
    plate.endTextEntry();

    name.mouseDown (mouseEventOn (name, juce::ModifierKeys::leftButtonModifier));
    CHECK (r.editor->getPresetBrowser().isVisible());
}

TEST_CASE ("editor: the MIDI map file dialog belongs to the editor and dies with it", "[ui]")
{
    EditorRig r;
    auto& modal = *juce::ModalComponentManager::getInstance();
    REQUIRE (modal.getNumModalComponents() == 0);

    r.editor->chooseMidiMapFile (true);
    CHECK (modal.getNumModalComponents() == 1);

    juce::Component* dialog = nullptr;

    for (auto* child : r.editor->getChildren())
        if (dynamic_cast<juce::FileChooserDialogBox*> (child) != nullptr)
            dialog = child;

    CHECK (dialog != nullptr);   // inside the plugin window, not a desktop window

    if (dialog != nullptr)
        CHECK (r.editor->getLocalBounds().contains (dialog->getBounds()));

    r.editor.reset();
    CHECK (modal.getNumModalComponents() == 0);

    // The smallest window there is (75 %, in external mode as in any other) still holds the dialog.
    EditorRig small;
    small.controller().setPlayMode (seq::PlayMode::External);
    small.editor->setZoom (0.75f);
    small.settle();
    REQUIRE (small.editor->getHeight() == 660);
    small.editor->chooseMidiMapFile (false);

    for (auto* child : small.editor->getChildren())
        if (dynamic_cast<juce::FileChooserDialogBox*> (child) != nullptr)
            CHECK (small.editor->getLocalBounds().contains (child->getBounds()));

    small.editor.reset();
    CHECK (modal.getNumModalComponents() == 0);
}

TEST_CASE ("editor: drag-out files are unique per instance", "[ui]")
{
    // docs/research/07 section 6.7 (b): the host may read the file after the drop; another
    // instance dragging a pattern of the same name must not delete it.
    EditorRig a, b;
    const auto fileA = a.editor->writeDragMidiFile();
    const auto fileB = b.editor->writeDragMidiFile();
    REQUIRE (fileA.existsAsFile());
    REQUIRE (fileB.existsAsFile());
    CHECK (fileA.getFileName() == fileB.getFileName());
    CHECK (fileA != fileB);

    b.editor.reset();   // the other instance goes away (and cleans its own file)
    CHECK (fileA.existsAsFile());
    CHECK (! fileB.existsAsFile());

    const auto again = a.editor->writeDragMidiFile();   // the next drag replaces this instance's file
    CHECK (again.existsAsFile());
    a.editor.reset();
    CHECK (! again.existsAsFile());
}

TEST_CASE ("editor: the preset browser keeps its source toggles inside the plate in external mode", "[ui]")
{
    EditorRig r;
    r.controller().setPlayMode (seq::PlayMode::External);
    r.settle();
    r.editor->openPresetBrowser (acid::PresetType::Synth, true);
    auto& browser = r.editor->getPresetBrowser();
    REQUIRE (browser.isVisible());
    REQUIRE (browser.getHeight() == EditorPlate::kDesignHeight - TopBar::kHeight);

    for (auto* child : browser.getChildren())
        if (child->isVisible())
        {
            INFO ("child " << child->getName() << " at " << child->getBounds().toString());
            CHECK (child->getBottom() <= browser.getHeight());
        }
}

TEST_CASE ("editor: one spelling of colour in the effects captions", "[ui]")
{
    CHECK (shortCaption (acid::kDistColorFreq) == "COLOUR FREQ");
    CHECK (shortCaption (acid::kDistColorReso) == "COLOUR RESO");
    CHECK (shortCaption (acid::kDistColorRange) == "COLOUR RANGE");
}


TEST_CASE ("editor: repaint cost breakdown at 200 % (diagnostic)", "[.][diag]")
{
    EditorRig r;
    r.editor->setZoom (2.0f);
    auto canvas = r.canvas();
    r.paintAll (canvas);
    auto& plate = r.editor->getPlate();
    const auto w = canvas.getWidth(), h = canvas.getHeight();

    const auto plateOnly = [&] { juce::Graphics g (canvas); g.addTransform (juce::AffineTransform::scale (2.0f)); plate.paintEntireComponent (g, false); };
    const auto editorFull = [&] { r.paintAll (canvas); };
    const auto buffersBefore = Faceplate::getBufferRenderCount();
    const auto tilesBefore = plate.getStaticRenderCount();
    WARN ("editor full: " << medianMs (5, 10, editorFull) << " ms");
    WARN ("buffer renders during 50 full paints: " << (Faceplate::getBufferRenderCount() - buffersBefore) << ", tile renders: " << (plate.getStaticRenderCount() - tilesBefore));
    WARN ("plate only: " << medianMs (5, 10, plateOnly) << " ms");

    juce::Image rgb (juce::Image::RGB, w, h, false);
    juce::Image argbCanvas (juce::Image::ARGB, w, h, false);
    juce::Image rgbCanvas (juce::Image::RGB, w, h, false);
    WARN ("raw RGB blit into ARGB canvas: " << medianMs (5, 10, [&] { juce::Graphics g (argbCanvas); g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); g.drawImageAt (rgb, 0, 0); }) << " ms");
    WARN ("raw RGB blit into RGB canvas: " << medianMs (5, 10, [&] { juce::Graphics g (rgbCanvas); g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); g.drawImageAt (rgb, 0, 0); }) << " ms");
    WARN ("plate into RGB canvas: " << medianMs (5, 10, [&] { juce::Graphics g (rgbCanvas); g.addTransform (juce::AffineTransform::scale (2.0f)); plate.paintEntireComponent (g, false); }) << " ms");

    std::vector<juce::Component*> panels;
    for (auto* child : plate.getChildren())
        if (child->isVisible() && dynamic_cast<StaticArt*> (child) != nullptr)
            panels.push_back (child);

    double sum = 0.0;
    for (auto* panel : panels)
    {
        const auto area = panel->getBounds();
        const auto ms = medianMs (5, 10, [&] { juce::Graphics g (canvas); g.addTransform (juce::AffineTransform::scale (2.0f)); g.reduceClipRegion (area); plate.paintEntireComponent (g, false); });
        sum += ms;
        WARN ("panel " << typeid (*panel).name() << " " << area.toString() << ": " << ms << " ms");
    }
    WARN ("panel sum: " << sum << " ms");

    for (auto* panel : panels)
        WARN ("panel opaque=" << (panel->isOpaque() ? 1 : 0) << " cached=" << (panel->getCachedComponentImage() != nullptr ? 1 : 0));

    WARN ("plate.paint with panels excluded from the clip: " << medianMs (5, 10, [&]
    {
        juce::Graphics g (canvas);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        for (auto* panel : panels) g.excludeClipRegion (panel->getBounds());
        plate.paint (g);
    }) << " ms");
    WARN ("plate.paint with the full clip: " << medianMs (5, 10, [&]
    {
        juce::Graphics g (canvas);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        plate.paint (g);
    }) << " ms");

    for (auto* panel : panels) panel->setVisible (false);
    WARN ("plate with panels hidden (background only): " << medianMs (5, 10, plateOnly) << " ms");
    for (auto* panel : panels) panel->setVisible (true);
    r.paintAll (canvas);
    WARN ("plate again: " << medianMs (5, 10, plateOnly) << " ms");
}


//==============================================================================
// QA pass, editor area. Each case pins a finding of the five-lens QA pass (the review_qa tests
// in tests/plugin pin the rest).

namespace
{
bool isValidUtf8 (const juce::String& s)
{
    const auto* raw = s.toRawUTF8();
    return juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (std::strlen (raw)));
}

juce::MouseEvent mouseAt (juce::Component& target, juce::Point<float> pos, juce::ModifierKeys mods)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                             juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

void click (juce::Component& target, juce::Point<float> pos)
{
    target.mouseDown (mouseAt (target, pos, juce::ModifierKeys::leftButtonModifier));
    target.mouseUp (mouseAt (target, pos, juce::ModifierKeys()));
}

/** Bank key b and pattern key s of the selector, in its own coordinates (four keys across). */
juce::Point<float> bankKeyAt (const PatternSelector& sel, int bank)
{
    return { sel.getWidth() * (0.125f + 0.25f * static_cast<float> (bank)), 13.0f + 13.0f };
}

juce::Point<float> slotKeyAt (const PatternSelector& sel, int slot)
{
    const auto b = sel.slotKeys.getBounds().toFloat();
    return { b.getX() + b.getWidth() * (0.125f + 0.25f * static_cast<float> (slot % 4)),
             b.getY() + b.getHeight() * (1.0f / 6.0f + static_cast<float> (slot / 4) / 3.0f) };
}
}

TEST_CASE ("editor qa: external text becomes valid UTF-8 before the editor draws it", "[ui][qa][utf8]")
{
    // Stray continuation, overlong lead, truncated sequence, surrogate, out of range, controls.
    const char* samples[] = { "\x8F\xC0\x8F\xBD" "1", "Caf\xE9 Bass", "cut \xE2\x80", "\xED\xA0\x80 surrogate", "\xF5\x80\x80\x80 big",
                              "\xC0\xAF overlong", "tab\there\nnewline", "\xF0\x9F\x8E\xB9 fine" };

    // Note: juce::String (std::string) itself must never see such bytes. In JUCE 8.0.15 it sizes
    // its buffer by bytes and then writes a three-byte U+FFFD for each bad byte, overrunning the
    // heap at construction; that is why the editor builds every external string through text::.
    for (const auto* bytes : samples)
    {
        const auto fromBytes = text::from (bytes);
        INFO ("sample: " << juce::String::toHexString (bytes, static_cast<int> (std::strlen (bytes))));
        CHECK (isValidUtf8 (fromBytes));
        CHECK (isValidUtf8 (text::sanitise (fromBytes)));
        CHECK (! fromBytes.containsAnyOf ("\t\n\r"));
    }

    // Valid text passes untouched, and a fixed array without a terminator is read by its size.
    CHECK (text::from ("A-01 Opening Line") == "A-01 Opening Line");
    CHECK (text::from ("\xF0\x9F\x8E\xB9 fine") == juce::String (juce::CharPointer_UTF8 ("\xF0\x9F\x8E\xB9 fine")));
    char full[4] = { 'a', 'b', 'c', 'd' };
    CHECK (text::fromArray (full) == "abcd");
    CHECK (text::from ("\x8F\xC0\x8F\xBD" "1") == juce::String (juce::CharPointer_UTF8 ("\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "1")));
}

TEST_CASE ("editor qa: the editor paints invalid UTF-8 safely whatever the core hands it", "[ui][qa][utf8]")
{
    // The scene name, a pattern name and an error message carry bytes that are not UTF-8, as a
    // session, a pattern file or a MIDI track name can. Whether or not the core cleans them, what
    // the editor shows is valid text (run under ASan to see that nothing overruns).
    EditorRig r;
    auto& c = r.controller();
    auto& plate = r.editor->getPlate();
    const std::string bad = "\x8F\xC0\x8F\xBD" "1 \xE2\x80";

    c.setSceneName (bad);
    auto p = r.driver.patternFromText ("C D E F");
    std::memcpy (p.name, bad.data(), bad.size());
    c.commitPattern (false, 0, p);
    r.settle();
    r.editor->showNotice (text::from (bad));

    auto canvas = r.canvas();
    r.paintAll (canvas);
    CHECK (isValidUtf8 (plate.topBar.presetBox.getText()));
    CHECK (isValidUtf8 (plate.sequencer.patternName.getText()));
    CHECK (isValidUtf8 (plate.getValueTooltip().getValueText()));

    r.editor->openPresetBrowser (acid::PresetType::Scene, true);   // the save strip offers the scene name
    r.paintAll (canvas);
    r.editor->getPresetBrowser().close (false);
}

TEST_CASE ("editor qa: a bank key browses; only a pattern key changes the selection", "[ui][qa][selector]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& sel = r.editor->getPlate().sequencer.selector;
    c.selectSlot (0);
    r.settle();

    click (sel, bankKeyAt (sel, 2));
    CHECK (c.selectedSlot() == 0);           // nothing queued
    CHECK (sel.getViewBank() == 2);          // the keys show bank C
    CHECK (sel.getSelectedBank() == 0);

    click (sel, slotKeyAt (sel, 4));
    CHECK (c.selectedSlot() == seq::slotIndex (2, 4));
    CHECK (sel.getSelectedBank() == 2);
    CHECK (sel.getSelectedSlot() == 4);
}

TEST_CASE ("editor qa: pattern bank and slot carry MIDI learn through the selector", "[ui][qa][learn]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& sel = r.editor->getPlate().sequencer.selector;
    auto& controls = r.editor->getPlate().getControls();

    CHECK (controls.findSlider (acid::kPatternBank) == &sel.bankKeys);
    CHECK (controls.findSlider (acid::kPatternSlot) == &sel.slotKeys);
    REQUIRE (sel.bankKeys.param.onMidiLearn != nullptr);
    sel.slotKeys.param.onMidiLearn();
    CHECK (c.midiLearning());
    CHECK (c.midiLearnTarget() == acid::kPatternSlot);
    c.cancelMidiLearn();

    // The attachment keeps the halves on the parameters: a host or CC change shows.
    c.setParam (acid::kPatternBank, 3.0f);
    r.settle();
    CHECK (juce::roundToInt (sel.bankKeys.getValue()) == 3);
    CHECK (sel.getSelectedBank() == 3);
}

TEST_CASE ("editor qa: an armed MIDI learn is shown until it ends, and every cancel path works", "[ui][qa][learn]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& plate = r.editor->getPlate();
    auto& indicator = r.editor->getLearnIndicator();
    auto* resonance = plate.getControls().findSlider (acid::kResonance);
    REQUIRE (resonance != nullptr);
    auto* dragSlider = dynamic_cast<DragSlider*> (resonance);
    REQUIRE (dragSlider != nullptr);

    const auto arm = [&]
    {
        dragSlider->param.onMidiLearn();
        r.settle();
        REQUIRE (c.midiLearning());
    };

    SECTION ("hovering other controls does not hide it")
    {
        arm();
        CHECK (indicator.isActive());
        CHECK (indicator.getTarget() == resonance);
        plate.showValueTip (*plate.getControls().findSlider (acid::kCutoff), "Cutoff", "50 %");
        r.settle();
        CHECK (indicator.isActive());
        c.cancelMidiLearn();
        r.settle();
        CHECK (! indicator.isActive());
    }

    SECTION ("Escape cancels")
    {
        arm();
        CHECK (r.editor->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK (! c.midiLearning());
        CHECK (! indicator.isActive());
    }

    SECTION ("a click on the indicator cancels")
    {
        arm();
        const auto banner = indicator.getBannerBounds().getCentre().toFloat();
        CHECK (indicator.hitTest (juce::roundToInt (banner.x), juce::roundToInt (banner.y)));
        indicator.mouseDown (mouseAt (indicator, banner, juce::ModifierKeys::leftButtonModifier));
        CHECK (! c.midiLearning());
    }

    SECTION ("a click elsewhere on the plate cancels; one on the target does not")
    {
        arm();
        auto* cutoff = plate.getControls().findSlider (acid::kCutoff);
        resonance->mouseDown (mouseEventOn (*resonance, juce::ModifierKeys::rightButtonModifier));
        CHECK (c.midiLearning());
        r.editor->handlePlateClick (*resonance);              // the target's own menu: kept
        CHECK (c.midiLearning());
        r.editor->handlePlateClick (*cutoff);                 // anything else: cancelled
        CHECK (! c.midiLearning());
    }

    SECTION ("the target's menu offers Cancel learn")
    {
        arm();
        REQUIRE (dragSlider->param.isLearning != nullptr);
        CHECK (dragSlider->param.isLearning());
        ParamMenuRequest req;
        req.target = resonance;
        req.hooks = &dragSlider->param;
        bool found = false;
        const auto menu = buildParamMenu (req);

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            found = found || it.getItem().text.startsWith ("Cancel learn");

        CHECK (found);
        dragSlider->param.onCancelLearn();
        CHECK (! c.midiLearning());
    }
}

TEST_CASE ("editor qa: patterns drag out as pattern files and drop back onto slots", "[ui][qa][files]")
{
    EditorRig r;
    auto& c = r.controller();
    auto p = r.driver.patternFromText ("C D# G A# C+ . G F");
    std::strcpy (p.name, "Drop Test");
    p.steps[2].ratchet = 3;
    c.commitPattern (false, 0, p);
    r.settle();

    const auto file = r.editor->writeDragPatternFile (0);
    REQUIRE (file.existsAsFile());
    CHECK (file.getFileName() == "Drop Test.mw12f");

    auto& sel = r.editor->getPlate().sequencer.selector;
    CHECK (sel.isInterestedInFileDrag ({ file.getFullPathName() }));
    CHECK (! sel.isInterestedInFileDrag ({ "/tmp/song.wav" }));
    REQUIRE (dynamic_cast<juce::FileDragAndDropTarget*> (&sel) != nullptr);

    // A drop on pattern key 6 of bank B: slot B-06.
    click (sel, bankKeyAt (sel, 1));
    const auto at = slotKeyAt (sel, 5);
    sel.filesDropped ({ file.getFullPathName(), file.getFullPathName() }, juce::roundToInt (at.x), juce::roundToInt (at.y));
    const auto b6 = c.pattern (false, seq::slotIndex (1, 5));
    const auto b7 = c.pattern (false, seq::slotIndex (1, 6));
    CHECK (seq::samePattern (b6, c.pattern (false, 0)));
    CHECK (seq::samePattern (b7, c.pattern (false, 0)));   // several files fill the following slots
    CHECK (b6.steps[2].ratchet == 3);
    CHECK (juce::String (b6.name) == "Drop Test");
    CHECK (c.canUndo());
}

TEST_CASE ("editor qa: the step editors set ratchet and per-step gate", "[ui][qa][steps]")
{
    EditorRig r;
    auto& c = r.controller();
    c.commitPattern (false, 0, r.driver.patternFromText ("C D E F"));
    r.settle();
    auto& keys = r.editor->getPlate().sequencer.keys;
    LedButton* ratchet = nullptr;
    LedButton* gate = nullptr;

    for (auto* child : keys.getChildren())
        if (auto* b = dynamic_cast<LedButton*> (child))
        {
            if (b->getButtonText().startsWith ("RATCHET")) ratchet = b;
            if (b->getButtonText().startsWith ("GATE")) gate = b;
        }

    REQUIRE (ratchet != nullptr);
    REQUIRE (gate != nullptr);
    ratchet->onClick();
    ratchet->onClick();
    r.settle();
    CHECK (c.pattern (false, 0).steps[0].ratchet == 3);
    CHECK (ratchet->getButtonText() == "RATCHET 3");
    gate->onClick();
    r.settle();
    CHECK (c.pattern (false, 0).steps[0].gate == 32);
    CHECK (gate->getButtonText() == "GATE 25%");
}

TEST_CASE ("editor qa: the generator keyboard is in absolute pitch", "[ui][qa][generator]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& gen = r.editor->getPlate().sequencer.generator;
    REQUIRE (gen.getWidth() > 0);

    auto gp = c.genParams (false, 0);
    gp.root = 9;                                            // A
    gp.noteMask = seq::noteMaskOf ({ 0, 2, 4, 5, 7, 9, 11 });   // major, relative to the root
    c.setGenParams (false, 0, gp);
    r.settle();

    // The pool in absolute pitch is A B C# D E F# G#, what the piano-roll legend lights.
    CHECK (gen.getAbsoluteNoteMask() == seq::noteMaskOf ({ 9, 11, 1, 2, 4, 6, 8 }));

    // A click on the white key labelled C (the first) toggles C, which is the minor third of
    // A (relative bit 3), not the root.
    const auto whiteW = static_cast<float> (gen.getWidth()) / 7.0f;
    gen.mouseDown (mouseAt (gen, { whiteW * 0.5f, 36.0f }, juce::ModifierKeys::leftButtonModifier));
    r.settle();
    CHECK ((c.genParams (false, 0).noteMask & (1 << 3)) != 0);
    CHECK ((c.genParams (false, 0).noteMask & 1) != 0);     // the root is still in
}

TEST_CASE ("editor qa: the preset browser filters by tag group, any within, all across", "[ui][qa][browser]")
{
    EditorRig r;
    auto& c = r.controller();
    r.editor->openPresetBrowser (acid::PresetType::Scene);
    auto& browser = r.editor->getPresetBrowser();
    const auto groups = browser.getTagGroups();
    CHECK (groups.contains ("WAVEFORM"));
    CHECK (groups.contains ("DISTORTION"));
    CHECK (groups.contains ("STYLE"));
    CHECK (groups.contains ("CHARACTER"));
    CHECK (PresetBrowser::tagDisplayName ("DISTORTION", "None") == "Off");
    CHECK (PresetBrowser::tagGroupFor (acid::PresetType::Synth, "Deep Bass") == "FAMILY");
    CHECK (PresetBrowser::tagGroupFor (acid::PresetType::Synth, "Liquid Line") == "FAMILY");
    CHECK (PresetBrowser::tagGroupFor (acid::PresetType::Synth, "Deep") == "CHARACTER");
    CHECK (PresetBrowser::tagGroupFor (acid::PresetType::Scene, "Deep") == "CHARACTER");

    acid::plugin::PresetFilter f;
    f.type = acid::PresetType::Scene;
    const auto all = c.presets().list (f);
    const auto count = [&] (auto&& pred) { return static_cast<int> (std::count_if (all.begin(), all.end(), pred)); };
    REQUIRE (browser.getNumShown() == static_cast<int> (all.size()));

    REQUIRE (browser.setTagSelected ("Saw", true));
    REQUIRE (browser.setTagSelected ("Square", true));
    CHECK (browser.getNumShown() == count ([] (auto* e) { return e->hasTag ("Saw") || e->hasTag ("Square"); }));

    REQUIRE (browser.setTagSelected ("Strong", true));
    CHECK (browser.getNumShown() == count ([] (auto* e) { return (e->hasTag ("Saw") || e->hasTag ("Square")) && e->hasTag ("Strong"); }));

    browser.setGroupMatchesAll ("WAVEFORM", true);
    CHECK (browser.getNumShown() == count ([] (auto* e) { return e->hasTag ("Saw") && e->hasTag ("Square") && e->hasTag ("Strong"); }));

    browser.clearTags();
    CHECK (browser.getNumShown() == static_cast<int> (all.size()));
    browser.close (false);
}

TEST_CASE ("editor qa: RUN shows the switch on its cap and the engine on its lamp", "[ui][qa][transport]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& run = r.editor->getPlate().sequencer.run;
    c.setParam (acid::kSeqRun, 1.0f);
    r.settle();
    CHECK (run.getToggleState());       // the switch is on (latched cap) ...
    CHECK (! run.isLit());              // ... and nothing has played yet
    c.setParam (acid::kSeqRun, 0.0f);
    r.settle();
    CHECK (! run.getToggleState());
}

TEST_CASE ("editor qa: every control has help text, and a coupled knob says why it is off", "[ui][qa][help]")
{
    EditorRig r;
    auto& plate = r.editor->getPlate();
    auto& controls = plate.getControls();
    int missing = 0;
    controls.forEachSlider ([&] (acid::ParamId id, juce::Slider& s) { if (s.getTooltip().isEmpty()) { ++missing; UNSCOPED_INFO (acid::paramInfo (id).id); } });
    controls.forEachButton ([&] (acid::ParamId id, juce::Button& b) { if (b.getTooltip().isEmpty()) { ++missing; UNSCOPED_INFO (acid::paramInfo (id).id); } });
    controls.forEachComboBox ([&] (acid::ParamId id, juce::ComboBox& b) { if (b.getTooltip().isEmpty()) { ++missing; UNSCOPED_INFO (acid::paramInfo (id).id); } });
    CHECK (missing == 0);

    for (auto* icon : std::initializer_list<juce::SettableTooltipClient*> { &plate.topBar.settingsButton, &plate.topBar.prevPreset,
                                                                            &plate.sequencer.shiftLeft, &plate.sequencer.transposeUp,
                                                                            &plate.sequencer.midiDrag, &plate.sequencer.prevPattern })
        CHECK (icon->getTooltip().isNotEmpty());

    r.controller().setParam (acid::kSweepCouple, 1.0f);
    r.settle();
    CHECK (! plate.synth.sweepAmt.isEnabled());
    CHECK (plate.synth.sweepAmt.getTooltip().contains ("out of use"));
    CHECK (plate.synth.sweepCoupleA.getTooltip().containsIgnoreCase ("couple"));
}

TEST_CASE ("editor qa: host automation from another thread reaches the controls through the timer only", "[ui][qa][realtime]")
{
    // The audio-thread side of a host automation is one atomic store: the control follows on
    // the editor's next tick (JUCE's own attachments post a message from the audio thread).
    EditorRig r;
    auto& c = r.controller();
    auto* cutoff = r.editor->getPlate().getControls().findSlider (acid::kCutoff);
    REQUIRE (cutoff != nullptr);
    c.setParam (acid::kCutoff, 20.0f);
    CHECK (cutoff->getValue() == Catch::Approx (20.0));       // message thread: at once

    auto& param = c.parameter (acid::kCutoff);
    std::thread host ([&param] { param.setValueNotifyingHost (param.convertTo0to1 (75.0f)); });
    host.join();
    CHECK (cutoff->getValue() == Catch::Approx (20.0));       // nothing yet
    r.editor->tick();
    CHECK (cutoff->getValue() == Catch::Approx (75.0).margin (0.01));
}

TEST_CASE ("editor qa: the value tip is wide enough for its whole text", "[ui][qa][notice]")
{
    EditorRig r;
    auto& tip = r.editor->getPlate().getValueTooltip();
    r.editor->showNotice ("Saved QA Acid Line");
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (fonts::readout (13.0f), "Saved QA Acid Line", 0.0f, 0.0f);
    CHECK (static_cast<float> (tip.getWidth()) >= glyphs.getBoundingBox (0, -1, true).getWidth() + 18.0f);
}

//==============================================================================
TEST_CASE ("editor: a play mode change never resizes the window", "[ui]")
{
    // pluginval's Editor Automation moves play_mode with the window embedded; a resize on a
    // parameter change crashed its X11 embedding. External mode swaps the screen in place.
    EditorRig r;
    auto& plate = r.editor->getPlate();
    auto& param = r.controller().parameter (acid::kPlayMode);

    for (auto zoom : { 0.75f, 1.0f, 1.5f, 2.0f })
    {
        r.editor->setZoom (zoom);
        r.settle();
        const auto bounds = r.editor->getBounds();
        INFO ("zoom " << zoom);

        for (int i = 0; i < 12; ++i)
        {
            // As a host automates it: the normalised value, from the parameter side.
            const auto mode = static_cast<seq::PlayMode> (i % 3);
            param.setValueNotifyingHost (param.convertTo0to1 (static_cast<float> (static_cast<int> (mode))));
            r.settle();
            CHECK (r.editor->getBounds() == bounds);
            CHECK (plate.getHeight() == EditorPlate::kDesignHeight);
            CHECK (plate.external.isVisible() == (mode == seq::PlayMode::External));
            CHECK (plate.sequencer.isVisible() == (mode != seq::PlayMode::External));
        }

        CHECK (std::abs (r.controller().zoom() - zoom) < 0.01f);
    }
}

TEST_CASE ("editor: the external panel shows and edits the external-mode settings", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    auto& ext = r.editor->getPlate().external;
    c.setPlayMode (seq::PlayMode::External);
    r.settle();
    REQUIRE (ext.isVisible());
    CHECK (ext.getBounds() == r.editor->getPlate().sequencer.getBounds());

    SECTION ("the controls write Settings, one undo step each")
    {
        ext.thresholdBox.setValue (100.0, juce::sendNotificationSync);
        CHECK (c.settings().accentThreshold == 100);
        ext.bendBox.setValue (12.0, juce::sendNotificationSync);
        CHECK (c.settings().bendRange == 12);
        ext.priorityBox.setValue (1.0, juce::sendNotificationSync);
        CHECK (c.settings().notePriority == seq::NotePriority::Low);
        const auto thruBefore = c.settings().midiThru;
        ext.thruButton.triggerClick();   // posted, as a real click's callback is
        pumpMessages (100);
        CHECK (c.settings().midiThru != thruBefore);
        REQUIRE (c.canUndo());
        c.undo();
        CHECK (c.settings().midiThru == thruBefore);
    }

    SECTION ("a settings change from elsewhere shows on the panel")
    {
        auto s = c.settings();
        s.accentThreshold = 33;
        s.bendRange = 5;
        s.notePriority = seq::NotePriority::High;
        s.midiThru = true;
        c.setSettings (s);
        r.settle();
        CHECK (juce::roundToInt (ext.thresholdBox.getValue()) == 33);
        CHECK (juce::roundToInt (ext.bendBox.getValue()) == 5);
        CHECK (ext.priorityBox.getSelectedSegment() == 2);
        CHECK (ext.thruButton.getToggleState());
        CHECK (c.settings().accentThreshold == 33);   // showing them wrote nothing back
    }

    SECTION ("the monitor follows the engine")
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 40, static_cast<juce::uint8> (127)), 0);
        r.driver.processor.processBlock (audio, midi);
        r.settle();
        CHECK (ext.gateLamp.isLit());
        CHECK (ext.accentLamp.isLit());

        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, 40), 0);
        r.driver.processor.processBlock (audio, midi);
        r.settle();
        CHECK (! ext.gateLamp.isLit());
    }
}

TEST_CASE ("editor: keyboard preview sounds while the key is held", "[ui]")
{
    EditorRig r;
    auto& plate = r.editor->getPlate();
    auto& model = r.editor->getPatternModel();
    auto& keys = plate.sequencer.keys;
    plate.sequencer.setView (SequencerPanel::View::Keyboard);
    keys.setNotePreview (true);

    const auto event = [&keys] (juce::Point<float> at)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, &keys, &keys, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };

    // Find a key: press along the bottom of the keyboard until a note sounds.
    bool pressed = false;

    for (float x = 40.0f; x < static_cast<float> (keys.getWidth()) && ! pressed; x += 6.0f)
    {
        const juce::Point<float> at (x, static_cast<float> (keys.getHeight()) - 10.0f);
        keys.mouseDown (event (at));

        if (model.getPreviewNote() >= 0)
        {
            pressed = true;
            const auto& step = model.getPattern().steps[static_cast<std::size_t> (model.getSelectedStep())];
            CHECK (model.getPreviewNote() == seq::stepMidiNote (step));

            // Held: nothing ends it but the release (no timer while the message loop runs).
            pumpMessages (400);
            CHECK (model.getPreviewNote() >= 0);

            keys.mouseUp (event (at));
            CHECK (model.getPreviewNote() == -1);
        }
        else
        {
            keys.mouseUp (event (at));
        }
    }

    CHECK (pressed);

    SECTION ("a gesture with no release ends by itself")
    {
        model.previewNote (48, false, false);
        CHECK (model.getPreviewNote() == 48);
        pumpMessages (600);
        CHECK (model.getPreviewNote() == -1);
    }

    SECTION ("closing the window ends a held note")
    {
        model.previewNote (50, true, true);
        CHECK (model.getPreviewNote() == 50);
        r.editor.reset();   // the editor's destructor calls Controller::stopPreview
    }
}

TEST_CASE ("editor: the settings menu saves and restores the default state", "[ui]")
{
    EditorRig r;
    auto& c = r.controller();
    const auto find = [] (const juce::PopupMenu& menu, const juce::String& text) -> const juce::PopupMenu::Item*
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return &it.getItem();

        return nullptr;
    };

    c.resetDefaultState();
    auto built = buildSettingsMenu (c, r.editor->getPlate(), r.editor->getPlate().topBar.settingsButton, r.editor->settingsMenuHooks());
    const auto* save = find (built.menu, "Save as default");
    const auto* restore = find (built.menu, "Restore factory default");
    REQUIRE (save != nullptr);
    REQUIRE (restore != nullptr);
    CHECK (save->isEnabled);
    CHECK (! restore->isEnabled);   // nothing to restore yet
    CHECK (find (built.menu, "A change applies at the host's next activation") == nullptr);   // not LV2

    built.actions[static_cast<std::size_t> (save->itemID - 1)]();
    CHECK (c.hasUserDefaultState());

    auto again = buildSettingsMenu (c, r.editor->getPlate(), r.editor->getPlate().topBar.settingsButton, r.editor->settingsMenuHooks());
    restore = find (again.menu, "Restore factory default");
    REQUIRE (restore != nullptr);
    CHECK (restore->isEnabled);
    again.actions[static_cast<std::size_t> (restore->itemID - 1)]();
    CHECK (! c.hasUserDefaultState());

    auto hooks = r.editor->settingsMenuHooks();
    hooks.qualityAppliesAtActivation = true;   // what an LV2 instance builds
    auto lv2 = buildSettingsMenu (c, r.editor->getPlate(), r.editor->getPlate().topBar.settingsButton, hooks);
    CHECK (find (lv2.menu, "A change applies at the host's next activation") != nullptr);
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
