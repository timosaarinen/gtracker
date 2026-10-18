#include "gtracker/project.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<typename Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected source root");
        const std::filesystem::path root = argv[1];
        auto pattern = gtracker::load_pattern(root / "examples/campfire.gtrk.json");
        const auto text = gtracker::pattern_json(pattern);
        check(gtracker::pattern_json(gtracker::parse_pattern_json(text)) == text, "Round trip changed musical data");
        auto duplicate = pattern;
        duplicate.events.push_back(duplicate.events.front());
        rejects([&] { gtracker::validate_pattern(duplicate); }, "Duplicate event ID accepted");
        auto bad = pattern;
        bad.events.front().beat = bad.length_beats;
        rejects([&] { gtracker::validate_pattern(bad); }, "End-boundary event accepted");
        bad = pattern;
        bad.bpm = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { gtracker::validate_pattern(bad); }, "NaN tempo accepted");
        rejects([&] { gtracker::parse_pattern_json("{\"schema_version\":99}"); }, "Unknown schema accepted");
        auto unknown = text;
        unknown.insert(unknown.find('{') + 1, "\"typo\": 1,");
        rejects([&] { gtracker::parse_pattern_json(unknown); }, "Unknown field silently accepted");
        auto negative_seed = text;
        const auto seed_pos = negative_seed.find("\"seed\": 42");
        check(seed_pos != std::string::npos, "Seed fixture missing");
        negative_seed.replace(seed_pos, std::string("\"seed\": 42").size(), "\"seed\": -1");
        rejects([&] { gtracker::parse_pattern_json(negative_seed); }, "Negative seed wrapped");
        auto boolean_strength = text;
        const auto value_begin = boolean_strength.find(':', boolean_strength.find("\"strength\"")) + 1;
        const auto value_end = boolean_strength.find_first_of(",\n}", value_begin);
        boolean_strength.replace(value_begin, value_end - value_begin, " true");
        rejects([&] { gtracker::parse_pattern_json(boolean_strength); }, "Boolean strength was coerced to a number");
        const auto rendered = gtracker::render_pattern(pattern, {24000, 0.25});
        check(rendered.samples.size() == 134000, "Musical timing/decay tail frame count changed");
        check(rendered.peak > 0.001f && rendered.rms > 0.0001f, "Fixture rendered silence");
        check(gtracker::render_pattern(pattern, {24000, 0.25}).samples == rendered.samples, "Renderer lost seed reproducibility");
        gtracker::RenderControl control;
        bool cancelled = false;
        std::uint64_t completed = 0, total = 0;
        std::size_t updates = 0;
        control.cancelled = [&] { return cancelled; };
        control.progress = [&](std::uint64_t done, std::uint64_t count) {
            check(done >= completed && done <= count && (total == 0 || total == count), "In-memory render progress is not bounded/monotonic");
            completed = done; total = count; ++updates;
        };
        const auto controlled = gtracker::render_pattern(pattern, {24000, 0.25}, control);
        check(controlled.samples == rendered.samples && controlled.peak == rendered.peak && controlled.rms == rendered.rms && controlled.identity == rendered.identity,
              "Job control changed audio/metrics/cache identity");
        check(completed == rendered.frames() && total == completed && updates > 2, "In-memory render did not report completion");
        cancelled = true;
        rejects([&] { gtracker::render_pattern(pattern, {24000, 0.25}, control); }, "Pre-cancelled render ran");
        cancelled = false;
        control.progress = [&](std::uint64_t done, std::uint64_t) { if (done >= 1024) cancelled = true; };
        rejects([&] { gtracker::render_pattern(pattern, {24000, 0.25}, control); }, "Mid-render cancellation was ignored");
        cancelled = false;
        control.progress = [&](std::uint64_t done, std::uint64_t count) { if (done == count) cancelled = true; };
        rejects([&] { gtracker::render_pattern(pattern, {24000, 0.25}, control); }, "Cancellation after audio generation published a completed result");
        control.progress = [](std::uint64_t, std::uint64_t) { throw std::runtime_error("injected progress failure"); };
        cancelled = false;
        rejects([&] { gtracker::render_pattern(pattern, {24000, 0.25}, control); }, "Progress callback failure was swallowed");
        check(gtracker::render_pattern(pattern, {24000, 0.25}).samples == rendered.samples, "Cancelled jobs changed later native state");
        auto reordered = pattern;
        std::swap(reordered.events.front(), reordered.events.back());
        check(gtracker::render_pattern(reordered, {24000, 0.25}).samples == rendered.samples, "Unsorted score changed scheduled result");
        const auto temp = std::filesystem::current_path() / "project-test-output";
        std::filesystem::create_directories(temp);
        gtracker::save_pattern(pattern, temp / "roundtrip.json");
        pattern.name = "Saved twice";
        gtracker::save_pattern(pattern, temp / "roundtrip.json");
        check(gtracker::load_pattern(temp / "roundtrip.json").name == pattern.name, "Replacing existing saved pattern failed");
        gtracker::write_wav(temp / "test.wav", rendered.samples, rendered.sample_rate);
        check(std::filesystem::file_size(temp / "test.wav") == 44 + rendered.samples.size() * 2, "WAV PCM/header length mismatch");
#if GT_HAS_LUA
        const auto before = pattern.events.front().strength;
        const auto revision = pattern.revision;
        gtracker::apply_lua(pattern, root / "scripts/soften.lua");
        check(pattern.revision == revision + 1, "Lua transaction revision did not advance");
        check(pattern.events.front().strength < before, "Lua softening command had no effect");
        const auto snapshot = gtracker::pattern_json(pattern);
        {
            std::ofstream out(temp / "invalid.lua");
            out << "gtracker.set_tempo(100)\nerror('deliberate test error')\n";
        }
        rejects([&] { gtracker::apply_lua(pattern, temp / "invalid.lua"); }, "Lua failure was not reported");
        check(gtracker::pattern_json(pattern) == snapshot, "Failed Lua command partially changed the pattern");
        auto exhausted = pattern;
        exhausted.revision = 9007199254740991ULL;
        const auto exhausted_snapshot = gtracker::pattern_json(exhausted);
        rejects([&] { gtracker::apply_lua(exhausted, root / "scripts/soften.lua"); }, "Lua exceeded JSON-safe revision range");
        check(gtracker::pattern_json(exhausted) == exhausted_snapshot, "Revision-limit failure changed the pattern");
#endif
        std::filesystem::remove_all(temp);
        std::cout << "project: serialization, validation, timing, WAV, reproducibility and Lua checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
