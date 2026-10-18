#include "gtracker/project.hpp"
#include "gtracker/core.h"
#include "gtracker/take.hpp"
#include "gtracker/alignment.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void help() {
    std::cout << "gtracker " << GT_VERSION << " - acoustic guitar prototype\n\n"
        "Usage:\n"
        "  gtracker_cli validate PATTERN [--json]\n"
        "  gtracker_cli render PATTERN --output WAV [--sample-rate HZ] [--tail SECONDS]\n"
        "                       [--script LUA] [--production JSON] [--bypass-production] [--json]\n"
        "  gtracker_cli transform PATTERN --script LUA --output PATTERN [--json]\n"
        "  gtracker_cli take-info TAKE [--json]\n"
        "  gtracker_cli calibration-probe --output WAV [--sample-rate HZ] [--seed N] [--json]\n"
        "  gtracker_cli align-audio --reference WAV --recorded WAV [--max-offset-ms MS]\n"
        "      [--window-seconds S] [--reference-channel 1|2] [--recorded-channel 1|2]\n"
        "      [--output JSON] [--json]\n"
        "  gtracker_cli --version\n\n"
        "Patterns use v1 baseline or explicit v2 models/slide curves, beat times,\n"
        "and one-based string numbers (highest first). Contact_0_2 is experimental.\n";
}
double number(const std::string& text, const char* flag) {
    std::size_t used = 0;
    const auto value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::runtime_error(std::string("Invalid value for ") + flag);
    return value;
}
int alignment_command(int argc, char** argv, bool as_json) {
    using json = nlohmann::json;
    const bool probe = std::string(argv[1]) == "calibration-probe";
    std::filesystem::path output, reference, recorded;
    int rate = 48000;
    std::uint32_t seed = 42;
    gtracker::AlignmentOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--json") continue;
        if (flag == "--help") { help(); return 0; }
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + flag);
        const std::string value = argv[++i];
        if (value.empty()) throw std::runtime_error("Empty value for " + flag);
        if (flag == "--output") output = std::filesystem::u8path(value);
        else if (flag == "--reference" && !probe) reference = std::filesystem::u8path(value);
        else if (flag == "--recorded" && !probe) recorded = std::filesystem::u8path(value);
        else if (flag == "--sample-rate" && probe) {
            const double parsed = number(value, "--sample-rate");
            if (parsed < 8000 || parsed > 192000 || std::floor(parsed) != parsed) throw std::runtime_error("Probe rate must be an integer in 8000..192000");
            rate = static_cast<int>(parsed);
        } else if (flag == "--seed" && probe) {
            const double parsed = number(value, "--seed");
            if (parsed < 0 || parsed > 4294967295.0 || std::floor(parsed) != parsed) throw std::runtime_error("Probe seed must be an unsigned 32-bit integer");
            seed = static_cast<std::uint32_t>(parsed);
        } else if (flag == "--max-offset-ms" && !probe) options.max_offset_seconds = number(value, "--max-offset-ms") / 1000;
        else if (flag == "--window-seconds" && !probe) options.window_seconds = number(value, "--window-seconds");
        else if ((flag == "--reference-channel" || flag == "--recorded-channel") && !probe) {
            const double parsed = number(value, "--channel");
            if (parsed != 1 && parsed != 2) throw std::runtime_error("Alignment channels are one-based 1 or 2");
            (flag == "--reference-channel" ? options.reference_channel : options.recorded_channel) = static_cast<int>(parsed) - 1;
        } else throw std::runtime_error("Unknown or inapplicable alignment option: " + flag);
    }
    if (!output.empty() && std::filesystem::exists(output)) throw std::runtime_error("Alignment/probe output already exists; choose a new file");
    if (probe) {
        if (output.empty()) throw std::runtime_error("calibration-probe requires --output WAV");
        const auto samples = gtracker::calibration_probe(rate, seed);
        gtracker::write_pcm16_wav(output, samples, rate, 1);
        const auto audio = gtracker::load_pcm_wav(output);
        json report{{"ok", true}, {"schema", "gtracker.calibration-probe"}, {"version", 1}, {"renderer_version", GT_VERSION},
            {"output", output.u8string()}, {"sha256", audio.sha256}, {"sample_rate_hz", rate}, {"frames", audio.frames}, {"seed", seed},
            {"note", "Generated reference only; no playback, recording or physical latency measurement performed."}};
        if (as_json) std::cout << report.dump(2) << '\n';
        else std::cout << "Generated two-second reference: " << output.u8string() << '\n';
        return 0;
    }
    if (reference.empty() || recorded.empty()) throw std::runtime_error("align-audio requires --reference and --recorded WAV paths");
    const auto ref = gtracker::load_pcm_wav(reference);
    const auto rec = gtracker::load_pcm_wav(recorded);
    const auto result = gtracker::estimate_file_delay(ref, rec, options);
    const bool matched = result.status == "matched";
    const auto source = [](const auto& path, const auto& audio, int channel) {
        return json{{"path", path.u8string()}, {"sha256", audio.sha256}, {"frames", audio.frames}, {"channels", audio.channels},
            {"sample_width_bits", audio.sample_width_bits}, {"selected_channel", channel + 1}};
    };
    json report{{"schema", "gtracker.alignment-result"}, {"version", 1}, {"ok", matched}, {"status", result.status},
        {"mode", "relative_file_delay"}, {"estimator", "envelope-pearson-refine-v1"}, {"renderer_version", GT_VERSION},
        {"reference", source(reference, ref, options.reference_channel)}, {"recorded", source(recorded, rec, options.recorded_channel)},
        {"sample_rate_hz", ref.sample_rate}, {"offset_frames", matched ? json(result.candidate_offset_frames) : json(nullptr)},
        {"offset_seconds", matched ? json(static_cast<double>(result.candidate_offset_frames) / ref.sample_rate) : json(nullptr)},
        {"candidate_offset_frames", result.candidate_offset_frames}, {"search_max_offset_frames", result.max_offset_frames},
        {"reference_window_frames", result.reference_window_frames}, {"coarse_step_frames", result.coarse_step_frames},
        {"refinement_start_frame", result.refinement_start_frame}, {"refinement_window_frames", result.refinement_window_frames},
        {"coarse_score", result.coarse_score}, {"alternative_score", result.alternative_score}, {"fine_score", result.fine_score},
        {"polarity", result.polarity}, {"resolution_frames", 1}, {"elapsed_ms", result.elapsed_ms},
        {"drift_measured", false}, {"physical_latency_measured", false},
        {"note", "Positive offset means the recorded waveform is later than the reference file. Scores are bounded match diagnostics, not probabilities. File origins, device start/buffering and clock drift are not separated. Applying an authoring offset remains manual."}};
    const auto text = report.dump(2) + "\n";
    if (!output.empty()) gtracker::write_text_atomic(output, text);
    if (as_json) std::cout << text;
    else std::cout << "Relative file delay: " << result.status << "; candidate " << result.candidate_offset_frames << " frames; fine score " << result.fine_score << '\n';
    return matched ? 0 : 1;
}
}

