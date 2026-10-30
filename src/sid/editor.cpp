#include "gtracker/hash.hpp"
#include "gtracker/io.hpp"
#include "gtracker/sid_backend.hpp"
#include "gtracker/sid_document.hpp"
#include "gtracker/sid_editor.hpp"
#include "gtracker/ui_scale.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <future>
#include <imgui.h>
#include <optional>
#include <stdexcept>

namespace gtracker::sid {
namespace {
struct Transport {
    SDL_AudioStream *stream = nullptr;
    std::vector<float> pcm;
    std::size_t cursor = 0, loop_start = 0, loop_end = 0;
    int latency = 512;
    bool active = false, paused = false;
    ~Transport() {
        if (stream)
            SDL_DestroyAudioStream(stream);
    }
    static void SDLCALL feed(void *userdata, SDL_AudioStream *output, int additional, int) {
        auto &t = *static_cast<Transport *>(userdata);
        std::size_t count = static_cast<std::size_t>(std::max(0, additional)) / sizeof(float);
        while (count && t.cursor < t.pcm.size()) {
            const auto end = t.loop_end ? t.loop_end : t.pcm.size();
            const auto n = std::min(count, end - t.cursor);
            SDL_PutAudioStreamData(output, t.pcm.data() + t.cursor, static_cast<int>(n * sizeof(float)));
            count -= n;
            t.cursor += n;
            if (t.loop_end && t.cursor == end)
                t.cursor = t.loop_start;
            if (!n)
                break;
        }
    }
    void stop() {
        if (stream) {
            SDL_PauseAudioStreamDevice(stream);
            SDL_LockAudioStream(stream);
            active = false;
            paused = false;
            cursor = 0;
            SDL_ClearAudioStream(stream);
            SDL_UnlockAudioStream(stream);
        }
    }
    void play(std::vector<float> data, std::size_t start = 0, std::size_t ls = 0, std::size_t le = 0) {
        stop();
        if (!stream) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
            if (!stream)
                throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec device{};
            SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, &latency);
            if (device.freq > 0)
                latency = latency * 48000 / device.freq;
        }
        SDL_LockAudioStream(stream);
        pcm = std::move(data);
        cursor = std::min(start, pcm.size());
        loop_start = ls;
        loop_end = le;
        active = true;
        paused = false;
        SDL_UnlockAudioStream(stream);
        SDL_ResumeAudioStreamDevice(stream);
    }
    void toggle() {
        if (!stream || !active)
            return;
        paused = !paused;
        if (paused)
            SDL_PauseAudioStreamDevice(stream);
        else
            SDL_ResumeAudioStreamDevice(stream);
    }
    std::size_t position() {
        if (!stream)
            return 0;
        SDL_LockAudioStream(stream);
        auto p = cursor;
        SDL_UnlockAudioStream(stream);
        return p > static_cast<std::size_t>(latency) ? p - static_cast<std::size_t>(latency) : 0;
    }
};
struct Occurrence {
    int section, index, pattern;
};
std::vector<Occurrence> order(const Song &s) {
    std::vector<Occurrence> out;
    for (std::size_t i = 0; i < s.sections.size(); ++i)
        for (std::size_t j = 0; j < s.sections[i].order.size(); ++j) {
            const auto &id = s.sections[i].order[j].pattern_id;
            for (std::size_t p = 0; p < s.patterns.size(); ++p)
                if (s.patterns[p].id == id)
                    out.push_back({static_cast<int>(i), static_cast<int>(j), static_cast<int>(p)});
        }
    return out;
}
void buffer(char *out, std::size_t size, const std::string &value) {
    std::snprintf(out, size, "%s", value.c_str());
}
} // namespace
struct Editor::Impl {
    Document doc;
    Backend backend;
    std::filesystem::path resources, data, path, recovery;
    char filename[1024]{}, title[257]{}, author[257]{}, rename[257]{};
    std::string status = "Edit enabled. Select a voice and play the piano keys.";
    Cursor cursor, anchor;
    int pattern = 0, occurrence = 0, section = 0, preset = 1, octave = 4, step = 1;
    bool edit = true, follow = true, close = false, guitar = false, ask_close = false, ask_open = false,
         show_file = false, confirm_replace = false;
    std::string file_action = "save", export_format = "sid";
    std::string pending_action;
    bool selection = false, focus_grid = true;
    bool reveal_cursor = false;
    std::vector<std::vector<std::optional<Event>>> clipboard;
    Transport transport;
    std::future<Result> job;
    std::atomic<bool> cancel{false};
    std::size_t job_generation = 0;
    enum class Purpose { song, loop, audition, output };
    Purpose purpose = Purpose::song;
    std::optional<Result> prepared;
    std::size_t prepared_generation = 0;
    int pending_audition = -1, pending_preset = 1;
    std::size_t seek_frame = 0, loop_first = 0, loop_last = 0;
    bool playing_loop = false, playing_audition = false;
    int playback_order = 0;
    double last_recovery = 0;
    std::string recovery_bytes;
    Impl(const std::filesystem::path &source, const std::filesystem::path &resource,
         const std::filesystem::path &userdata)
        : resources(resource), data(userdata) {
        if (data.empty())
            data = std::filesystem::temp_directory_path() / "gtracker-sid";
        std::filesystem::create_directories(data / "sid-cache");
        backend = {resources / "sid-tools", data / "sid-cache/jobs"};
        if (!std::filesystem::exists(backend.tools))
            backend.tools = ".local/sid/tools";
        if (!source.empty()) {
            doc.opened(load(source));
            path = source;
        }
        buffer(title, sizeof(title), doc.song.name);
        buffer(author, sizeof(author), doc.song.author);
        buffer(filename, sizeof(filename),
               path.empty() ? (data / "Untitled.sidtrk.json").u8string() : path.u8string());
        recovery = unique_sibling_path(data / "recovery.sidtrk.json", "sid");
        anchor = cursor;
    }
    ~Impl() {
        cancel = true;
        if (job.valid())
            job.wait();
        transport.stop();
    }
    bool busy() const { return job.valid(); }
    void stop() {
        transport.stop();
        cancel = true;
        pending_audition = -1;
        playing_loop = false;
        playing_audition = false;
    }
    template <class F> void attempt(F f) {
        try {
            f();
        } catch (const std::exception &e) {
            status = e.what();
        }
    }
    template <class F> void mutate(F f) {
        stop();
        try {
            doc.change(f);
        } catch (...) {
            clamp();
            throw;
        }
        prepared.reset();
        clamp();
    }
    void clamp() {
        pattern = std::clamp(pattern, 0, std::max(0, static_cast<int>(doc.song.patterns.size()) - 1));
        section = std::clamp(section, 0, std::max(0, static_cast<int>(doc.song.sections.size()) - 1));
        occurrence = std::clamp(occurrence, 0, std::max(0, static_cast<int>(order(doc.song).size()) - 1));
    }
    Pattern &current() {
        if (doc.song.patterns.empty())
            throw std::runtime_error("Add a pattern first");
        return doc.song.patterns.at(static_cast<std::size_t>(pattern));
    }
    void bounds(int &r0, int &r1, int &v0, int &v1) const {
        r0 = selection ? std::min(anchor.row, cursor.row) : cursor.row;
        r1 = selection ? std::max(anchor.row, cursor.row) : cursor.row;
        v0 = selection ? std::min(anchor.voice, cursor.voice) : cursor.voice;
        v1 = selection ? std::max(anchor.voice, cursor.voice) : cursor.voice;
    }
    void copy() {
        int r0, r1, v0, v1;
        bounds(r0, r1, v0, v1);
        clipboard.clear();
        for (int r = r0; r <= r1; ++r) {
            std::vector<std::optional<Event>> line;
            for (int v = v0; v <= v1; ++v)
                line.push_back(cell(current(), r, v));
            clipboard.push_back(line);
        }
        status = "Selection copied.";
    }
    void clear() {
        int r0, r1, v0, v1;
        bounds(r0, r1, v0, v1);
        mutate([&](Song &s) {
            for (int r = r0; r <= r1; ++r)
                for (int v = v0; v <= v1; ++v)
                    put(s.patterns[static_cast<std::size_t>(pattern)], r, v, {});
        });
    }
    void paste() {
        if (clipboard.empty())
            return;
        mutate([&](Song &s) {
            auto &p = s.patterns[static_cast<std::size_t>(pattern)];
            for (std::size_t r = 0; r < clipboard.size() && cursor.row + static_cast<int>(r) < 64; ++r)
                for (std::size_t v = 0; v < clipboard[r].size() && cursor.voice + static_cast<int>(v) < 3;
                     ++v)
                    put(p, cursor.row + static_cast<int>(r), cursor.voice + static_cast<int>(v),
                        clipboard[r][v]);
        });
        status = "Pasted within current pattern; excess rows/voices omitted.";
    }
    void shift(int n) {
        int r0, r1, v0, v1;
        bounds(r0, r1, v0, v1);
        mutate([&](Song &s) { transpose(s.patterns[static_cast<std::size_t>(pattern)], r0, r1, v0, v1, n); });
    }
    void apply() {
        int r0, r1, v0, v1;
        bounds(r0, r1, v0, v1);
        mutate([&](Song &s) {
            auto &p = s.patterns[static_cast<std::size_t>(pattern)];
            for (int r = r0; r <= r1; ++r)
                for (int v = v0; v <= v1; ++v) {
                    auto e = cell(p, r, v);
                    if (e && e->type == EventType::note) {
                        e->instrument = preset;
                        if (presets()[static_cast<std::size_t>(preset - 1)].drum)
                            e->note = 60;
                        put(p, r, v, e);
                    }
                }
        });
    }
    void save_to(const std::filesystem::path &target) {
        if (target.empty())
            throw std::runtime_error("Choose a filename");
        save(doc.song, target);
        path = target;
        doc.mark_saved();
        status = "Saved " + target.u8string();
    }
    void resolve_pending() {
        const auto action = pending_action;
        pending_action.clear();
        stop();
        if (action == "open")
            file("open");
        else if (action == "close")
            close = true;
        else if (action == "guitar")
            guitar = true;
        else if (action == "new") {
            doc.opened(new_song());
            path.clear();
            prepared.reset();
            pattern = section = occurrence = 0;
            buffer(title, sizeof(title), doc.song.name);
            buffer(author, sizeof(author), doc.song.author);
        }
    }
    void request(const std::string &action) {
        pending_action = action;
        if (doc.dirty())
            ImGui::OpenPopup("Unsaved SID song");
        else
            resolve_pending();
    }
    void file(const std::string &action) {
        file_action = action;
        show_file = true;
        confirm_replace = false;
        if (action == "export") {
            auto p = data / ((doc.song.name.empty() ? "song" : std::string("song")) + "." + export_format);
            buffer(filename, sizeof(filename), p.u8string());
        } else
            buffer(filename, sizeof(filename),
                   path.empty() ? (data / "Untitled.sidtrk.json").u8string() : path.u8string());
    }
    void start(bool loop, bool beginning = false) {
        if (busy()) {
            cancel = true;
            status = "Cancelling previous job; press Play again when ready.";
            return;
        }
        transport.stop();
        playing_audition = false;
        Song snapshot = doc.song;
        playing_loop = loop;
        seek_frame = 0;
        loop_first = loop_last = 0;
        if (loop) {
            const auto p = current();
            snapshot.patterns = {p};
            snapshot.sections = {
                {"loop", "Pattern audition", {{"l1", p.id}, {"l2", p.id}, {"l3", p.id}, {"l4", p.id}}}};
            const double frames = 64 * snapshot.speed / frame_rate * 48000;
            loop_first = static_cast<std::size_t>(
                std::llround(((snapshot.speed + 3) / frame_rate) * 48000 + frames * 2));
            loop_last = static_cast<std::size_t>(
                std::llround(((snapshot.speed + 3) / frame_rate) * 48000 + frames * 3));
        } else if (!beginning) {
            const auto o = order(snapshot);
            if (o.empty() || occurrence >= static_cast<int>(o.size()) ||
                o[static_cast<std::size_t>(occurrence)].pattern != pattern)
                throw std::runtime_error("Selected pattern is not this order occurrence. Select it in the "
                                         "order, or use Loop Pattern.");
            seek_frame = static_cast<std::size_t>(
                std::llround((snapshot.speed + 3 + ((occurrence * 64 + cursor.row) * snapshot.speed)) /
                             frame_rate * 48000));
        }
        if (!loop && prepared && prepared_generation == doc.generation) {
            transport.play(prepared->audio.interleaved, seek_frame);
            status = "Playing prepared song.";
            return;
        }
        cancel = false;
        job_generation = doc.generation;
        purpose = loop ? Purpose::loop : Purpose::song;
        job = std::async(std::launch::async,
                         [this, snapshot, loop] { return prepare(snapshot, backend, cancel, true, loop); });
        status = "Compiling and rendering exact SID in background...";
    }
    void audition(int note) {
        pending_audition = note;
        pending_preset = preset;
        if (busy()) {
            cancel = true;
            return;
        }
        const auto bank = compile_audition(note, preset);
        const auto identity = audition_identity(backend);
        const auto name = std::to_string(preset) + "-" + std::to_string(note) + ".wav";
        auto cached = resources / "sid-auditions" / identity / name;
        if (!std::filesystem::exists(cached))
            cached = resources / ".local/sid/auditions" / identity / name;
        if (!std::filesystem::exists(cached))
            cached = data / "sid-cache" / identity / name;
        if (std::filesystem::exists(cached)) {
            const auto start = std::chrono::steady_clock::now();
            auto audio = load_pcm_wav(cached);
            transport.play(std::move(audio.interleaved), static_cast<std::size_t>(7 / frame_rate * 48000));
            status = "Cached audition queued in " +
                     std::to_string(
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                             .count()) +
                     " ms";
            pending_audition = -1;
            playing_audition = true;
            return;
        }
        cancel = false;
        job_generation = doc.generation;
        purpose = Purpose::audition;
        job = std::async(std::launch::async, [this, bank, cached] {
            auto r = prepare(bank, backend, cancel, true, false, 3.1);
            std::filesystem::create_directories(cached.parent_path());
            if (!cancel)
                write_binary_atomic(cached, [&](std::ostream &out) {
                    std::ifstream input(r.wav, std::ios::binary);
                    out << input.rdbuf();
                });
            return r;
        });
        status = "Preparing missing note audition in background...";
    }
    static Song compile_audition(int note, int instrument) {
        auto s = new_song();
        s.speed = 4;
        s.patterns.resize(1);
        s.sections.resize(1);
        s.patterns[0].voices[0] = {{0, EventType::note, note, instrument}, {3, EventType::off, 60, 1}};
        return s;
    }
    void note(int pitch) {
        if (pitch < 24 || pitch > 95) {
            status = "Pitch outside supported C1..B6";
            return;
        }
        if (presets()[static_cast<std::size_t>(preset - 1)].drum)
            pitch = 60;
        if (edit) {
            mutate([&](Song &s) {
                put(s.patterns[static_cast<std::size_t>(pattern)], cursor.row, cursor.voice,
                    Event{cursor.row, EventType::note, pitch, preset});
            });
            cursor.row = (cursor.row + step) % 64;
            reveal_cursor = true;
            anchor = cursor;
            selection = false;
        }
        audition(pitch);
    }
    void output(const std::filesystem::path &target) {
        if (busy())
            throw std::runtime_error("Wait for the current job or cancel it first");
        require_distinct_output(target, path.empty() ? std::vector<std::filesystem::path>{}
                                                     : std::vector<std::filesystem::path>{path});
        const auto snapshot = doc.song;
        const auto format = export_format;
        cancel = false;
        job_generation = doc.generation;
        purpose = Purpose::output;
        job = std::async(std::launch::async, [this, snapshot, target, format] {
            export_file(snapshot, backend, target, format, cancel);
            return Result{};
        });
        status = "Exporting " + target.u8string();
    }
    void poll() {
        if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto r = job.get();
                if (cancel || job_generation != doc.generation) {
                    status = "Discarded cancelled or stale SID job.";
                } else if (purpose == Purpose::output) {
                    status = "Export completed.";
                } else if (purpose == Purpose::audition) {
                    transport.play(std::move(r.audio.interleaved),
                                   static_cast<std::size_t>(7 / frame_rate * 48000));
                    playing_audition = true;
                    status = "Audition ready.";
                    pending_audition = -1;
                } else {
                    if (purpose == Purpose::song) {
                        prepared = r;
                        prepared_generation = doc.generation;
                    }
                    if (purpose == Purpose::loop && loop_first >= 128 &&
                        loop_last <= r.audio.interleaved.size()) {
                        // Preserve the complete loop length; blend the final 128 samples
                        // toward the preceding context of its first sample (2.67 ms).
                        for (std::size_t i = 0; i < 128; ++i) {
                            const float t = static_cast<float>(i + 1) / 128;
                            auto &sample = r.audio.interleaved[loop_last - 128 + i];
                            sample = sample * (1 - t) + r.audio.interleaved[loop_first - 128 + i] * t;
                        }
                    }
                    transport.play(std::move(r.audio.interleaved), seek_frame, loop_first, loop_last);
                    status = purpose == Purpose::loop ? "Pattern audition: cold first pass, warmed repeat."
                                                      : "Playing exact exported SID.";
                }
            } catch (const std::exception &e) {
                status = e.what();
                pending_audition = -1;
            }
            if (pending_audition >= 0) {
                const auto note = pending_audition;
                preset = pending_preset;
                audition(note);
            }
        }
        const double now = ImGui::GetTime();
        if (now - last_recovery >= 5 && doc.dirty()) {
            last_recovery = now;
            const auto bytes = serialize(doc.song);
            if (bytes != recovery_bytes)
                attempt([&] {
                    save(doc.song, recovery);
                    recovery_bytes = bytes;
                });
        }
    }
    void navigation(int row, int voice, int column, bool extend) {
        reveal_cursor = true;
        follow = false;
        if (extend && !selection)
            anchor = cursor;
        selection = extend;
        cursor = {std::clamp(row, 0, 63), std::clamp(voice, 0, 2), std::clamp(column, 0, 1)};
        if (!extend)
            anchor = cursor;
    }
    void keyboard() {
        auto &io = ImGui::GetIO();
        if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return;
        const bool ctrl = io.KeyCtrl || io.KeySuper, shift = io.KeyShift;
        auto key = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };
        if (ctrl) {
            if (key(ImGuiKey_S)) {
                if (shift || path.empty())
                    file("save");
                else
                    save_to(path);
            }
            if (key(ImGuiKey_O)) {
                ask_open = true;
            }
            if (key(ImGuiKey_Z)) {
                stop();
                if (shift)
                    doc.redo();
                else
                    doc.undo();
                prepared.reset();
                clamp();
            }
            if (key(ImGuiKey_Y)) {
                stop();
                doc.redo();
                prepared.reset();
                clamp();
            }
            if (key(ImGuiKey_C))
                copy();
            if (key(ImGuiKey_X)) {
                copy();
                clear();
            }
            if (key(ImGuiKey_V))
                paste();
            if (key(ImGuiKey_1))
                step = 1;
            if (key(ImGuiKey_2))
                step = 2;
            if (key(ImGuiKey_4))
                step = 4;
            return;
        }
        if (key(ImGuiKey_Escape))
            edit = !edit;
        if (key(ImGuiKey_F8)) {
            stop();
            return;
        }
        if (key(ImGuiKey_F5))
            start(false, true);
        if (key(ImGuiKey_F6))
            start(true);
        if (key(ImGuiKey_F7))
            start(false);
        if (key(ImGuiKey_Space)) {
            if (transport.active)
                transport.toggle();
            else
                start(false);
        }
        if (key(ImGuiKey_KeypadDivide))
            octave = std::max(1, octave - 1);
        if (key(ImGuiKey_KeypadMultiply))
            octave = std::min(6, octave + 1);
        if (key(ImGuiKey_KeypadSubtract))
            preset = std::max(1, preset - 1);
        if (key(ImGuiKey_KeypadAdd))
            preset = std::min(11, preset + 1);
        if (!focus_grid || doc.song.patterns.empty())
            return;
        if (key(ImGuiKey_UpArrow, true))
            navigation(cursor.row - 1, cursor.voice, cursor.column, shift);
        if (key(ImGuiKey_DownArrow, true))
            navigation(cursor.row + 1, cursor.voice, cursor.column, shift);
        if (key(ImGuiKey_LeftArrow, true)) {
            int col = cursor.voice * 2 + cursor.column - 1;
            navigation(cursor.row, std::max(0, col) / 2, std::max(0, col) % 2, shift);
        }
        if (key(ImGuiKey_RightArrow, true)) {
            int col = std::min(5, cursor.voice * 2 + cursor.column + 1);
            navigation(cursor.row, col / 2, col % 2, shift);
        }
        if (key(ImGuiKey_Tab, true))
            navigation(cursor.row, (cursor.voice + (shift ? 2 : 1)) % 3, cursor.column, false);
        if (key(ImGuiKey_Home))
            navigation(0, cursor.voice, cursor.column, shift);
        if (key(ImGuiKey_End))
            navigation(63, cursor.voice, cursor.column, shift);
        if (key(ImGuiKey_PageUp, true))
            navigation(cursor.row - 16, cursor.voice, cursor.column, shift);
        if (key(ImGuiKey_PageDown, true))
            navigation(cursor.row + 16, cursor.voice, cursor.column, shift);
        if (edit && key(ImGuiKey_Delete))
            clear();
        if (cursor.column == 1) {
            for (int n = 0; n < 10; ++n)
                if (key(static_cast<ImGuiKey>(ImGuiKey_0 + n))) {
                    const auto old = cell(current(), cursor.row, cursor.voice);
                    if (old && old->type == EventType::note && edit) {
                        int next = (old->instrument % 10) * 10 + n;
                        if (next > 11 || next < 1)
                            next = n;
                        if (next >= 1 && next <= 11) {
                            preset = next;
                            apply();
                        }
                    }
                }
            return;
        }
        if (key(ImGuiKey_A) || key(ImGuiKey_CapsLock)) {
            if (edit) {
                mutate([&](Song &s) {
                    put(s.patterns[static_cast<std::size_t>(pattern)], cursor.row, cursor.voice,
                        Event{cursor.row, EventType::off, 60, 1});
                });
                cursor.row = (cursor.row + step) % 64;
                reveal_cursor = true;
            }
            return;
        }
        const ImGuiKey low[] = {ImGuiKey_Z, ImGuiKey_S, ImGuiKey_X, ImGuiKey_D, ImGuiKey_C, ImGuiKey_V,
                                ImGuiKey_G, ImGuiKey_B, ImGuiKey_H, ImGuiKey_N, ImGuiKey_J, ImGuiKey_M};
        const ImGuiKey high[] = {ImGuiKey_Q, ImGuiKey_2, ImGuiKey_W, ImGuiKey_3, ImGuiKey_E, ImGuiKey_R,
                                 ImGuiKey_5, ImGuiKey_T, ImGuiKey_6, ImGuiKey_Y, ImGuiKey_7, ImGuiKey_U};
        for (int i = 0; i < 12; ++i) {
            if (key(low[i]))
                note((octave + 1) * 12 + i);
            if (key(high[i]))
                note((octave + 2) * 12 + i);
        }
    }
    void arrangement() {
        ImGui::TextUnformatted("SONG ORDER");
        const auto flattened = order(doc.song);
        int index = 0;
        for (std::size_t s = 0; s < doc.song.sections.size(); ++s) {
            ImGui::PushID(static_cast<int>(s));
            if (ImGui::Selectable(doc.song.sections[s].name.c_str(), section == static_cast<int>(s))) {
                section = static_cast<int>(s);
                buffer(rename, sizeof(rename), doc.song.sections[s].name);
                focus_grid = false;
            }
            for (std::size_t o = 0; o < doc.song.sections[s].order.size(); ++o, ++index) {
                const auto &entry = doc.song.sections[s].order[o];
                const auto found = std::find_if(doc.song.patterns.begin(), doc.song.patterns.end(),
                                                [&](const Pattern &p) { return p.id == entry.pattern_id; });
                const auto label = std::to_string(index + 1) + "  " + found->name + "##" + entry.id;
                if (ImGui::Selectable(label.c_str(), occurrence == index)) {
                    occurrence = index;
                    pattern = static_cast<int>(found - doc.song.patterns.begin());
                    section = static_cast<int>(s);
                    focus_grid = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        auto add = [&](int mode) {
            mutate([&](Song &s) {
                if (s.sections.empty())
                    s.sections.push_back({fresh_id(s, "section"), "SECTION", {}});
                auto &sec = s.sections[static_cast<std::size_t>(section)];
                std::size_t at = sec.order.size();
                if (!flattened.empty()) {
                    const auto occ = flattened[static_cast<std::size_t>(occurrence)];
                    if (occ.section == section)
                        at = static_cast<std::size_t>(occ.index + 1);
                }
                std::string id;
                if (mode == 1) {
                    id = current().id;
                } else {
                    Pattern p;
                    if (mode == 2)
                        p = current();
                    p.id = fresh_id(s, "pattern");
                    p.name = mode == 2 ? p.name + " variation" : "New Pattern";
                    id = p.id;
                    s.patterns.push_back(p);
                    pattern = static_cast<int>(s.patterns.size()) - 1;
                }
                const auto oid = fresh_id(s, "order");
                sec.order.insert(sec.order.begin() + static_cast<std::ptrdiff_t>(at), {oid, id});
            });
        };
        if (ImGui::Button("New Pattern"))
            add(0);
        if (!doc.song.patterns.empty()) {
            if (ImGui::Button("Repeat"))
                add(1);
            ImGui::SameLine();
            if (ImGui::Button("Duplicate"))
                add(2);
        }
        if (!flattened.empty()) {
            const auto selected = flattened[static_cast<std::size_t>(occurrence)];
            auto move = [&](int direction) {
                mutate([&](Song &s) {
                    auto &list = s.sections[static_cast<std::size_t>(selected.section)].order;
                    const int to = selected.index + direction;
                    if (to >= 0 && to < static_cast<int>(list.size())) {
                        std::swap(list[static_cast<std::size_t>(to)],
                                  list[static_cast<std::size_t>(selected.index)]);
                        occurrence += direction;
                    }
                });
            };
            if (ImGui::Button("Up"))
                move(-1);
            ImGui::SameLine();
            if (ImGui::Button("Down"))
                move(1);
            ImGui::TextUnformatted("Move to section");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##move_section",
                                  doc.song.sections[static_cast<std::size_t>(section)].name.c_str())) {
                for (std::size_t i = 0; i < doc.song.sections.size(); ++i)
                    if (ImGui::Selectable(doc.song.sections[i].name.c_str())) {
                        mutate([&](Song &s) {
                            auto &from = s.sections[static_cast<std::size_t>(selected.section)].order;
                            auto entry = from[static_cast<std::size_t>(selected.index)];
                            from.erase(from.begin() + selected.index);
                            s.sections[i].order.push_back(entry);
                            section = static_cast<int>(i);
                        });
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Remove from Song"))
                mutate([&](Song &s) {
                    auto &list = s.sections[static_cast<std::size_t>(selected.section)].order;
                    list.erase(list.begin() + selected.index);
                });
        }
        if (ImGui::Button("Add Section"))
            mutate([&](Song &s) {
                s.sections.push_back({fresh_id(s, "section"), "New Section", {}});
                section = static_cast<int>(s.sections.size()) - 1;
            });
        if (!doc.song.sections.empty()) {
            ImGui::InputText("Name##section", rename, sizeof(rename));
            if (ImGui::Button("Rename Section"))
                mutate([&](Song &s) { s.sections[static_cast<std::size_t>(section)].name = rename; });
            if (ImGui::Button("Remove Section..."))
                ImGui::OpenPopup("Remove section");
            if (ImGui::BeginPopupModal("Remove section", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Move its entries to a neighbor, or remove entries? Patterns remain.");
                if (doc.song.sections.size() > 1 && ImGui::Button("Move entries")) {
                    mutate([&](Song &s) {
                        const int to = section ? section - 1 : 1;
                        auto entries = s.sections[static_cast<std::size_t>(section)].order;
                        auto &dest = s.sections[static_cast<std::size_t>(to)].order;
                        dest.insert(dest.end(), entries.begin(), entries.end());
                        s.sections.erase(s.sections.begin() + section);
                    });
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::Button("Remove entries")) {
                    mutate([&](Song &s) { s.sections.erase(s.sections.begin() + section); });
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::Separator();
        ImGui::TextUnformatted("PATTERN LIBRARY");
        for (std::size_t p = 0; p < doc.song.patterns.size(); ++p)
            if (ImGui::Selectable((doc.song.patterns[p].name + "##library" + doc.song.patterns[p].id).c_str(),
                                  pattern == static_cast<int>(p))) {
                pattern = static_cast<int>(p);
                focus_grid = true;
            }
        if (!doc.song.patterns.empty()) {
            const auto id = current().id;
            int uses = 0;
            for (const auto &sec : doc.song.sections)
                for (const auto &o : sec.order)
                    if (o.pattern_id == id)
                        ++uses;
            ImGui::Text("Used %d times", uses);
            if (ImGui::Button("Rename Pattern...")) {
                buffer(rename, sizeof(rename), current().name);
                ImGui::OpenPopup("Pattern name");
            }
            if (ImGui::BeginPopup("Pattern name")) {
                ImGui::InputText("Name", rename, sizeof(rename));
                if (ImGui::Button("Apply")) {
                    mutate([&](Song &s) { s.patterns[static_cast<std::size_t>(pattern)].name = rename; });
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (!uses && ImGui::Button("Delete unused pattern"))
                mutate([&](Song &s) { s.patterns.erase(s.patterns.begin() + pattern); });
            if (!uses && ImGui::Button("Place in section"))
                add(1);
        }
    }
    void grid() {
        if (doc.song.patterns.empty()) {
            ImGui::TextUnformatted("Create a pattern to enter notes.");
            return;
        }
        ImGui::Text("%s  |  Row %d  Voice %d %s", current().name.c_str(), cursor.row, cursor.voice + 1,
                    cursor.column ? "INSTR" : "NOTE");
        if (ImGui::Button("Copy"))
            copy();
        ImGui::SameLine();
        if (ImGui::Button("Paste"))
            paste();
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            clear();
        for (int amount : {-12, -1, 1, 12}) {
            ImGui::SameLine();
            if (ImGui::SmallButton(
                    (amount > 0 ? "+" + std::to_string(amount) : std::to_string(amount)).c_str()))
                shift(amount);
        }
        int playrow = -1, playorder = -1;
        if (transport.active && !playing_audition) {
            const double tick =
                static_cast<double>(transport.position()) / 48000 * frame_rate - (doc.song.speed + 3);
            const int absolute = static_cast<int>(std::max(0.0, tick) / doc.song.speed);
            playrow = absolute % 64;
            playorder = playing_loop ? occurrence : absolute / 64;
            ImGui::Text("Playing order %d / row %d%s", playorder + 1, playrow,
                        transport.paused ? " (paused)" : "");
        }
        if (ImGui::BeginTable("SID grid", 7,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                              ImVec2(0, -1))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, gtracker::ui::pixels(38));
            for (const char *label : {"Lead", "Inst##1", "Arp", "Inst##2", "Bass", "Inst##3"})
                ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            int r0, r1, v0, v1;
            bounds(r0, r1, v0, v1);
            for (int r = 0; r < 64; ++r) {
                ImGui::PushID(r);
                ImGui::TableNextRow(0, gtracker::ui::pixels(22));
                if (r % 16 == 0)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 70, 80, 255));
                else if (r % 4 == 0)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(30, 44, 54, 255));
                if (r == playrow && playorder == occurrence)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 95, 60, 255));
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%02d", r);
                for (int v = 0; v < 3; ++v) {
                    const auto event = cell(current(), r, v);
                    for (int col = 0; col < 2; ++col) {
                        ImGui::TableSetColumnIndex(1 + v * 2 + col);
                        ImGui::PushID(v * 2 + col);
                        std::string label =
                            col ? (event && event->type == EventType::note ? std::to_string(event->instrument)
                                                                           : "..")
                            : event ? (event->type == EventType::off ? "OFF" : note_name(event->note))
                                    : "---";
                        const bool selected = (selection && r >= r0 && r <= r1 && v >= v0 && v <= v1) ||
                                              (r == cursor.row && v == cursor.voice && col == cursor.column);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowOverlap,
                                              ImVec2(0, gtracker::ui::pixels(20)))) {
                            navigation(r, v, col, ImGui::GetIO().KeyShift);
                            focus_grid = true;
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(0)) {
                            if (!selection)
                                anchor = cursor;
                            selection = true;
                            cursor = {r, v, col};
                            focus_grid = true;
                        }
                        if (follow && transport.active && !playing_audition && r == playrow && v == 0 &&
                            col == 0 && playorder == occurrence && !ImGui::IsMouseDown(0))
                            ImGui::SetScrollHereY(0.5f);
                        if (reveal_cursor && r == cursor.row && v == cursor.voice && col == cursor.column &&
                            !ImGui::IsMouseDown(0)) {
                            ImGui::SetScrollHereY(0.5f);
                            reveal_cursor = false;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    void dialogs() {
        if (ask_close || ask_open) {
            request(ask_open ? "open" : "close");
            ask_close = ask_open = false;
        }
        if (ImGui::BeginPopupModal("Unsaved SID song", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save this SID song before continuing?");
            if (ImGui::Button("Save...")) {
                file("save");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                resolve_pending();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pending_action.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (show_file) {
            ImGui::OpenPopup("SID file");
            show_file = false;
        }
        if (ImGui::BeginPopupModal("SID file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", file_action.c_str());
            ImGui::SetNextItemWidth(gtracker::ui::pixels(650));
            ImGui::InputText("Path", filename, sizeof(filename));
            ImGui::TextWrapped("%s", status.c_str());
            const auto target = std::filesystem::u8path(filename);
            if (confirm_replace)
                ImGui::TextUnformatted("This file exists. Replace it?");
            if (ImGui::Button(confirm_replace ? "Replace" : "OK")) {
                attempt([&] {
                    if (file_action != "open" && std::filesystem::exists(target) && !confirm_replace &&
                        target != path) {
                        confirm_replace = true;
                        return;
                    }
                    if (file_action == "open") {
                        stop();
                        doc.opened(load(target));
                        path = target;
                        pattern = occurrence = section = 0;
                        prepared.reset();
                        buffer(title, sizeof(title), doc.song.name);
                        buffer(author, sizeof(author), doc.song.author);
                    } else if (file_action == "save") {
                        save_to(target);
                        resolve_pending();
                    } else
                        output(target);
                    ImGui::CloseCurrentPopup();
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pending_action.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    void draw() {
        poll();
        if (transport.active && !playing_audition && !playing_loop && !transport.paused) {
            const double ticks =
                static_cast<double>(transport.position()) / 48000 * frame_rate - (doc.song.speed + 3);
            playback_order = static_cast<int>(std::max(0.0, ticks) / (64 * doc.song.speed));
        }
        if (transport.active && !playing_loop &&
            transport.position() + static_cast<std::size_t>(transport.latency) >= transport.pcm.size()) {
            transport.stop();
            playing_audition = false;
        }
        if (follow && transport.active && !playing_loop && !playing_audition) {
            const auto entries = order(doc.song);
            if (playback_order < static_cast<int>(entries.size())) {
                const auto entry = entries[static_cast<std::size_t>(playback_order)];
                occurrence = playback_order;
                pattern = entry.pattern;
                section = entry.section;
            }
        }
        ImGui::SetNextWindowPos(gtracker::ui::content_position());
        ImGui::SetNextWindowSize(gtracker::ui::content_size());
        ImGui::Begin("SID Tracker", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        attempt([&] { keyboard(); });
        auto button = [&](const char *label, auto f) {
            if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(label).x + gtracker::ui::pixels(24) > ImGui::GetWindowWidth())
                ImGui::NewLine();
            if (ImGui::Button(label))
                attempt(f);
        };
        button("Play Song [F5]", [&] { start(false, true); });
        ImGui::SameLine();
        button("Loop Pattern [F6]", [&] { start(true); });
        ImGui::SameLine();
        button(transport.paused ? "Resume" : "Pause", [&] { transport.toggle(); });
        ImGui::SameLine();
        button("Stop [F8]", [&] { stop(); });
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(gtracker::ui::scale_label().c_str()).x + gtracker::ui::pixels(24) > ImGui::GetWindowWidth())
            ImGui::NewLine();
        gtracker::ui::scale_button();
        ImGui::SameLine();
        ImGui::Checkbox("Edit", &edit);
        ImGui::SameLine();
        ImGui::Checkbox("Follow", &follow);
        ImGui::SetNextItemWidth(gtracker::ui::pixels(90));
        ImGui::SliderInt("Octave", &octave, 1, 6);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(gtracker::ui::pixels(70));
        if (ImGui::BeginCombo("Step", std::to_string(step).c_str())) {
            for (int n : {0, 1, 2, 4})
                if (ImGui::Selectable(std::to_string(n).c_str(), step == n))
                    step = n;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        int speed = doc.song.speed;
        ImGui::SetNextItemWidth(gtracker::ui::pixels(100));
        if (ImGui::SliderInt("Speed", &speed, 4, 12))
            attempt([&] { mutate([&](Song &s) { s.speed = speed; }); });
        ImGui::SameLine();
        ImGui::Text("~%.2f BPM", 60 * frame_rate / (4 * doc.song.speed));
        button("Save", [&] {
            if (path.empty())
                file("save");
            else
                save_to(path);
        });
        ImGui::SameLine();
        button("Save As", [&] { file("save"); });
        ImGui::SameLine();
        button("Open SID", [&] { ask_open = true; });
        ImGui::SameLine();
        button("New SID", [&] { request("new"); });
        ImGui::SameLine();
        button("Guitar mode", [&] { request("guitar"); });
        ImGui::SameLine();
        button("Export SID", [&] {
            export_format = "sid";
            file("export");
        });
        ImGui::SameLine();
        ImGui::SetNextItemWidth(gtracker::ui::pixels(80));
        if (ImGui::GetCursorPosX() + gtracker::ui::pixels(100) > ImGui::GetWindowWidth())
            ImGui::NewLine();
        if (ImGui::BeginCombo("##export", "More")) {
            for (const auto &f : std::vector<std::pair<const char *, const char *>>{
                     {"Export MP4 Video", "mp4"}, {"Export WAV", "wav"}, {"Export C64 Program", "prg"}, {"Export SNG", "sng"}})
                if (ImGui::Selectable(f.first)) {
                    export_format = f.second;
                    file("export");
                }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(gtracker::ui::pixels(220));
        if (ImGui::InputText("Title", title, sizeof(title), ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit())
            attempt([&] { mutate([&](Song &s) { s.name = title; }); });
        ImGui::SameLine();
        ImGui::SetNextItemWidth(gtracker::ui::pixels(180));
        if (ImGui::InputText("Author", author, sizeof(author), ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit())
            attempt([&] { mutate([&](Song &s) { s.author = author; }); });
        ImGui::SameLine();
        ImGui::TextUnformatted(doc.dirty() ? "* Unsaved" : "Saved");
        const float available =
            std::max(120.0f, ImGui::GetContentRegionAvail().y -
                                 std::max(90.0f, ImGui::GetTextLineHeightWithSpacing() * 5));
        const auto layout_id="layout##"+std::to_string(static_cast<int>(gtracker::ui::pixels(100)));
        if (ImGui::BeginTable(layout_id.c_str(), 3, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, gtracker::ui::pixels(210));
            ImGui::TableSetupColumn("Grid", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Presets", ImGuiTableColumnFlags_WidthFixed, gtracker::ui::pixels(210));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("arrangement", ImVec2(0, available));
            attempt([&] { arrangement(); });
            ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("grid", ImVec2(0, available));
            attempt([&] { grid(); });
            ImGui::EndChild();
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginChild("presets", ImVec2(0, available));
            ImGui::TextUnformatted("FIXED BANK 1");
            for (const auto &p : presets()) {
                if (ImGui::Selectable((std::to_string(p.id) + " " + p.name).c_str(), preset == p.id)) {
                    preset = p.id;
                    focus_grid = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", p.description);
            }
            button("Apply to Selection", [&] { apply(); });
            ImGui::Separator();
            ImGui::TextWrapped("Z S X D C V G B H N J M\nQ 2 W 3 E R 5 T 6 Y 7 U\nA: OFF   Delete: "
                               "clear\nEsc: Edit   Space: transport\nTab: voice   Shift: select\nCtrl+C/X/V: "
                               "block edit\nCtrl+Z/Y: undo/redo\nF7: play from cursor");
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());
        if (busy()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel job")) {
                cancel = true;
                pending_audition = -1;
            }
        }
        if (ImGui::SmallButton("Review recovery copies"))
            ImGui::OpenPopup("SID recovery");
        if (ImGui::BeginPopup("SID recovery")) {
            ImGui::TextUnformatted("Open a copy, review it, then Save As a new filename.");
            for (const auto &entry : std::filesystem::directory_iterator(data)) {
                const auto name = entry.path().filename().u8string();
                if (name.rfind("recovery.sidtrk.json.sid-", 0) == 0 && entry.path() != recovery &&
                    ImGui::Selectable(name.c_str()))
                    attempt([&] {
                        if (doc.dirty())
                            throw std::runtime_error("Save current changes before reviewing recovery.");
                        stop();
                        doc.opened(load(entry.path()));
                        doc.saved.clear();
                        path.clear();
                        prepared.reset();
                        pattern = occurrence = section = 0;
                        buffer(title, sizeof(title), doc.song.name);
                        buffer(author, sizeof(author), doc.song.author);
                        status = "Recovered score copy. Save As a new filename after review.";
                    });
            }
            ImGui::EndPopup();
        }
        ImGui::TextWrapped(
            "Rows/instruments decimal | C4 = MIDI 60 | HOLD keeps voice state | Any preset on any voice");
        dialogs();
        ImGui::End();
    }
};
Editor::Editor(const std::filesystem::path &s, const std::filesystem::path &r, const std::filesystem::path &d)
    : state(std::make_unique<Impl>(s, r, d)) {}
Editor::~Editor() = default;
void Editor::draw() { state->draw(); }
void Editor::request_close() { state->ask_close = true; }
bool Editor::closed() const { return state->close; }
bool Editor::guitar_requested() const { return state->guitar; }
} // namespace gtracker::sid
