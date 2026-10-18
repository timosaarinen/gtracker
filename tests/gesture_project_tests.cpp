#include "gtracker/project.hpp"
#include "gtracker/editor_document.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
void serialization_and_renderer(const std::filesystem::path& root, const std::filesystem::path& output) {
    auto score = gtracker::load_pattern(root / "examples/slide-study.gtrk.json");
    check(score.schema_version == 2 && score.model == "contact_0_2" && score.gestures.size() == 4, "v2 fixture/model missing");
    score.gestures[0].points[1].beat = 0.1234567890123;
    const auto text = gtracker::pattern_json(score);
    const auto parsed = gtracker::parse_pattern_json(text);
    check(parsed.gestures[0].points[1].beat == score.gestures[0].points[1].beat && parsed.gestures[0].points[1].fret == 2.25f, "curve microtiming/fractional fret lost");
    check(gtracker::pattern_json(parsed) == text, "v2 round trip unstable");
    const auto audio = gtracker::render_pattern(score);
    auto changed = score;
    changed.gestures[0].points[1].fret = 5;
    check(gtracker::render_pattern(changed).samples != audio.samples, "interior curve point was flattened/ignored");
    auto sorted = score;
    std::reverse(sorted.gestures.begin(), sorted.gestures.end());
    check(gtracker::render_pattern(sorted).samples == audio.samples, "gesture storage order changed timing");
    gtracker::save_pattern(score, output / "slide.json");
    check(gtracker::render_pattern(gtracker::load_pattern(output / "slide.json")).samples == audio.samples, "saved slide cannot be reproduced");
    auto v1 = gtracker::load_pattern(root / "examples/campfire.gtrk.json");
    auto contact = v1; contact.schema_version = 2; contact.model = "contact_0_2";
    check(gtracker::render_pattern(contact).samples == gtracker::render_pattern(v1).samples, "v2 migration changed music without curves");
    check(gtracker::pattern_json(v1).find("gestures") == std::string::npos && gtracker::pattern_json(v1).find("model") == std::string::npos, "v1 silently migrated on save");
    auto silent = score; silent.events.clear();
    const auto no_excitation = gtracker::render_pattern(silent).samples;
    check(std::all_of(no_excitation.begin(), no_excitation.end(), [](float value) { return value == 0; }), "slide generated implicit plucks");

    gtracker::EditorDocument document(v1);
    document.commit(contact);
    contact.gestures.push_back(score.gestures.front());
    document.commit(contact, "curve");
    const auto id = document.pattern().gestures.front().id;
    auto edited = document.pattern(); edited.gestures.front().points[1].fret = 3.5f;
    document.commit(edited, "curve");
    document.end_edit();
    check(document.undo() && document.pattern().gestures.empty(), "grouped curve undo failed");
    check(document.undo() && document.pattern().schema_version == 1 && !document.dirty(), "model migration undo failed");
    check(document.undo(true) && document.undo(true) && document.pattern().gestures.front().id == id, "curve redo lost stable ID");
}
void invalid_curves(const std::filesystem::path& root) {
    const auto original = gtracker::load_pattern(root / "examples/slide-study.gtrk.json");
    for (int variant = 0; variant < 9; ++variant) {
        auto score = original;
        switch (variant) {
        case 0: score.schema_version = 1; break;
        case 1: score.model = "baseline_0_1"; break;
        case 2: score.gestures[0].type = "future_hammer"; break;
        case 3: score.gestures[0].id = score.events[0].id; break;
        case 4: score.gestures[0].points[0].beat = 0.1; break;
        case 5: score.gestures[0].points[1].beat = 0; break;
        case 6: score.gestures[0].points.back().beat = 99; break;
        case 7: score.gestures[1].string_index = 5; score.gestures[1].beat = 0.5; break;
        case 8: score.events[0].beat = 0.3; break;
        }
        rejects([&] { gtracker::validate_pattern(score); }, "unsupported/ambiguous curve accepted");
    }
    auto tiny = original;
    tiny.gestures[0].points[1].beat = 1e-12;
    gtracker::validate_pattern(tiny); // Preserve the data; refuse lossy rendering at this rate.
    rejects([&] { gtracker::render_pattern(tiny); }, "sub-frame curve points silently flattened");
    auto future = gtracker::pattern_json(original);
    const auto position = future.find("\"type\": \"slide\"");
    future.replace(position, std::string("\"type\": \"slide\"").size(), "\"type\": \"future_hammer\"");
    rejects([&] { gtracker::parse_pattern_json(future); }, "future gesture JSON accepted");
    auto boundary = original;
    boundary.events = {{"before", "pluck", 0, 0, 0, 0.8f}, {"at-end", "pluck", 0.3, 0, 4, 0.8f}};
    boundary.gestures = {{"decimal-boundary", "slide", 0.1, 0, {{0, 0}, {0.2, 2}}}};
    gtracker::validate_pattern(boundary);
    check(gtracker::render_pattern(boundary).peak > 0, "decimal beat arithmetic rejected a touching endpoint");
}
void lua_authoring(const std::filesystem::path& root, const std::filesystem::path& output) {
#if GT_HAS_LUA
    auto score = gtracker::load_pattern(root / "examples/campfire.gtrk.json");
    gtracker::apply_lua(score, root / "scripts/slide.lua");
    check(score.schema_version == 2 && score.gestures.size() == 1 && score.model == "contact_0_2", "Lua slide opt-in failed");
    const auto id = score.gestures[0].id;
    const auto time = score.gestures[0].points[1].beat;
    const auto fret = score.gestures[0].points[1].fret;
    { std::ofstream script(output / "transpose.lua"); script << "gtracker.transpose(1)\n"; }
    gtracker::apply_lua(score, output / "transpose.lua");
    check(score.gestures[0].id == id && score.gestures[0].points[1].beat == time && score.gestures[0].points[1].fret == fret + 1, "Lua transpose changed timing/IDs or omitted curves");
    const auto saved = gtracker::pattern_json(score);
    for (const char* code : {"gtracker.set_model('baseline_0_1')", "gtracker.add_slide(2, 1, {{beat=0,fret=0},{beat='bad',fret=2}})",
                             "gtracker.add_slide(2, 1, {{beat=0,fret=0},{beat=1,fret=math.huge}})"}) {
        { std::ofstream script(output / "bad.lua"); script << code; }
        rejects([&] { gtracker::apply_lua(score, output / "bad.lua"); }, "Lua invalid curve/model accepted");
        check(gtracker::pattern_json(score) == saved, "failed Lua curve changed score");
    }
#else
    (void)root; (void)output;
#endif
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected source root");
        const auto root = std::filesystem::weakly_canonical(argv[1]);
        const auto parent = root / ".local/tests";
        std::filesystem::create_directories(parent);
        const auto output = parent / ("gesture-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(output), "cannot reserve test output");
        serialization_and_renderer(root, output);
        invalid_curves(root);
        lua_authoring(root, output);
        // Remove only this test's known individual files, then its empty folder.
        for (const auto* name : {"slide.json", "transpose.lua", "bad.lua"}) std::filesystem::remove(output / name);
        std::filesystem::remove(output);
        std::cout << "gesture documents: v1/v2, exact curves, model/undo/save, renderer and Lua passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gesture project: " << error.what() << '\n'; return 1;
    }
}
