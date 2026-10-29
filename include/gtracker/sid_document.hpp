#pragma once
#include "gtracker/sid_song.hpp"
#include <functional>
#include <optional>

namespace gtracker::sid {
struct Cursor {
    int row = 0, voice = 0, column = 0;
};
class Document {
  public:
    Song song = new_song();
    std::size_t generation = 0;
    std::string saved = serialize(song);
    std::vector<Song> undo_stack, redo_stack;
    void change(const std::function<void(Song &)> &edit);
    bool undo();
    bool redo();
    bool dirty() const { return serialize(song) != saved; }
    void opened(Song value);
    void mark_saved() { saved = serialize(song); }
};
std::optional<Event> cell(const Pattern &pattern, int row, int voice);
void put(Pattern &pattern, int row, int voice, std::optional<Event> event);
void transpose(Pattern &pattern, int r0, int r1, int v0, int v1, int semitones);
std::string fresh_id(const Song &song, const std::string &prefix);
} // namespace gtracker::sid
