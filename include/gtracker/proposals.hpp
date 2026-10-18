#pragma once

#include "gtracker/take.hpp"

namespace gtracker {
struct FretAlternative { int string_index = 0; float fret = 0; };
struct NoteProposal {
    std::string id;
    std::uint64_t onset_frame = 0, end_frame = 0, onset_uncertainty_frames = 0;
    double pitch_midi = 0, periodicity = 0, voiced_fraction = 0, pitch_spread_cents = 0;
    std::vector<FretAlternative> alternatives;
};
struct NoteProposals {
    std::string job_id, take_id, source_sha256, status, engine, engine_version;
    std::uint64_t source_frames = 0;
    int sample_rate = 0, channels = 0, channel = 0; // Channel is zero-based here.
    Pattern source_pattern;
    TakeAlignment alignment;
    std::vector<NoteProposal> notes;
    std::vector<std::string> limitations;
};
struct ProposalChoice { std::size_t note = 0, alternative = 0; };

// Standalone optional research sidecar; the preserved take/metadata worker stay
// v1 and untouched. Unknown formats/engines fail. Source identity/timing is checked
// at load; score freshness is checked separately so the UI can show stale jobs.
NoteProposals parse_note_proposals(std::string_view json, const Take& take);
NoteProposals load_note_proposals(const std::filesystem::path& path, const Take& take);
bool note_proposals_match(const NoteProposals& proposals, const Pattern& current, const Take& take);

// Add only explicitly chosen plucks to a validated copy. No quantization, source
// writes, implicit damping, clearing old notes or revision mutation. The editor
// commits the returned copy as one undoable operation and advances its revision.
Pattern accept_note_proposals(const Pattern& current, const Take& take, const NoteProposals& proposals,
                              const std::vector<ProposalChoice>& choices, float strength = 0.6f);
} // namespace gtracker
