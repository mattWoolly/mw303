#include "Model.h"

#include <charconv>
#include <cstring>

namespace acid::plugin
{
int MidiMap::controllerFor (int paramIndex) const noexcept
{
    for (int cc = 0; cc < kNumControllers; ++cc)
        if (param[static_cast<std::size_t> (cc)] == paramIndex)
            return cc;
    return -1;
}

void MidiMap::clearParam (int paramIndex) noexcept
{
    for (auto& p : param)
        if (p == paramIndex)
            p = -1;
}

bool MidiMap::empty() const noexcept
{
    for (const auto p : param)
        if (p >= 0)
            return false;
    return true;
}

xml::Element MidiMap::toXml() const
{
    xml::Element e;
    e.name = "midi_map";
    for (int cc = 0; cc < kNumControllers; ++cc)
    {
        const int p = param[static_cast<std::size_t> (cc)];
        if (p < 0 || p >= kNumParams) continue;
        xml::Element row;
        row.name = "cc";
        row.setAttribute ("number", std::to_string (cc));
        row.setAttribute ("param", kParamTable[static_cast<std::size_t> (p)].id);
        e.children.push_back (std::move (row));
    }
    return e;
}

void MidiMap::fromXml (const xml::Element& e) noexcept
{
    clear();
    for (const auto& row : e.children)
    {
        if (row.name != "cc") continue;
        const std::string* number = row.attribute ("number");
        const std::string* id = row.attribute ("param");
        if (number == nullptr || id == nullptr) continue;
        int cc = -1;
        const auto r = std::from_chars (number->data(), number->data() + number->size(), cc);
        if (r.ec != std::errc {} || r.ptr != number->data() + number->size() || ! bindable (cc)) continue;
        const int p = findParam (*id);
        if (p < 0) continue;
        param[static_cast<std::size_t> (cc)] = static_cast<std::int16_t> (p);
    }
}

Model::Model()
    : bank (std::make_unique<seq::PatternBank>())
{
    ui.name = "ui";
    sceneName = "Init";
    carried.type = PresetType::State;
}

void Model::pushUndo (UndoEntry entry)
{
    redoList.clear();

    if (gestureDepth > 0 && gestureOpen && ! undoList.empty())
    {
        UndoEntry& top = undoList.back();
        if (top.kind == entry.kind && top.arp == entry.arp && top.slot == entry.slot)
        {
            // Same target inside one mouse gesture: keep the first `before`, take the latest `after`.
            top.after = entry.after;
            top.settingsAfter = entry.settingsAfter;
            if (entry.bankAfter != nullptr)
            {
                if (top.bankBefore == nullptr) top.bankBefore = entry.bankBefore;
                top.bankAfter = entry.bankAfter;
            }
            return;
        }
    }

    undoList.push_back (std::move (entry));
    while (undoList.size() > kUndoDepth)
        undoList.pop_front();
    std::size_t withBank = 0;
    for (const UndoEntry& e : undoList)
        if (e.bankAfter != nullptr) ++withBank;
    while (withBank > kBankUndoDepth && ! undoList.empty())
    {
        if (undoList.front().bankAfter != nullptr) --withBank;
        undoList.pop_front();
    }
    gestureOpen = gestureDepth > 0;
}

bool Model::popUndo (UndoEntry& out)
{
    if (undoList.empty()) return false;
    out = undoList.back();
    undoList.pop_back();
    redoList.push_back (out);
    gestureOpen = false;
    return true;
}

bool Model::popRedo (UndoEntry& out)
{
    if (redoList.empty()) return false;
    out = redoList.back();
    redoList.pop_back();
    undoList.push_back (out);
    gestureOpen = false;
    return true;
}

void Model::clearHistory()
{
    undoList.clear();
    redoList.clear();
    gestureOpen = false;
}

std::span<const ParamId> distortionVoicingParams() noexcept
{
    static const std::vector<ParamId> ids = [] {
        std::vector<ParamId> v;
        for (int i = 0; i < kNumParams; ++i)
            if (std::strcmp (kParamTable[static_cast<std::size_t> (i)].group, "Distortion Voicing") == 0)
                v.push_back (static_cast<ParamId> (i));
        return v;
    }();
    return ids;
}
}
