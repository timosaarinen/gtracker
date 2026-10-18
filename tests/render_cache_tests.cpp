#include "gtracker/project.hpp"
#include "gtracker/render_cache.hpp"
#include "gtracker/hash.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void text(const std::filesystem::path& path, const std::string& value) { std::ofstream out(path, std::ios::binary); out << value; }
void mutate(const std::filesystem::path& path, std::streamoff at) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(at); char value = 0; file.get(value); file.seekp(at); file.put(static_cast<char>(value ^ 0x23)); file.flush();
    check(static_cast<bool>(file), "could not mutate owned cache fixture");
}
}
int main() {
    try {
        const auto directory = std::filesystem::current_path() / "cache-test-output";
        std::filesystem::create_directories(directory);
        const auto output = directory / "audio.wav";
        gtracker::Pattern pattern;
        pattern.bpm = 120; pattern.length_beats = 2; pattern.decay_seconds = 1;
        pattern.events = {{"note", "pluck", 0, 5, 0, 0.7f}, {"tap", "body_hit", 0.4, 0, 0, 0.2f}};
        gtracker::RenderOptions options(16000, 0.125);
        gtracker::ProductionSettings room; room.enabled = true; room.decay_seconds = 0.15f; room.predelay_ms = 0;
        options.production_override = room;
        gtracker::CacheOptions cache; cache.directory = directory / "cache";
        auto run = [&](const gtracker::Pattern& score, const gtracker::CacheOptions& policy, const gtracker::StreamOptions& stream = gtracker::StreamOptions{}) {
            auto renderer = gtracker::pattern_renderer(score, options);
            return gtracker::cached_export_wav(renderer, output, gtracker::render_identity(score, options), stream, policy);
        };
        const auto first = run(pattern, cache);
        check(first.status == "miss" && first.render.render_ms > 0, "first cache load was not generated");
        const auto original = bytes(output);
        check(gtracker::sha256_file(output) == gtracker::sha256(original), "streaming file SHA-256 differs from memory digest");
        for (std::size_t step : {1U, 7U, 63U, 64U, 65U, 997U, 65536U}) {
            gtracker::Sha256 hash;
            for (std::size_t offset = 0; offset < original.size(); offset += step) {
                hash.update(std::string_view(original).substr(offset, std::min(step, original.size() - offset)));
                const auto checkpoint = hash.digest();
                check(checkpoint.size() == 64, "intermediate digest consumed its state");
            }
            check(hash.digest() == gtracker::sha256(original), "incremental SHA-256 block partition changed its digest");
        }
        auto fresh = gtracker::pattern_renderer(pattern, options);
        const auto hit = gtracker::cached_export_wav(fresh, output, first.key, {}, cache);
        check(hit.status == "hit" && hit.render.render_ms == 0 && fresh.position() == 0 && hit.render.audio_buffer_bytes == 0,
              "cache hit still generated audio");
        check(bytes(output) == original && hit.render.peak == first.render.peak && hit.render.rms == first.render.rms,
              "cache hit changed PCM or checked float metrics");
        auto base = cache.directory / "gtracker-v1" / first.key.substr(0, 2) / first.key;
        auto wav = base; wav += ".wav"; auto receipt = base; receipt += ".json";
        mutate(wav, 100);
        check(run(pattern, cache).status == "rebuilt" && bytes(output) == original, "damaged PCM was reused");
        mutate(wav, 0);
        check(run(pattern, cache).status == "rebuilt", "wrong cached WAV header was reused");
        text(receipt, "{broken receipt");
        check(run(pattern, cache).status == "rebuilt", "damaged receipt was reused");
        text(receipt, std::string(1000, '[') + "0" + std::string(1000, ']'));
        check(run(pattern, cache).status == "rebuilt", "deep receipt did not recover safely");
        auto read_only = cache; read_only.read_only = true;
        const auto saved_receipt = bytes(receipt), saved_wav = bytes(wav);
        check(run(pattern, read_only).status == "hit", "read-only valid cache did not reuse");
        auto changed = pattern; changed.seed += 1;
        check(run(changed, read_only).status == "read_only_miss", "read-only miss did not render independently");
        check(bytes(receipt) == saved_receipt && bytes(wav) == saved_wav, "read-only miss modified existing entries");
        text(receipt, "broken"); const auto damaged = bytes(receipt);
        check(run(pattern, read_only).status == "read_only_invalid" && bytes(receipt) == damaged, "read-only recovery rewrote the cache");
        check(run(pattern, cache).status == "rebuilt", "writable recovery failed after read-only rejection");
        auto refresh = cache; refresh.refresh = true;
        check(run(pattern, refresh).status == "refreshed" && bytes(output) == original, "explicit refresh changed output");
        auto unavailable = cache; unavailable.directory = directory / "not-a-directory";
        text(unavailable.directory, "unrelated owned fixture");
        check(run(pattern, unavailable).status == "store_failed" && bytes(output) == original && bytes(unavailable.directory) == "unrelated owned fixture",
              "unavailable cache prevented or corrupted a successful output");
        bool changed_during_copy = false;
        auto racing = cache;
        racing.progress = [&](const gtracker::CacheProgress& p) {
            if (!changed_during_copy && p.phase == "copy_hit" && p.bytes == 0) { mutate(wav, 100); changed_during_copy = true; }
        };
        check(run(pattern, racing).status == "rebuilt" && changed_during_copy && bytes(output) == original,
              "PCM changed between validation/copy was published");
        for (const std::string phase : {"lookup_hash", "copy_hit", "hash_render", "store"}) {
            bool cancel = false;
            auto score = pattern;
            if (phase == "hash_render" || phase == "store") score.seed += phase == "store" ? 10 : 20;
            gtracker::StreamOptions stream; stream.cancelled = [&] { return cancel; };
            auto policy = cache; policy.progress = [&](const gtracker::CacheProgress& p) { if (p.phase == phase && p.bytes > 0) cancel = true; };
            text(output, "previous completed output");
            bool cancelled = false;
            try { run(score, policy, stream); } catch (const gtracker::RenderCancelled&) { cancelled = true; }
            check(cancelled && bytes(output) == "previous completed output", "cache cancellation replaced the caller's output");
        }
        auto failing = cache;
        failing.progress = [](const gtracker::CacheProgress& p) { if (p.phase == "store") throw std::runtime_error("injected callback failure"); };
        changed = pattern; changed.seed += 40;
        rejects([&] { run(changed, failing); }, "cache callback failure was treated as successful output");
        check(bytes(output) == "previous completed output", "callback failure published audio");
        changed = pattern; changed.seed += 50;
        auto complete_renderer = gtracker::pattern_renderer(changed, options);
        const auto complete_key = gtracker::render_identity(changed, options);
        auto complete_receipt = cache.directory / "gtracker-v1" / complete_key.substr(0, 2) / (complete_key + ".json");
        gtracker::StreamOptions after_store;
        after_store.cancelled = [&] { return std::filesystem::exists(complete_receipt); };
        bool cancelled_after_store = false;
        try { gtracker::cached_export_wav(complete_renderer, output, complete_key, after_store, cache); }
        catch (const gtracker::RenderCancelled&) { cancelled_after_store = true; }
        check(cancelled_after_store && bytes(output) == "previous completed output" && std::filesystem::exists(complete_receipt),
              "late cancellation published the output or lost the completed cache transaction");
        check(run(changed, cache).status == "hit", "complete entry left by late cancellation could not be reused");
        auto conflicting = cache; conflicting.read_only = true; conflicting.refresh = true;
        rejects([&] { run(pattern, conflicting); }, "conflicting cache modes accepted");
        gtracker::StreamOptions crop; crop.start_frame = 1234; crop.end_frame = 23001;
        const auto cropped = run(pattern, cache, crop);
        const auto cropped_pcm = bytes(output);
        const auto reused = run(pattern, cache, crop);
        check(cropped.key != first.key && reused.status == "hit" && reused.render.start_frame == 1234 && reused.render.warmup_frames == 0,
              "preview cache omitted crop identity or claimed new warm-up");
        check(bytes(output) == cropped_pcm, "preview cache changed PCM");
        for (const auto& path : std::filesystem::recursive_directory_iterator(directory))
            check(path.path().filename().u8string().find(".tmp-") == std::string::npos && path.path().filename().u8string().find(".cache-stage-") == std::string::npos,
                  "cache leaked handled temporary work");
        check(std::filesystem::weakly_canonical(directory).parent_path() == std::filesystem::current_path(), "cache test cleanup escaped its build directory");
        std::filesystem::remove_all(directory);
        std::cout << "render cache: checksum/build identity, hits, corruption/races, read-only, failure and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
