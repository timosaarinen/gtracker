#include "gtracker/proposals.hpp"
#include "gtracker/hash.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace gtracker {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_json = 2 * 1024 * 1024;
constexpr std::uint64_t max_integer = 9007199254740991ULL;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void keys(const json& value, std::initializer_list<const char*> names, const char* where) {
    require(value.is_object() && value.size() == names.size(), std::string(where) + " has missing or unknown fields");
    for (const char* name : names) require(value.contains(name), std::string(where) + ": missing " + name);
}
std::string text(const json& value, std::size_t maximum, const char* where) {
    require(value.is_string(), std::string(where) + " must be a string");
    auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= maximum, std::string(where) + " exceeds its UTF-8 bounds");
    return result;
}
std::uint64_t integer(const json& value, std::uint64_t low, std::uint64_t high, const char* where) {
    require(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0), std::string(where) + " must be a nonnegative integer");
    auto result = value.get<std::uint64_t>();
    require(result >= low && result <= high, std::string(where) + " is out of range");
    return result;
}
double real(const json& value, double low, double high, const char* where) {
    require(value.is_number(), std::string(where) + " must be a number");
    const double result = value.get<double>();
    require(std::isfinite(result) && result >= low && result <= high, std::string(where) + " is out of range");
    return result;
}
bool digest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool same_alignment(const TakeAlignment& a, const TakeAlignment& b) {
    return a.pattern_zero_frame == b.pattern_zero_frame && a.input_offset_frames == b.input_offset_frames &&
        a.offset_status == b.offset_status && a.bpm == b.bpm && a.pattern_length_beats == b.pattern_length_beats &&
        a.selection_start_frame == b.selection_start_frame && a.selection_end_frame == b.selection_end_frame;
}
void validate(const NoteProposals& p, const Take& take) {
    require(!p.job_id.empty() && p.job_id.size() <= 128 && p.take_id == take.id, "Proposal job/take identity does not match");
    require(digest(p.source_sha256) && p.source_sha256 == take.audio.sha256 && p.source_frames == take.audio.frames &&
        p.sample_rate == take.audio.sample_rate && p.channels == take.audio.channels && p.channel >= 0 && p.channel < p.channels,
        "Proposal source/format does not match the preserved take");
    require(same_alignment(p.alignment, take.alignment), "Proposal source alignment changed; repeat the job");
    require(p.engine == "gtracker.monophonic-yin" && p.engine_version == "0.1.0", "Unsupported proposal engine/version");
    validate_pattern(p.source_pattern);
    require(p.notes.size() <= 128 && p.status == (p.notes.empty() ? "no_confident_candidates" : "proposals"), "Proposal status/count is inconsistent");
    std::set<std::string> ids;
    std::uint64_t previous = 0;
    for (std::size_t index = 0; index < p.notes.size(); ++index) {
        const auto& note = p.notes[index];
        require(!note.id.empty() && note.id.size() <= 64 && ids.insert(note.id).second, "Proposal note IDs must be unique, 1..64 bytes");
        require(note.onset_frame >= p.alignment.selection_start_frame && note.onset_frame < note.end_frame &&
            note.end_frame <= p.alignment.selection_end_frame && (index == 0 || note.onset_frame > previous), "Proposal note timing is out of selection/order");
        previous = note.onset_frame;
        require(note.onset_uncertainty_frames > 0 && note.onset_uncertainty_frames <= static_cast<std::uint64_t>(p.sample_rate), "Proposal onset uncertainty is out of range");
        require(std::isfinite(note.pitch_midi) && note.pitch_midi >= 0 && note.pitch_midi <= 127 &&
            std::isfinite(note.periodicity) && note.periodicity >= 0 && note.periodicity <= 1 &&
            std::isfinite(note.voiced_fraction) && note.voiced_fraction > 0 && note.voiced_fraction <= 1 &&
            std::isfinite(note.pitch_spread_cents) && note.pitch_spread_cents >= 0 && note.pitch_spread_cents <= 100,
            "Proposal pitch/diagnostics are out of range");
        require(note.alternatives.size() <= p.source_pattern.tuning_midi.size(), "Too many fingering alternatives");
        std::set<int> strings;
        for (const auto& alternative : note.alternatives) {
            require(alternative.string_index >= 0 && static_cast<std::size_t>(alternative.string_index) < p.source_pattern.tuning_midi.size() &&
                strings.insert(alternative.string_index).second && std::isfinite(alternative.fret) && alternative.fret >= 0 && alternative.fret <= 36,
                "Invalid/duplicate fingering alternative");
            require(std::abs(p.source_pattern.tuning_midi[alternative.string_index] + alternative.fret - note.pitch_midi) <= 0.25001,
                "Fingering alternative does not represent the proposed pitch");
        }
    }
}
}

