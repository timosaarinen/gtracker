#pragma once
#include "gtracker/project.hpp"

namespace gtracker {
struct SongPattern {
    std::string id;
    Pattern pattern;
    std::string source_file; // Relative authoring reference; snapshots embed data instead.
};
struct PatternInstance {
    std::string id;
    std::string pattern_id;
    int repeat = 1;
    double time_scale = 1;
    float transpose = 0;
    float strength_scale = 1; // Excitation only; explicit damping is unchanged.
    double gap_beats = 0; // Global beats after each repetition, including the last.
};
struct Song {
    std::string name = "Untitled song";
    std::uint64_t revision = 1;
    double bpm = 90;
    std::uint32_t seed = 42;
    std::string instrument_pattern;
    std::vector<SongPattern> patterns;
    std::vector<PatternInstance> order;
    std::optional<ProductionSettings> production;
    std::vector<std::filesystem::path> source_files; // Read-only provenance, not serialized.
};
struct SongPlacement {
    std::string instance_id, pattern_id;
    int repetition = 0;
    double start_beat = 0, end_beat = 0;
};
struct CompiledSong {
    NativePerformance performance;
    std::vector<SongPlacement> placements;
    double length_beats = 0;
    double bpm = 90;
    std::optional<ProductionSettings> production;
    std::uint64_t musical_frames = 0;
};
void validate_song(const Song& song);
Song parse_song_json(std::string_view json, const std::filesystem::path& reference_directory = {});
Song load_song(const std::filesystem::path& path);
// Complete portable source snapshot: referenced patterns become embedded copies.
std::string song_snapshot_json(const Song& song);
CompiledSong compile_song(const Song& song, int sample_rate);
std::string song_render_identity(const Song& song, const RenderOptions& options = {});
ProductionSettings song_production(const Song& song, const RenderOptions& options);
NativeRenderer song_renderer(const Song& song, const RenderOptions& options = {});
NativeRenderer compiled_song_renderer(const CompiledSong& song, const RenderOptions& options = {});
StreamRenderResult render_song_wav(const Song& song, const std::filesystem::path& path,
                                  const RenderOptions& options = {}, const StreamOptions& stream = {});
} // namespace gtracker
