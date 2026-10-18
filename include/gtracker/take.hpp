#pragma once

#include "gtracker/audio_source.hpp"
#include "gtracker/project.hpp"

namespace gtracker {
struct TakeAlignment {
    std::uint64_t pattern_zero_frame = 0;
    std::int64_t input_offset_frames = 0;
    std::string offset_status = "uncalibrated";
    double bpm = 90;
    double pattern_length_beats = 8;
    std::uint64_t selection_start_frame = 0;
    std::uint64_t selection_end_frame = 0;
};

struct Take {
    std::filesystem::path manifest_path;
    std::filesystem::path pattern_path;
    std::filesystem::path source_path;
    std::filesystem::path analysis_path;
    std::string id;
    std::string source_name;
    std::uint64_t source_pattern_revision = 0;
    TakeAlignment alignment;
    PcmAudio audio;
    Pattern pattern;

    double source_frame_to_beat(double frame) const;
};

// Read a v1 preserved import, verify its exact source bytes/PCM metadata and
// metadata-only analysis identity, then load its editable pattern. No Python,
// device access, quantization, source writes or note inference occurs here.
Take load_take(const std::filesystem::path& path);
} // namespace gtracker
