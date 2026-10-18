#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gtracker {
struct PcmAudio {
    int sample_rate = 0;
    int channels = 0;
    int sample_width_bits = 0;
    std::uint64_t frames = 0;
    std::uint64_t size_bytes = 0;
    std::string sha256;
    double peak = 0;
    double rms = 0;
    std::array<double, 2> channel_peak{};
    std::array<double, 2> channel_rms{};
    std::vector<float> interleaved;
};

// Playback/preview copy only. The original integer PCM file is never rewritten.
// Supports RIFF little-endian mono/stereo PCM (including extensible PCM).
// Preview allocation is bounded to max_samples and a 256 MiB input file.
PcmAudio load_pcm_wav(const std::filesystem::path& path, std::size_t max_samples = 32 * 1024 * 1024);
} // namespace gtracker
