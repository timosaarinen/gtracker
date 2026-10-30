#include "gtracker/sid_backend.hpp"
#include "gtracker/hash.hpp"
#include "gtracker/io.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace gtracker::sid {
namespace {
#ifdef _WIN32
constexpr auto executable_suffix = ".exe";
constexpr char path_separator = ';';
#else
constexpr auto executable_suffix = "";
constexpr char path_separator = ':';
#endif
std::filesystem::path ffmpeg_path(const Backend &backend) {
    if (const auto *override_path = std::getenv("GTRACKER_FFMPEG")) {
        const auto path = std::filesystem::u8path(override_path);
        if (std::filesystem::is_regular_file(path)) return std::filesystem::absolute(path);
        throw std::runtime_error("GTRACKER_FFMPEG must name an FFmpeg executable");
    }
    const auto name = std::string("ffmpeg") + executable_suffix;
    const auto downloaded=backend.jobs.parent_path().parent_path()/"tools/ffmpeg/bin"/name;
    if(std::filesystem::is_regular_file(downloaded))return std::filesystem::absolute(downloaded);
    if (std::filesystem::is_regular_file(backend.tools / name))
        return std::filesystem::absolute(backend.tools / name);
    if (const auto *path = std::getenv("PATH")) {
        std::istringstream entries(path);
        std::string directory;
        while (std::getline(entries, directory, path_separator)) {
            if (directory.size() >= 2 && directory.front() == '"' && directory.back() == '"')
                directory = directory.substr(1, directory.size() - 2);
            if (directory.empty()) continue;
            const auto candidate = std::filesystem::u8path(directory) / name;
            if (std::filesystem::is_regular_file(candidate)) return std::filesystem::absolute(candidate);
        }
    }
    throw std::runtime_error("MP4 export needs FFmpeg (libx264, AAC, drawtext, showwaves, showfreqs). "
                             "Choose More > Set up MP4 export, or install FFmpeg on PATH.");
}
std::filesystem::path video_font() {
    if (const auto *font = std::getenv("GTRACKER_VIDEO_FONT")) {
        const auto path = std::filesystem::u8path(font);
        if (std::filesystem::is_regular_file(path)) return path;
        throw std::runtime_error("GTRACKER_VIDEO_FONT must name a TrueType/OpenType font");
    }
#ifdef _WIN32
    if (const auto *windows = std::getenv("WINDIR")) {
        const auto path = std::filesystem::u8path(windows) / "Fonts" / "segoeuib.ttf";
        if (std::filesystem::is_regular_file(path)) return path;
    }
#endif
    for (const auto *path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/System/Library/Fonts/Supplemental/Arial.ttf"})
        if (std::filesystem::is_regular_file(path)) return path;
    throw std::runtime_error("MP4 title font unavailable; set GTRACKER_VIDEO_FONT to a Unicode TTF font");
}
void text_file(const std::filesystem::path &path, const std::string &text) {
    write_binary_atomic(path, [&](std::ostream &out) { out << text; });
}
// Break at UTF-8 character boundaries. Text never enters the filter language and
// expansion=none also preserves literal %, backslashes, quotes and %{...}.
std::string wrap(const std::string &text, int columns) {
    std::string result;
    int column = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xc0) != 0x80) {
            if (column == columns) { result += '\n'; column = 0; }
            ++column;
        }
        result += ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : static_cast<char>(ch);
    }
    return result;
}
std::string graph() {
    // Logarithmic FFT of the actual mix fills the canvas. A translucent grid,
    // three separately rendered voices and a fixed title sit in front of it.
    // Pad visual inputs so FFT/window EOF cannot stall framesync in FFmpeg 9.
    // The output duration bounds these branches; the soundtrack is unpadded.
    std::string g =
        "[0:a]apad,highpass=f=20,showfreqs=s=1920x1080:mode=bar:ascale=log:fscale=log:"
        "win_size=4096:averaging=3:colors=0x19657c,fps=30,format=rgba,"
        "drawgrid=w=120:h=90:t=1:c=0x71b5c8@0.10,"
        "drawbox=x=0:y=0:w=iw:h=280:c=0x060d18@0.92:t=fill,"
        "drawbox=x=48:y=322:w=1824:h=190:c=0x06121e@0.70:t=fill,"
        "drawbox=x=48:y=542:w=1824:h=190:c=0x06121e@0.70:t=fill,"
        "drawbox=x=48:y=762:w=1824:h=190:c=0x06121e@0.70:t=fill[base];";
    const std::array<const char *, 3> colors{"0x58efff", "0xffba69", "0xc5a0ff"};
    for (int voice = 0; voice < 3; ++voice) {
        const auto n = std::to_string(voice);
        const auto input = std::to_string(voice + 1);
        g += "[" + input + ":a]apad,highpass=f=20,volume=3,showwaves=s=1600x160:mode=p2p:"
             "rate=30:draw=full:colors=" + colors[static_cast<std::size_t>(voice)] +
             ",format=rgba,colorkey=black:0.08:0.1,split[sharp" + n + "][blur" + n + "];";
        g += "[blur" + n + "]gblur=sigma=5[glow" + n + "];";
        const auto previous = voice == 0 ? std::string("base") : "scope" + std::to_string(voice - 1);
        const auto y = std::to_string(340 + voice * 220);
        g += "[" + previous + "][glow" + n + "]overlay=x=248:y=" + y + ":shortest=1[lit" + n + "];";
        g += "[lit" + n + "][sharp" + n + "]overlay=x=248:y=" + y + ":shortest=1[scope" + n + "];";
    }
    g += "[scope2][4:v]overlay=0:0:shortest=1,";
    for (int voice = 0; voice < 3; ++voice)
        g += "drawtext=fontfile=font.ttf:text='VOICE 0" + std::to_string(voice + 1) +
             "':fontcolor=" + colors[static_cast<std::size_t>(voice)] + ":fontsize=24:x=72:y=" +
             std::to_string(400 + voice * 220) + ",";
    g += "drawtext=fontfile=font.ttf:text='BASS':fontcolor=0x82c5d8:fontsize=20:x=64:y=1020,"
         "drawtext=fontfile=font.ttf:text='TREBLE':fontcolor=0x82c5d8:fontsize=20:x=w-tw-64:y=1020,"
         "drawtext=fontfile=font.ttf:text='PAL / 6581   |   gtracker':fontcolor=0x82c5d8:fontsize=20:"
         "x=(w-tw)/2:y=1020,format=yuv420p[out]";
    return g;
}
} // namespace

