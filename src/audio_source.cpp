#include "gtracker/audio_source.hpp"
#include "gtracker/hash.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace gtracker {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t little(const std::vector<unsigned char>& bytes, std::size_t at, unsigned count) {
    require(at <= bytes.size() && count <= bytes.size() - at, "Truncated WAV field");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= std::uint32_t{bytes[at + i]} << (i * 8);
    return value;
}

bool tag(const std::vector<unsigned char>& bytes, std::size_t at, const char* text) {
    return at <= bytes.size() && bytes.size() - at >= 4 && std::memcmp(bytes.data() + at, text, 4) == 0;
}
}

PcmAudio load_pcm_wav(const std::filesystem::path& path, std::size_t max_samples) {
    const auto size = std::filesystem::file_size(path);
    require(size >= 12 && size <= 256ULL * 1024 * 1024, "Native WAV preview requires a 12-byte..256-MiB file");
    const auto modified = std::filesystem::last_write_time(path);
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot open source WAV: " + path.u8string());
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(input.gcount() == static_cast<std::streamsize>(bytes.size()), "Source WAV was truncated while reading");
    require(std::filesystem::file_size(path) == size && std::filesystem::last_write_time(path) == modified,
            "Source WAV changed while reading; reopen a stable take");
    require(tag(bytes, 0, "RIFF") && tag(bytes, 8, "WAVE"), "Only little-endian RIFF/WAVE PCM is supported");
    const std::uint64_t riff_end = std::uint64_t{little(bytes, 4, 4)} + 8;
    require(riff_end >= 12 && riff_end <= bytes.size(), "Truncated/invalid RIFF size");
    PcmAudio result;
    bool format_seen = false;
    bool data_seen = false;
    std::size_t data_at = 0, data_bytes = 0;
    for (std::size_t offset = 12; offset < riff_end;) {
        require(riff_end - offset >= 8, "Truncated WAV chunk header");
        const auto length = little(bytes, offset + 4, 4);
        const auto payload = offset + 8;
        require(length <= riff_end - payload, "Truncated WAV chunk payload");
        if (tag(bytes, offset, "fmt ")) {
            require(!format_seen && !data_seen && length >= 16, "Duplicate, misplaced or truncated WAV format");
            const auto format = little(bytes, payload, 2);
            result.channels = static_cast<int>(little(bytes, payload + 2, 2));
            result.sample_rate = static_cast<int>(little(bytes, payload + 4, 4));
            result.sample_width_bits = static_cast<int>(little(bytes, payload + 14, 2));
            require(result.channels == 1 || result.channels == 2, "Source WAV requires one or two channels");
            require(result.sample_rate >= 8000 && result.sample_rate <= 192000, "Source WAV rate must be in 8000..192000 Hz");
            require(result.sample_width_bits == 8 || result.sample_width_bits == 16 || result.sample_width_bits == 24 ||
                    result.sample_width_bits == 32, "Source WAV requires 8/16/24/32-bit integer PCM");
            const auto frame_bytes = static_cast<std::uint32_t>(result.channels * (result.sample_width_bits / 8));
            require(little(bytes, payload + 12, 2) == frame_bytes &&
                    little(bytes, payload + 8, 4) == static_cast<std::uint32_t>(result.sample_rate) * frame_bytes,
                    "Source WAV block alignment/byte rate is inconsistent");
            if (format == 0xfffe) {
                constexpr std::array<unsigned char, 16> pcm_guid = {
                    1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
                };
                require(length >= 40 && little(bytes, payload + 16, 2) >= 22 &&
                        little(bytes, payload + 16, 2) <= length - 18,
                        "Truncated extensible WAV format");
                require(std::memcmp(bytes.data() + payload + 24, pcm_guid.data(), pcm_guid.size()) == 0,
                        "Extensible WAV must contain integer PCM");
                const auto valid_bits = little(bytes, payload + 18, 2);
                require(valid_bits > 0 && valid_bits <= static_cast<unsigned>(result.sample_width_bits),
                        "Invalid extensible WAV valid-bit count");
            } else {
                require(format == 1, "Compressed/floating-point source WAV is not implemented");
            }
            format_seen = true;
        } else if (tag(bytes, offset, "data")) {
            require(format_seen && !data_seen, "WAV needs one data chunk after its format");
            data_seen = true;
            data_at = payload;
            data_bytes = length;
        }
        offset = payload + length;
        // Common PCM writers omit a final odd-size pad; interior chunks need it.
        if ((length & 1U) && offset < riff_end) ++offset;
    }
    require(format_seen && data_seen && data_bytes > 0, "Source WAV has no PCM audio");
    const auto width = static_cast<unsigned>(result.sample_width_bits / 8);
    const auto frame_bytes = width * static_cast<unsigned>(result.channels);
    require(data_bytes % frame_bytes == 0, "Source WAV ends in a partial PCM frame");
    const auto samples = data_bytes / width;
    require(samples <= max_samples, "Native source preview exceeds its sample buffer limit; keep the original and use a shorter preview take");
    result.frames = data_bytes / frame_bytes;
    result.size_bytes = size;
    result.sha256 = gtracker::sha256(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    result.interleaved.resize(samples);
    const auto bits = static_cast<unsigned>(result.sample_width_bits);
    const double scale = static_cast<double>(std::uint64_t{1} << (bits - 1));
    std::array<double, 2> squares{};
    for (std::size_t i = 0; i < samples; ++i) {
        const auto raw = little(bytes, data_at + i * width, width);
        std::int64_t value = raw;
        if (width == 1) value -= 128;
        else if (raw & (std::uint32_t{1} << (bits - 1))) value -= std::int64_t{1} << bits;
        const double sample = value / scale;
        result.interleaved[i] = static_cast<float>(sample);
        const auto channel = i % static_cast<std::size_t>(result.channels);
        result.channel_peak[channel] = std::max(result.channel_peak[channel], std::abs(sample));
        squares[channel] += sample * sample;
    }
    result.peak = std::max(result.channel_peak[0], result.channel_peak[1]);
    result.rms = std::sqrt((squares[0] + squares[1]) / static_cast<double>(samples));
    for (int channel = 0; channel < result.channels; ++channel)
        result.channel_rms[static_cast<std::size_t>(channel)] = std::sqrt(squares[static_cast<std::size_t>(channel)] / result.frames);
    return result;
}
} // namespace gtracker
