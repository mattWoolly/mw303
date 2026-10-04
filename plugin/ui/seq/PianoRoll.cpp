#include "seq/PianoRoll.h"

namespace acid::ui
{
namespace
{
constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr bool kIsBlack[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
constexpr int kOctRows = seq::kMaxOctave - seq::kMinOctave + 1;
constexpr int kAttrRows = 4;
constexpr const char* kAttrNames[kAttrRows] = { "ACCENT", "SLIDE", "VIBRATO", "RATCHET" };
constexpr int kRatchetRow = 3;
constexpr float kHeaderH = 16.0f, kPagerH = 15.0f, kGap = 4.0f, kPad = 5.0f;
}

PianoRoll::PianoRoll (PatternEditModel& m) : model (m)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    model.addListener (this);
}

PianoRoll::~PianoRoll()
{
    model.removeListener (this);
}

int PianoRoll::noteRows() const noexcept
{
    return model.isArpPattern() ? seq::kMaxArpIndex : 12;
}

void PianoRoll::setPage (int newPage)
{
    newPage = juce::jlimit (0, kNumPages - 1, newPage);

    if (newPage != page)
    {
        page = newPage;
        repaint();

        if (onPageChanged != nullptr)
            onPageChanged (page);
    }
}

void PianoRoll::setGeneratorHighlight (bool on, std::uint16_t noteMask, std::uint8_t octaveMask)
{
    if (on != generatorHighlight || noteMask != highlightNotes || octaveMask != highlightOctaves)
    {
        generatorHighlight = on;
        highlightNotes = noteMask;
        highlightOctaves = octaveMask;
        repaint (0, 0, juce::roundToInt (legendW + kPad) + 1, getHeight());
    }
}

void PianoRoll::resized()
{
    const auto b = getLocalBounds().toFloat().reduced (kPad);
    const auto rows = static_cast<float> (noteRows());
    colW = std::floor ((b.getWidth() - legendW) / static_cast<float> (kVisibleSteps));

    // Whole-unit row heights keep the grid crisp at 100 %. The probability lane takes the remainder.
    const auto available = b.getHeight() - kHeaderH - kPagerH - 4.0f * kGap;
    rowH = std::floor (available / (static_cast<float> (kOctRows) * 0.8f + rows + static_cast<float> (kAttrRows) + 1.6f));
    octH = std::round (rowH * 0.8f);

    auto y = b.getY();
    const auto w = legendW + colW * static_cast<float> (kVisibleSteps);
    headerLane = { b.getX(), y, w, kHeaderH };
    y += kHeaderH + kGap;
    octaveLane = { b.getX(), y, w, octH * static_cast<float> (kOctRows) };
    y = octaveLane.getBottom() + kGap;
    noteLane = { b.getX(), y, w, rowH * rows };
    y = noteLane.getBottom() + kGap;
    attrLane = { b.getX(), y, w, rowH * static_cast<float> (kAttrRows) };
    y = attrLane.getBottom() + kGap;
    pagerLane = { b.getX(), b.getBottom() - kPagerH, w, kPagerH };
    probLane = { b.getX(), y, w, pagerLane.getY() - kGap - y };
}

float PianoRoll::columnX (int visibleColumn) const noexcept
{
    return headerLane.getX() + legendW + colW * static_cast<float> (visibleColumn);
}

juce::Rectangle<float> PianoRoll::cell (const juce::Rectangle<float>& lane, int visibleColumn, int row, float rowHeight) const noexcept
{
    return { columnX (visibleColumn) + 1.0f, lane.getY() + rowHeight * static_cast<float> (row), colW - 1.0f, rowHeight - 1.0f };
}

juce::Rectangle<int> PianoRoll::columnBounds (int absoluteStep) const noexcept
{
    const auto c = absoluteStep - page * kVisibleSteps;

    if (c < 0 || c >= kVisibleSteps)
        return {};

    // Just the column: paint() redraws whatever reaches into it from its neighbours (ties, slide marks).
    return juce::Rectangle<float> (columnX (c), 0.0f, colW + 1.0f, pagerLane.getY()).getSmallestIntegerContainer();
}

int PianoRoll::rowForStep (const seq::Pattern& p, int step) const noexcept
{
    const auto* s = &p.steps[static_cast<std::size_t> (step)];

    if (s->state == seq::StepState::Rest)
        return -1;

    if (s->state == seq::StepState::Tie)
    {
        // A tie sits on the row of the note it holds.
        int i = step - 1;

        while (i >= 0 && p.steps[static_cast<std::size_t> (i)].state == seq::StepState::Tie)
            --i;

        if (i < 0 || p.steps[static_cast<std::size_t> (i)].state != seq::StepState::Note)
            return -1;

        s = &p.steps[static_cast<std::size_t> (i)];
    }

    if (model.isArpPattern())
        return seq::kMaxArpIndex - juce::jlimit (1, seq::kMaxArpIndex, static_cast<int> (s->arpIndex));

    return 11 - (s->note % 12);
}

void PianoRoll::setStepPitchFromRow (seq::Step& s, int row) const noexcept
{
    if (model.isArpPattern())
        s.arpIndex = static_cast<std::uint8_t> (juce::jlimit (1, seq::kMaxArpIndex, seq::kMaxArpIndex - row));
    else
        s.note = static_cast<std::uint8_t> (juce::jlimit (0, 11, 11 - row));
}

//==============================================================================
void PianoRoll::paintLegend (juce::Graphics& g, const Theme& t)
{
    const auto x = headerLane.getX();
    const auto w = legendW - 3.0f;
    const auto small = fonts::readout (10.5f);   // legible at 75 % zoom (about 8 px)
    const auto tag = fonts::legend (10.0f, 0.06f);
    const auto arp = model.isArpPattern();

    draw::text (g, "STEP", headerLane.withWidth (w), tag, t.screenTextDim, juce::Justification::centredLeft);

    // Octave rows.
    draw::text (g, "OCT", octaveLane.withWidth (w).withHeight (octH), tag, t.screenTextDim, juce::Justification::centredLeft);

    for (int r = 0; r < kOctRows; ++r)
    {
        const auto octave = seq::kMaxOctave - r;
        const auto inPool = generatorHighlight && (highlightOctaves & (1 << (octave - seq::kMinOctave))) != 0;
        const auto label = octave > 0 ? "+" + juce::String (octave) : juce::String (octave);
        draw::text (g, label, juce::Rectangle<float> (x, octaveLane.getY() + octH * static_cast<float> (r), w, octH - 1.0f), small,
                    inPool ? t.accent : (octave == 0 ? t.screenText : t.screenTextDim), juce::Justification::centredRight);
    }

    // Note rows: a keyboard legend, or held-note numbers for arpeggiator patterns.
    const auto rows = noteRows();

    for (int r = 0; r < rows; ++r)
    {
        const juce::Rectangle<float> key (x, noteLane.getY() + rowH * static_cast<float> (r), w, rowH - 1.0f);

        if (arp)
        {
            g.setColour (t.screenRow);
            g.fillRect (key);
            draw::text (g, juce::String (rows - r), key.withTrimmedRight (4.0f), small, t.screenText, juce::Justification::centredRight);
            continue;
        }

        const auto pc = 11 - r;
        const auto black = kIsBlack[pc];
        const auto inPool = generatorHighlight && (highlightNotes & (1 << pc)) != 0;
        // White keys run the full legend width, black keys stop short, like a keyboard seen side-on.
        const auto keyArea = black ? key.withTrimmedRight (12.0f) : key;
        g.setColour (inPool ? t.accent : (black ? t.screenGridBar.interpolatedWith (t.screenFill, 0.35f) : t.screenText.interpolatedWith (t.screenFill, 0.18f)));
        g.fillRect (keyArea);
        draw::text (g, kNoteNames[pc], keyArea.withTrimmedRight (4.0f), small,
                    inPool ? t.accentInk : (black ? t.screenText : t.screenFill), juce::Justification::centredRight);
    }

    for (int r = 0; r < kAttrRows; ++r)
        draw::text (g, kAttrNames[r], juce::Rectangle<float> (x, attrLane.getY() + rowH * static_cast<float> (r), w, rowH - 1.0f), tag,
                    t.screenTextDim, juce::Justification::centredLeft);

    draw::text (g, "PROB %", probLane.withWidth (w), tag, t.screenTextDim, juce::Justification::centredLeft);
    draw::text (g, "PAGE", pagerLane.withWidth (w), tag, t.screenTextDim, juce::Justification::centredLeft);
}

void PianoRoll::paintPager (juce::Graphics& g, const Theme& t, const seq::Pattern& p)
{
    const auto x0 = columnX (0);
    const auto segW = colW * 4.0f;
    const auto playStep = model.getPlayStep();
    const auto font = fonts::readout (10.0f);

    for (int i = 0; i < kNumPages; ++i)
    {
        const juce::Rectangle<float> seg (x0 + segW * static_cast<float> (i) + 1.0f, pagerLane.getY(), segW - 1.0f, pagerLane.getHeight());
        const auto first = i * kVisibleSteps;
        const auto inUse = first < static_cast<int> (p.length);
        const auto current = i == page;

        g.setColour (current ? t.screenText.interpolatedWith (t.screenFill, 0.72f) : t.screenRowDark);
        g.fillRoundedRectangle (seg, 2.0f);

        if (current)
        {
            g.setColour (t.accent);
            g.fillRect (seg.getX() + 2.0f, seg.getBottom() - 2.0f, seg.getWidth() - 4.0f, 2.0f);
        }

        draw::text (g, juce::String (first + 1) + "-" + juce::String (first + kVisibleSteps), seg, font,
                    current ? t.accent : (inUse ? t.screenText : t.screenTextDim.withAlpha (0.5f)));

        if (playStep >= first && playStep < first + kVisibleSteps)
        {
            g.setColour (t.cursor);
            g.fillEllipse (seg.getX() + 6.0f, seg.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto& p = model.getPattern();
    const auto clip = g.getClipBounds().toFloat();
    g.fillAll (t.screenFill);

    const auto gridX = columnX (0);
    const auto first = page * kVisibleSteps;
    const auto length = static_cast<int> (p.length);
    const auto c0 = juce::jlimit (0, kVisibleSteps - 1, static_cast<int> (std::floor ((clip.getX() - gridX) / colW)));
    const auto c1 = juce::jlimit (0, kVisibleSteps - 1, static_cast<int> (std::floor ((clip.getRight() - gridX) / colW)));
    const auto xa = columnX (c0), xb = columnX (c1 + 1);
    const auto rows = noteRows();
    const auto arp = model.isArpPattern();

    if (clip.getX() < gridX)
        paintLegend (g, t);

    if (clip.getBottom() > pagerLane.getY())
        paintPager (g, t, p);

    // Row bands.
    for (int r = 0; r < kOctRows; ++r)
    {
        g.setColour (seq::kMaxOctave - r == 0 ? t.screenRow : t.screenRowDark);
        g.fillRect (xa, octaveLane.getY() + octH * static_cast<float> (r), xb - xa, octH - 1.0f);
    }

    for (int r = 0; r < rows; ++r)
    {
        const auto dark = arp ? (r % 2 == 1) : kIsBlack[11 - r];
        g.setColour (dark ? t.screenRowDark : t.screenRow);
        g.fillRect (xa, noteLane.getY() + rowH * static_cast<float> (r), xb - xa, rowH - 1.0f);
    }

    for (int r = 0; r < kAttrRows; ++r)
    {
        g.setColour (t.screenRowDark);
        g.fillRect (xa, attrLane.getY() + rowH * static_cast<float> (r), xb - xa, rowH - 1.0f);
    }

    g.setColour (t.screenRowDark);
    g.fillRect (xa, probLane.getY(), xb - xa, probLane.getHeight());

    // Column separators, heavier on the beat.
    const auto gridTop = octaveLane.getY();
    const auto gridBottom = probLane.getBottom();

    for (int c = c0; c <= c1 + 1; ++c)
    {
        const auto beat = c % 4 == 0;
        g.setColour (beat ? t.screenGridBar : t.screenGrid);
        g.fillRect (columnX (c), gridTop, 1.0f, gridBottom - gridTop);
    }

    const auto playStep = model.getPlayStep();
    const auto selected = model.getSelectedStep();
    const auto headerFont = fonts::readout (10.5f);

    for (int c = juce::jmax (0, c0 - 1); c <= juce::jmin (kVisibleSteps - 1, c1 + 1); ++c)
    {
        const auto s = first + c;
        const auto inPattern = s < length;
        const auto& st = p.steps[static_cast<std::size_t> (s)];

        // Header cell.
        {
            const juce::Rectangle<float> h (columnX (c) + 1.0f, headerLane.getY(), colW - 1.0f, headerLane.getHeight() - 1.0f);
            auto ink = ! inPattern ? t.screenTextDim.withAlpha (0.4f) : (c % 4 == 0 ? t.screenText : t.screenTextDim);

            if (s == playStep)
            {
                g.setColour (t.cursor);
                g.fillRoundedRectangle (h, 2.0f);
                ink = juce::Colours::white;
            }
            else if (s == selected && inPattern)
            {
                g.setColour (t.accent);
                g.drawRoundedRectangle (h.reduced (0.5f), 2.0f, 1.0f);
                ink = t.accent;
            }

            draw::text (g, juce::String (s + 1), h, headerFont, ink);
        }

        if (! inPattern)
        {
            g.setColour (t.screenDisabled);
            g.fillRect (columnX (c) + 1.0f, gridTop, colW - 1.0f, gridBottom - gridTop);
            continue;
        }

        const auto sounding = st.state == seq::StepState::Note;
        const auto row = rowForStep (p, s);

        // Octave lane.
        if (st.state != seq::StepState::Rest || st.octave != 0)
        {
            const auto r = cell (octaveLane, c, seq::kMaxOctave - juce::jlimit (seq::kMinOctave, seq::kMaxOctave, static_cast<int> (st.octave)), octH);
            g.setColour (sounding ? t.accent.interpolatedWith (t.screenRow, 0.42f) : t.accentDim.withAlpha (0.55f));
            g.fillRoundedRectangle (r.reduced (colW * 0.22f, 1.5f), 1.5f);
        }

        // Note lane: a note is a bar, a tie continues the bar to its left.
        if (row >= 0)
        {
            auto r = cell (noteLane, c, row, rowH).reduced (0.0f, 1.0f);
            const auto nextTied = s + 1 < length && p.steps[static_cast<std::size_t> (s + 1)].state == seq::StepState::Tie;

            if (st.state == seq::StepState::Tie)
            {
                r = r.withLeft (r.getX() - 3.0f);
                g.setColour (t.accent.interpolatedWith (t.screenRow, 0.35f));
                g.fillRect (nextTied ? r.withRight (r.getRight() + 1.0f) : r.withTrimmedRight (1.0f));
            }
            else
            {
                r = r.withTrimmedLeft (1.0f);
                g.setColour (st.accent ? t.accentBright : t.accent);
                g.fillRoundedRectangle (nextTied ? r.withRight (r.getRight() + 3.0f) : r.withTrimmedRight (1.0f), 2.0f);

                if (st.accent)
                {
                    g.setColour (t.accentInk);
                    g.fillRect (r.getX() + 3.0f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
                }
            }

            // Slide: a glide mark from the end of this bar to the next note.
            if (st.slide && s + 1 < length)
            {
                const auto nextRow = rowForStep (p, s + 1);

                if (nextRow >= 0 && p.steps[static_cast<std::size_t> (s + 1)].state == seq::StepState::Note)
                {
                    const auto from = juce::Point<float> (r.getRight() - 4.0f, r.getCentreY());
                    const auto to = juce::Point<float> (columnX (c + 1) + 4.0f, noteLane.getY() + rowH * (static_cast<float> (nextRow) + 0.5f) - 0.5f);
                    g.setColour (t.accentInk.withAlpha (0.9f));
                    g.drawLine ({ from, to }, 3.6f);
                    g.setColour (t.accentBright);
                    g.drawLine ({ from, to }, 1.6f);
                }
            }
        }
        else if (st.state == seq::StepState::Tie)
        {
            // A tie with nothing to hold: show it hollow so it can be found and removed.
            const auto ghostRow = arp ? seq::kMaxArpIndex - juce::jlimit (1, seq::kMaxArpIndex, static_cast<int> (st.arpIndex)) : 11 - (st.note % 12);
            g.setColour (t.accentDim);
            g.drawRoundedRectangle (cell (noteLane, c, ghostRow, rowH).reduced (1.5f), 2.0f, 1.0f);
        }

        // Attribute lane.
        const bool stepFlags[3] = { st.accent, st.slide, st.vibrato };

        for (int r = 0; r < 3; ++r)
            if (stepFlags[r])
            {
                g.setColour (st.state != seq::StepState::Rest ? t.accent : t.accentDim.withAlpha (0.7f));
                g.fillRoundedRectangle (cell (attrLane, c, r, rowH).reduced (1.0f, 1.0f), 1.5f);
            }

        // Ratchet: one tick per hit when the step re-strikes (2..4), nothing for a single hit.
        if (st.ratchet > 1)
        {
            const auto r = cell (attrLane, c, kRatchetRow, rowH).reduced (colW * 0.18f, 2.0f);
            const auto hits = juce::jlimit (2, 4, static_cast<int> (st.ratchet));
            const auto pitch = r.getWidth() / static_cast<float> (hits);
            g.setColour (st.state != seq::StepState::Rest ? t.accent : t.accentDim.withAlpha (0.7f));

            for (int i = 0; i < hits; ++i)
                g.fillRoundedRectangle (r.getX() + pitch * static_cast<float> (i) + 1.0f, r.getY(), juce::jmax (1.5f, pitch - 2.5f), r.getHeight(), 1.0f);
        }

        // Probability lane.
        {
            const auto full = juce::Rectangle<float> (columnX (c) + 1.0f, probLane.getY(), colW - 1.0f, probLane.getHeight()).reduced (colW * 0.26f, 2.0f);
            const auto frac = static_cast<float> (juce::jlimit (0, 100, static_cast<int> (st.probability))) / 100.0f;
            const auto bar = full.withTop (full.getBottom() - juce::jmax (1.5f, full.getHeight() * frac));
            const auto certain = st.probability >= 100;
            g.setColour (! sounding ? t.screenRow : (certain ? t.accentDim : t.accent));
            g.fillRoundedRectangle (bar, 1.5f);
        }

        if (s == playStep)
        {
            g.setColour (t.cursor.withAlpha (0.20f));
            g.fillRect (columnX (c) + 1.0f, gridTop, colW - 1.0f, gridBottom - gridTop);
        }
    }
}

//==============================================================================
PianoRoll::Hit PianoRoll::locate (juce::Point<float> pos) const noexcept
{
    Hit h;
    const auto gridX = columnX (0);

    if (pos.x < gridX || colW <= 0.0f)
        return h;

    const auto column = static_cast<int> ((pos.x - gridX) / colW);

    if (pagerLane.contains (pos))
    {
        h.lane = Lane::Pager;
        h.row = juce::jlimit (0, kNumPages - 1, column / 4);
        return column < kVisibleSteps ? h : Hit();
    }

    if (column < 0 || column >= kVisibleSteps)
        return h;

    h.step = page * kVisibleSteps + column;

    const auto inLane = [&] (const juce::Rectangle<float>& lane, float rh, int numRows, Lane which)
    {
        if (pos.y >= lane.getY() && pos.y < lane.getBottom())
        {
            h.lane = which;
            h.row = juce::jlimit (0, numRows - 1, static_cast<int> ((pos.y - lane.getY()) / rh));
            return true;
        }

        return false;
    };

    if (inLane (headerLane, headerLane.getHeight(), 1, Lane::Header)) return h;
    if (inLane (octaveLane, octH, kOctRows, Lane::Octave))            return h;
    if (inLane (noteLane, rowH, noteRows(), Lane::Note))              return h;
    if (inLane (attrLane, rowH, kAttrRows, Lane::Attribute))          return h;

    if (pos.y >= probLane.getY() - 2.0f && pos.y < probLane.getBottom() + 2.0f)
    {
        h.lane = Lane::Probability;
        h.fraction = juce::jlimit (0.0f, 1.0f, (probLane.getBottom() - 2.0f - pos.y) / (probLane.getHeight() - 4.0f));
    }

    return h;
}

void PianoRoll::applyGesture (const Hit& h)
{
    const auto current = model.getPattern();
    const auto length = static_cast<int> (current.length);
    const auto step = juce::jlimit (0, length - 1, h.step);
    const auto from = lastStep < 0 ? step : juce::jmin (lastStep, step);
    const auto to = lastStep < 0 ? step : juce::jmax (lastStep, step);
    auto edited = current;

    const auto forRange = [&] (auto&& fn)
    {
        for (int s = from; s <= to; ++s)
            fn (edited.steps[static_cast<std::size_t> (s)]);
    };

    switch (gesture)
    {
        case Gesture::None:
            return;

        case Gesture::Paint:
            forRange ([&] (seq::Step& s)
            {
                s.state = seq::StepState::Note;
                setStepPitchFromRow (s, h.row);
            });
            break;

        case Gesture::Erase:
            forRange ([] (seq::Step& s) { s.state = seq::StepState::Rest; });
            break;

        case Gesture::TieOrErase:
            // Restart from the pattern as it was at mouse-down, so dragging back shortens the tie again.
            edited = gestureBase;

            for (int s = anchorStep + 1; s <= step; ++s)
            {
                auto& tied = edited.steps[static_cast<std::size_t> (s)];
                const auto& anchor = gestureBase.steps[static_cast<std::size_t> (anchorStep)];
                tied.state = seq::StepState::Tie;
                tied.note = anchor.note;
                tied.octave = anchor.octave;
                tied.arpIndex = anchor.arpIndex;
            }

            if (step != anchorStep)
                gestureMoved = true;

            break;

        case Gesture::FlagOn:
        case Gesture::FlagOff:
            forRange ([&] (seq::Step& s)
            {
                const auto on = gesture == Gesture::FlagOn;

                if (flagRow == 0)       s.accent = on;
                else if (flagRow == 1)  s.slide = on;
                else                    s.vibrato = on;
            });
            break;

        case Gesture::Ratchet:
            forRange ([&] (seq::Step& s) { s.ratchet = static_cast<std::uint8_t> (ratchetValue); });
            break;

        case Gesture::Octave:
            edited.steps[static_cast<std::size_t> (step)].octave = static_cast<std::int8_t> (seq::kMaxOctave - h.row);
            break;

        case Gesture::Probability:
            forRange ([&] (seq::Step& s) { s.probability = static_cast<std::uint8_t> (juce::roundToInt (h.fraction * 100.0f)); });
            break;
    }

    lastStep = step;
    lastRow = h.row;
    model.applyPattern (edited);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    gesture = Gesture::None;
    const auto h = locate (e.position);
    const auto& p = model.getPattern();

    if (h.lane == Lane::Pager)
    {
        setPage (h.row);
        return;
    }

    if (h.lane == Lane::None || h.step < 0 || h.step >= static_cast<int> (p.length))
        return;

    model.setSelectedStep (h.step);

    if (h.lane == Lane::Header)
        return;

    const auto& st = p.steps[static_cast<std::size_t> (h.step)];
    gestureBase = p;
    anchorStep = h.step;
    lastStep = -1;
    lastRow = -1;
    gestureMoved = false;

    switch (h.lane)
    {
        case Lane::Note:
        {
            const auto onBar = rowForStep (p, h.step) == h.row;

            if (e.mods.isPopupMenu())                                gesture = Gesture::Erase;
            else if (onBar && st.state == seq::StepState::Note)       gesture = Gesture::TieOrErase;
            else if (onBar && st.state == seq::StepState::Tie)        gesture = Gesture::Erase;
            else                                                      gesture = Gesture::Paint;
            break;
        }

        case Lane::Attribute:
        {
            if (h.row == kRatchetRow)
            {
                // A click steps the hit count 1, 2, 3, 4, 1; a drag paints that count along the
                // row; the right button resets to a single hit.
                ratchetValue = e.mods.isPopupMenu() ? 1 : static_cast<int> (st.ratchet) % 4 + 1;
                gesture = Gesture::Ratchet;
                break;
            }

            flagRow = h.row;
            const auto isOn = h.row == 0 ? st.accent : (h.row == 1 ? st.slide : st.vibrato);
            gesture = (isOn || e.mods.isPopupMenu()) ? Gesture::FlagOff : Gesture::FlagOn;
            break;
        }

        case Lane::Octave:       gesture = Gesture::Octave; break;
        case Lane::Probability:  gesture = Gesture::Probability; break;
        case Lane::None:
        case Lane::Header:
        case Lane::Pager:
            return;
    }

    model.beginEditGesture();
    applyGesture (h);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (gesture == Gesture::None)
        return;

    // Keep the gesture in its lane: clamp the pointer into it, then re-run the hit test.
    const auto& lane = gesture == Gesture::Octave ? octaveLane
                     : (gesture == Gesture::Probability ? probLane
                     : ((gesture == Gesture::FlagOn || gesture == Gesture::FlagOff || gesture == Gesture::Ratchet) ? attrLane : noteLane));
    const auto x = juce::jlimit (columnX (0) + 0.5f, columnX (kVisibleSteps) - 0.5f, e.position.x);
    const auto y = juce::jlimit (lane.getY() + 0.5f, lane.getBottom() - 0.5f, e.position.y);
    auto h = locate ({ x, y });

    if (h.step < 0)
        return;

    if (gesture == Gesture::Probability)
        h.fraction = juce::jlimit (0.0f, 1.0f, (probLane.getBottom() - 2.0f - e.position.y) / (probLane.getHeight() - 4.0f));

    const auto clampedStep = juce::jmin (h.step, static_cast<int> (model.getPattern().length) - 1);

    if (clampedStep != lastStep || h.row != lastRow || gesture == Gesture::Probability)
        applyGesture (h);
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (gesture == Gesture::None)
        return;

    if (gesture == Gesture::TieOrErase && ! gestureMoved)
    {
        // A plain click on a note removes it.
        auto edited = model.getPattern();
        edited.steps[static_cast<std::size_t> (anchorStep)].state = seq::StepState::Rest;
        model.applyPattern (edited);
    }

    gesture = Gesture::None;
    model.endEditGesture();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;

    if (delta > 0.0f)       setPage (page - 1);
    else if (delta < 0.0f)  setPage (page + 1);
    else                    juce::Component::mouseWheelMove (e, wheel);
}

//==============================================================================
void PianoRoll::patternChanged()
{
    const auto rowsBefore = juce::roundToInt (noteLane.getHeight() / juce::jmax (1.0f, rowH));

    if (rowsBefore != noteRows())
        resized();

    repaint();
}

void PianoRoll::playStepChanged (int previous, int now)
{
    if (followPlayback && now >= 0 && now / kVisibleSteps != page)
    {
        setPage (now / kVisibleSteps);
        return;
    }

    for (auto s : { previous, now })
        if (s >= 0)
            repaint (columnBounds (s));

    // The pager shows which page is playing: only repaint it when that changes.
    if ((previous < 0 ? -1 : previous / kVisibleSteps) != (now < 0 ? -1 : now / kVisibleSteps))
        repaint (pagerLane.getSmallestIntegerContainer());
}

void PianoRoll::selectedStepChanged (int previous, int now)
{
    for (auto s : { previous, now })
        if (s >= 0)
            repaint (columnBounds (s).withHeight (juce::roundToInt (headerLane.getBottom()) + 1));
}
}
