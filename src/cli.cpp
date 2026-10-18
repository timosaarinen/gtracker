#include "gtracker/project.hpp"
#include "gtracker/core.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void help() {
    std::cout << "gtracker " << GT_VERSION << " - acoustic guitar prototype\n\n"
        "Usage:\n"
        "  gtracker_cli validate PATTERN [--json]\n"
        "  gtracker_cli render PATTERN --output WAV [--sample-rate HZ] [--tail SECONDS]\n"
        "                       [--script LUA] [--json]\n"
        "  gtracker_cli transform PATTERN --script LUA --output PATTERN [--json]\n"
        "  gtracker_cli --version\n\n"
        "Patterns use schema v1, beat times, one-based string numbers (highest first).\n"
        "The current instrument is an independent plucked-string/body-tap baseline.\n";
}
double number(const std::string& text, const char* flag) {
    std::size_t used = 0;
    const auto value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::runtime_error(std::string("Invalid value for ") + flag);
    return value;
}
}

int main(int argc, char** argv) {
    bool as_json = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--json") as_json = true;
    try {
        if (argc == 1 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { help(); return 0; }
        if (std::string(argv[1]) == "--version") { std::cout << "gtracker " << GT_VERSION << "\n"; return 0; }
        const std::string command = argv[1];
        if (command != "validate" && command != "render" && command != "transform") throw std::runtime_error("Unknown command: " + command);
        if (argc < 3 || std::string(argv[2]).rfind("--", 0) == 0) throw std::runtime_error("A pattern path is required");
        const std::filesystem::path input = argv[2];
        std::filesystem::path output, script;
        gtracker::RenderOptions options;
        for (int i = 3; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--json") continue;
            if (flag == "--help") { help(); return 0; }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + flag);
            const std::string value = argv[++i];
            if (flag == "--output" || flag == "-o") output = value;
            else if (flag == "--script") script = value;
            else if (flag == "--sample-rate" && command == "render") {
                const double rate = number(value, "--sample-rate");
                if (rate < 8000 || rate > 192000 || std::floor(rate) != rate) throw std::runtime_error("Sample rate must be an integer in 8000..192000");
                options.sample_rate = static_cast<int>(rate);
            } else if (flag == "--tail" && command == "render") options.tail_seconds = number(value, "--tail");
            else throw std::runtime_error("Unknown or inapplicable option: " + flag);
        }
        if (command == "validate" && (!output.empty() || !script.empty())) throw std::runtime_error("validate does not accept --output or --script");
        if (command != "validate" && output.empty()) throw std::runtime_error("--output is required");
        if (command == "transform" && script.empty()) throw std::runtime_error("transform requires --script");
        if (command == "render") {
            std::error_code error;
            const bool same_existing_file = std::filesystem::equivalent(input, output, error);
            if (same_existing_file || std::filesystem::weakly_canonical(input) == std::filesystem::weakly_canonical(output))
                throw std::runtime_error("Render output must differ from the pattern input");
        }
        auto pattern = gtracker::load_pattern(input);
        if (!script.empty()) gtracker::apply_lua(pattern, script);
        nlohmann::json report{{"ok", true}, {"command", command}, {"renderer_version", GT_VERSION},
            {"schema_version", 1}, {"input", input.string()}, {"pattern_revision", pattern.revision},
            {"events", pattern.events.size()}, {"strings", pattern.tuning_midi.size()}, {"seed", pattern.seed}};
        if (command == "render") {
            const auto rendered = gtracker::render_pattern(pattern, options);
            gtracker::write_wav(output, rendered.mono, rendered.sample_rate);
            report["output"] = output.string(); report["sample_rate"] = rendered.sample_rate;
            report["frames"] = rendered.mono.size(); report["seconds"] = static_cast<double>(rendered.mono.size()) / rendered.sample_rate;
            report["peak"] = rendered.peak; report["rms"] = rendered.rms; report["render_ms"] = rendered.elapsed_ms;
            report["wav_encoding"] = "PCM_S16LE_MONO";
            if (!as_json) std::cout << "Rendered " << report["seconds"] << " s to " << output.string()
                << " (" << rendered.elapsed_ms << " ms synthesis, peak " << rendered.peak << ")\n";
        } else if (command == "transform") {
            gtracker::save_pattern(pattern, output);
            report["output"] = output.string();
            if (!as_json) std::cout << "Saved revision " << pattern.revision << " to " << output.string() << "\n";
        } else if (!as_json) std::cout << "Valid pattern: " << pattern.name << " (" << pattern.events.size() << " events)\n";
        if (as_json) std::cout << report.dump(2) << "\n";
        return 0;
    } catch (const std::exception& error) {
        if (as_json) std::cout << nlohmann::json{{"ok", false}, {"error", error.what()}}.dump() << "\n";
        else std::cerr << "gtracker: " << error.what() << "\n";
        return 2;
    }
}
