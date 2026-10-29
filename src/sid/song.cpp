#include "gtracker/io.hpp"
#include "gtracker/sid_song.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace gtracker::sid {
namespace {
using Json = nlohmann::json;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error("SID: " + message);
}
void text(const std::string &value, const std::string &location, bool nonempty = false) {
    require(value.size() <= 256 && (!nonempty || !value.empty()), location + ": invalid text length");
    require(value.find('\0') == std::string::npos, location + ": embedded NUL");
    // dump validates UTF-8 even for programmatically constructed documents.
    (void)Json(value).dump();
}
int integer(const Json &object, const char *key) {
    const auto &v = object.at(key);
    require(v.is_number_integer(), std::string(key) + " must be an integer");
    require(v >= 0 && v <= 1000000, std::string(key) + " out of range");
    return v.get<int>();
}
void keys(const Json &object, std::initializer_list<const char *> allowed) {
    require(object.is_object(), "expected object");
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool found = false;
        for (const auto *k : allowed)
            if (it.key() == k)
                found = true;
        require(found, "unknown field: " + it.key());
    }
}
} // namespace
const std::array<Preset, 11> &presets() {
    static const std::array<Preset, 11> bank{{{1, "Heavy Lead", "Animated pulse lead", 0, false},
                                              {2, "Pulse Bass", "Punchy pulse bass", 0, false},
                                              {3, "Bright Major Arp", "Bright 0,4,7 cycle", 4, false},
                                              {4, "Bright Minor Arp", "Bright 0,3,7 cycle", 3, false},
                                              {5, "Warm Lead", "Gentle sustained lead", 0, false},
                                              {6, "Soft Bass", "Smooth triangle bass", 0, false},
                                              {7, "Soft Major Arp", "Soft 0,4,7 cycle", 4, false},
                                              {8, "Soft Minor Arp", "Soft 0,3,7 cycle", 3, false},
                                              {9, "Kick", "Fixed pitch kick", 0, true},
                                              {10, "Snare", "Fixed noise snare", 0, true},
                                              {11, "Hat", "Fixed short noise", 0, true}}};
    return bank;
}
Song new_song() {
    Song s;
    s.patterns = {{"p01", "Heavy", {}}, {"p02", "Mellow", {}}};
    s.sections = {{"s01", "HEAVY", {{"o01", "p01"}}}, {"s02", "MELLOW", {{"o02", "p02"}}}};
    return s;
}
std::size_t order_count(const Song &s) {
    std::size_t n = 0;
    for (const auto &section : s.sections)
        n += section.order.size();
    return n;
}
double duration(const Song &s) { return static_cast<double>(order_count(s)) * 64 * s.speed / frame_rate; }
void validate(const Song &s, bool completed) {
    text(s.name, "name");
    text(s.author, "author");
    require(s.revision >= 1 && s.revision <= 1000000, "revision must be 1..1000000");
    require(s.speed >= 4 && s.speed <= 12, "ticks_per_row must be 4..12");
    require(s.patterns.size() <= 64, "at most 64 patterns");
    require(s.sections.size() <= 16, "at most 16 sections");
    require(order_count(s) <= 128, "at most 128 order entries");
    std::set<std::string> patterns, sections, orders, sounding;
    for (const auto &p : s.patterns) {
        text(p.id, "pattern id", true);
        text(p.name, "pattern name");
        require(patterns.insert(p.id).second, "duplicate pattern id: " + p.id);
        for (std::size_t voice = 0; voice < 3; ++voice) {
            std::set<int> rows;
            for (const auto &e : p.voices[voice]) {
                const auto where =
                    p.id + " voice " + std::to_string(voice + 1) + " row " + std::to_string(e.row);
                require(e.row >= 0 && e.row < 64, where + ": invalid row");
                require(rows.insert(e.row).second, where + ": duplicate event");
                require(e.type == EventType::note || e.type == EventType::off, where + ": invalid type");
                if (e.type == EventType::off)
                    continue;
                require(e.instrument >= 1 && e.instrument <= 11, where + ": invalid preset");
                require(e.note >= 24 && e.note <= 95, where + ": pitch outside C1..B6");
                const auto &preset = presets()[static_cast<std::size_t>(e.instrument - 1)];
                require(!preset.arp || e.note + 7 <= 107, where + ": expanded arpeggio pitch out of range");
                require(!preset.drum || e.note == 60, where + ": drums use canonical MIDI 60");
                sounding.insert(p.id);
            }
        }
    }
    bool audible = false;
    for (const auto &section : s.sections) {
        text(section.id, "section id", true);
        text(section.name, "section name");
        require(sections.insert(section.id).second, "duplicate section id: " + section.id);
        for (const auto &o : section.order) {
            text(o.id, "order id", true);
            require(orders.insert(o.id).second, "duplicate order id: " + o.id);
            require(patterns.count(o.pattern_id) != 0, "order " + o.id + ": unknown pattern " + o.pattern_id);
            audible = audible || sounding.count(o.pattern_id) != 0;
        }
    }
    require(!completed || audible, "add a pattern containing NOTE events to the song before export");
}
Song parse(const std::string &input) {
    require(input.size() <= 1024 * 1024, "source exceeds 1 MiB");
    const auto j = Json::parse(input);
    keys(j, {"format", "schema_version", "name", "author", "revision", "instrument_bank_version", "target",
             "timing", "patterns", "sections"});
    require(j.at("format") == "gtracker.sid", "unknown format");
    require(integer(j, "schema_version") == 1, "unsupported schema version");
    require(integer(j, "instrument_bank_version") == 1, "unsupported instrument bank version");
    keys(j.at("target"), {"clock", "sid_model"});
    require(j.at("target").at("clock") == "pal" && j.at("target").at("sid_model") == "6581",
            "only PAL/6581 supported");
    const auto &timing = j.at("timing");
    keys(timing, {"ticks_per_row", "rows_per_beat"});
    require(integer(timing, "rows_per_beat") == 4, "rows_per_beat must be 4");
    Song s;
    s.name = j.at("name").get<std::string>();
    s.author = j.at("author").get<std::string>();
    s.revision = static_cast<std::uint64_t>(integer(j, "revision"));
    s.speed = integer(timing, "ticks_per_row");
    require(j.at("patterns").is_array() && j.at("sections").is_array(),
            "patterns and sections must be arrays");
    for (const auto &p : j.at("patterns")) {
        keys(p, {"id", "name", "rows", "voices"});
        require(integer(p, "rows") == 64, "patterns require 64 rows");
        Pattern pattern;
        pattern.id = p.at("id").get<std::string>();
        pattern.name = p.at("name").get<std::string>();
        const auto &voices = p.at("voices");
        require(voices.is_array() && voices.size() == 3, "exactly three voices required");
        for (std::size_t v = 0; v < 3; ++v) {
            require(voices[v].is_array(), "voice must be an event array");
            for (const auto &e : voices[v]) {
                Event event;
                event.row = integer(e, "row");
                if (e.at("type") == "note") {
                    keys(e, {"row", "type", "note", "instrument"});
                    event.note = integer(e, "note");
                    event.instrument = integer(e, "instrument");
                } else {
                    require(e.at("type") == "off", "event must be note or off");
                    keys(e, {"row", "type"});
                    event.type = EventType::off;
                }
                pattern.voices[v].push_back(event);
            }
        }
        s.patterns.push_back(pattern);
    }
    for (const auto &sec : j.at("sections")) {
        keys(sec, {"id", "name", "order"});
        Section section;
        section.id = sec.at("id").get<std::string>();
        section.name = sec.at("name").get<std::string>();
        require(sec.at("order").is_array(), "order must be an array");
        for (const auto &o : sec.at("order")) {
            keys(o, {"id", "pattern_id"});
            section.order.push_back({o.at("id").get<std::string>(), o.at("pattern_id").get<std::string>()});
        }
        s.sections.push_back(section);
    }
    validate(s);
    return s;
}
std::string serialize(const Song &s) {
    validate(s);
    Json j = {{"format", "gtracker.sid"},
              {"schema_version", 1},
              {"instrument_bank_version", 1},
              {"name", s.name},
              {"author", s.author},
              {"revision", s.revision},
              {"target", {{"clock", "pal"}, {"sid_model", "6581"}}},
              {"timing", {{"ticks_per_row", s.speed}, {"rows_per_beat", 4}}},
              {"patterns", Json::array()},
              {"sections", Json::array()}};
    for (const auto &p : s.patterns) {
        Json voices = Json::array();
        for (const auto &voice : p.voices) {
            Json events = Json::array();
            for (const auto &e : voice) {
                Json event = {{"row", e.row}, {"type", e.type == EventType::note ? "note" : "off"}};
                if (e.type == EventType::note) {
                    event["note"] = e.note;
                    event["instrument"] = e.instrument;
                }
                events.push_back(event);
            }
            voices.push_back(events);
        }
        j["patterns"].push_back({{"id", p.id}, {"name", p.name}, {"rows", 64}, {"voices", voices}});
    }
    for (const auto &section : s.sections) {
        Json order = Json::array();
        for (const auto &o : section.order)
            order.push_back({{"id", o.id}, {"pattern_id", o.pattern_id}});
        j["sections"].push_back({{"id", section.id}, {"name", section.name}, {"order", order}});
    }
    auto output = j.dump(2) + "\n";
    require(output.size() <= 1024 * 1024, "serialized source exceeds 1 MiB");
    return output;
}
Song load(const std::filesystem::path &path) {
    require(std::filesystem::file_size(path) <= 1024 * 1024, "source exceeds 1 MiB");
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "cannot open source");
    std::string input(1024 * 1024 + 1, '\0');
    in.read(input.data(), static_cast<std::streamsize>(input.size()));
    input.resize(static_cast<std::size_t>(in.gcount()));
    return parse(input);
}
void save(const Song &s, const std::filesystem::path &path) {
    const auto data = serialize(s);
    write_binary_atomic(
        path, [&](std::ostream &out) { out.write(data.data(), static_cast<std::streamsize>(data.size())); });
}
std::string note_name(int midi) {
    require(midi >= 24 && midi <= 95, "invalid display pitch");
    static const char *names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
}
} // namespace gtracker::sid
