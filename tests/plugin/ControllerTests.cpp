// The controller: pattern edits with undo and gestures, pattern ops, generator, settings, MIDI
// file export and import, text clipboard, UI persistence, change notifications.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace ptest;

namespace
{
struct Spy final : Controller::Listener
{
    std::uint32_t flags = 0;
    int calls = 0;
    void controllerChanged (std::uint32_t f) override { flags |= f; ++calls; }
};
}

TEST_CASE ("controller: pattern edits are undoable and gestures coalesce", "[plugin][controller][undo]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    Spy spy;
    c.addListener (&spy);
    const std::uint32_t v0 = c.version();

    CHECK (! c.canUndo());
    const seq::Pattern a = d.patternFromText ("C C C C");
    const seq::Pattern b = d.patternFromText ("D D D D");
    c.commitPattern (false, 2, a, "A");
    CHECK (c.version() > v0);
    CHECK ((spy.flags & kChangedPatterns) != 0);
    CHECK ((spy.flags & kChangedUndo) != 0);
    c.commitPattern (false, 2, b, "B");
    CHECK (c.canUndo());
    CHECK (c.undoLabel() == "B");
    c.undo();
    CHECK (seq::samePattern (c.pattern (false, 2), a));
    CHECK (c.canRedo());
    CHECK (c.redoLabel() == "B");
    c.undo();
    CHECK (seq::samePattern (c.pattern (false, 2), seq::Pattern {}));
    c.redo();
    c.redo();
    CHECK (seq::samePattern (c.pattern (false, 2), b));
    CHECK (! c.canRedo());

    // An identical commit is not an undo step.
    c.commitPattern (false, 2, b, "same");
    CHECK (c.undoLabel() == "B");

    // A drag: many commits, one step.
    c.beginGesture();
    for (int i = 0; i < 20; ++i)
    {
        seq::Pattern p = c.pattern (false, 2);
        p.steps[0].note = static_cast<std::uint8_t> (i % 12);
        c.commitPattern (false, 2, p, "Drag");
    }
    c.endGesture();
    CHECK (c.pattern (false, 2).steps[0].note == 19 % 12);
    c.undo();
    CHECK (seq::samePattern (c.pattern (false, 2), b));

    // Depth: at least 100 steps survive.
    for (int i = 0; i < 150; ++i)
    {
        seq::Pattern p = c.pattern (false, 4);
        p.steps[i % 64].state = (i / 64) % 2 == 0 ? seq::StepState::Note : seq::StepState::Rest;
        p.steps[i % 64].note = static_cast<std::uint8_t> (i % 12);
        c.commitPattern (false, 4, p);
    }
    int undone = 0;
    while (c.canUndo() && undone < 100) { c.undo(); ++undone; }
    CHECK (undone == 100);

    // Settings edits are undoable too.
    Settings s = c.settings();
    s.midiOut = ! s.midiOut;
    c.setSettings (s, "MIDI out");
    CHECK (c.settings().midiOut == s.midiOut);
    CHECK (c.undoLabel() == "MIDI out");
    c.undo();
    CHECK (c.settings().midiOut != s.midiOut);
    CHECK ((spy.flags & kChangedSettings) != 0);

    // A state load clears the history.
    d.setStateText (d.stateText());
    d.processor.handlePendingMessages();
    CHECK (! c.canUndo());
    CHECK (! c.canRedo());
    c.removeListener (&spy);
}

