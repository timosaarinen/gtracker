#include "gtracker/project.hpp"
#include "gtracker/io.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "source directory required");
        const auto root = std::filesystem::path(argv[1]);
        const auto directory = std::filesystem::current_path() / "export-test-output";
        std::filesystem::create_directories(directory);
        const auto destination = directory / "stream.wav";
        auto pattern = gtracker::load_pattern(root / "examples/slide-study.gtrk.json");
        gtracker::RenderOptions options;
        gtracker::ProductionSettings settings; settings.enabled = true;
        options.production_override = settings;
        const auto full = gtracker::render_pattern(pattern, options);
        gtracker::write_wav(directory / "reference.wav", full.samples, full.sample_rate, full.channels);
        const auto reference = bytes(directory / "reference.wav");
        for (std::size_t block : {127U, 4096U, 65536U}) {
            gtracker::StreamOptions stream; stream.block_frames = block;
            std::uint64_t previous = 0;
            stream.progress = [&](const gtracker::RenderProgress& progress) {
                check(progress.processed_frames >= previous && progress.processed_frames <= progress.processing_frames,
                      "progress regressed or exceeded timeline");
                check(progress.written_frames <= progress.output_frames, "progress over-reported output");
                previous = progress.processed_frames;
            };
            const auto rendered = gtracker::render_pattern_wav(pattern, destination, options, stream);
            check(bytes(destination) == reference, "streamed room/slide PCM differs from in-memory render");
            check(rendered.identity == full.identity && rendered.frames == full.frames() && previous == full.frames(), "stream identity/progress/length differs");
            check(rendered.peak == full.peak && rendered.rms == full.rms && rendered.end_peak == full.end_peak, "stream signal metrics differ");
            check(rendered.audio_buffer_bytes == block * 2 * sizeof(float), "stream buffer does not match bounded block request");
        }
        gtracker::StreamOptions crop; crop.start_frame = 17003; crop.end_frame = 91239; crop.block_frames = 311;
        const auto preview = gtracker::render_pattern_wav(pattern, destination, options, crop);
        const auto cropped = bytes(destination);
        check(cropped.substr(44) == reference.substr(44 + 17003 * 4, (91239 - 17003) * 4), "preview reset state instead of warming the full performance");
        check(preview.warmup_frames == 17003 && preview.identity != full.identity, "crop omitted warm-up or cache identity");
        const auto retained = bytes(destination);
        auto renderer = gtracker::pattern_renderer(pattern, options);
        gtracker::StreamOptions cancel;
        cancel.cancelled = [&] { return renderer.position() >= 8192; };
        bool cancelled = false;
        try { gtracker::export_wav(renderer, destination, full.identity, cancel); }
        catch (const gtracker::RenderCancelled&) { cancelled = true; }
        check(cancelled && renderer.position() == 8192 && bytes(destination) == retained, "cancellation published partial PCM or lost old output");
        rejects([&] { gtracker::export_wav(renderer, destination, full.identity); }, "partially consumed renderer exported without an explicit reset");
        renderer.reset();
        gtracker::StreamOptions failed;
        failed.progress = [](const gtracker::RenderProgress& p) { if (p.processed_frames > 0) throw std::runtime_error("injected progress failure"); };
        rejects([&] { gtracker::export_wav(renderer, destination, full.identity, failed); }, "progress failure swallowed");
        check(bytes(destination) == retained, "failed export replaced prior output");
        rejects([&] { gtracker::write_binary_atomic(destination, [](std::ostream& out) {
            out.write("partial", 7); out.setstate(std::ios::badbit);
        }); }, "injected output write failure ignored");
        check(bytes(destination) == retained, "failed stream write replaced prior output");
        const float invalid[] = {0.2f, std::numeric_limits<float>::quiet_NaN()};
        rejects([&] { gtracker::write_binary_atomic(destination, [&](std::ostream& out) {
            gtracker::write_float_pcm16(out, invalid, 2);
        }); }, "nonfinite PCM was encoded");
        check(bytes(destination) == retained, "nonfinite PCM replaced prior output");
        for (const auto& item : std::filesystem::directory_iterator(directory))
            check(item.path().filename().u8string().find(".tmp-") == std::string::npos, "export leaked owned temporary files");
        gtracker::NativePerformance very_long; very_long.instrument.sample_rate = 192000;
        gtracker::NativeRenderer large(very_long, 192000ULL * 86400, settings);
        rejects([&] { gtracker::export_wav(large, destination, "large"); }, "classic RIFF overflow not rejected before rendering");
        check(large.position() == 0 && bytes(destination) == retained, "oversized RIFF consumed timeline or replaced output");
        gtracker::StreamOptions bad; bad.block_frames = 65537;
        rejects([&] { gtracker::render_pattern_wav(pattern, destination, options, bad); }, "unbounded export block accepted");
        bad = {}; bad.start_frame = full.frames();
        rejects([&] { gtracker::render_pattern_wav(pattern, destination, options, bad); }, "empty preview accepted");
        check(bytes(destination) == retained, "bad options replaced output");
        check(std::filesystem::weakly_canonical(directory).parent_path() == std::filesystem::current_path(), "test cleanup escaped build directory");
        std::filesystem::remove_all(directory);
        std::cout << "export: bounded blocks, exact PCM/crop, progress, cancellation, failure recovery and RIFF bounds passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
