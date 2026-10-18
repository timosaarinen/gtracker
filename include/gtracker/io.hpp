#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <vector>

namespace gtracker {
// Publish only a fully written, flushed and closed sibling temporary file.
// A thrown writer leaves the previous destination intact and removes its temp.
void write_binary_atomic(const std::filesystem::path& destination, const std::function<void(std::ostream&)>& writer);
void write_pcm16_header(std::ostream& out, std::uint64_t frames, int sample_rate, int channels);
void write_float_pcm16(std::ostream& out, const float* samples, std::size_t count);
void require_distinct_output(const std::filesystem::path& destination, const std::vector<std::filesystem::path>& sources);
} // namespace gtracker
