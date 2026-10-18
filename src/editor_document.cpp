#include "gtracker/editor_document.hpp"

#include <stdexcept>
#include <utility>

namespace gtracker {
namespace {
constexpr std::uint64_t max_revision = 9007199254740991ULL;

std::string content(const Pattern& pattern) {
    Pattern comparison = pattern;
    comparison.revision = 1;
    return pattern_json(comparison);
}

void next_revision(Pattern& next, const Pattern& current) {
    if (current.revision >= max_revision) {
        throw std::overflow_error("pattern revision exhausted its JSON-safe range");
    }
    next.revision = current.revision + 1;
}
}

EditorDocument::EditorDocument(Pattern initial, std::filesystem::path filename)
    : pattern_(std::move(initial)), filename_(std::move(filename)) {
    validate_pattern(pattern_);
    content_ = content(pattern_);
    saved_content_ = content_;
}

bool EditorDocument::commit(Pattern next, const std::string& edit_key) {
    next_revision(next, pattern_);
    validate_pattern(next);
    auto next_content = content(next);
    if (next_content == content_) {
        return false;
    }
    const bool continuing = !edit_key.empty() && edit_key == edit_key_;
    if (!continuing) {
        end_edit();
        undo_.push_back(pattern_);
        if (undo_.size() > 128) {
            undo_.erase(undo_.begin());
        }
        edit_start_content_ = content_;
    }
    redo_.clear();
    edit_key_ = edit_key;
    pattern_ = std::move(next);
    content_ = std::move(next_content);
    ++generation_;
    return true;
}

void EditorDocument::end_edit() {
    // Dragging back to the starting value should not leave a useless undo step.
    if (!edit_key_.empty() && content_ == edit_start_content_ && !undo_.empty()) {
        undo_.pop_back();
    }
    edit_key_.clear();
    edit_start_content_.clear();
}

bool EditorDocument::undo(bool redo) {
    end_edit();
    auto& source = redo ? redo_ : undo_;
    auto& destination = redo ? undo_ : redo_;
    if (source.empty()) {
        return false;
    }
    Pattern restored = source.back();
    next_revision(restored, pattern_);
    validate_pattern(restored);
    auto restored_content = content(restored);
    destination.push_back(pattern_);
    pattern_ = std::move(restored);
    content_ = std::move(restored_content);
    source.pop_back();
    ++generation_;
    return true;
}

void EditorDocument::save(const std::filesystem::path& path) {
    save_pattern(pattern_, path);
    // Only a successful write establishes a savepoint or changes its path.
    filename_ = path;
    saved_content_ = content_;
    end_edit();
}

void EditorDocument::load(const std::filesystem::path& path) {
    Pattern loaded = load_pattern(path);
    auto loaded_content = content(loaded);
    pattern_ = std::move(loaded);
    filename_ = path;
    content_ = std::move(loaded_content);
    saved_content_ = content_;
    undo_.clear();
    redo_.clear();
    edit_key_.clear();
    edit_start_content_.clear();
    ++generation_;
}

void EditorDocument::request_load(const std::filesystem::path& path) {
    end_edit();
    if (pending_ != Pending::none) {
        return;
    }
    if (dirty()) {
        pending_path_ = path;
        pending_ = Pending::load;
    } else {
        load(path);
    }
}

void EditorDocument::request_close() {
    end_edit();
    if (pending_ != Pending::none) {
        return;
    }
    if (dirty()) {
        pending_ = Pending::close;
    } else {
        close_requested_ = true;
    }
}

void EditorDocument::complete_pending() {
    if (pending_ == Pending::load) {
        load(pending_path_);
    } else if (pending_ == Pending::close) {
        close_requested_ = true;
    }
    pending_ = Pending::none;
    pending_path_.clear();
}

void EditorDocument::resolve(Decision decision, const std::filesystem::path& save_path) {
    if (pending_ == Pending::none) {
        return;
    }
    if (decision == Decision::cancel) {
        pending_ = Pending::none;
        pending_path_.clear();
        return;
    }
    if (decision == Decision::save) {
        save(save_path.empty() ? filename_ : save_path);
    }
    complete_pending();
}
} // namespace gtracker
