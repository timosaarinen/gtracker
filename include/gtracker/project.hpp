#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "gtracker/production.hpp"
#include "gtracker/renderer.hpp"

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

struct FretPoint {
    double beat = 0; // Offset from the gesture start, never quantized on load/save.
    float fret = 0;
};
struct Gesture {
    std::string id;
    std::string type = "slide";
    double beat = 0;
    int string_index = 0;
    std::vector<FretPoint> points;
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
    int schema_version = 1;
    std::string model = "baseline_0_1";
    std::vector<Gesture> gestures;
    std::optional<ProductionSettings> production;
};

struct RenderOptions {
    RenderOptions() = default;
    RenderOptions(int rate, double tail) : sample_rate(rate), tail_seconds(tail) {}
    int sample_rate = 48000;
    double tail_seconds = 2.0;
    bool bypass_production = false;
    std::optional<ProductionSettings> production_override;
};

struct RenderResult {
    std::vector<float> samples; // Interleaved when channels == 2.
    int sample_rate = 48000;
    int channels = 1;
    double elapsed_ms = 0.0;
    float peak = 0.0f;
    float rms = 0.0f;
    std::size_t clipped_samples = 0;
    double production_tail_seconds = 0;
    float end_peak = 0;
    std::string identity;
    std::size_t frames() const { return samples.size() / static_cast<std::size_t>(channels); }
};

void validate_pattern(const Pattern& pattern);
ProductionSettings parse_production_json(std::string_view source);
std::string production_json(const ProductionSettings& settings);
Pattern parse_pattern_json(std::string_view json);
std::string pattern_json(const Pattern& pattern);
Pattern load_pattern(const std::filesystem::path& path);
void save_pattern(const Pattern& pattern, const std::filesystem::path& path);
RenderResult render_pattern(const Pattern& pattern, const RenderOptions& options = {});
NativePerformance compile_pattern(const Pattern& pattern, int sample_rate);
std::string render_identity(const Pattern& pattern, const RenderOptions& options = {});
void write_wav(const std::filesystem::path& path, const std::vector<float>& samples, int sample_rate, int channels = 1);
// Store application-captured PCM samples exactly, retaining interleaved channels.
void write_pcm16_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& samples, int sample_rate, int channels);
void write_text_atomic(const std::filesystem::path& path, std::string_view text);

// A script is an undoable whole-document operation at the caller. The function
// validates a copy and commits it only if the script and validation succeed.
void apply_lua(Pattern& pattern, const std::filesystem::path& script_path);

} // namespace gtracker
