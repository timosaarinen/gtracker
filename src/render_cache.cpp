#include "gtracker/render_cache.hpp"
#include "gtracker/hash.hpp"
#include "gtracker/io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gtracker {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double elapsed(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
bool digest_string(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void keys(const json& value, std::initializer_list<const char*> allowed) {
    require(value.is_object() && value.size() == allowed.size(), "Cache receipt fields are missing or unknown");
    for (const auto& item : value.items())
        require(std::any_of(allowed.begin(), allowed.end(), [&](const char* key) { return item.key() == key; }), "Unknown cache receipt field");
}
std::uint64_t integer(const json& value) {
    require(value.is_number_integer(), "Cache receipt integer has the wrong type");
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    const auto result = value.get<std::int64_t>();
    require(result >= 0, "Cache receipt integer is negative");
    return static_cast<std::uint64_t>(result);
}
double real(const json& value) {
    require(value.is_number(), "Cache receipt metric has the wrong type");
    const auto result = value.get<double>();
    require(std::isfinite(result) && result >= 0, "Cache receipt metric must be finite and nonnegative");
    return result;
}
struct OwnedFile {
    std::filesystem::path path;
    ~OwnedFile() { if (!path.empty()) { std::error_code ignored; std::filesystem::remove(path, ignored); } }
};
struct Entry {
    StreamRenderResult render;
    std::string wav_hash;
    std::uint64_t wav_bytes = 0;
    double source_render_ms = 0;
};
std::uint64_t wav_size(const StreamRenderResult& info) {
    const auto frame_bytes = static_cast<std::uint64_t>(info.channels * 2);
    require(info.frames <= (std::numeric_limits<std::uint32_t>::max() - 44ULL) / frame_bytes, "Cache output exceeds classic PCM RIFF size limit");
    return 44 + info.frames * frame_bytes;
}
void check_wav_header(const std::filesystem::path& path, const Entry& entry) {
    require(std::filesystem::file_size(path) == entry.wav_bytes, "Cached WAV size mismatch");
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 44> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    require(input.gcount() == 44, "Cached WAV header is incomplete");
    auto u16 = [&](std::size_t i) { return static_cast<std::uint32_t>(header[i]) | static_cast<std::uint32_t>(header[i + 1]) << 8; };
    auto u32 = [&](std::size_t i) { return u16(i) | u16(i + 2) << 16; };
    auto tag = [&](std::size_t i, const char* name) { return std::equal(name, name + 4, header.begin() + i); };
    const auto channels = static_cast<std::uint32_t>(entry.render.channels), rate = static_cast<std::uint32_t>(entry.render.sample_rate);
    require(tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data") &&
        u32(4) == entry.wav_bytes - 8 && u32(16) == 16 && u16(20) == 1 && u16(22) == channels &&
        u32(24) == rate && u32(28) == rate * channels * 2 && u16(32) == channels * 2 && u16(34) == 16 &&
        u32(40) == entry.wav_bytes - 44, "Cached WAV format/header mismatch");
}
Entry read_receipt(const std::filesystem::path& path, const StreamRenderResult& expected) {
    require(std::filesystem::file_size(path) <= 65536, "Cache receipt exceeds 64 KiB");
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot open cache receipt");
    std::string text(65537, '\0'); input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    require(text.size() <= 65536, "Cache receipt exceeds 64 KiB");
    const auto receipt = json::parse(text, [](int depth, json::parse_event_t, json&) {
        require(depth <= 16, "Cache receipt nesting exceeds 16"); return true;
    });
    keys(receipt, {"payload", "sha256"});
    const auto& p = receipt.at("payload");
    require(receipt.at("sha256").get<std::string>() == sha256(p.dump()), "Cache receipt checksum mismatch");
    keys(p, {"schema", "version", "identity", "renderer_version", "renderer_build_id", "sample_rate", "channels", "frames", "timeline_frames", "start_frame",
        "wav_sha256", "wav_bytes", "peak", "rms", "end_peak", "clipped_samples", "source_render_ms"});
    require(p.at("schema") == "gtracker.render-cache" && integer(p.at("version")) == 1 && p.at("identity") == expected.identity &&
        p.at("renderer_version") == GT_VERSION && p.at("renderer_build_id") == gt_renderer_build_id(), "Stale cache schema/build/identity");
    require(integer(p.at("sample_rate")) == static_cast<std::uint64_t>(expected.sample_rate) && integer(p.at("channels")) == static_cast<std::uint64_t>(expected.channels) &&
        integer(p.at("frames")) == expected.frames && integer(p.at("timeline_frames")) == expected.timeline_frames && integer(p.at("start_frame")) == expected.start_frame,
        "Cache receipt timing/format mismatch");
    Entry result; result.render = expected;
    result.wav_hash = p.at("wav_sha256").get<std::string>(); result.wav_bytes = integer(p.at("wav_bytes"));
    require(digest_string(result.wav_hash) && result.wav_bytes == wav_size(expected), "Cache WAV identity/size is invalid");
    const double peak = real(p.at("peak")), rms = real(p.at("rms")), end_peak = real(p.at("end_peak"));
    require(peak <= 64 && rms <= peak && end_peak <= peak, "Cache signal metrics are inconsistent");
    result.render.peak = static_cast<float>(peak); result.render.rms = static_cast<float>(rms); result.render.end_peak = static_cast<float>(end_peak);
    result.render.clipped_samples = integer(p.at("clipped_samples"));
    require(result.render.clipped_samples <= expected.frames * expected.channels, "Cache full-scale count is invalid");
    require((result.render.clipped_samples > 0) == (peak >= 1), "Cache peak and full-scale count disagree");
    result.source_render_ms = real(p.at("source_render_ms"));
    return result;
}
std::string receipt_text(const Entry& entry) {
    const auto& r = entry.render;
    const json payload{{"schema", "gtracker.render-cache"}, {"version", 1}, {"identity", r.identity}, {"renderer_version", GT_VERSION},
        {"renderer_build_id", gt_renderer_build_id()}, {"sample_rate", r.sample_rate}, {"channels", r.channels}, {"frames", r.frames},
        {"timeline_frames", r.timeline_frames}, {"start_frame", r.start_frame}, {"wav_sha256", entry.wav_hash}, {"wav_bytes", entry.wav_bytes},
        {"peak", r.peak}, {"rms", r.rms}, {"end_peak", r.end_peak}, {"clipped_samples", r.clipped_samples}, {"source_render_ms", r.render_ms}};
    return json{{"payload", payload}, {"sha256", sha256(payload.dump())}}.dump(2) + "\n";
}
void copy_checked(const std::filesystem::path& source, const std::filesystem::path& destination, const Entry& entry,
                  const std::function<void()>& checkpoint, const std::function<void(std::uint64_t)>& progress) {
    write_binary_atomic(destination, [&](std::ostream& output) {
        std::ifstream input(source, std::ios::binary); require(static_cast<bool>(input), "Cannot open cached PCM for copying");
        std::array<char, 65536> block{}; Sha256 hash; std::uint64_t copied = 0;
        if (progress) progress(0);
        for (;;) {
            checkpoint(); input.read(block.data(), static_cast<std::streamsize>(block.size()));
            const auto count = input.gcount();
            require(!input.bad() && !(input.fail() && !input.eof()), "Cannot read complete cached PCM");
            copied += static_cast<std::uint64_t>(count);
            require(copied <= entry.wav_bytes, "Cached PCM grew during copying");
            hash.update(std::string_view(block.data(), static_cast<std::size_t>(count))); output.write(block.data(), count);
            require(static_cast<bool>(output), "Cannot copy cached PCM output");
            if (progress) progress(copied);
            if (static_cast<std::size_t>(count) < block.size()) break;
        }
        checkpoint();
        require(copied == entry.wav_bytes && hash.digest() == entry.wav_hash, "Cached PCM changed while copying");
    });
}
}

