#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <vector>
#include <string_view>

namespace gtracker {
// Publish only a fully written, flushed and closed sibling temporary file.
// A thrown writer leaves the previous destination intact and removes its temp.
void write_binary_atomic(const std::filesystem::path& destination, const std::function<void(std::ostream&)>& writer);
void write_pcm16_header(std::ostream& out, std::uint64_t frames, int sample_rate, int channels);
void write_float_pcm16(std::ostream& out, const float* samples, std::size_t count);
void require_distinct_output(const std::filesystem::path& destination, const std::vector<std::filesystem::path>& sources);
std::filesystem::path unique_sibling_path(const std::filesystem::path& destination, std::string_view tag);
// The caller owns a complete, closed sibling file. Moves it into place atomically.
void publish_binary_file(const std::filesystem::path& temporary, const std::filesystem::path& destination);
} // namespace gtracker
