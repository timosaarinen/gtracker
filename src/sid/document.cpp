#include "gtracker/sid_document.hpp"
#include <algorithm>
#include <stdexcept>

namespace gtracker::sid {
void Document::change(const std::function<void(Song &)> &edit) {
    Song next = song;
    edit(next);
    validate(next);
    if (serialize(next) == serialize(song))
        return;
    undo_stack.push_back(song);
    if (undo_stack.size() > 128)
        undo_stack.erase(undo_stack.begin());
    redo_stack.clear();
    song = std::move(next);
    ++generation;
}
bool Document::undo() {
    if (undo_stack.empty())
        return false;
    redo_stack.push_back(song);
    song = undo_stack.back();
    undo_stack.pop_back();
    ++generation;
    return true;
}
bool Document::redo() {
    if (redo_stack.empty())
        return false;
    undo_stack.push_back(song);
    song = redo_stack.back();
    redo_stack.pop_back();
    ++generation;
    return true;
}
void Document::opened(Song value) {
    validate(value);
    song = std::move(value);
    undo_stack.clear();
    redo_stack.clear();
    mark_saved();
    ++generation;
}
std::optional<Event> cell(const Pattern &p, int row, int voice) {
    for (const auto &e : p.voices.at(static_cast<std::size_t>(voice)))
        if (e.row == row)
            return e;
    return {};
}
void put(Pattern &p, int row, int voice, std::optional<Event> event) {
    if (row < 0 || row >= 64)
        throw std::runtime_error("SID row out of range");
    auto &events = p.voices.at(static_cast<std::size_t>(voice));
    events.erase(std::remove_if(events.begin(), events.end(), [&](const Event &e) { return e.row == row; }),
                 events.end());
    if (event) {
        event->row = row;
        events.push_back(*event);
    }
    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.row < b.row; });
}
void transpose(Pattern &p, int r0, int r1, int v0, int v1, int semitones) {
    auto next = p;
    for (int v = v0; v <= v1; ++v)
        for (auto &e : next.voices.at(static_cast<std::size_t>(v)))
            if (e.row >= r0 && e.row <= r1 && e.type == EventType::note &&
                !presets().at(static_cast<std::size_t>(e.instrument - 1)).drum) {
                if (e.note + semitones < 24 || e.note + semitones > 95)
                    throw std::runtime_error("Transpose would exceed C1..B6; selection unchanged");
                e.note += semitones;
            }
    p = std::move(next);
}
std::string fresh_id(const Song &song, const std::string &prefix) {
    for (int n = 1;; ++n) {
        auto id = prefix + std::to_string(n);
        bool used = false;
        for (const auto &p : song.patterns)
            used = used || p.id == id;
        for (const auto &s : song.sections) {
            used = used || s.id == id;
            for (const auto &o : s.order)
                used = used || o.id == id;
        }
        if (!used)
            return id;
    }
}
} // namespace gtracker::sid