NoteProposals parse_note_proposals(std::string_view source, const Take& take) {
    require(source.size() <= max_json, "Proposal JSON exceeds 2 MiB");
    const auto doc = json::parse(source, [](int depth, json::parse_event_t, json&) {
        if (depth > 64) throw std::runtime_error("Proposal JSON exceeds its nesting bound");
        return true;
    });
    keys(doc, {"schema", "version", "job_id", "take_id", "source_pattern_revision", "source_pattern", "source", "alignment", "status", "parameters", "notes", "limitations", "provenance"}, "proposal result");
    require(doc.at("schema") == "gtracker.note-proposals" && integer(doc.at("version"), 1, 1, "proposal version") == 1, "Unsupported proposal schema/version");
    NoteProposals p;
    p.job_id = text(doc.at("job_id"), 128, "job_id"); p.take_id = text(doc.at("take_id"), 128, "take_id");
    p.source_pattern = parse_pattern_json(doc.at("source_pattern").dump());
    require(integer(doc.at("source_pattern_revision"), 1, max_integer, "source_pattern_revision") == p.source_pattern.revision, "Proposal revision/snapshot mismatch");
    const auto& s = doc.at("source");
    keys(s, {"sha256", "frames", "sample_rate_hz", "channels", "channel"}, "proposal source");
    p.source_sha256 = text(s.at("sha256"), 64, "source sha256");
    p.source_frames = integer(s.at("frames"), 1, max_integer, "source frames");
    p.sample_rate = static_cast<int>(integer(s.at("sample_rate_hz"), 8000, 192000, "source rate"));
    p.channels = static_cast<int>(integer(s.at("channels"), 1, 2, "source channels"));
    p.channel = static_cast<int>(integer(s.at("channel"), 1, static_cast<std::uint64_t>(p.channels), "selected channel"))-1;
    const auto& a = doc.at("alignment");
    keys(a, {"coordinate_system", "pattern_zero_frame", "input_offset_frames", "offset_status", "tempo_bpm", "pattern_length_beats", "selection_start_frame", "selection_end_frame"}, "proposal alignment");
    require(a.at("coordinate_system") == "source_samples", "Proposals must retain original source-sample coordinates");
    p.alignment.pattern_zero_frame = integer(a.at("pattern_zero_frame"), 0, p.source_frames, "pattern_zero_frame");
    const auto& offset = a.at("input_offset_frames");
    require(offset.is_number_integer() && std::abs(real(offset, -static_cast<double>(p.source_frames), static_cast<double>(p.source_frames), "input_offset_frames")) <= static_cast<double>(p.source_frames), "Invalid proposal input offset");
    p.alignment.input_offset_frames = offset.get<std::int64_t>();
    p.alignment.offset_status = text(a.at("offset_status"), 32, "offset_status");
    p.alignment.bpm = real(a.at("tempo_bpm"), 20, 400, "tempo_bpm");
    p.alignment.pattern_length_beats = real(a.at("pattern_length_beats"), 0.000001, 256, "pattern_length_beats");
    p.alignment.selection_start_frame = integer(a.at("selection_start_frame"), 0, p.source_frames, "selection_start_frame");
    p.alignment.selection_end_frame = integer(a.at("selection_end_frame"), p.alignment.selection_start_frame+1, p.source_frames, "selection_end_frame");
    p.status = text(doc.at("status"), 32, "proposal status");
    const auto& parameters = doc.at("parameters");
    keys(parameters, {"min_hz", "max_hz", "analysis_rate_hz", "box_decimation_factor", "difference_window_samples", "hop_samples", "rms_gate", "minimum_voiced_frames", "maximum_gap_frames", "default_render_strength", "assumed_monophonic", "technique_recognition", "timing_quantized"}, "proposal parameters");
    require(parameters.at("min_hz") == 65 && parameters.at("max_hz") == 1000 && parameters.at("minimum_voiced_frames") == 8 && parameters.at("maximum_gap_frames") == 2 &&
        parameters.at("assumed_monophonic") == true && parameters.at("technique_recognition") == false && parameters.at("timing_quantized") == false,
        "Unsupported monophonic baseline parameters/capabilities");
    real(parameters.at("analysis_rate_hz"), 8000, 16000, "analysis rate"); integer(parameters.at("box_decimation_factor"), 1, 24, "decimation");
    integer(parameters.at("difference_window_samples"), 256, 640, "difference window"); integer(parameters.at("hop_samples"), 80, 160, "analysis hop");
    real(parameters.at("rms_gate"), 0.001, 1, "RMS gate"); real(parameters.at("default_render_strength"), 0, 1, "default strength");
    require(doc.at("notes").is_array() && doc.at("notes").size() <= 128, "Too many proposal notes");
    for (const auto& item : doc.at("notes")) {
        keys(item, {"id", "onset_frame", "end_frame", "pitch_midi", "periodicity", "voiced_fraction", "pitch_spread_cents", "onset_uncertainty_frames", "alternatives"}, "proposal note");
        NoteProposal n;
        n.id = text(item.at("id"), 64, "note id"); n.onset_frame = integer(item.at("onset_frame"), 0, p.source_frames, "note onset");
        n.end_frame = integer(item.at("end_frame"), 1, p.source_frames, "note end");
        n.pitch_midi = real(item.at("pitch_midi"), 0, 127, "pitch_midi"); n.periodicity = real(item.at("periodicity"), 0, 1, "periodicity");
        n.voiced_fraction = real(item.at("voiced_fraction"), 0, 1, "voiced_fraction"); n.pitch_spread_cents = real(item.at("pitch_spread_cents"), 0, 100, "pitch spread");
        n.onset_uncertainty_frames = integer(item.at("onset_uncertainty_frames"), 1, static_cast<std::uint64_t>(p.sample_rate), "onset uncertainty");
        require(item.at("alternatives").is_array() && item.at("alternatives").size() <= GT_MAX_STRINGS, "Too many note alternatives");
        for (const auto& alternative : item.at("alternatives")) {
            keys(alternative, {"string", "fret"}, "fingering alternative");
            n.alternatives.push_back({static_cast<int>(integer(alternative.at("string"), 1, GT_MAX_STRINGS, "alternative string"))-1,
                static_cast<float>(real(alternative.at("fret"), 0, 36, "alternative fret"))});
        }
        p.notes.push_back(std::move(n));
    }
    require(doc.at("limitations").is_array() && doc.at("limitations").size() <= 32, "Invalid proposal limitations");
    for (const auto& limitation : doc.at("limitations")) p.limitations.push_back(text(limitation, 1024, "proposal limitation"));
    const auto& provenance = doc.at("provenance");
    keys(provenance, {"engine", "engine_version", "source_code_sha256", "python", "created_utc", "renderer_version", "renderer_build_id"}, "proposal provenance");
    p.engine = text(provenance.at("engine"), 128, "proposal engine"); p.engine_version = text(provenance.at("engine_version"), 128, "engine version");
    require(digest(text(provenance.at("source_code_sha256"), 64, "engine source SHA-256")), "Invalid engine source SHA-256");
    for (const auto* field : {"python", "created_utc", "renderer_version", "renderer_build_id"}) text(provenance.at(field), 128, field);
    validate(p, take);
    return p;
}

