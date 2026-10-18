#include "gtracker/take.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace gtracker {
namespace {
using json = nlohmann::json;
constexpr std::uint64_t max_integer = 9007199254740991ULL;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

json read_json(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    require(size <= 1024 * 1024, "Take/job JSON exceeds its 1 MiB authoring limit");
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot open take/job JSON: " + path.u8string());
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    require(input.gcount() == static_cast<std::streamsize>(text.size()), "Take/job JSON changed while reading");
    return json::parse(text);
}

void keys(const json& value, std::initializer_list<const char*> names, const char* where) {
    require(value.is_object() && value.size() == names.size(), std::string(where) + " has missing or unknown fields");
    for (const auto* name : names) require(value.contains(name), std::string(where) + ": missing field " + name);
}

std::string text(const json& value, std::size_t maximum, const char* where) {
    require(value.is_string(), std::string(where) + " must be a string");
    auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= maximum, std::string(where) + " exceeds its UTF-8 byte bounds");
    return result;
}

std::uint64_t number(const json& value, std::uint64_t minimum, std::uint64_t maximum, const char* where) {
    require(value.is_number_integer(), std::string(where) + " must be an integer");
    std::uint64_t result = 0;
    if (value.is_number_unsigned()) result = value.get<std::uint64_t>();
    else {
        const auto signed_value = value.get<std::int64_t>();
        require(signed_value >= 0, std::string(where) + " cannot be negative");
        result = static_cast<std::uint64_t>(signed_value);
    }
    require(result >= minimum && result <= maximum, std::string(where) + " is out of range");
    return result;
}

double real(const json& value, double minimum, double maximum, const char* where) {
    require(value.is_number(), std::string(where) + " must be a number");
    const auto result = value.get<double>();
    require(std::isfinite(result) && result >= minimum && result <= maximum, std::string(where) + " is out of range");
    return result;
}

std::filesystem::path reference(const std::filesystem::path& directory, const json& value) {
    const auto name = text(value, 4096, "take reference");
    const auto path = std::filesystem::u8path(name);
    require(name.find_first_of("\\:") == std::string::npos && !path.is_absolute(), "Take references must be portable relative paths");
    for (const auto& part : path) require(part != "..", "Take reference escapes its bundle");
    std::error_code error;
    const auto result = std::filesystem::canonical(directory / path, error);
    require(!error, "Cannot open take reference: " + name);
    const auto relative = result.lexically_relative(directory);
    require(!relative.empty() && !relative.is_absolute() && *relative.begin() != "..", "Take reference escapes its bundle");
    return result;
}

void same_number(const json& value, std::uint64_t expected, const char* where) {
    require(number(value, 0, max_integer, where) == expected, std::string(where) + " does not match the preserved source");
}

void metric(const json& value, double actual, const char* where) {
    require(std::abs(real(value, 0, 1, where) - actual) < 1e-6, std::string(where) + " does not match the preserved source");
}
}

double Take::source_frame_to_beat(double frame) const {
    return (frame - static_cast<double>(alignment.pattern_zero_frame) - static_cast<double>(alignment.input_offset_frames))
           / audio.sample_rate * alignment.bpm / 60.0;
}

