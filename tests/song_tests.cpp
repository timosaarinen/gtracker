#include "gtracker/song.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
std::vector<float> audio(gtracker::NativeRenderer renderer) {
    std::vector<float> samples(static_cast<std::size_t>(renderer.total_frames()) * renderer.channels());
    for (std::size_t offset = 0; offset < renderer.total_frames();) {
        const auto count = renderer.read(samples.data() + offset * renderer.channels(), 311);
        check(count > 0, "early song EOF"); offset += count;
    }
    return samples;
}
gtracker::Song one_pattern(const gtracker::Pattern& pattern) {
    gtracker::Song song;
    song.bpm = pattern.bpm; song.seed = pattern.seed; song.instrument_pattern = "phrase";
    song.patterns = {{"phrase", pattern, {}}};
    song.order = {{"first", "phrase", 1, 1, 0, 1, 0}};
    return song;
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "source directory required");
        const auto root = std::filesystem::path(argv[1]);
        const auto baseline = gtracker::load_pattern(root / "examples/campfire.gtrk.json");
        auto song = one_pattern(baseline);
        check(audio(gtracker::song_renderer(song)) == gtracker::render_pattern(baseline).samples, "single-instance song differs from pattern renderer");
        for (double length : {0.3, 1.1, 7.123456789, 8.0}) {
            auto pattern = baseline; pattern.length_beats = length;
            pattern.events.erase(std::remove_if(pattern.events.begin(), pattern.events.end(), [&](const gtracker::Event& e) { return e.beat >= length; }), pattern.events.end());
            check(audio(gtracker::song_renderer(one_pattern(pattern), {44100, 0})) == gtracker::render_pattern(pattern, {44100, 0}).samples,
                  "song vs pattern floating beat/frame rounding differs");
        }
        auto phrase = baseline;
        phrase.bpm = 120; phrase.length_beats = 2; phrase.decay_seconds = 8;
        phrase.events = {{"held", "pluck", 0, 5, 0, 0.7f}};
        auto empty = phrase; empty.events.clear(); empty.name = "Retained ringing";
        song = one_pattern(phrase);
        song.patterns.push_back({"rest", empty, {}});
        song.order.push_back({"second", "rest", 1, 1, 0, 1, 0});
        auto flat = phrase; flat.length_beats = 4;
        const auto continuous = audio(gtracker::song_renderer(song, {48000, 0}));
        check(continuous == gtracker::render_pattern(flat, {48000, 0}).samples, "arrangement reset instrument state at a pattern boundary");
        double ringing = 0;
        for (std::size_t i = 48000; i < 96000; ++i) ringing += std::abs(continuous[i]);
        check(ringing > 0.1, "ringing did not cross into an empty pattern");
        auto room = gtracker::ProductionSettings{}; room.enabled = true;
        song.production = room; flat.schema_version = 2; flat.production = room;
        check(audio(gtracker::song_renderer(song, {48000, 0})) == gtracker::render_pattern(flat, {48000, 0}).samples,
              "room state/tails reset at pattern boundaries");
        auto slide = gtracker::load_pattern(root / "examples/slide-study.gtrk.json");
        song = one_pattern(slide);
        const auto original = gtracker::pattern_json(song.patterns[0].pattern);
        song.order[0].repeat = 3; song.order[0].time_scale = 1.125; song.order[0].gap_beats = 0.375;
        song.order[0].transpose = 2.25f; song.order[0].strength_scale = 0.5f;
        const auto compiled = gtracker::compile_song(song, 44100);
        check(compiled.placements.size() == 3 && compiled.length_beats == 28.125, "repeat/scale/gap duration wrong");
        check(compiled.performance.events.size() == slide.events.size() * 3 && compiled.performance.slides.size() == slide.gestures.size() * 3, "expanded schedule lost instances");
        check(compiled.performance.events[0].fret == 2.25f && compiled.performance.events[0].strength == slide.events[0].strength * 0.5f, "explicit variation not applied");
        const auto first_damp = std::find_if(compiled.performance.events.begin(), compiled.performance.events.end(), [](const gt_event& e) { return e.type == GT_EVENT_DAMP; });
        check(first_damp != compiled.performance.events.end() && first_damp->strength == 0.8f, "strength variation changed explicit damping");
        check(compiled.performance.slides[4].frame == static_cast<std::uint64_t>(std::llround((9.375 + 0.2 * 1.125) * (60.0 / song.bpm) * 44100)), "instance timing quantized at a pattern boundary");
        const auto varied = audio(gtracker::compiled_song_renderer(compiled, {44100, 0}));
        check(gtracker::pattern_json(song.patterns[0].pattern) == original, "placement variation mutated the source pattern");
        check(!varied.empty(), "varied song is empty");
        const auto snapshot = gtracker::song_snapshot_json(song);
        const auto identity = gtracker::song_render_identity(song);
        check(gtracker::song_render_identity(gtracker::parse_song_json(snapshot)) == identity, "portable snapshot changes identity");
        auto changed = song; changed.order[0].time_scale = 1;
        check(gtracker::song_render_identity(changed) != identity, "placement timing missing from song identity");
        changed = song; changed.seed += 1;
        check(gtracker::song_render_identity(changed) != identity, "song seed missing from identity");
        changed = song; changed.patterns[0].pattern.events[0].strength -= 0.1f;
        check(gtracker::song_render_identity(changed) != identity, "referenced pattern content missing from identity");
        changed = song; changed.patterns[0].pattern.seed += 1; changed.patterns[0].pattern.bpm = 200;
        check(audio(gtracker::song_renderer(changed, {44100, 0})) == varied, "song tempo/seed did not override per-template values explicitly");
        changed = song; changed.order[0].transpose = -1;
        rejects([&] { gtracker::validate_song(changed); }, "negative transposed frets accepted");
        changed = song; changed.order[0].repeat = 0;
        rejects([&] { gtracker::validate_song(changed); }, "zero repeat accepted");
        changed = song; changed.patterns[0].pattern.production = room;
        rejects([&] { gtracker::validate_song(changed); }, "per-pattern room silently replaced song state");
        changed = song; auto incompatible = slide; incompatible.brightness = 0.1f;
        changed.patterns.push_back({"other", incompatible, {}});
        rejects([&] { gtracker::validate_song(changed); }, "incompatible instrument silently switched mid-song");
        rejects([&] { gtracker::compiled_song_renderer(compiled, {48000, 0}); }, "compiled schedule silently reused at another rate");
        std::cout << "song: pattern identity, ringing/room continuity, variations, snapshots, source preservation and bounds passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