TEST_CASE ("controller: pattern operations, generator and templates", "[plugin][controller]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const seq::Pattern p = d.patternFromText ("C D E F");
    c.commitPattern (false, 0, p);

    c.shiftPattern (false, 0, 1);
    CHECK (c.pattern (false, 0).steps[0].note == 5);         // F moved to the front
    c.shiftPattern (false, 0, -1);
    CHECK (seq::samePattern (c.pattern (false, 0), p));
    c.transposePattern (false, 0, 2);
    CHECK (c.pattern (false, 0).steps[0].note == 2);
    c.undo();
    c.reversePattern (false, 0);
    CHECK (c.pattern (false, 0).steps[0].note == 5);
    c.undo();
    c.setPatternLength (false, 0, 8);
    CHECK (c.pattern (false, 0).length == 8);
    c.duplicatePattern (false, 0);
    CHECK (c.pattern (false, 0).length == 16);
    CHECK (c.pattern (false, 0).steps[8].note == 0);
    c.mutePattern (false, 0);
    CHECK (c.pattern (false, 0).steps[0].state == seq::StepState::Rest);
    CHECK (c.pattern (false, 0).steps[1].note == 2);        // pitches kept
    c.clearPattern (false, 0);
    CHECK (seq::samePattern (c.pattern (false, 0), [&] { seq::Pattern q = c.pattern (false, 0); seq::clearSteps (q); return q; }()));
    c.copySlot (false, 1, true, 3);
    CHECK (seq::samePattern (c.pattern (true, 3), c.pattern (false, 1)));
    c.invertArpPattern (3, 1);
    CHECK (c.pattern (true, 3).steps[0].arpIndex == 2);

    // Text clipboard forms: encoded and compact.
    c.commitPattern (false, 5, p);
    const std::string text = c.patternAsText (false, 5);
    CHECK (seq::looksLikeEncodedPattern (text));
    REQUIRE (c.patternFromText (false, 6, text));
    CHECK (seq::samePattern (c.pattern (false, 6), p));
    REQUIRE (c.patternFromText (false, 7, "G G G"));
    CHECK (c.pattern (false, 7).length == 3);
    std::string error;
    CHECK (! c.patternFromText (false, 7, "this is not a pattern %%", &error));
    CHECK (! error.empty());
    CHECK (c.pattern (false, 7).length == 3);

    // Generator: a draw fills the pattern, densities move only what they should.
    c.generate (false, 8, 42);
    const seq::Pattern drawn = c.pattern (false, 8);
    int notes = 0;
    for (int i = 0; i < drawn.length; ++i) notes += drawn.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note;
    CHECK (notes > 0);
    seq::GenParams g = c.genParams (false, 8);
    g.amountNote = 100;
    c.setGenParams (false, 8, g);
    const seq::Pattern dense = c.pattern (false, 8);
    for (int i = 0; i < dense.length; ++i)
        if (drawn.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note)
            CHECK (dense.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note);
    c.generate (false, 9, 42);
    seq::Pattern again = c.pattern (false, 9);
    // determinism: the same seed from the same start gives the same line
    seq::Pattern fromScratch;
    seq::regenerate (fromScratch, 42);
    CHECK (seq::samePattern (again, fromScratch));

    REQUIRE (c.scaleTemplates().size() >= 50);
    c.applyScaleTemplate (false, 8, 3);
    CHECK (c.matchingScaleTemplate (false, 8) == 3);
    c.flattenPattern (false, 8);
    CHECK (c.pattern (false, 8).steps[0].magGate == 255);

    // Selection helpers.
    c.selectSlot (seq::slotIndex (2, 7));
    CHECK (c.selectedSlot() == seq::slotIndex (2, 7));
    CHECK (c.getParam (kPatternBank) == 2.0f);
    CHECK (c.getParam (kPatternSlot) == 8.0f);
    c.setPlayMode (seq::PlayMode::Arpeggiator);
    CHECK (c.playMode() == seq::PlayMode::Arpeggiator);
}

