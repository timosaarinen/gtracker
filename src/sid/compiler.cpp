#include "gtracker/hash.hpp"
#include "gtracker/sid_backend.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace gtracker::sid {
namespace {
using Bytes = std::vector<unsigned char>;
using Table = std::vector<std::pair<int, int>>;
void byte(Bytes &b, int v) {
    if (v < 0 || v > 255)
        throw std::runtime_error("SID backend byte capacity exceeded");
    b.push_back(static_cast<unsigned char>(v));
}
void fixed(Bytes &b, const std::string &s, int n) {
    for (int i = 0; i < n; ++i)
        byte(b,
             i < static_cast<int>(s.size()) ? static_cast<unsigned char>(s[static_cast<std::size_t>(i)]) : 0);
}
std::string metadata(const std::string &s, std::vector<std::string> &warnings) {
    std::string result;
    for (unsigned char c : s) {
        if (c >= 32 && c < 127)
            result += static_cast<char>(c);
        else if ((c & 0xc0) != 0x80)
            result += '?';
    }
    if (result.size() > 31)
        result.resize(31);
    if (result != s)
        warnings.push_back("Export text converted to printable ASCII and limited to 31 characters: " + s);
    return result;
}
int append(Table &t, Table program, bool looping = false) {
    const int start = static_cast<int>(t.size()) + 1;
    t.insert(t.end(), program.begin(), program.end());
    t.push_back({255, looping ? start : 0});
    if (t.size() > 255)
        throw std::runtime_error("SID instrument table exceeds 255 steps");
    return start;
}
} // namespace
Compilation compile(const Song &song, bool loop) {
    validate(song, true);
    Compilation c;
    c.preroll_ticks = song.speed + 3;
    auto &out = c.sng;
    fixed(out, "GTS5", 4);
    fixed(out, metadata(song.name, c.warnings), 32);
    fixed(out, metadata(song.author, c.warnings), 32);
    fixed(out, "Original gtracker song", 32);
    byte(out, 1);
    std::vector<Bytes> patterns;
    auto intern = [&](Bytes data) {
        data.insert(data.end(), {255, 0, 0, 0});
        auto it = std::find(patterns.begin(), patterns.end(), data);
        if (it != patterns.end())
            return static_cast<int>(it - patterns.begin());
        if (patterns.size() >= 208)
            throw std::runtime_error("SID converted pattern pool exceeds 208");
        patterns.push_back(data);
        return static_cast<int>(patterns.size() - 1);
    };
    std::map<std::string, std::array<int, 3>> mapping;
    for (const auto &p : song.patterns) {
        std::array<int, 3> indices{};
        for (std::size_t v = 0; v < 3; ++v) {
            Bytes data(64 * 4, 0);
            for (int r = 0; r < 64; ++r)
                data[static_cast<std::size_t>(r) * 4] = 0xbd;
            for (const auto &e : p.voices[v]) {
                const auto at = static_cast<std::size_t>(e.row) * 4;
                data[at] = static_cast<unsigned char>(e.type == EventType::off ? 0xbe : 0x60 + e.note - 12);
                if (e.type == EventType::note)
                    data[at + 1] = static_cast<unsigned char>(e.instrument);
            }
            indices[v] = intern(data);
        }
        mapping[p.id] = indices;
    }
    const int terminal = intern({0xbe, 0, 0, 0});
    const int silence = intern({0xbd, 0, 0, 0});
    for (std::size_t voice = 0; voice < 3; ++voice) {
        Bytes order;
        for (const auto &section : song.sections)
            for (const auto &o : section.order)
                byte(order, mapping.at(o.pattern_id)[voice]);
        if (!loop) {
            byte(order, terminal);
            byte(order, silence);
        }
        byte(out, static_cast<int>(order.size()) + 1);
        out.insert(out.end(), order.begin(), order.end());
        byte(out, 255);
        byte(out, loop ? 0 : static_cast<int>(order.size()) - 1);
    }
    Table wave, pulse, speed;
    Bytes bank;
    // Every program explicitly initializes pulse and vibrato state on each note.
    for (const auto &preset : presets()) {
        const int id = preset.id;
        int ad = 0x09, sr = 0xa6, first = 0x41;
        Table w, p;
        bool cycle = false;
        if (id == 1) {
            w = {{0x41, 0}};
            p = {{0x84, 0}, {24, 24}, {48, 232}, {24, 24}};
            cycle = true;
        }
        if (id == 2) {
            ad = 0x02;
            sr = 0x84;
            w = {{0x41, 0}};
            p = {{0x86, 0}, {12, 8}, {12, 248}};
            cycle = true;
        }
        if (preset.arp) {
            ad = id < 5 ? 0x03 : 0x28;
            sr = id < 5 ? 0x94 : 0x86;
            first = id < 5 ? 0x21 : 0x11;
            w = {{first, 0}, {first, preset.arp}, {first, 7}};
            p = {{0x88, 0}};
            cycle = true;
        }
        if (id == 5) {
            ad = 0x38;
            sr = 0x96;
            first = 0x11;
            w = {{0x11, 0}};
            p = {{0x88, 0}};
        }
        if (id == 6) {
            ad = 0x26;
            sr = 0x85;
            first = 0x11;
            w = {{0x11, 0}};
            p = {{0x88, 0}};
        }
        if (id == 9) {
            ad = 0x00;
            sr = 0x06;
            first = 0x11;
            w = {{0x11, 0xa4}, {0x11, 0x9c}, {0x11, 0x96}, {0x10, 0x90}};
            p = {{0x88, 0}};
        }
        if (id == 10) {
            ad = 0x00;
            sr = 0x07;
            first = 0x81;
            w = {{0x81, 0xcd}, {0x81, 0xc1}, {0x11, 0xa4}, {0x80, 0xc8}};
            p = {{0x88, 0}};
        }
        if (id == 11) {
            ad = 0x00;
            sr = 0x03;
            first = 0x81;
            w = {{0x81, 0xd8}, {0x80, 0xd4}};
            p = {{0x88, 0}};
        }
        const int wp = append(wave, w, preset.arp != 0);
        const int pp = append(pulse, p, cycle && !preset.arp);
        byte(bank, ad);
        byte(bank, sr);
        byte(bank, wp);
        byte(bank, pp);
        byte(bank, 0);
        // Speed zero resets vibrato; delayed vibrato uses a dedicated speed entry.
        int sp = 0, delay = 0;
        if (id == 1 || id == 5) {
            speed.push_back({id == 1 ? 8 : 12, id == 1 ? 10 : 5});
            sp = static_cast<int>(speed.size());
            delay = id == 1 ? 12 : 18;
        }
        byte(bank, sp);
        byte(bank, delay);
        byte(bank, 2);
        byte(bank, first);
        fixed(bank, preset.name, 16);
    }
    // Upstream's documented unused instrument 63 sets DEFAULTTEMPO before any note.
    byte(out, 63);
    out.insert(out.end(), bank.begin(), bank.end());
    for (int i = 12; i <= 63; ++i) {
        byte(out, i == 63 ? song.speed : 0);
        for (int j = 1; j < 25; ++j)
            byte(out, 0);
    }
    for (const auto &table : {wave, pulse, Table{}, speed}) {
        byte(out, static_cast<int>(table.size()));
        for (const auto &entry : table)
            byte(out, entry.first);
        for (const auto &entry : table)
            byte(out, entry.second);
    }
    byte(out, static_cast<int>(patterns.size()));
    for (const auto &pattern : patterns) {
        byte(out, static_cast<int>(pattern.size() / 4));
        out.insert(out.end(), pattern.begin(), pattern.end());
    }
    std::string identity(bank.begin(), bank.end());
    for (const auto &table : {wave, pulse, speed})
        for (const auto &e : table) {
            identity += static_cast<char>(e.first);
            identity += static_cast<char>(e.second);
        }
    c.bank_hash = sha256(identity);
    c.backend_patterns = patterns.size();
    return c;
}
} // namespace gtracker::sid
