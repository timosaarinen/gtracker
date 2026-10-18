#include "gtracker/io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace gtracker {
void require_distinct_output(const std::filesystem::path& destination, const std::vector<std::filesystem::path>& sources) {
    for (const auto& source : sources) {
        if (source.empty()) continue;
        std::error_code error;
        const bool same = std::filesystem::equivalent(source, destination, error);
        if (same || std::filesystem::weakly_canonical(source) == std::filesystem::weakly_canonical(destination))
            throw std::runtime_error("Output must differ from the pattern input, song, referenced patterns, script and production settings");
    }
}
std::filesystem::path unique_sibling_path(const std::filesystem::path& destination, std::string_view tag) {
    static std::atomic<unsigned long long> counter{0};
    auto result = destination;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    result += "." + std::string(tag) + "-" + std::to_string(pid) + "-" + std::to_string(stamp) + "-" + std::to_string(counter++);
    return result;
}
void publish_binary_file(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace output file (Windows error " + std::to_string(GetLastError()) + ")");
#else
    std::filesystem::rename(temporary, destination);
#endif
}
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
void write_binary_atomic(const std::filesystem::path& path, const std::function<void(std::ostream&)>& writer) {
    require(!path.empty(), "Output path must not be empty");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = unique_sibling_path(path, "tmp");
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create output: " + path.u8string());
        writer(out);
        out.flush();
        if (!out) throw std::runtime_error("Cannot write output: " + path.u8string());
        out.close();
        if (!out) throw std::runtime_error("Cannot close output: " + path.u8string());
        publish_binary_file(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
void write_pcm16_header(std::ostream& out, std::uint64_t frames, int rate, int channels) {
    require(rate >= 8000 && rate <= 192000, "WAV rate must be in 8000..192000");
    require(channels == 1 || channels == 2, "WAV needs one or two channels");
    const auto frame_bytes = static_cast<std::uint16_t>(channels * 2);
    require(frames <= (std::numeric_limits<std::uint32_t>::max() - 44ULL) / frame_bytes, "WAV exceeds the PCM RIFF size limit; choose a shorter preview or lower rate");
    const auto bytes = static_cast<std::uint32_t>(frames * frame_bytes);
    const auto u16 = [&](std::uint16_t value) { out.put(static_cast<char>(value & 255)); out.put(static_cast<char>(value >> 8)); };
    const auto u32 = [&](std::uint32_t value) { u16(static_cast<std::uint16_t>(value)); u16(static_cast<std::uint16_t>(value >> 16)); };
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16);
    u16(1); u16(static_cast<std::uint16_t>(channels)); u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * frame_bytes); u16(frame_bytes); u16(16);
    out.write("data", 4); u32(bytes);
}
void write_float_pcm16(std::ostream& out, const float* samples, std::size_t count) {
    require(count == 0 || samples, "PCM encoder needs samples");
    std::array<char, 16384> encoded{};
    for (std::size_t offset = 0; offset < count;) {
        const auto block = std::min(count - offset, encoded.size() / 2);
        for (std::size_t i = 0; i < block; ++i) {
            const float value = samples[offset + i];
            require(std::isfinite(value), "Cannot write non-finite WAV samples");
            const auto sample = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f)));
            encoded[i * 2] = static_cast<char>(sample & 255);
            encoded[i * 2 + 1] = static_cast<char>(sample >> 8);
        }
        out.write(encoded.data(), static_cast<std::streamsize>(block * 2));
        offset += block;
    }
}
} // namespace gtracker
