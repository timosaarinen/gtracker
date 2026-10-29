#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gtracker::sid {
constexpr double frame_rate = 985248.0 / 19656.0;
enum class EventType { note, off };
struct Event { int row = 0; EventType type = EventType::note; int note = 60; int instrument = 1; };
struct Pattern { std::string id, name; std::array<std::vector<Event>, 3> voices; };
struct Order { std::string id, pattern_id; };
struct Section { std::string id, name; std::vector<Order> order; };
struct Song {
    std::string name = "Untitled SID", author;
    std::uint64_t revision = 1;
    int speed = 6;
    std::vector<Pattern> patterns;
    std::vector<Section> sections;
};
struct Preset { int id; const char* name; const char* description; int arp; bool drum; };
const std::array<Preset, 11>& presets();
Song new_song();
void validate(const Song& song, bool completed = false);
Song parse(const std::string& json);
std::string serialize(const Song& song);
Song load(const std::filesystem::path& path);
void save(const Song& song, const std::filesystem::path& path);
std::string note_name(int midi);
std::size_t order_count(const Song& song);
double duration(const Song& song);
} // namespace gtracker::sid
