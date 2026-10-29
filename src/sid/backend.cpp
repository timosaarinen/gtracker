#include "gtracker/core.h"
#include "gtracker/hash.hpp"
#include "gtracker/io.hpp"
#include "gtracker/sid_backend.hpp"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gtracker::sid {
namespace {
using Bytes = std::vector<unsigned char>;
Bytes read(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("SID: cannot read " + p.u8string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void write(const std::filesystem::path &p, const Bytes &b) {
    write_binary_atomic(p, [&](std::ostream &out) {
        out.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
    });
}
int be(const Bytes &b, std::size_t p) { return b.at(p) * 256 + b.at(p + 1); }
struct Header {
    int load, init, play, offset;
};
Header header(const Bytes &b) {
    if (b.size() < 126 || std::string(b.begin(), b.begin() + 4) != "PSID" || be(b, 4) != 2 ||
        be(b, 6) != 124 || be(b, 14) != 1 || be(b, 16) != 1)
        throw std::runtime_error("SID: invalid single-subtune PSID v2 header");
    Header h{be(b, 8), be(b, 10), be(b, 12), 124};
    if (!h.load) {
        h.load = b[124] + 256 * b[125];
        h.offset += 2;
    }
    const auto end = h.load + static_cast<int>(b.size()) - h.offset;
    if (h.load < 0x1000 || end > 0xa000 || h.init < h.load || h.init >= end || h.play < h.load ||
        h.play >= end)
        throw std::runtime_error("SID: music memory map outside $1000..$9fff");
    if ((b[119] & 0x3c) != 0x14)
        throw std::runtime_error("SID: PAL/6581 flags missing");
    return h;
}
} // namespace
void run_process(const std::filesystem::path &exe, const std::vector<std::string> &args,
                 const std::filesystem::path &cwd, const std::atomic<bool> &cancel) {
    if (cancel)
        throw std::runtime_error("SID job cancelled");
    if (!std::filesystem::is_regular_file(exe))
        throw std::runtime_error("SID backend missing: " + exe.u8string() +
                                 ". Run python tools/sid_backend.py prepare.");
    const auto log = cwd / "process.log";
#ifdef _WIN32
    auto quote = [](const std::wstring &s) {
        std::wstring q = L"\"";
        int slashes = 0;
        for (wchar_t ch : s) {
            if (ch == L'\\') {
                ++slashes;
                continue;
            }
            if (ch == L'\"')
                q.append(static_cast<std::size_t>(slashes * 2 + 1), L'\\');
            else
                q.append(static_cast<std::size_t>(slashes), L'\\');
            slashes = 0;
            q += ch;
        }
        q.append(static_cast<std::size_t>(slashes * 2), L'\\');
        return q + L"\"";
    };
    std::wstring command = quote(exe.wstring());
    for (const auto &a : args)
        command += L" " + quote(std::filesystem::u8path(a).wstring());
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = output;
    si.hStdError = output;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const bool ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, cwd.c_str(), &si, &pi) != 0;
    if (output != INVALID_HANDLE_VALUE)
        CloseHandle(output);
    if (!ok)
        throw std::runtime_error("SID backend launch failed: " + std::to_string(GetLastError()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (WaitForSingleObject(pi.hProcess, 20) == WAIT_TIMEOUT)
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            throw std::runtime_error("SID job cancelled or timed out");
        }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    std::vector<std::string> strings{exe.string()};
    strings.insert(strings.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (auto &a : strings)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error("SID fork failed");
    if (pid == 0) {
        chdir(cwd.c_str());
        int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    for (;;) {
        const auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
            break;
        if (waited < 0 && errno != EINTR)
            throw std::runtime_error("SID child wait failed");
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            throw std::runtime_error("SID job cancelled or timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    if (code != 0) {
        const auto bytes = read(log);
        throw std::runtime_error("SID backend failed: " +
                                 std::string(bytes.begin(), bytes.end()).substr(0, 4096));
    }
}
std::string audition_identity(const Backend &backend) {
    auto song = new_song();
    song.patterns[0].voices[0] = {{0, EventType::note, 60, 1}};
    return sha256(compile(song).bank_hash + sha256_file(backend.tools / "backend.json") +
                  "gtracker.sid.compiler.v1|serial-tables-adapter1|residfp3.1.1|PAL6581|delay0|ri|48kmono16|"
                  "fcurve.5|frange.5|cwa|of0|3.1seconds");
}
Result prepare(const Song &song, const Backend &backend, const std::atomic<bool> &cancel, bool render,
               bool loop, double limit_seconds) {
    const auto manifest_bytes = read(backend.tools / "backend.json");
    const auto manifest = nlohmann::json::parse(manifest_bytes);
    for (const auto &item : manifest.at("files").items()) {
        if (item.key().find('/') != std::string::npos || item.key().find('\\') != std::string::npos ||
            item.key() == "..")
            throw std::runtime_error("SID backend manifest contains unsafe path");
        if (sha256_file(backend.tools / std::filesystem::u8path(item.key())) !=
            item.value().get<std::string>())
            throw std::runtime_error("SID backend checksum mismatch: " + item.key());
    }
    const auto c = compile(song, loop);
    const double seconds = limit_seconds > 0
                               ? limit_seconds
                               : duration(song) + 3 + static_cast<double>(c.preroll_ticks) / frame_rate;
    if (render && seconds * 48000 > 32 * 1024 * 1024)
        throw std::runtime_error(
            "SID preview exceeds 32 million mono samples; shorten arrangement before rendering");
    std::filesystem::create_directories(backend.jobs);
    const auto job = unique_sibling_path(backend.jobs / "job", "sid");
    std::filesystem::create_directories(job);
    Result r;
    r.bank_hash = c.bank_hash;
    r.warnings = c.warnings;
    r.seconds = seconds;
    r.preroll_ticks = c.preroll_ticks;
    r.sid = job / "song.sid";
    r.wav = job / "song.wav";
    write(job / "song.sng", c.sng);
#ifdef _WIN32
    constexpr auto suffix = ".exe";
#else
    constexpr auto suffix = "";
#endif
    run_process(std::filesystem::absolute(backend.tools / (std::string("gt2reloc") + suffix)),
                {"song.sng", "song.sid", "-P", "-S1", "-B1", "-G440"}, job, cancel);
    const auto data = read(r.sid);
    const auto h = header(data);
    r.load = h.load;
    r.init = h.init;
    r.play = h.play;
    r.sid_hash = sha256_file(r.sid);
    if (render) {
        const auto cache_key =
            sha256(serialize(song) + r.sid_hash + r.bank_hash + sha256_file(backend.tools / "backend.json") +
                   gt_renderer_build_id() + std::to_string(seconds) + (loop ? "loop" : "finite"));
        const auto cache_dir = backend.jobs.parent_path() / "renders";
        const auto cache_wav = cache_dir / (cache_key + ".wav"),
                   cache_receipt = cache_dir / (cache_key + ".json");
        if (limit_seconds == 0 && std::filesystem::exists(cache_wav) &&
            std::filesystem::exists(cache_receipt)) {
            try {
                const auto receipt = nlohmann::json::parse(read(cache_receipt));
                if (receipt.at("wav_sha256") == sha256_file(cache_wav)) {
                    r.audio = load_pcm_wav(cache_wav);
                    if (r.audio.sample_rate == 48000 && r.audio.channels == 1 &&
                        r.audio.sample_width_bits == 16 &&
                        r.audio.frames == static_cast<std::uint64_t>(std::llround(seconds * 48000))) {
                        r.wav = cache_wav;
                        return r;
                    }
                }
            } catch (const std::exception &) { /* A corrupt cache is regenerated from exact SID. */
            }
        }
        const auto frames = static_cast<std::size_t>(std::llround(seconds * 48000));
        run_process(std::filesystem::absolute(backend.tools / (std::string("gtracker_sidrender") + suffix)),
                    {"song.sid", "song.wav", std::to_string(frames)}, job, cancel);
        if (cancel)
            throw std::runtime_error("SID job cancelled");
        r.audio = load_pcm_wav(r.wav);
        if (r.audio.sample_rate != 48000 || r.audio.channels != 1 || r.audio.sample_width_bits != 16 ||
            r.audio.frames != frames)
            throw std::runtime_error("SID renderer returned unexpected PCM format or length");
        if (limit_seconds == 0 && !cancel) {
            std::filesystem::create_directories(cache_dir);
            write(cache_wav, read(r.wav));
            const auto receipt = nlohmann::json{
                {"identity", cache_key},
                {"sid_sha256", r.sid_hash},
                {"wav_sha256", sha256_file(cache_wav)},
                {"frames", frames}}.dump(2);
            write_binary_atomic(cache_receipt, [&](std::ostream &out) { out << receipt; });
        }
    }
    return r;
}
std::vector<unsigned char> runnable_prg(const Bytes &sid) {
    const auto h = header(sid);
    // BASIC 10 SYS2061, launcher at $080d, music at $1000. Raster IRQ owns CIA/VIC.
    Bytes b = {0x01, 0x08, 0x0b, 0x08, 0x0a, 0x00, 0x9e, 0x32, 0x30, 0x36, 0x31, 0x00, 0x00, 0x00};
    auto emit = [&](std::initializer_list<int> bytes) {
        for (int v : bytes)
            b.push_back(static_cast<unsigned char>(v));
    };
    emit({0x78, 0xa9, 0x35, 0x85, 0x01, 0xa9, 0x7f, 0x8d, 0x0d, 0xdc, 0x8d, 0x0d, 0xdd, 0xad, 0x0d, 0xdc,
          0xad, 0x0d, 0xdd});
    emit({0xa9, 0, 0x20, h.init & 255, h.init >> 8});
    const auto irqPatch = b.size();
    emit({0xa9, 0, 0x8d, 0xfe, 0xff, 0xa9, 0, 0x8d, 0xff, 0xff});
    emit({0xa9, 0x1b, 0x8d, 0x11, 0xd0, 0xa9, 0x30, 0x8d, 0x12, 0xd0, 0xa9, 1, 0x8d, 0x19, 0xd0, 0x8d, 0x1a,
          0xd0, 0x58});
    const int idle = 0x0801 + static_cast<int>(b.size()) - 2;
    emit({0x4c, idle & 255, idle >> 8});
    const int irq = 0x0801 + static_cast<int>(b.size()) - 2;
    b[irqPatch + 1] = static_cast<unsigned char>(irq);
    b[irqPatch + 6] = static_cast<unsigned char>(irq >> 8);
    emit({0x48, 0x8a, 0x48, 0x98, 0x48, 0x20, h.play & 255, h.play >> 8, 0xa9, 1, 0x8d, 0x19, 0xd0, 0x68,
          0xa8, 0x68, 0xaa, 0x68, 0x40});
    if (b.size() > static_cast<std::size_t>(h.load - 0x0801 + 2))
        throw std::runtime_error("SID launcher overlaps music");
    b.resize(static_cast<std::size_t>(h.load - 0x0801 + 2), 0);
    b.insert(b.end(), sid.begin() + h.offset, sid.end());
    return b;
}
Result export_file(const Song &song, const Backend &backend, const std::filesystem::path &output,
                   const std::string &format, const std::atomic<bool> &cancel) {
    if (format == "sng") {
        const auto c = compile(song);
        write(output, c.sng);
        Result r;
        r.bank_hash = c.bank_hash;
        r.warnings = c.warnings;
        return r;
    }
    const auto r = prepare(song, backend, cancel, format == "wav");
    if (cancel)
        throw std::runtime_error("SID job cancelled");
    if (format == "sid")
        write(output, read(r.sid));
    else if (format == "prg")
        write(output, runnable_prg(read(r.sid)));
    else if (format == "wav")
        write(output, read(r.wav));
    else
        throw std::runtime_error("Unknown SID export format");
    return r;
}
} // namespace gtracker::sid