int cli_main(int argc, char** argv) {
    bool as_json = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--json") as_json = true;
    try {
        if (argc == 1 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { help(); return 0; }
        if (std::string(argv[1]) == "--version") { std::cout << "gtracker " << GT_VERSION << "\n"; return 0; }
        const std::string command = argv[1];
        if (command == "calibration-probe" || command == "align-audio") return alignment_command(argc, argv, as_json);
        if (command != "validate" && command != "render" && command != "transform" && command != "take-info") throw std::runtime_error("Unknown command: " + command);
        if (argc < 3 || std::string(argv[2]).rfind("--", 0) == 0) throw std::runtime_error("A pattern/take path is required");
        const auto input = std::filesystem::u8path(argv[2]);
        std::filesystem::path output, script, production_path;
        gtracker::RenderOptions options;
        for (int i = 3; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--json") continue;
            if (flag == "--help") { help(); return 0; }
            if (flag == "--bypass-production" && command == "render") { options.bypass_production = true; continue; }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + flag);
            const std::string value = argv[++i];
            if (flag == "--output" || flag == "-o") output = std::filesystem::u8path(value);
            else if (flag == "--script") script = std::filesystem::u8path(value);
            else if (flag == "--sample-rate" && command == "render") {
                const double rate = number(value, "--sample-rate");
                if (rate < 8000 || rate > 192000 || std::floor(rate) != rate) throw std::runtime_error("Sample rate must be an integer in 8000..192000");
                options.sample_rate = static_cast<int>(rate);
            } else if (flag == "--tail" && command == "render") options.tail_seconds = number(value, "--tail");
            else if (flag == "--production" && command == "render") {
                production_path = std::filesystem::u8path(value);
                if (std::filesystem::file_size(production_path) > 65536) throw std::runtime_error("Production settings exceed 64 KiB");
                std::ifstream in(production_path, std::ios::binary);
                if (!in) throw std::runtime_error("Cannot open production settings: " + value);
                std::string text(65537, '\0');
                in.read(text.data(), static_cast<std::streamsize>(text.size()));
                text.resize(static_cast<std::size_t>(in.gcount()));
                options.production_override = gtracker::parse_production_json(text);
            }
            else throw std::runtime_error("Unknown or inapplicable option: " + flag);
        }
        if ((command == "validate" || command == "take-info") && (!output.empty() || !script.empty())) throw std::runtime_error(command + " does not accept --output or --script");
        if (command == "take-info") {
            const auto take = gtracker::load_take(input);
            nlohmann::json report{{"ok", true}, {"command", command}, {"take", input.u8string()}, {"take_id", take.id},
                {"source_name", take.source_name}, {"source_sha256", take.audio.sha256}, {"frames", take.audio.frames},
                {"sample_rate", take.audio.sample_rate}, {"channels", take.audio.channels}, {"sample_width_bits", take.audio.sample_width_bits},
                {"peak", take.audio.peak}, {"rms", take.audio.rms}, {"analysis_status", "metadata_only"},
                {"pattern_revision", take.pattern.revision}, {"source_pattern_revision", take.source_pattern_revision},
                {"analysis_matches_pattern_revision", take.pattern.revision == take.source_pattern_revision},
                {"input_offset_status", take.alignment.offset_status},
                {"selection_start_beat", take.source_frame_to_beat(static_cast<double>(take.alignment.selection_start_frame))},
                {"selection_end_beat", take.source_frame_to_beat(static_cast<double>(take.alignment.selection_end_frame))}};
            if (as_json) std::cout << report.dump(2) << '\n';
            else std::cout << "Take: " << take.source_name << " (" << take.audio.frames << " frames, "
                           << take.audio.channels << " channels at " << take.audio.sample_rate << " Hz; metadata only)\n";
            return 0;
        }
        if (command != "validate" && output.empty()) throw std::runtime_error("--output is required");
        if (command == "transform" && script.empty()) throw std::runtime_error("transform requires --script");
        if (command == "render") {
            for (const auto& source_path : {input, script, production_path}) {
                if (source_path.empty()) continue;
                std::error_code error;
                const bool same_existing_file = std::filesystem::equivalent(source_path, output, error);
                if (same_existing_file || std::filesystem::weakly_canonical(source_path) == std::filesystem::weakly_canonical(output))
                    throw std::runtime_error("Render output must differ from the pattern input, script and production settings");
            }
        }
        auto pattern = gtracker::load_pattern(input);
        if (!script.empty()) gtracker::apply_lua(pattern, script);
        nlohmann::json report{{"ok", true}, {"command", command}, {"renderer_version", GT_VERSION},
            {"schema_version", pattern.schema_version}, {"input", input.u8string()}, {"pattern_revision", pattern.revision},
            {"events", pattern.events.size()}, {"gestures", pattern.gestures.size()}, {"instrument_model", pattern.model},
            {"strings", pattern.tuning_midi.size()}, {"seed", pattern.seed}};
        if (command == "render") {
            const auto rendered = gtracker::render_pattern(pattern, options);
            gtracker::write_wav(output, rendered.samples, rendered.sample_rate, rendered.channels);
            report["output"] = output.u8string(); report["sample_rate"] = rendered.sample_rate;
            report["frames"] = rendered.frames(); report["seconds"] = static_cast<double>(rendered.frames()) / rendered.sample_rate;
            report["peak"] = rendered.peak; report["rms"] = rendered.rms; report["render_ms"] = rendered.elapsed_ms;
            report["channels"] = rendered.channels;
            report["wav_encoding"] = rendered.channels == 2 ? "PCM_S16LE_STEREO" : "PCM_S16LE_MONO";
            report["render_identity_sha256"] = rendered.identity;
            report["production_tail_seconds"] = rendered.production_tail_seconds;
            report["clipped_samples"] = rendered.clipped_samples;
            report["end_peak_last_50ms"] = rendered.end_peak;
            report["production_bypassed"] = options.bypass_production;
            report["production_active"] = rendered.channels == 2;
            const auto settings = options.production_override ? options.production_override : pattern.production;
            report["production"] = settings ? nlohmann::json::parse(gtracker::production_json(*settings)) : nlohmann::json(nullptr);
            if (!as_json) std::cout << "Rendered " << report["seconds"] << " s to " << output.u8string()
                << " (" << rendered.elapsed_ms << " ms render, peak " << rendered.peak << ")\n";
        } else if (command == "transform") {
            gtracker::save_pattern(pattern, output);
            report["output"] = output.u8string();
            if (!as_json) std::cout << "Saved revision " << pattern.revision << " to " << output.u8string() << "\n";
        } else if (!as_json) std::cout << "Valid pattern: " << pattern.name << " (" << pattern.events.size() << " events)\n";
        if (as_json) std::cout << report.dump(2) << "\n";
        return 0;
    } catch (const std::exception& error) {
        // Native filesystem messages and trusted Lua errors may contain bytes
        // outside UTF-8. The machine-readable failure path must still be JSON.
        if (as_json) std::cout << nlohmann::json{{"ok", false}, {"error", error.what()}}
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
        else std::cerr << "gtracker: " << error.what() << "\n";
        return 2;
    }
}

#ifdef _WIN32
// The Windows narrow CRT argv has already lost characters outside its current
// code page. Convert the original wide arguments once, then use UTF-8 everywhere
// at tool/document boundaries just as SDL does for the editor.
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) storage.push_back(std::filesystem::path(argv[i]).u8string());
    std::vector<char*> utf8;
    utf8.reserve(storage.size());
    for (auto& argument : storage) utf8.push_back(argument.data());
    return cli_main(argc, utf8.data());
}
#else
int main(int argc, char** argv) { return cli_main(argc, argv); }
#endif