CachedRenderResult cached_export_wav(NativeRenderer& renderer, const std::filesystem::path& output, const std::string& performance_identity,
                                     const StreamOptions& stream, const CacheOptions& cache) {
    const auto started = Clock::now();
    CachedRenderResult result;
    if (cache.directory.empty()) {
        require(!cache.refresh && !cache.read_only, "Cache modes require an explicit directory");
        result.render = export_wav(renderer, output, performance_identity, stream); result.key = result.render.identity;
        result.source_render_ms = result.render.render_ms; return result;
    }
    require(!(cache.refresh && cache.read_only), "Cannot refresh a read-only cache");
    require(renderer.position() == 0 && stream.block_frames > 0 && stream.block_frames <= 65536, "Cache export needs a fresh renderer and bounded blocks");
    const auto end = stream.end_frame.value_or(renderer.total_frames());
    require(stream.start_frame < end && end <= renderer.total_frames(), "Cache export crop is outside the timeline");
    bool callback_failed = false;
    auto checkpoint = [&] {
        bool cancelled = false;
        try { cancelled = stream.cancelled && stream.cancelled(); }
        catch (...) { callback_failed = true; throw; }
        if (cancelled) throw RenderCancelled();
    };
    checkpoint();
    StreamRenderResult expected;
    expected.sample_rate = renderer.sample_rate(); expected.channels = renderer.channels(); expected.frames = end - stream.start_frame;
    expected.timeline_frames = renderer.total_frames(); expected.start_frame = stream.start_frame;
    expected.identity = export_identity(performance_identity, stream.start_frame, end, renderer.total_frames()); result.key = expected.identity;
    require(digest_string(result.key), "Cache identity must be a lowercase SHA-256 digest");
    const auto bytes = wav_size(expected);
    const auto base = cache.directory / "gtracker-v1" / result.key.substr(0, 2) / result.key;
    auto wav = base; wav += ".wav"; auto receipt = base; receipt += ".json";
    require_distinct_output(output, {wav, receipt});
    auto progress = [&](const std::string& phase) -> std::function<void(std::uint64_t)> {
        return [&, phase](std::uint64_t count) {
            checkpoint(); require(count <= bytes, "Cache PCM grew during hashing");
            try { if (cache.progress) cache.progress({phase, count, bytes}); }
            catch (...) { callback_failed = true; throw; }
        };
    };
    bool present = false;
    const auto lookup_start = Clock::now();
    if (!cache.refresh) {
        try {
            present = std::filesystem::exists(wav) || std::filesystem::exists(receipt);
            if (present) {
                const auto entry = read_receipt(receipt, expected);
                check_wav_header(wav, entry);
                require(sha256_file(wav, checkpoint, progress("lookup_hash")) == entry.wav_hash, "Cached PCM checksum mismatch");
                result.cache_lookup_ms = elapsed(lookup_start);
                copy_checked(wav, output, entry, checkpoint, progress("copy_hit"));
                result.render = entry.render; result.render.render_ms = 0; result.render.warmup_frames = 0;
                result.render.audio_buffer_bytes = 0; result.render.elapsed_ms = elapsed(started);
                result.source_render_ms = entry.source_render_ms; result.status = "hit";
                return result;
            }
        } catch (const RenderCancelled&) { throw; }
        catch (const std::exception& error) { if (callback_failed) throw; result.detail = error.what(); present = true; }
    }
    result.cache_lookup_ms = elapsed(lookup_start);
    result.status = cache.read_only ? (present ? "read_only_invalid" : "read_only_miss") : cache.refresh ? "refreshed" : present ? "rebuilt" : "miss";
    OwnedFile stage{unique_sibling_path(output, "cache-stage")};
    result.render = export_wav(renderer, stage.path, performance_identity, stream);
    Entry entry; entry.render = result.render; entry.wav_bytes = bytes;
    require(std::filesystem::file_size(stage.path) == bytes, "Completed cache stage has an unexpected size");
    entry.wav_hash = sha256_file(stage.path, checkpoint, progress("hash_render"));
    result.source_render_ms = result.render.render_ms;
    if (!cache.read_only) {
        const auto store_start = Clock::now();
        try {
            copy_checked(stage.path, wav, entry, checkpoint, progress("store"));
            const auto text = receipt_text(entry);
            write_binary_atomic(receipt, [&](std::ostream& out) { checkpoint(); out.write(text.data(), static_cast<std::streamsize>(text.size())); checkpoint(); });
        } catch (const RenderCancelled&) { throw; }
        catch (const std::exception& error) { if (callback_failed) throw; result.status = "store_failed"; result.detail = error.what(); }
        result.cache_store_ms = elapsed(store_start);
    }
    checkpoint(); publish_binary_file(stage.path, output); stage.path.clear();
    result.render.elapsed_ms = elapsed(started);
    return result;
}
} // namespace gtracker
