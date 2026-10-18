#pragma once
#include "gtracker/renderer.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace gtracker {
class RenderCancelled : public std::runtime_error {
public:
    RenderCancelled() : std::runtime_error("Render cancelled; previous output retained") {}
};
struct RenderProgress {
    std::uint64_t processed_frames = 0; // Includes warm-up from the original seed.
    std::uint64_t processing_frames = 0;
    std::uint64_t written_frames = 0;
    std::uint64_t output_frames = 0;
};
struct StreamOptions {
    std::size_t block_frames = 4096;
    std::uint64_t start_frame = 0;
    std::optional<std::uint64_t> end_frame; // Half-open crop, including any desired tail.
    std::function<bool()> cancelled;
    std::function<void(const RenderProgress&)> progress;
};
struct StreamRenderResult {
    int sample_rate = 48000, channels = 1;
    std::uint64_t frames = 0, warmup_frames = 0, timeline_frames = 0;
    std::size_t audio_buffer_bytes = 0;
    double render_ms = 0, elapsed_ms = 0;
    float peak = 0, rms = 0, end_peak = 0;
    std::uint64_t clipped_samples = 0;
    std::string identity;
};
std::string export_identity(const std::string& performance_identity, std::uint64_t start_frame,
                            std::uint64_t end_frame, std::uint64_t total_frames);
// Renderer must be at frame zero. A preview warms up the full native state and
// exports an exact crop, with no artificial reset or automatic extra tail.
StreamRenderResult export_wav(NativeRenderer& renderer, const std::filesystem::path& path,
                              const std::string& identity, const StreamOptions& options = {});
} // namespace gtracker