void setup_ffmpeg(const Backend &backend,const std::filesystem::path &script,const std::atomic<bool> &cancel) {
#ifdef _WIN32
    const auto* windows=std::getenv("WINDIR");
    if(!windows)throw std::runtime_error("Windows system directory is unavailable");
    if(!std::filesystem::is_regular_file(script))throw std::runtime_error("FFmpeg setup script is missing from resources");
    const auto powershell=std::filesystem::u8path(windows)/"System32/WindowsPowerShell/v1.0/powershell.exe";
    std::filesystem::create_directories(backend.jobs);
    const auto job=std::filesystem::absolute(unique_sibling_path(backend.jobs/"ffmpeg-setup","job"));
    std::filesystem::create_directories(job);
    try {
        run_process(powershell,{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",
            std::filesystem::absolute(script).u8string(),"-DataDir",std::filesystem::absolute(backend.jobs.parent_path().parent_path()).u8string(),
            "-WorkDir",job.u8string()},job,cancel);
    }catch(...){std::error_code ignored;std::filesystem::remove_all(job,ignored);throw;}
    std::error_code ignored;std::filesystem::remove_all(job,ignored);
#else
    (void)backend;(void)script;(void)cancel;
    throw std::runtime_error("Automatic FFmpeg setup currently supports Windows. Install FFmpeg on PATH.");
#endif
}

Result export_video(const Song &song, const Backend &backend, const std::filesystem::path &output,
                    const std::atomic<bool> &cancel) {
    if (cancel) throw std::runtime_error("SID job cancelled");
    const auto ffmpeg = ffmpeg_path(backend);
    const auto font = video_font();
    const auto helper_name = std::string("gtracker_sidvisual") + executable_suffix;
    const auto helper = std::filesystem::absolute(backend.tools / helper_name);
    std::ifstream receipt(backend.tools / "video.json");
    if (!receipt) throw std::runtime_error("MP4 voice renderer missing; run python tools/sid_backend.py prepare-video");
    const auto manifest = nlohmann::json::parse(receipt);
    if (manifest.at("version") != 1 || manifest.at("files").at(helper_name) != sha256_file(helper))
        throw std::runtime_error("MP4 voice renderer checksum mismatch; run tools/sid_backend.py prepare-video");
    auto result = prepare(song, backend, cancel, true);
    const auto job = result.sid.parent_path();
    const auto frames = std::to_string(result.audio.frames);
    for (int voice = 1; voice <= 3; ++voice) {
        const auto filename = "voice" + std::to_string(voice) + ".wav";
        run_process(helper, {"song.sid", filename, frames, std::to_string(voice)}, job, cancel);
        const auto audio = load_pcm_wav(job / filename);
        if (audio.sample_rate != 48000 || audio.channels != 1 || audio.frames != result.audio.frames)
            throw std::runtime_error("MP4 voice renderer returned unexpected PCM");
    }
    std::filesystem::copy_file(font, job / "font.ttf");
    text_file(job / "title.txt", wrap(song.name.empty() ? "Untitled SID song" : song.name, 56));
    text_file(job / "author.txt", wrap(song.author, 100));
    text_file(job / "visualizer.ffgraph", graph());
    const std::string title_graph =
        "color=c=0x060d18:s=1920x280,drawtext=fontfile=font.ttf:textfile=title.txt:expansion=none:"
        "fontcolor=white:fontsize=30:line_spacing=5:x=64:y=24,"
        "drawtext=fontfile=font.ttf:textfile=author.txt:expansion=none:"
        "fontcolor=0x82c5d8:fontsize=16:line_spacing=3:x=64:y=216[out]";
    text_file(job / "title.ffgraph", title_graph);
    run_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-filter_complex", title_graph, "-map", "[out]", "-frames:v", "1", "header.png"}, job, cancel);
    const auto destination = std::filesystem::absolute(output);
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = unique_sibling_path(destination, "video");
    try {
        run_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", std::filesystem::absolute(result.wav).u8string(),
                    "-i", "voice1.wav", "-i", "voice2.wav", "-i", "voice3.wav", "-loop", "1",
                    "-t", std::to_string(result.seconds), "-i", "header.png",
                    "-filter_complex_threads", "2", "-filter_complex", graph(),
                    "-map", "[out]", "-map", "0:a", "-t", std::to_string(result.seconds),
                    "-c:v", "libx264", "-preset", "veryfast", "-crf", "22", "-maxrate", "4500k",
                    "-bufsize", "9000k", "-pix_fmt", "yuv420p", "-r", "30",
                    "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
                    "-movflags", "+faststart", "-f", "mp4", temporary.u8string()}, job, cancel);
        if (cancel) throw std::runtime_error("SID job cancelled");
        publish_binary_file(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
    return result;
}
} // namespace gtracker::sid
