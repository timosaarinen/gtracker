#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace gtracker {
// Incremental original SHA-256; digest() does not consume the running state.
class Sha256 {
public:
    Sha256();
    void update(std::string_view bytes);
    std::string digest() const;
private:
    std::array<std::uint32_t, 8> state_;
    std::array<unsigned char, 64> pending_{};
    std::size_t pending_count_ = 0;
    std::uint64_t byte_count_ = 0;
};
// SHA-256 over the supplied bytes, used for original-source identity.
std::string sha256(std::string_view bytes);
std::string sha256_file(const std::filesystem::path& path, const std::function<void()>& checkpoint = {},
                        const std::function<void(std::uint64_t)>& progress = {});
}
