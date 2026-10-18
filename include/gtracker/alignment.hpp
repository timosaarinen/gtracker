#pragma once

#include "gtracker/audio_source.hpp"

namespace gtracker {
struct AlignmentOptions {
    double max_offset_seconds = 0.5;
    double window_seconds = 5;
    int reference_channel = 0;
    int recorded_channel = 0;
};
struct DelayEstimate {
    std::string status = "no_match";
    std::int64_t candidate_offset_frames = 0; // Positive: recorded file is later.
    std::int64_t max_offset_frames = 0;
    std::uint64_t reference_window_frames = 0;
    std::uint64_t refinement_start_frame = 0;
    std::uint64_t refinement_window_frames = 0;
    int coarse_step_frames = 0;
    double coarse_score = 0;
    double alternative_score = 0;
    double fine_score = 0;
    int polarity = 0;
    double elapsed_ms = 0;
};

// Original, deterministic two-second low-level broadband reference. It is a
// calibration aid, not instrument audio; no device is opened or data written.
std::vector<std::int16_t> calibration_probe(int sample_rate, std::uint32_t seed = 42);
// Bounded relative file delay, not a physical input-latency/clock measurement.
// Equal source rates are required; neither source is modified or resampled.
DelayEstimate estimate_file_delay(const PcmAudio& reference, const PcmAudio& recorded,
                                 const AlignmentOptions& options = {});
} // namespace gtracker