Take load_take(const std::filesystem::path& path) {
    Take take;
    take.manifest_path = std::filesystem::canonical(path);
    const auto directory = take.manifest_path.parent_path();
    const auto document = read_json(take.manifest_path);
    keys(document, {"schema", "version", "id", "origin", "pattern_path", "source_pattern_revision", "source", "alignment", "analysis_path", "created_utc"}, "take");
    require(document.at("schema") == "gtracker.take" && number(document.at("version"), 1, 1, "take version") == 1,
            "Unsupported take schema/version; expected gtracker.take v1");
    require(document.at("origin") == "import", "Only preserved import takes are implemented");
    take.id = text(document.at("id"), 128, "take id");
    take.source_pattern_revision = number(document.at("source_pattern_revision"), 1, max_integer, "source_pattern_revision");
    text(document.at("created_utc"), 128, "created_utc");
    take.pattern_path = reference(directory, document.at("pattern_path"));
    take.analysis_path = reference(directory, document.at("analysis_path"));
    const auto& source = document.at("source");
    keys(source, {"path", "name", "sha256", "size_bytes", "encoding", "sample_width_bits", "channels", "sample_rate_hz", "frames", "duration_seconds"}, "take source");
    take.source_path = reference(directory, source.at("path"));
    take.source_name = text(source.at("name"), 1023, "source name");
    const auto digest = text(source.at("sha256"), 64, "source SHA-256");
    require(digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }), "Source SHA-256 must contain 64 lowercase hexadecimal characters");
    require(source.at("encoding") == "integer_pcm", "Only integer-PCM takes are implemented");
    take.audio = load_pcm_wav(take.source_path);
    require(take.audio.sha256 == digest, "Preserved source SHA-256 no longer matches this take");
    same_number(source.at("size_bytes"), take.audio.size_bytes, "source size_bytes");
    same_number(source.at("frames"), take.audio.frames, "source frames");
    same_number(source.at("channels"), static_cast<std::uint64_t>(take.audio.channels), "source channels");
    same_number(source.at("sample_rate_hz"), static_cast<std::uint64_t>(take.audio.sample_rate), "source sample_rate_hz");
    same_number(source.at("sample_width_bits"), static_cast<std::uint64_t>(take.audio.sample_width_bits), "source sample_width_bits");
    const auto seconds = static_cast<double>(take.audio.frames) / take.audio.sample_rate;
    require(std::abs(real(source.at("duration_seconds"), 0, max_integer, "source duration") - seconds) <= seconds * 1e-12,
            "Source duration does not match its frame count/rate");
    const auto& alignment = document.at("alignment");
    keys(alignment, {"coordinate_system", "pattern_zero_frame", "input_offset_frames", "offset_status", "tempo_bpm", "pattern_length_beats", "selection_start_frame", "selection_end_frame"}, "take alignment");
    require(alignment.at("coordinate_system") == "source_samples", "Take timing must use source_samples");
    take.alignment.pattern_zero_frame = number(alignment.at("pattern_zero_frame"), 0, take.audio.frames, "pattern_zero_frame");
    const auto& offset = alignment.at("input_offset_frames");
    require(offset.is_number_integer(), "input_offset_frames must be an integer");
    if (offset.is_number_unsigned())
        take.alignment.input_offset_frames = static_cast<std::int64_t>(number(offset, 0, take.audio.frames, "input_offset_frames"));
    else {
        take.alignment.input_offset_frames = offset.get<std::int64_t>();
        require(std::abs(static_cast<double>(take.alignment.input_offset_frames)) <= take.audio.frames, "input_offset_frames is out of range");
    }
    take.alignment.offset_status = text(alignment.at("offset_status"), 32, "offset_status");
    require(take.alignment.offset_status == "manual" || (take.alignment.offset_status == "uncalibrated" && take.alignment.input_offset_frames == 0),
            "Only manual/uncalibrated input offsets are implemented");
    take.alignment.bpm = real(alignment.at("tempo_bpm"), 20, 400, "tempo_bpm");
    take.alignment.pattern_length_beats = real(alignment.at("pattern_length_beats"), 0, 256, "pattern_length_beats");
    require(take.alignment.pattern_length_beats > 0, "pattern_length_beats must be positive");
    take.alignment.selection_start_frame = number(alignment.at("selection_start_frame"), 0, take.audio.frames, "selection_start_frame");
    take.alignment.selection_end_frame = number(alignment.at("selection_end_frame"), take.alignment.selection_start_frame + 1, take.audio.frames, "selection_end_frame");
    take.pattern = load_pattern(take.pattern_path);

    const auto analysis = read_json(take.analysis_path);
    keys(analysis, {"schema", "version", "job_id", "source_pattern_revision", "status", "capabilities", "source", "timing", "diagnostics", "events", "limitations", "provenance"}, "analysis result");
    require(analysis.at("schema") == "gtracker.analysis-result" && number(analysis.at("version"), 1, 1, "analysis version") == 1,
            "Unsupported analysis result schema/version");
    require(analysis.at("job_id") == take.id && number(analysis.at("source_pattern_revision"), 1, max_integer, "analysis pattern revision") == take.source_pattern_revision,
            "Analysis job/source/revision does not match this take");
    require(analysis.at("status") == "metadata_only" && analysis.at("events").is_array() && analysis.at("events").empty(),
            "Only metadata-only analysis is implemented; no note transcript may be imported");
    const auto& capabilities = analysis.at("capabilities");
    keys(capabilities, {"pcm_metadata", "peak_rms", "note_transcription", "technique_recognition"}, "analysis capabilities");
    for (const auto* name : {"pcm_metadata", "peak_rms", "note_transcription", "technique_recognition"})
        require(capabilities.at(name).is_boolean() && capabilities.at(name) == (std::string(name) == "pcm_metadata" || std::string(name) == "peak_rms"),
                "Analysis capabilities contradict metadata-only status");
    const auto& analyzed_source = analysis.at("source");
    keys(analyzed_source, {"name", "sha256", "size_bytes", "encoding", "sample_width_bits", "channels", "sample_rate_hz", "frames", "duration_seconds"}, "analysis source");
    for (const auto& item : analyzed_source.items())
        require(item.value() == source.at(item.key()), "Analysis source metadata does not match this take: " + item.key());
    const auto& timing = analysis.at("timing");
    keys(timing, {"coordinate_system", "selection_start_frame", "selection_end_frame", "bpm_hint"}, "analysis timing");
    require(timing.at("coordinate_system") == "source_samples", "Analysis timing must use source_samples");
    same_number(timing.at("selection_start_frame"), 0, "analysis selection start");
    same_number(timing.at("selection_end_frame"), take.audio.frames, "analysis selection end");
    require(real(timing.at("bpm_hint"), 20, 400, "analysis bpm_hint") == take.alignment.bpm, "Analysis tempo hint does not match take timing");
    const auto& diagnostics = analysis.at("diagnostics");
    keys(diagnostics, {"peak", "rms", "channels"}, "analysis diagnostics");
    metric(diagnostics.at("peak"), take.audio.peak, "analysis peak");
    metric(diagnostics.at("rms"), take.audio.rms, "analysis rms");
    require(diagnostics.at("channels").is_array() && diagnostics.at("channels").size() == static_cast<std::size_t>(take.audio.channels),
            "Analysis channel diagnostics do not match the preserved source");
    for (int i = 0; i < take.audio.channels; ++i) {
        const auto channel = static_cast<std::size_t>(i);
        const auto& item = diagnostics.at("channels").at(channel);
        keys(item, {"index", "peak", "rms"}, "analysis channel");
        same_number(item.at("index"), channel, "analysis channel index");
        metric(item.at("peak"), take.audio.channel_peak[channel], "analysis channel peak");
        metric(item.at("rms"), take.audio.channel_rms[channel], "analysis channel rms");
    }
    return take;
}
} // namespace gtracker
