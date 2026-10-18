#include "gtracker/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
std::vector<float> read_all(gtracker::NativeRenderer& renderer, std::size_t step) {
    std::vector<float> result(static_cast<std::size_t>(renderer.total_frames()) * static_cast<std::size_t>(renderer.channels()));
    std::size_t offset = 0;
    while (const auto count = renderer.read(result.data() + offset * renderer.channels(), step)) offset += count;
    check(offset == renderer.total_frames(), "stream stopped early");
    check(renderer.read(nullptr, 0) == 0 && renderer.position() == renderer.total_frames(), "EOF moved renderer");
    return result;
}
}
int main() {
    try {
        gtracker::NativePerformance music;
        music.instrument.seed = 42;
        music.events = {{0, GT_EVENT_PLUCK, 5, 0, 0.7f}, {12345, GT_EVENT_DAMP, 5, 0, 0.1f}, {24000, GT_EVENT_PLUCK, 2, 2, 0.5f}};
        for (bool wet : {false, true}) {
            gtracker::ProductionSettings settings; settings.enabled = wet;
            gtracker::NativeRenderer renderer(music, 96000, settings);
            const auto audio = read_all(renderer, 65536);
            renderer.reset();
            check(read_all(renderer, 127) == audio, "state changed at arbitrary render/arrangement boundaries");
            check(std::any_of(audio.begin(), audio.end(), [](float value) { return value != 0; }), "runtime example is silent");
            if (!wet) {
                gt_synth* synth = nullptr;
                check(gt_create(&music.instrument, &synth) == GT_OK, "baseline create");
                check(gt_set_events(synth, music.events.data(), music.events.size()) == GT_OK, "baseline set");
                std::vector<float> direct(96000);
                check(gt_render(synth, direct.data(), direct.size()) == GT_OK, "baseline render");
                gt_destroy(synth);
                check(audio == direct, "shared native renderer changed direct core PCM");
            }
        }
        music.model = GT_MODEL_CONTACT_0_2;
        music.events = {{0, GT_EVENT_PLUCK, 5, 0, 0.7f}};
        music.slides = {{0, 5, {{0, 0}, {24000, 7}}}};
        gtracker::NativeRenderer copied(music, 96000);
        const auto audio = read_all(copied, 1000);
        music.slides[0].points[1].fret = 3;
        copied.reset();
        check(read_all(copied, 311) == audio, "renderer retained caller's curve/event pointers");
        rejects([&] { copied.read(nullptr, 65537); }, "unbounded block request accepted");
        music.events[0].frame = 96000;
        rejects([&] { gtracker::NativeRenderer bad(music, 96000); }, "out-of-timeline event accepted");
        music.events[0].frame = 0;
        rejects([&] { gtracker::NativeRenderer bad(music, 0); }, "zero duration accepted");
        music.slides[0].points[1].frame_offset = 96001;
        rejects([&] { gtracker::NativeRenderer bad(music, 96000); }, "out-of-timeline slide accepted");
        std::cout << "renderer: continuous blocks/reset, dry core identity, copied curves and bounds passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
