#include "gtracker/sid_backend.hpp"
#include "gtracker/sid_document.hpp"
#include "gtracker/sid_song.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace gtracker::sid;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("SID test failed");
}
void rejects(const std::function<void()> &f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed);
}
int main() {
    try {
        auto s = new_song();
        validate(s);
        rejects([&] { validate(s, true); });
        s.name = "Heavy / Mellow";
        s.patterns[0].voices[0] = {{0, EventType::note, 60, 1}, {63, EventType::off, 60, 1}};
        s.sections[0].order.push_back({"repeat", "p01"});
        validate(s, true);
        const auto json = serialize(s);
        check(serialize(parse(json)) == json);
        check(order_count(s) == 3);
        const auto compiled = compile(s);
        check(compiled.sng.size() > 100 && compiled.sng[0] == 'G' && compiled.preroll_ticks == 9);
        check(compile(parse(json)).sng == compiled.sng);
        auto repeated = s;
        repeated.sections[0].order.push_back({"again", "p01"});
        check(compile(repeated).backend_patterns == compiled.backend_patterns);
        Document document;
        document.opened(s);
        document.change([](Song &song) { song.sections[0].order.push_back({"third", "p01"}); });
        check(document.dirty());
        check(document.undo());
        check(!document.dirty());
        check(document.redo());
        auto overflow = s.patterns[0];
        put(overflow, 2, 0, Event{2, EventType::note, 95, 1});
        const auto saved = overflow.voices[0];
        rejects([&] { transpose(overflow, 0, 63, 0, 0, 1); });
        check(overflow.voices[0].size() == saved.size() && cell(overflow, 2, 0)->note == 95);
        check(note_name(60) == "C-4" && note_name(69) == "A-4");
        auto bad = s;
        bad.patterns[0].voices[0].push_back({0, EventType::note, 61, 2});
        rejects([&] { validate(bad); });
        bad = s;
        bad.sections[0].order[0].pattern_id = "missing";
        rejects([&] { validate(bad); });
        bad = s;
        bad.speed = 3;
        rejects([&] { validate(bad); });
        bad = s;
        bad.patterns[0].voices[0][0].note = 96;
        rejects([&] { validate(bad); });
        bad = s;
        bad.patterns[0].voices[0][0].instrument = 12;
        rejects([&] { validate(bad); });
        rejects([&] { parse(std::string(1024 * 1024 + 1, ' ')); });
        auto modified = json;
        modified.replace(modified.find("\"schema_version\": 1"), 19, "\"schema_version\": 2");
        rejects([&] { parse(modified); });
        std::cout << "SID document checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
