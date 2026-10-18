// Minimal game/load-time use: no editor, JSON, Lua, SDL or Python dependency.
// A game adapter compiles its declarative score into absolute sample frames,
// keeps the renderer across musical boundaries and consumes interleaved blocks.
#include "gtracker/renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

int main() {
    gtracker::NativePerformance music;
    music.instrument.sample_rate = 48000;
    music.instrument.seed = 42;
    music.events = {{0, GT_EVENT_PLUCK, 5, 0, 0.7f}, {48000, GT_EVENT_PLUCK, 2, 2, 0.5f}};
    gtracker::ProductionSettings room;
    room.enabled = true;
    gtracker::NativeRenderer renderer(music, 48000 * 4, room);
    std::array<float, 512 * 2> block{};
    float peak = 0;
    std::uint64_t frames = 0;
    while (const auto count = renderer.read(block.data(), 512)) {
        // Submit count * renderer.channels() samples to the game's PCM sink.
        for (std::size_t i = 0; i < count * static_cast<std::size_t>(renderer.channels()); ++i) {
            if (!std::isfinite(block[i])) return 1;
            peak = std::max(peak, std::abs(block[i]));
        }
        frames += count;
    }
    std::cout << "runtime renderer: " << frames << " frames, " << renderer.channels()
              << " channels at " << renderer.sample_rate() << " Hz, peak " << peak << '\n';
    return peak > 0 && frames == renderer.total_frames() ? 0 : 1;
}
