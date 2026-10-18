#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gtracker {

// Prototype document. String indices are zero-based internally; string 0 is
// the highest-pitched lane. JSON uses one-based string numbers, as guitar tabs do.
struct Event {
    std::string id;
    std::string type = "pluck";
    double beat = 0.0;
    int string_index = 0;
    float fret = 0.0f;
    float strength = 0.7f;
};

struct Pattern {
    std::string name = "Untitled";
    std::uint64_t revision = 1;
    double bpm = 90.0;
    double length_beats = 8.0;
    std::uint32_t seed = 42;
    std::vector<double> tuning_midi = {64, 59, 55, 50, 45, 40};
    float decay_seconds = 2.5f;
    float brightness = 0.6f;
    float body_mix = 0.15f;
    std::vector<Event> events;
};

struct RenderOptions {
    int sample_rate = 48000;
    double tail_seconds = 2.0;
};

struct RenderResult {
    std::vector<float> mono;
    int sample_rate = 48000;
    double elapsed_ms = 0.0;
    float peak = 0.0f;
    float rms = 0.0f;
};

void validate_pattern(const Pattern& pattern);
Pattern parse_pattern_json(std::string_view json);
std::string pattern_json(const Pattern& pattern);
Pattern load_pattern(const std::filesystem::path& path);
void save_pattern(const Pattern& pattern, const std::filesystem::path& path);
RenderResult render_pattern(const Pattern& pattern, const RenderOptions& options = {});
void write_wav(const std::filesystem::path& path, const std::vector<float>& mono, int sample_rate);
// Store application-captured PCM samples exactly, retaining interleaved channels.
void write_pcm16_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& samples, int sample_rate, int channels);
void write_text_atomic(const std::filesystem::path& path, std::string_view text);

// A script is an undoable whole-document operation at the caller. The function
// validates a copy and commits it only if the script and validation succeed.
void apply_lua(Pattern& pattern, const std::filesystem::path& script_path);

} // namespace gtracker
