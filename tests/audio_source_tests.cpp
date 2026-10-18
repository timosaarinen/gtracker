#include "gtracker/audio_source.hpp"
#include "gtracker/hash.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

void u16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value)); bytes.push_back(static_cast<char>(value >> 8));
}
void u32(std::string& bytes, std::uint32_t value) {
    u16(bytes, static_cast<std::uint16_t>(value)); u16(bytes, static_cast<std::uint16_t>(value >> 16));
}
void patch32(std::string& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<char>(value >> (i * 8));
}

std::string wav(int rate, int channels, int width, bool extensible = false) {
    std::string bytes = "RIFF";
    u32(bytes, 0); bytes += "WAVEJUNK"; u32(bytes, 3); bytes += "xyz"; bytes.push_back('\0');
    bytes += "fmt "; u32(bytes, extensible ? 40 : 16);
    u16(bytes, extensible ? 0xfffe : 1); u16(bytes, static_cast<std::uint16_t>(channels));
    u32(bytes, static_cast<std::uint32_t>(rate)); u32(bytes, static_cast<std::uint32_t>(rate * channels * width));
    u16(bytes, static_cast<std::uint16_t>(channels * width)); u16(bytes, static_cast<std::uint16_t>(width * 8));
    if (extensible) {
        u16(bytes, 22); u16(bytes, static_cast<std::uint16_t>(width * 8)); u32(bytes, 0);
        const unsigned char guid[] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
        bytes.append(reinterpret_cast<const char*>(guid), sizeof(guid));
    }
    bytes += "data"; u32(bytes, static_cast<std::uint32_t>(3 * channels * width));
    for (int frame = 0; frame < 3; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const std::int64_t scale = std::int64_t{1} << (width * 8 - 1);
            const std::int64_t value = frame == 0 ? 0 : (frame == 1 ? scale / 2 : -scale);
            const auto stored = static_cast<std::uint32_t>(width == 1 ? value + 128 : value);
            for (int byte = 0; byte < width; ++byte) bytes.push_back(static_cast<char>(stored >> (byte * 8)));
        }
    }
    patch32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
    return bytes;
}

void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output), "could not write generated fixture");
}

void hashes() {
    check(gtracker::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty hash");
    check(gtracker::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc hash");
    check(gtracker::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "two-block padding hash");
    check(gtracker::sha256(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "million-byte hash");
}

void formats(const std::filesystem::path& path) {
    for (int rate : {44100, 48000}) for (int channels : {1, 2}) for (int width : {1, 2, 3, 4}) {
        for (bool extensible : {false, true}) {
            auto bytes = wav(rate, channels, width, extensible);
            // Bytes after RIFF are preserved in the file identity too.
            bytes += "original extra context";
            write(path, bytes);
            const auto audio = gtracker::load_pcm_wav(path);
            check(audio.sample_rate == rate && audio.channels == channels && audio.sample_width_bits == width * 8, "source format changed");
            check(audio.frames == 3 && audio.interleaved.size() == static_cast<std::size_t>(3 * channels), "source frame count changed");
            check(audio.sha256 == gtracker::sha256(bytes) && audio.size_bytes == bytes.size(), "source file identity lost extra chunks/context");
            check(audio.peak == 1 && std::abs(audio.rms - std::sqrt(1.25 / 3)) < 1e-12, "source signal metrics");
            for (int channel = 0; channel < channels; ++channel) {
                check(audio.interleaved[static_cast<std::size_t>(channel)] == 0, "zero decoding");
                check(audio.interleaved[static_cast<std::size_t>(channels + channel)] == 0.5f, "positive decoding");
                check(audio.interleaved[static_cast<std::size_t>(2 * channels + channel)] == -1, "negative sign decoding");
            }
            rejects([&] { gtracker::load_pcm_wav(path, 2); }, "preview allocation bound ignored");
        }
    }
}

void malformed(const std::filesystem::path& path) {
    const auto good = wav(48000, 2, 2);
    for (std::size_t length = 0; length < good.size(); ++length) {
        write(path, good.substr(0, length));
        rejects([&] { gtracker::load_pcm_wav(path); }, "truncated WAV accepted");
    }
    for (int variant = 0; variant < 6; ++variant) {
        auto bytes = good;
        if (variant == 0) bytes[0] = 'X';
        if (variant == 1) bytes[32] = 3; // format code, after the padded JUNK chunk
        if (variant == 2) bytes[34] = 3; // channels
        if (variant == 3) bytes[46] = 7; // sample bits
        if (variant == 4) patch32(bytes, 40, 1); // byte rate
        if (variant == 5) patch32(bytes, 52, 0xffffffffU); // data length
        write(path, bytes);
        rejects([&] { gtracker::load_pcm_wav(path); }, "malformed WAV accepted");
    }
}
}

int main() {
    try {
        hashes();
        const auto path = std::filesystem::current_path() / "generated-source-test.wav";
        formats(path);
        malformed(path);
        std::filesystem::remove(path);
        std::cout << "source audio: SHA-256, PCM widths/rates/channels, chunks and bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "source audio: " << error.what() << '\n';
        return 1;
    }
}
