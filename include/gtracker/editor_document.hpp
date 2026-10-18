#pragma once

#include "gtracker/project.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gtracker {

// Authoring state shared by the native editor and headless checks. No UI or
// audio-device dependencies. Revisions advance on undo too; dirty state compares
// musical content with the last successful save, independently of revision.
class EditorDocument {
public:
    enum class Pending { none, load, close };
    enum class Decision { save, discard, cancel };

    explicit EditorDocument(Pattern initial, std::filesystem::path filename = {});

    const Pattern& pattern() const { return pattern_; }
    const std::filesystem::path& filename() const { return filename_; }
    std::uint64_t generation() const { return generation_; }
    bool dirty() const { return content_ != saved_content_; }
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // Nonempty keys coalesce consecutive updates from one activated control.
    // End the edit on deactivation, save, selection changes or another command.
    bool commit(Pattern next, const std::string& edit_key = {});
    void end_edit();
    bool undo(bool redo = false);
    void save(const std::filesystem::path& path);

    void request_load(const std::filesystem::path& path);
    void request_close();
    Pending pending() const { return pending_; }
    const std::filesystem::path& pending_path() const { return pending_path_; }
    bool close_requested() const { return close_requested_; }
    // A failed save/load leaves the current document and pending action intact.
    // Save receives a separate path so a pending Load cannot overwrite its target.
    void resolve(Decision decision, const std::filesystem::path& save_path = {});

private:
    void load(const std::filesystem::path& path);
    void complete_pending();
    Pattern pattern_;
    std::filesystem::path filename_;
    std::vector<Pattern> undo_;
    std::vector<Pattern> redo_;
    std::string content_;
    std::string saved_content_;
    std::string edit_key_;
    std::string edit_start_content_;
    std::uint64_t generation_ = 1;
    Pending pending_ = Pending::none;
    std::filesystem::path pending_path_;
    bool close_requested_ = false;
};

} // namespace gtracker