TEST_CASE ("controller: MIDI file export and import", "[plugin][controller][midifile]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const seq::Pattern p = d.patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    c.commitPattern (false, 0, p);
    Settings s = c.settings();
    s.outAccentVelocity = 100;
    s.outNormalVelocity = 40;
    c.setSettings (s);

    const auto bytes = c.patternMidiFile (false, 0);
    REQUIRE (bytes.size() > 50);
    CHECK (std::string (bytes.begin(), bytes.begin() + 4) == "MThd");

    // The configured velocities are in the file, and the file imports back losslessly.
    seq::SmfFile smf;
    REQUIRE (seq::parseSmf (bytes, smf));
    bool saw100 = false, saw40 = false;
    for (const auto& t : smf.tracks)
        for (const auto& e : t.events)
            if ((e.status & 0xF0) == 0x90 && e.data2 > 0) { saw100 = saw100 || e.data2 == 100; saw40 = saw40 || e.data2 == 40; }
    CHECK (saw100);
    CHECK (saw40);

    seq::MidiImportReport report;
    REQUIRE (c.importMidiData (bytes, false, 1, &report));
    CHECK (report.usedEmbedded);
    CHECK (seq::samePattern (c.pattern (false, 1), p));
    CHECK (c.undoLabel() == "Import MIDI");

    // Through files: export, import.
    const auto file = scratch.dir / "out.mid";
    std::string error;
    REQUIRE (c.exportPatternMidiFile (false, 0, file, &error));
    REQUIRE (c.importMidiFile (file, false, 2, &report));
    CHECK (seq::samePattern (c.pattern (false, 2), p));

    // The drag file lives until the next drag.
    const auto drag1 = c.writeDragMidiFile (false, 0);
    REQUIRE (! drag1.empty());
    CHECK (std::filesystem::exists (drag1));
    const auto drag2 = c.writeDragMidiFile (false, 1);
    CHECK (std::filesystem::exists (drag2));
    CHECK ((drag1 == drag2 || ! std::filesystem::exists (drag1)));

    // An arpeggiator take: held notes through the pattern.
    c.commitPattern (true, 0, d.patternFromText ("1 2 3 2+"));
    const int chord[] = { 48, 52, 55 };
    const auto take = c.patternMidiFile (true, 0, chord, 2);
    REQUIRE (seq::parseSmf (take, smf));
    std::vector<int> notes;
    for (const auto& t : smf.tracks)
        for (const auto& e : t.events)
            if ((e.status & 0xF0) == 0x90 && e.data2 > 0) notes.push_back (e.data1);
    REQUIRE (notes.size() == 8);
    CHECK (notes[0] == 48);
    CHECK (notes[1] == 52);
    CHECK (notes[2] == 55);
    CHECK (notes[3] == 64);
    CHECK (notes[4] == 48);

    // Garbage does not import.
    const std::uint8_t junk[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK (! c.importMidiData (junk, false, 3, &report));
    CHECK (! report.ok);
}

TEST_CASE ("controller: UI values per instance and global defaults", "[plugin][controller][ui]")
{
    ScratchConfigDir scratch;
    {
        Driver d;
        auto& c = d.processor.controller();
        CHECK (c.zoom() == 1.0f);
        CHECK (c.theme() == "night");
        c.setZoom (1.5f);                 // also the global default
        c.setTheme ("silver");
        c.setUiValue ("edit_view", "roll");
        CHECK (c.uiValue ("edit_view") == "roll");
        CHECK (c.uiValue ("missing", "fallback") == "fallback");
        CHECK (d.stateText().find ("edit_view=\"roll\"") != std::string::npos);
        c.setZoom (3.0f, false);
        CHECK (c.zoom() == 2.0f);          // clamped
        CHECK (c.globalUiValue ("zoom") == "1.5");
    }
    {
        // A new instance starts from the global defaults.
        Driver e;
        CHECK (e.processor.controller().zoom() == 1.5f);
        CHECK (e.processor.controller().theme() == "silver");
        // but its own state wins once it has one
        e.setStateText ("<mw12F schema=\"1\" type=\"state\"><ui zoom=\"0.75\"/></mw12F>");
        CHECK (e.processor.controller().zoom() == 0.75f);
        CHECK (e.processor.controller().theme() == "silver");
    }
}

TEST_CASE ("controller: engine status is readable before and after prepare", "[plugin][controller]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    CHECK (! c.engineStatus().prepared);
    d.prepare();
    CHECK (c.engineStatus().prepared);
    CHECK (c.engineStatus().latencySamples == d.processor.getLatencySamples());
}
