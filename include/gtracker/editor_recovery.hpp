#pragma once
#include "gtracker/editor_document.hpp"

namespace gtracker {
struct RecoveryCopy {
    std::filesystem::path path;
    std::string name;
    std::uint64_t revision = 0;
    std::filesystem::file_time_type modified;
};
struct RecoveryListing {
    std::vector<RecoveryCopy> copies;
    std::size_t unreadable = 0;
    bool truncated = false;
};

// Periodic normal-pattern copies in writable user data, independent of SDL and
// the instrument. No original document, source audio or another session's copy
// is overwritten/removed. Copies contain scores, not transport/undo/take state.
class RecoveryCopies {
public:
    explicit RecoveryCopies(const std::filesystem::path& data_directory);
    bool update(const EditorDocument& document, double monotonic_seconds);
    // Save/discard/load clears only this instance's owned copy. Loading that
    // same copy relinquishes ownership instead, preserving its only disk copy.
    void clear_owned(const std::filesystem::path& preserve = {});
    RecoveryListing list() const;
    std::filesystem::path suggested_save_path() const;
    bool contains(const std::filesystem::path& path) const;
    const std::filesystem::path& directory() const { return directory_; }
    const std::filesystem::path& owned_path() const { return owned_; }
private:
    std::filesystem::path data_directory_, directory_, owned_;
    double next_attempt_ = 5;
    std::uint64_t saved_generation_ = 0;
};
} // namespace gtracker
