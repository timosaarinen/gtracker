#include "gtracker/editor_recovery.hpp"
#include "gtracker/io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace gtracker {
namespace {
bool copy_name(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    return name.rfind("recovery.",0) == 0 && name.size() > 10 && name.compare(name.size()-10,10,".gtrk.json") == 0;
}
std::filesystem::path unique_copy(const std::filesystem::path& directory, const char* prefix) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto path = unique_sibling_path(directory/prefix,"score-"+std::to_string(seconds));
    path += ".gtrk.json";
    if (std::filesystem::exists(path)) throw std::runtime_error("Recovery copy filename collision; retry the operation");
    return path;
}
}

RecoveryCopies::RecoveryCopies(const std::filesystem::path& data_directory)
    : data_directory_(std::filesystem::weakly_canonical(std::filesystem::absolute(data_directory))),
      directory_(data_directory_/"recovery") {}

bool RecoveryCopies::contains(const std::filesystem::path& path) const {
    return copy_name(path) && std::filesystem::weakly_canonical(path).parent_path() == directory_;
}
void RecoveryCopies::clear_owned(const std::filesystem::path& preserve) {
    if (!owned_.empty()) {
        if (!contains(owned_)) throw std::runtime_error("Owned recovery path changed; refusing cleanup");
        if (preserve.empty() || std::filesystem::weakly_canonical(preserve) != std::filesystem::weakly_canonical(owned_))
            std::filesystem::remove(owned_);
        owned_.clear();
    }
    saved_generation_ = 0;
    next_attempt_ = 0;
}
bool RecoveryCopies::update(const EditorDocument& document, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Recovery clock must be finite and nonnegative");
    if (!document.dirty()) {
        if (!owned_.empty() && seconds < next_attempt_) return false;
        next_attempt_ = seconds+5;
        clear_owned(document.filename());
        next_attempt_ = seconds+5;
        return false;
    }
    if (seconds < next_attempt_ || document.generation() == saved_generation_) return false;
    next_attempt_ = seconds+5; // Failed attempts retry later, not every UI frame.
    if (std::filesystem::weakly_canonical(directory_) != directory_) throw std::runtime_error("Recovery directory changed; choose a writable data directory");
    if (owned_.empty()) owned_ = unique_copy(directory_,"recovery");
    if (!contains(owned_)) throw std::runtime_error("Owned recovery copy escaped its data directory");
    save_pattern(document.pattern(),owned_);
    saved_generation_ = document.generation();
    return true;
}
RecoveryListing RecoveryCopies::list() const {
    std::vector<RecoveryCopy> candidates;
    RecoveryListing result;
    std::error_code error;
    if (!std::filesystem::exists(directory_,error)) {
        if (error) throw std::runtime_error("Cannot inspect recovery directory: "+error.message());
        return result;
    }
    if (std::filesystem::weakly_canonical(directory_) != directory_) throw std::runtime_error("Recovery directory changed");
    std::size_t scanned = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (++scanned > 512) { result.truncated = true; break; }
        if (entry.is_symlink() || !entry.is_regular_file() || !copy_name(entry.path()) || entry.path() == owned_) continue;
        candidates.push_back({entry.path(),{},0,entry.last_write_time()});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.modified != b.modified ? a.modified > b.modified : a.path.u8string() < b.path.u8string();
    });
    for (auto& candidate : candidates) {
        if (result.copies.size() == 20) { result.truncated = true; break; }
        try {
            const auto pattern = load_pattern(candidate.path);
            candidate.name = pattern.name; candidate.revision = pattern.revision;
            result.copies.push_back(std::move(candidate));
        } catch (const std::exception&) {
            // Malformed/future copies stay on disk for manual inspection.
            ++result.unreadable;
        }
    }
    return result;
}
std::filesystem::path RecoveryCopies::suggested_save_path() const { return unique_copy(data_directory_,"recovered"); }
} // namespace gtracker
