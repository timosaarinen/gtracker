#pragma once
#include "gtracker/audio_source.hpp"
#include "gtracker/sid_song.hpp"
#include <atomic>
#include <functional>

namespace gtracker::sid {
struct Compilation {
    std::vector<unsigned char> sng;
    std::string bank_hash;
    std::vector<std::string> warnings;
    // Player initialization + initial pattern fetch, measured against register traces.
    int preroll_ticks = 3;
    std::size_t backend_patterns = 0;
};
Compilation compile(const Song &song, bool loop = false);
struct Backend {
    std::filesystem::path tools, jobs;
};
std::string audition_identity(const Backend &backend);
struct Result {
    std::filesystem::path sid, wav;
    PcmAudio audio;
    std::string sid_hash, bank_hash;
    int load = 0, init = 0, play = 0;
    double seconds = 0;
    int preroll_ticks = 3;
    std::vector<std::string> warnings;
};
void run_process(const std::filesystem::path &executable, const std::vector<std::string> &args,
                 const std::filesystem::path &cwd, const std::atomic<bool> &cancel);
Result prepare(const Song &song, const Backend &backend, const std::atomic<bool> &cancel, bool render = true,
               bool loop = false, double limit_seconds = 0);
Result export_file(const Song &song, const Backend &backend, const std::filesystem::path &output,
                   const std::string &format, const std::atomic<bool> &cancel);
void setup_ffmpeg(const Backend &backend, const std::filesystem::path &script, const std::atomic<bool> &cancel);
Result export_video(const Song &song, const Backend &backend, const std::filesystem::path &output,
                    const std::atomic<bool> &cancel);
std::vector<unsigned char> runnable_prg(const std::vector<unsigned char> &sid);
} // namespace gtracker::sid
