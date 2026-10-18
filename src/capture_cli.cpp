#include "gtracker/capture.hpp"
#include "gtracker/audio_source.hpp"
#include "gtracker/core.h"
#include "gtracker/hash.hpp"
#include <nlohmann/json.hpp>
#include <csignal>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using json = nlohmann::json;
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
double number(const std::string& text, const std::string& flag) {
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    require(used == text.size() && std::isfinite(value), "Invalid number for " + flag);
    return value;
}
int integer(const std::string& text, const std::string& flag, int low, int high) {
    const auto value = number(text, flag);
    require(value >= low && value <= high && std::floor(value) == value, "Out-of-range integer for " + flag);
    return static_cast<int>(value);
}
json format(const gtracker::CaptureFormat& value) {
    return {{"sample_rate_hz", value.sample_rate}, {"channels", value.channels}, {"encoding", value.encoding}, {"buffer_frames", value.buffer_frames}};
}
std::string serialize(const json& value) { return value.dump(2, ' ', false, json::error_handler_t::replace) + "\n"; }
void help() {
    std::cout << "gtracker capture " << GT_VERSION << "\n\n"
        "  gtracker_capture devices [--json]\n"
        "  gtracker_capture record --pattern PATTERN --output NEW_DIRECTORY\n"
        "      (--device default|INDEX|EXACT_NAME | --simulate PCM_WAV)\n"
        "      [--sample-rate HZ] [--channels 1|2] [--count-in BEATS]\n"
        "      [--pre-roll SECONDS] [--tail SECONDS] [--metronome] [--silent]\n"
        "      [--job-id ID] [--cancel-file PATH] [--json]\n"
        "      [--realtime-simulation] [--simulate-disconnect-frame N]\n"
        "      [--simulate-cancel-frame N]\n\n"
        "Explicit input choice is required. Output is a new directory, never overwritten.\n"
        "PCM16 is preserved as received by the application; device conversion is reported.\n"
        "Count-in/sample mapping is nominal and uncalibrated. Ctrl+C retains a partial take.\n";
}
}

