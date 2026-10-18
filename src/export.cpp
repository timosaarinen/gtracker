#include "gtracker/export.hpp"
#include "gtracker/io.hpp"
#include "gtracker/hash.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ostream>
#include <vector>

namespace gtracker {
std::string export_identity(const std::string& identity, std::uint64_t start, std::uint64_t end, std::uint64_t total) {
    if (start >= end || end > total) throw std::invalid_argument("Export identity needs a nonempty valid crop");
    if (start == 0 && end == total) return identity;
    return sha256(nlohmann::json{{"schema", "gtracker.export-identity"}, {"version", 1},
        {"performance", identity}, {"start_frame", start}, {"end_frame", end}, {"encoding", "PCM_S16LE"}}.dump());
}
StreamRenderResult export_wav(NativeRenderer& renderer, const std::filesystem::path& path,
                              const std::string& identity, const StreamOptions& options) {
    const auto start = std::chrono::steady_clock::now();
    if (renderer.position() != 0) throw std::invalid_argument("Export requires a renderer at frame zero; reset explicitly");
    if (!options.block_frames || options.block_frames > 65536) throw std::invalid_argument("Export block_frames must be in 1..65536");
    const auto end = options.end_frame.value_or(renderer.total_frames());
    if (options.start_frame >= end || end > renderer.total_frames()) throw std::invalid_argument("Export crop must be nonempty and inside the render timeline");
    auto checkpoint = [&] { if (options.cancelled && options.cancelled()) throw RenderCancelled(); };
    checkpoint();
    StreamRenderResult result;
    result.sample_rate = renderer.sample_rate(); result.channels = renderer.channels();
    result.frames = end - options.start_frame; result.warmup_frames = options.start_frame;
    result.timeline_frames = renderer.total_frames();
    result.identity = export_identity(identity, options.start_frame, end, renderer.total_frames());
    const auto channels = static_cast<std::size_t>(result.channels);
    std::vector<float> block(options.block_frames * channels);
    result.audio_buffer_bytes = block.size() * sizeof(float);
    const auto end_begin = end - std::min<std::uint64_t>(end - options.start_frame, result.sample_rate / 20);
    std::uint64_t written = 0;
    double squares = 0;
    auto progress = [&] {
        if (options.progress) options.progress({renderer.position(), end, written, result.frames});
    };
    write_binary_atomic(path, [&](std::ostream& out) {
        write_pcm16_header(out, result.frames, result.sample_rate, result.channels);
        progress();
        while (renderer.position() < end) {
            checkpoint();
            const auto first = renderer.position();
            const auto capacity = static_cast<std::size_t>(std::min<std::uint64_t>(options.block_frames, end - first));
            const auto audio_start = std::chrono::steady_clock::now();
            const auto count = renderer.read(block.data(), capacity);
            result.render_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - audio_start).count();
            if (count != capacity) throw std::runtime_error("Native render ended before the export endpoint");
            const auto skip = static_cast<std::size_t>(std::min<std::uint64_t>(count, first < options.start_frame ? options.start_frame - first : 0));
            for (std::size_t frame = skip; frame < count; ++frame) {
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    const float value = block[frame * channels + channel];
                    if (!std::isfinite(value)) throw std::runtime_error("Renderer produced nonfinite audio");
                    result.peak = std::max(result.peak, std::abs(value));
                    squares += static_cast<double>(value) * value;
                    if (std::abs(value) >= 1) ++result.clipped_samples;
                    if (first + frame >= end_begin) result.end_peak = std::max(result.end_peak, std::abs(value));
                }
            }
            write_float_pcm16(out, block.data() + skip * channels, (count - skip) * channels);
            if (!out) throw std::runtime_error("Cannot write streamed PCM output");
            written += count - skip;
            progress();
        }
        checkpoint();
    });
    result.rms = static_cast<float>(std::sqrt(squares / (static_cast<double>(result.frames) * result.channels)));
    result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace gtracker