NoteProposals load_note_proposals(const std::filesystem::path& path, const Take& take) {
    const auto size = std::filesystem::file_size(path);
    require(size <= max_json, "Proposal JSON exceeds 2 MiB");
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot open note proposal JSON: " + path.u8string());
    std::string data(static_cast<std::size_t>(size), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    require(input.gcount() == static_cast<std::streamsize>(data.size()) && input.peek() == std::char_traits<char>::eof(), "Proposal JSON changed while reading");
    return parse_note_proposals(data, take);
}
bool note_proposals_match(const NoteProposals& p, const Pattern& current, const Take& take) {
    validate(p, take);
    return pattern_json(p.source_pattern) == pattern_json(current);
}
Pattern accept_note_proposals(const Pattern& current, const Take& take, const NoteProposals& p,
                              const std::vector<ProposalChoice>& choices, float strength) {
    require(note_proposals_match(p, current, take), "Proposal score snapshot is stale; repeat the job against current edits");
    require(!choices.empty() && choices.size() <= p.notes.size(), "Choose at least one proposal note explicitly");
    require(std::isfinite(strength) && strength >= 0 && strength <= 1, "Proposal pluck strength must be in 0..1");
    Pattern next = current;
    std::set<std::size_t> selected;
    const auto prefix = "proposal-" + sha256(p.job_id).substr(0, 16) + "-";
    for (const auto& choice : choices) {
        require(choice.note < p.notes.size() && selected.insert(choice.note).second, "Invalid/duplicate proposal choice");
        const auto& note = p.notes[choice.note];
        require(choice.alternative < note.alternatives.size(), "Choose a valid string/fret alternative");
        const auto& alternative = note.alternatives[choice.alternative];
        const double beat = take.source_frame_to_beat(static_cast<double>(note.onset_frame));
        require(beat >= 0 && beat < current.length_beats, "Proposal onset lies outside the pattern; adjust the take alignment explicitly");
        next.events.push_back({prefix + note.id, "pluck", beat, alternative.string_index, alternative.fret, strength});
    }
    validate_pattern(next);
    return next;
}
} // namespace gtracker