int capture_main(int argc, char** argv) {
    bool as_json = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--json") as_json = true;
    try {
        if (argc == 1 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { help(); return 0; }
        const std::string command = argv[1];
        if (command == "devices") {
            require(argc == 2 || (argc == 3 && as_json), "devices accepts only --json");
            json devices = json::array();
            for (const auto& device : gtracker::recording_devices()) {
                devices.push_back({{"index", device.index}, {"runtime_id", device.runtime_id}, {"name", device.name}, {"preferred_format", format(device.preferred)}});
                if (!as_json) std::cout << device.index << ": " << device.name << " (" << device.preferred.sample_rate << " Hz, " << device.preferred.channels << " channels)\n";
            }
            if (as_json) std::cout << serialize({{"ok", true}, {"devices", devices}, {"note", "Indices/IDs are current-session selectors; buffer size is not measured latency."}});
            return 0;
        }
        require(command == "record", "Unknown capture command: " + command);
        gtracker::CaptureOptions options;
        std::filesystem::path pattern_path, output, cancel_file;
        std::string job = "capture-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::uint64_t cancel_after = 0;
        std::set<std::string> seen;
        for (int i = 2; i < argc; ++i) {
            const std::string flag = argv[i];
            require(seen.insert(flag).second, "Duplicate option: " + flag);
            if (flag == "--json") continue;
            if (flag == "--metronome") { options.metronome = true; continue; }
            if (flag == "--silent") { options.silent = true; continue; }
            if (flag == "--realtime-simulation") { options.realtime_simulation = true; continue; }
            if (flag == "--help") { help(); return 0; }
            require(i + 1 < argc, "Missing value for " + flag);
            const std::string value = argv[++i];
            require(!value.empty(), "Empty value for " + flag);
            if (flag == "--pattern") pattern_path = std::filesystem::u8path(value);
            else if (flag == "--output") output = std::filesystem::u8path(value);
            else if (flag == "--device") options.device = value;
            else if (flag == "--simulate") options.simulated_input = std::filesystem::u8path(value);
            else if (flag == "--cancel-file") cancel_file = std::filesystem::u8path(value);
            else if (flag == "--job-id") job = value;
            else if (flag == "--sample-rate") options.sample_rate = integer(value, flag, 8000, 192000);
            else if (flag == "--channels") options.channels = integer(value, flag, 1, 2);
            else if (flag == "--count-in") options.count_in_beats = integer(value, flag, 0, 16);
            else if (flag == "--pre-roll") options.pre_roll_seconds = number(value, flag);
            else if (flag == "--tail") options.tail_seconds = number(value, flag);
            else if (flag == "--simulate-disconnect-frame") options.simulate_disconnect_frame = static_cast<std::uint64_t>(integer(value, flag, 1, 120 * 192000));
            else if (flag == "--simulate-cancel-frame") cancel_after = static_cast<std::uint64_t>(integer(value, flag, 1, 120 * 192000));
            else throw std::runtime_error("Unknown capture option: " + flag);
        }
        require(!pattern_path.empty() && !output.empty(), "record requires --pattern and --output");
        require(job.size() <= 128, "Job ID exceeds 128 UTF-8 bytes");
        require(options.device.empty() != options.simulated_input.empty(), "Choose exactly one explicit --device or --simulate input");
        require(!cancel_after || !options.simulated_input.empty(), "Simulated cancellation requires --simulate");
        std::ifstream pattern_input(pattern_path, std::ios::binary | std::ios::ate);
        require(static_cast<bool>(pattern_input), "Cannot open capture pattern: " + pattern_path.u8string());
        const auto pattern_size = pattern_input.tellg();
        require(pattern_size >= 0 && pattern_size <= 1024 * 1024, "Capture pattern exceeds the 1 MiB document limit");
        std::string pattern_text(static_cast<std::size_t>(pattern_size), '\0');
        pattern_input.seekg(0);
        pattern_input.read(pattern_text.data(), static_cast<std::streamsize>(pattern_text.size()));
        require(pattern_input.gcount() == static_cast<std::streamsize>(pattern_text.size()), "Could not snapshot complete capture pattern");
        pattern_input.close();
        options.pattern = gtracker::parse_pattern_json(pattern_text);
        // Validate all source-independent bounds before reserving a directory or touching a device.
        gtracker::plan_capture(options, options.sample_rate ? options.sample_rate : 48000, options.channels ? options.channels : 1);
        require(!std::filesystem::exists(output), "Capture output already exists; choose a new directory");
        require(std::filesystem::is_directory(output.parent_path().empty() ? std::filesystem::path(".") : output.parent_path()), "Capture output parent must already exist");
        require(std::filesystem::create_directory(output), "Could not reserve a new capture directory");
        gtracker::write_text_atomic(output / "pattern.gtrk.json", pattern_text);
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        options.cancelled = [&](std::uint64_t frames) {
            return interrupted || (cancel_after && frames >= cancel_after) || (!cancel_file.empty() && std::filesystem::exists(cancel_file));
        };
        auto result = gtracker::capture_audio(options);
        json source = nullptr;
        if (result.captured_frames) {
            gtracker::write_pcm16_wav(output / "source.wav", result.interleaved, result.plan.sample_rate, result.plan.channels);
            const auto audio = gtracker::load_pcm_wav(output / "source.wav");
            source = {{"path", "source.wav"}, {"sha256", audio.sha256}, {"sample_rate_hz", audio.sample_rate}, {"channels", audio.channels},
                {"sample_width_bits", 16}, {"frames", audio.frames}, {"size_bytes", audio.size_bytes}};
        }
        std::string reference_hash;
        if (!result.reference_mono.empty()) {
            gtracker::write_pcm16_wav(output / "reference.wav", result.reference_mono, result.plan.sample_rate, 1);
            reference_hash = gtracker::load_pcm_wav(output / "reference.wav").sha256;
        }
        const auto& plan = result.plan;
        json report{{"schema", "gtracker.capture-result"}, {"version", 1}, {"renderer_version", GT_VERSION}, {"job_id", job},
            {"ok", result.status == "complete"}, {"status", result.status}, {"error", result.error},
            {"source_kind", result.source_kind}, {"audio_driver", result.driver}, {"device_name", result.device_name},
            {"device_format", format(result.device_format)}, {"application_encoding", "PCM_S16LE"},
            {"format_conversion", result.format_conversion}, {"source", source},
            {"pattern_path", "pattern.gtrk.json"}, {"source_pattern_revision", options.pattern.revision},
            {"pattern_sha256", gtracker::sha256(pattern_text)},
            {"timing", {{"sample_rate_hz", plan.sample_rate}, {"channels", plan.channels}, {"tempo_bpm", options.pattern.bpm},
                {"pattern_length_beats", options.pattern.length_beats}, {"pre_roll_frames", plan.pre_roll_frames}, {"count_in_frames", plan.count_in_frames},
                {"pattern_zero_frame", plan.pattern_zero_frame}, {"musical_frames", plan.musical_frames}, {"tail_frames", plan.tail_frames},
                {"planned_frames", plan.total_frames}, {"captured_frames", result.captured_frames}, {"input_offset_frames", 0}, {"offset_status", "uncalibrated"}}},
            {"reference_path", result.reference_mono.empty() ? json(nullptr) : json("reference.wav")}, {"reference_played", result.reference_played},
            {"reference_sha256", reference_hash},
            {"metronome", options.metronome}, {"simulation_input_sha256", result.simulation_input_sha256},
            {"simulation", {{"realtime", options.realtime_simulation}, {"disconnect_frame", options.simulate_disconnect_frame}, {"cancel_after_frame", cancel_after}}},
            {"timing_note", "Sample clock is the application input stream. Input/output start offset, device latency and clock drift have not been measured; SRC does not calibrate them."}};
        gtracker::write_text_atomic(output / "capture.json", serialize(report));
        if (as_json) std::cout << serialize(report);
        else std::cout << "Capture " << result.status << ": " << result.captured_frames << " frames; preserved in " << output.u8string() << "\n" << result.error << "\n";
        return result.status == "complete" ? 0 : result.status == "cancelled" ? 130 : 1;
    } catch (const std::exception& error) {
        if (as_json) std::cout << serialize({{"ok", false}, {"error", error.what()}});
        else std::cerr << "capture: " << error.what() << '\n';
        return 2;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    for (int i = 0; i < argc; ++i) storage.push_back(std::filesystem::path(argv[i]).u8string());
    std::vector<char*> utf8;
    for (auto& value : storage) utf8.push_back(value.data());
    return capture_main(argc, utf8.data());
}
#else
int main(int argc, char** argv) { return capture_main(argc, argv); }
#endif
