#include "gtracker/project.hpp"
#include "gtracker/editor_document.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using gtracker::Event;
using gtracker::Pattern;
using gtracker::RenderResult;

constexpr ImU32 cyan = IM_COL32(78, 211, 222, 255);
constexpr ImU32 amber = IM_COL32(246, 188, 94, 255);
constexpr ImU32 text_color = IM_COL32(215, 224, 233, 255);
constexpr ImU32 quiet_color = IM_COL32(122, 144, 162, 255);

struct Arguments {
    std::filesystem::path document;
    std::filesystem::path script;
    std::filesystem::path screenshot;
    std::filesystem::path data_dir;
    std::filesystem::path resource_dir;
    int frames = 0;
    bool smoke = false;
    bool help = false;
};

Arguments arguments(int argc, char* argv[]) {
    Arguments result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> const char* {
            if (++i >= argc) {
                throw std::invalid_argument("missing value after " + argument);
            }
            return argv[i];
        };
        if (argument == "--script") {
            result.script = std::filesystem::u8path(value());
        } else if (argument == "--screenshot") {
            result.screenshot = std::filesystem::u8path(value());
        } else if (argument == "--data-dir") {
            result.data_dir = std::filesystem::u8path(value());
        } else if (argument == "--resource-dir") {
            result.resource_dir = std::filesystem::u8path(value());
        } else if (argument == "--frames") {
            const std::string frame_count = value();
            std::size_t consumed = 0;
            const long count = std::stol(frame_count, &consumed);
            if (consumed != frame_count.size() || count < 1 || count > 100000) {
                throw std::invalid_argument("--frames must be between 1 and 100000");
            }
            result.frames = static_cast<int>(count);
        } else if (argument == "--smoke-test") {
            result.smoke = true;
        } else if (argument == "--help" || argument == "-h") {
            result.help = true;
        } else if (!argument.empty() && argument[0] == '-') {
            throw std::invalid_argument("unknown option: " + argument);
        } else if (result.document.empty()) {
            result.document = std::filesystem::u8path(argument);
        } else {
            throw std::invalid_argument("only one pattern path may be supplied");
        }
    }
    if (result.frames == 0 && (result.smoke || !result.screenshot.empty())) {
        result.frames = 8;
    }
    return result;
}

Pattern initial_pattern() {
    Pattern pattern;
    pattern.name = "Campfire sketch";
    pattern.events = {
        {"p1", "pluck", 0.0, 5, 0.0f, 0.66f},
        {"p2", "pluck", 0.5, 2, 0.0f, 0.47f},
        {"p3", "pluck", 1.0, 1, 0.0f, 0.53f},
        {"p4", "pluck", 1.5, 0, 0.0f, 0.48f},
        {"p5", "pluck", 2.0, 4, 2.0f, 0.61f},
        {"p6", "pluck", 2.5, 2, 0.0f, 0.43f},
        {"p7", "pluck", 3.0, 1, 0.0f, 0.49f},
        {"p8", "pluck", 3.5, 0, 2.0f, 0.57f},
        {"p9", "pluck", 4.0, 5, 3.0f, 0.64f},
        {"p10", "pluck", 4.5, 2, 0.0f, 0.43f},
        {"p11", "pluck", 5.0, 1, 0.0f, 0.51f},
        {"p12", "pluck", 5.5, 0, 3.0f, 0.58f},
        {"p13", "pluck", 6.0, 3, 2.0f, 0.52f},
        {"p14", "pluck", 6.5, 1, 0.0f, 0.47f},
        {"p15", "pluck", 7.0, 0, 0.0f, 0.62f},
        {"d1", "damp", 7.5, 5, 0.0f, 0.75f},
        {"b1", "body_hit", 1.0, 0, 0.0f, 0.42f},
        {"b2", "body_hit", 3.0, 0, 0.0f, 0.34f},
        {"b3", "body_hit", 5.0, 0, 0.0f, 0.41f},
        {"b4", "body_hit", 7.0, 0, 0.0f, 0.35f},
    };
    return pattern;
}

std::filesystem::path editor_data_directory(const Arguments& args) {
    if (!args.data_dir.empty()) return std::filesystem::absolute(args.data_dir);
    char* location = SDL_GetPrefPath("LustOfWar", "gtracker");
    if (!location) {
        throw std::runtime_error(std::string("Cannot find a writable user-data folder; pass --data-dir: ") + SDL_GetError());
    }
    const auto path = std::filesystem::u8path(location);
    SDL_free(location);
    return path;
}

std::filesystem::path editor_resource_directory(const Arguments& args) {
    if (!args.resource_dir.empty()) return std::filesystem::absolute(args.resource_dir);
    const char* location = SDL_GetBasePath();
    if (!location) throw std::runtime_error(std::string("Cannot find application resources: ") + SDL_GetError());
    return std::filesystem::u8path(location);
}

struct CompletedRender {
    RenderResult audio;
    std::vector<float> times;
    std::vector<float> minimum;
    std::vector<float> maximum;
};

CompletedRender render_snapshot(const Pattern& pattern) {
    CompletedRender result;
    result.audio = gtracker::render_pattern(pattern);
    const std::size_t count = std::min<std::size_t>(2048, result.audio.mono.size());
    result.times.reserve(count);
    result.minimum.reserve(count);
    result.maximum.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t begin = i * result.audio.mono.size() / count;
        const std::size_t end = (i + 1) * result.audio.mono.size() / count;
        auto bounds = std::minmax_element(result.audio.mono.begin() + begin,
                                          result.audio.mono.begin() + end);
        result.times.push_back(static_cast<float>(begin + end) * 0.5f /
                               static_cast<float>(result.audio.sample_rate));
        result.minimum.push_back(*bounds.first);
        result.maximum.push_back(*bounds.second);
    }
    return result;
}

struct Editor {
    gtracker::EditorDocument document{initial_pattern()};
    std::filesystem::path data_directory;
    std::filesystem::path resource_directory;
    std::uint64_t pending_generation = 0;
    std::uint64_t rendered_generation = 0;
    std::uint64_t rendered_revision = 0;
    std::future<CompletedRender> pending;
    CompletedRender rendered;
    SDL_AudioStream* audio_stream = nullptr;
    int audio_rate = 0;
    int selected = 0;
    int cursor_lane = 0;
    double cursor_beat = 0.0;
    float snap = 0.5f;
    bool status_error = false;
    bool render_failed = false;
    bool play_after_render = false;
    bool playing = false;
    std::string status = "Select a fret number, edit the inspector, then Render and Play.";
    std::array<char, 1024> path{};
    std::array<char, 1024> script_path{};
    std::array<char, 1024> confirmation_save_path{};

    explicit Editor(const Arguments& args) {
        data_directory = editor_data_directory(args);
        resource_directory = editor_resource_directory(args);
        const std::string document_text = args.document.empty()
            ? (data_directory / "sketch.gtrk.json").u8string() : args.document.u8string();
        const std::string script_text = args.script.empty()
            ? (resource_directory / "scripts/soften.lua").u8string() : args.script.u8string();
        if (document_text.size() >= path.size() || script_text.size() >= script_path.size()) {
            throw std::invalid_argument("Editor paths must fit within 1023 bytes; launch path was not loaded or applied.");
        }
        std::copy(document_text.begin(), document_text.end(), path.begin());
        std::copy(script_text.begin(), script_text.end(), script_path.begin());
        if (!args.document.empty()) {
            document = gtracker::EditorDocument(gtracker::load_pattern(args.document), args.document);
        }
        if (!args.script.empty()) {
            Pattern edited = document.pattern();
            gtracker::apply_lua(edited, args.script);
            commit(std::move(edited), "Applied Lua script");
        }
        if (document.pattern().events.empty()) {
            selected = -1;
        }
    }

    ~Editor() {
        // The renderer owns its snapshot; finishing this job never touches UI
        // state. The prototype waits for an in-flight job on shutdown.
        if (pending.valid()) {
            pending.wait();
        }
        if (audio_stream) {
            SDL_DestroyAudioStream(audio_stream);
        }
    }

    void message(std::string text, bool error = false) {
        status = std::move(text);
        status_error = error;
    }

    bool rendering() const { return pending.valid(); }
    bool current_audio() const {
        return rendered_generation == document.generation() && !rendered.audio.mono.empty();
    }

    void stop() {
        if (audio_stream) {
            SDL_ClearAudioStream(audio_stream);
            SDL_PauseAudioStreamDevice(audio_stream);
        }
        playing = false;
        play_after_render = false;
    }

    void changed() {
        stop();
        if (selected >= static_cast<int>(document.pattern().events.size())) {
            selected = static_cast<int>(document.pattern().events.size()) - 1;
        }
    }

    void commit(Pattern next, const char* action, const std::string& edit_key = {}) {
        if (document.commit(std::move(next), edit_key)) {
            changed();
            message(std::string(action) + ". Render to hear this edit.");
        }
    }

    template <typename Action>
    void attempt(Action action) {
        try {
            action();
        } catch (const std::exception& error) {
            message(error.what(), true);
        }
    }

    void undo(bool redo = false) {
        if (document.undo(redo)) {
            changed();
            message(redo ? "Redid document edit." : "Undid document edit.");
        }
    }

    void loaded() {
        selected = document.pattern().events.empty() ? -1 : 0;
        cursor_beat = 0.0;
        cursor_lane = 0;
        changed();
        message("Loaded " + document.pattern().name + ".");
    }

    void load() {
        const auto before = document.generation();
        document.request_load(std::filesystem::u8path(path.data()));
        if (document.generation() != before) loaded();
    }

    void resolve(gtracker::EditorDocument::Decision decision) {
        if (decision == gtracker::EditorDocument::Decision::save && confirmation_save_path[0] == '\0') {
            throw std::invalid_argument("Enter a path for the current pattern before saving.");
        }
        const auto before = document.generation();
        document.resolve(decision, std::filesystem::u8path(confirmation_save_path.data()));
        if (document.generation() != before) loaded();
    }

    void save() {
        if (path[0] == '\0') {
            throw std::invalid_argument("Enter a pattern path before saving.");
        }
        document.save(std::filesystem::u8path(path.data()));
        message("Saved " + std::string(path.data()) + ".");
    }

    void run_script() {
        Pattern next = document.pattern();
        gtracker::apply_lua(next, std::filesystem::u8path(script_path.data()));
        commit(std::move(next), "Applied Lua script");
    }

    std::string new_id() const {
        const auto& pattern = document.pattern();
        std::size_t suffix = pattern.events.size() + 1;
        for (;;) {
            const std::string id = "event-" + std::to_string(suffix++);
            if (std::none_of(pattern.events.begin(), pattern.events.end(),
                             [&](const Event& event) { return event.id == id; })) {
                return id;
            }
        }
    }

    void add(const char* type) {
        const auto& pattern = document.pattern();
        Pattern next = pattern;
        Event event;
        event.id = new_id();
        event.type = type;
        event.beat = std::clamp(cursor_beat, 0.0, std::nextafter(pattern.length_beats, 0.0));
        event.string_index = std::clamp(cursor_lane, 0, static_cast<int>(pattern.tuning_midi.size()) - 1);
        event.strength = event.type == "damp" ? 1.0f : 0.65f;
        next.events.push_back(std::move(event));
        commit(std::move(next), "Added event");
        selected = static_cast<int>(document.pattern().events.size()) - 1;
    }

    void erase() {
        const auto& pattern = document.pattern();
        if (selected < 0 || selected >= static_cast<int>(pattern.events.size())) {
            return;
        }
        Pattern next = pattern;
        next.events.erase(next.events.begin() + selected);
        commit(std::move(next), "Deleted event");
    }

    void request_render(bool then_play = false) {
        if (rendering()) {
            return;
        }
        document.end_edit();
        const Pattern snapshot = document.pattern();
        gtracker::validate_pattern(snapshot);
        pending_generation = document.generation();
        play_after_render = then_play;
        render_failed = false;
        pending = std::async(std::launch::async, [snapshot]() { return render_snapshot(snapshot); });
        message("Rendering revision " + std::to_string(snapshot.revision) + "...");
    }

    void poll_render() {
        if (!rendering() || pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            return;
        }
        try {
            CompletedRender completed = pending.get();
            if (pending_generation != document.generation()) {
                play_after_render = false;
                message("Render finished for an older edit and was discarded. Render the current pattern.");
                return;
            }
            rendered = std::move(completed);
            rendered_generation = document.generation();
            rendered_revision = document.pattern().revision;
            char text[256];
            std::snprintf(text, sizeof(text), "Rendered revision %llu in %.1f ms. Peak %.3f; RMS %.3f.",
                          static_cast<unsigned long long>(rendered_revision), rendered.audio.elapsed_ms,
                          rendered.audio.peak, rendered.audio.rms);
            message(text);
            if (play_after_render) {
                play_after_render = false;
                play();
            }
        } catch (const std::exception& error) {
            render_failed = true;
            play_after_render = false;
            message(std::string("Render: ") + error.what(), true);
        }
    }

    void play() {
        if (!current_audio()) {
            request_render(true);
            return;
        }
        if (!audio_stream || audio_rate != rendered.audio.sample_rate) {
            if (audio_stream) {
                SDL_DestroyAudioStream(audio_stream);
                audio_stream = nullptr;
            }
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                throw std::runtime_error(std::string("Audio initialization: ") + SDL_GetError());
            }
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_F32;
            spec.channels = 1;
            spec.freq = rendered.audio.sample_rate;
            audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!audio_stream) {
                throw std::runtime_error(std::string("Audio output: ") + SDL_GetError());
            }
            audio_rate = spec.freq;
        }
        const auto bytes = rendered.audio.mono.size() * sizeof(float);
        if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Rendered audio is too large for this prototype's playback queue.");
        }
        SDL_ClearAudioStream(audio_stream);
        if (!SDL_PutAudioStreamData(audio_stream, rendered.audio.mono.data(), static_cast<int>(bytes)) ||
            !SDL_FlushAudioStream(audio_stream) || !SDL_ResumeAudioStreamDevice(audio_stream)) {
            throw std::runtime_error(std::string("Audio playback: ") + SDL_GetError());
        }
        playing = true;
        message("Playing rendered revision " + std::to_string(rendered_revision) + ".");
    }
};

// Capture the actual changed widget's ID before another widget is drawn. One
// activation (including Ctrl-click numeric entry) becomes one history operation.
template<typename Action>
void edit_control(Editor& editor, bool changed, Action action) {
    if (ImGui::IsItemActivated()) editor.document.end_edit();
    if (changed) {
        const auto key = std::to_string(ImGui::GetItemID());
        editor.attempt([&]() { action(key); });
    }
    if (ImGui::IsItemDeactivated()) editor.document.end_edit();
}

void unsaved_dialog(Editor& editor) {
    using Pending = gtracker::EditorDocument::Pending;
    using Decision = gtracker::EditorDocument::Decision;
    if (editor.document.pending() == Pending::none) return;
    if (!ImGui::IsPopupOpen("Unsaved changes")) {
        const auto destination = editor.document.filename().empty()
            ? (editor.data_directory / "sketch.gtrk.json").u8string() : editor.document.filename().u8string();
        std::fill(editor.confirmation_save_path.begin(), editor.confirmation_save_path.end(), '\0');
        std::copy(destination.begin(), destination.end(), editor.confirmation_save_path.begin());
        editor.message("Choose whether to save the current pattern before continuing.");
        ImGui::OpenPopup("Unsaved changes");
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(550.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool loading = editor.document.pending() == Pending::load;
        ImGui::TextWrapped("Save changes to \"%s\" before %s?", editor.document.pattern().name.c_str(),
                           loading ? "loading another pattern" : "closing");
        ImGui::Spacing();
        ImGui::TextUnformatted("Save current pattern to");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##save_before_leave", editor.confirmation_save_path.data(), editor.confirmation_save_path.size());
        if (editor.status_error) {
            ImGui::TextWrapped("%s", editor.status.c_str());
        }
        auto resolve = [&](Decision decision) {
            editor.attempt([&]() { editor.resolve(decision); });
            if (editor.document.pending() == Pending::none) ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button(loading ? "Save and Load" : "Save and Close")) resolve(Decision::save);
        ImGui::SameLine();
        if (ImGui::Button("Discard")) resolve(Decision::discard);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) resolve(Decision::cancel);
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) resolve(Decision::cancel);
        ImGui::EndPopup();
    }
}

void theme() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.FrameBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.043f, 0.060f, 0.081f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.078f, 0.103f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.90f, 0.93f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.47f, 0.56f, 0.64f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.13f, 0.21f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.115f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.22f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.30f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.19f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.32f, 0.37f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.43f, 0.47f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.83f, 0.87f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.83f, 0.87f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.96f, 0.74f, 0.37f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.11f, 0.27f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.13f, 0.35f, 0.39f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.43f, 0.45f, 1.0f);
}

std::string tuning_label(double midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int note = static_cast<int>(std::lround(midi));
    return std::string(names[(note % 12 + 12) % 12]) + std::to_string(note / 12 - 1);
}

void canvas(Editor& editor, ImVec2 size) {
    size.x = std::max(size.x, 200.0f);
    size.y = std::max(size.y, 170.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tab_canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(14, 22, 30, 255));

    const int strings = static_cast<int>(editor.document.pattern().tuning_midi.size());
    const int lanes = strings + 1;
    const float left = origin.x + 64.0f;
    const float right = origin.x + size.x - 20.0f;
    const float top = origin.y + 32.0f;
    const float lane_height = (size.y - 42.0f) / static_cast<float>(lanes);
    const double pixels_per_beat = static_cast<double>(right - left) / editor.document.pattern().length_beats;
    auto lane_y = [&](int lane) { return top + (static_cast<float>(lane) + 0.5f) * lane_height; };
    auto beat_x = [&](double beat) {
        return left + static_cast<float>(beat / editor.document.pattern().length_beats) * (right - left);
    };

    for (int lane = 0; lane < lanes; ++lane) {
        const float y = top + static_cast<float>(lane) * lane_height;
        if ((lane & 1) == 0) {
            draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y + lane_height),
                                IM_COL32(19, 29, 39, 255));
        }
        draw->AddLine(ImVec2(left, lane_y(lane)), ImVec2(right, lane_y(lane)),
                       lane == strings ? IM_COL32(101, 77, 44, 255) : IM_COL32(54, 72, 88, 255));
        if (lane == strings) {
            draw->AddText(ImVec2(origin.x + 9.0f, lane_y(lane) - 8.0f), amber, "BODY");
        } else {
            const std::string name = tuning_label(editor.document.pattern().tuning_midi[static_cast<std::size_t>(lane)]);
            draw->AddText(ImVec2(origin.x + 10.0f, lane_y(lane) - 8.0f), quiet_color, name.c_str());
        }
    }

    double grid = 1.0;
    while (grid * pixels_per_beat < 34.0) {
        grid *= 2.0;
    }
    for (double beat = 0.0; beat <= editor.document.pattern().length_beats + 0.0001; beat += grid) {
        const float x = beat_x(beat);
        const bool bar = std::fmod(beat, 4.0) < 0.001;
        draw->AddLine(ImVec2(x, top - 3.0f), ImVec2(x, origin.y + size.y - 6.0f),
                       bar ? IM_COL32(60, 89, 107, 255) : IM_COL32(33, 50, 64, 255));
        char label[32];
        std::snprintf(label, sizeof(label), "%g", beat);
        draw->AddText(ImVec2(x - 3.0f, origin.y + 7.0f), bar ? cyan : quiet_color, label);
    }

    const float cursor_x = beat_x(editor.cursor_beat);
    draw->AddLine(ImVec2(cursor_x, top), ImVec2(cursor_x, origin.y + size.y - 7.0f),
                   IM_COL32(246, 188, 94, 75));
    int hit = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (std::size_t i = 0; i < editor.document.pattern().events.size(); ++i) {
        const auto& event = editor.document.pattern().events[i];
        const bool body = event.type == "body_hit";
        const bool damp = event.type == "damp";
        const float x = beat_x(event.beat);
        const float y = lane_y(body ? strings : event.string_index);
        char label[24];
        if (body) {
            std::snprintf(label, sizeof(label), "%s", "+");
        } else if (damp) {
            std::snprintf(label, sizeof(label), "%s", "x");
        } else {
            std::snprintf(label, sizeof(label), "%g", static_cast<double>(event.fret));
        }
        const ImVec2 text = ImGui::CalcTextSize(label);
        const float half_width = std::max(12.0f, text.x * 0.5f + 5.0f);
        const float half_height = std::min(lane_height * 0.42f, 14.0f);
        const bool selected = static_cast<int>(i) == editor.selected;
        const ImVec2 a(x - half_width, y - half_height);
        const ImVec2 b(x + half_width, y + half_height);
        draw->AddRectFilled(a, b, selected ? IM_COL32(43, 96, 108, 255) : IM_COL32(20, 37, 49, 255), 3.0f);
        draw->AddRect(a, b, selected ? cyan : (body ? IM_COL32(150, 111, 58, 255) : IM_COL32(48, 83, 103, 255)),
                       3.0f, 0, selected ? 2.0f : 1.0f);
        draw->AddText(ImVec2(x - text.x * 0.5f, y - text.y * 0.5f), body ? amber : text_color, label);
        if (hovered && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
            hit = static_cast<int>(i);
        }
    }
    draw->PopClipRect();

    if (hovered && hit >= 0) {
        const auto& event = editor.document.pattern().events[static_cast<std::size_t>(hit)];
        ImGui::BeginTooltip();
        ImGui::Text("%s | beat %.3f | strength %.2f", event.type.c_str(), event.beat, event.strength);
        ImGui::EndTooltip();
    }
    if (clicked) {
        editor.document.end_edit();
        if (hit >= 0) {
            editor.selected = hit;
            const auto& event = editor.document.pattern().events[static_cast<std::size_t>(hit)];
            editor.cursor_beat = event.beat;
            editor.cursor_lane = event.type == "body_hit" ? strings : event.string_index;
        } else if (mouse.y >= top && mouse.x >= left && mouse.x <= right) {
            const double beat = static_cast<double>((mouse.x - left) / (right - left)) * editor.document.pattern().length_beats;
            editor.cursor_beat = std::clamp(std::round(beat / editor.snap) * editor.snap,
                                            0.0, std::nextafter(editor.document.pattern().length_beats, 0.0));
            editor.cursor_lane = std::clamp(static_cast<int>((mouse.y - top) / lane_height), 0, strings);
            editor.selected = -1;
            if (double_clicked) {
                editor.attempt([&]() { editor.add(editor.cursor_lane == strings ? "body_hit" : "pluck"); });
            }
        }
    }
}

void inspector(Editor& editor) {
    ImGui::TextColored(ImVec4(0.31f, 0.83f, 0.87f, 1.0f), "EVENT");
    ImGui::Separator();
    if (editor.selected < 0 || editor.selected >= static_cast<int>(editor.document.pattern().events.size())) {
        ImGui::TextWrapped("Select a fret number or body hit. Double-click an empty lane to add an event.");
    } else {
        Event event = editor.document.pattern().events[static_cast<std::size_t>(editor.selected)];
        ImGui::PushID(event.id.c_str());
        ImGui::TextDisabled("%s", event.id.c_str());
        auto edit = [&](bool changed) {
            edit_control(editor, changed, [&](const std::string& key) {
                Pattern next = editor.document.pattern();
                next.events[static_cast<std::size_t>(editor.selected)] = event;
                editor.commit(std::move(next), "Edited event", key);
                editor.cursor_beat = event.beat;
                editor.cursor_lane = event.type == "body_hit" ? static_cast<int>(editor.document.pattern().tuning_midi.size())
                                                              : event.string_index;
            });
        };
        const char* types[] = {"Pluck", "Damp", "Body hit"};
        int type = event.type == "body_hit" ? 2 : (event.type == "damp" ? 1 : 0);
        ImGui::SetNextItemWidth(-1.0f);
        const bool type_changed = ImGui::Combo("##type", &type, types, 3);
        if (type_changed) {
            event.type = type == 2 ? "body_hit" : (type == 1 ? "damp" : "pluck");
        }
        edit(type_changed);
        ImGui::TextUnformatted("Beat");
        ImGui::SetNextItemWidth(-1.0f);
        const bool beat_changed = ImGui::InputDouble("##beat", &event.beat, editor.snap, 1.0, "%.3f");
        if (beat_changed) {
            event.beat = std::clamp(event.beat, 0.0, std::nextafter(editor.document.pattern().length_beats, 0.0));
        }
        edit(beat_changed);
        if (type != 2) {
            int string = event.string_index + 1;
            ImGui::TextUnformatted("String (1 = highest lane)");
            ImGui::SetNextItemWidth(-1.0f);
            const bool string_changed = ImGui::InputInt("##string", &string);
            if (string_changed) {
                event.string_index = std::clamp(string, 1, static_cast<int>(editor.document.pattern().tuning_midi.size())) - 1;
            }
            edit(string_changed);
            if (type == 0) {
                ImGui::TextUnformatted("Fret");
                ImGui::SetNextItemWidth(-1.0f);
                const bool fret_changed = ImGui::InputFloat("##fret", &event.fret, 1.0f, 5.0f, "%.2f");
                if (fret_changed) {
                    event.fret = std::clamp(event.fret, 0.0f, 36.0f);
                }
                edit(fret_changed);
            }
        }
        ImGui::TextUnformatted(type == 1 ? "Damping amount" : "Strength");
        ImGui::SetNextItemWidth(-1.0f);
        edit(ImGui::SliderFloat("##strength", &event.strength, 0.0f, 1.0f, "%.2f"));
        if (ImGui::Button("Delete event", ImVec2(-1.0f, 0.0f))) {
            editor.attempt([&]() { editor.erase(); });
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::TreeNode("Instrument controls")) {
        float decay = editor.document.pattern().decay_seconds;
        float brightness = editor.document.pattern().brightness;
        float body_mix = editor.document.pattern().body_mix;
        auto edit = [&](bool changed) {
            edit_control(editor, changed, [&](const std::string& key) {
                Pattern next = editor.document.pattern();
                next.decay_seconds = decay;
                next.brightness = brightness;
                next.body_mix = body_mix;
                editor.commit(std::move(next), "Edited instrument", key);
            });
        };
        ImGui::TextUnformatted("Decay (seconds)");
        ImGui::SetNextItemWidth(-1.0f);
        edit(ImGui::SliderFloat("##decay", &decay, 0.1f, 12.0f, "%.2f"));
        ImGui::TextUnformatted("Brightness");
        ImGui::SetNextItemWidth(-1.0f);
        edit(ImGui::SliderFloat("##brightness", &brightness, 0.0f, 1.0f, "%.2f"));
        ImGui::TextUnformatted("Body mix");
        ImGui::SetNextItemWidth(-1.0f);
        edit(ImGui::SliderFloat("##body", &body_mix, 0.0f, 1.0f, "%.2f"));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Lua command")) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##script", editor.script_path.data(), editor.script_path.size());
        if (ImGui::Button("Run script", ImVec2(-1.0f, 0.0f))) {
            editor.attempt([&]() { editor.run_script(); });
        }
        ImGui::TextDisabled("Trusted local scripts.");
        ImGui::TreePop();
    }
}

void waveform(const Editor& editor, float height) {
    if (editor.rendered.audio.mono.empty()) {
        ImGui::BeginChild("##wave_empty", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        ImGui::TextColored(ImVec4(0.31f, 0.83f, 0.87f, 1.0f), "RENDERED WAVEFORM");
        ImGui::TextWrapped("Render the pattern to inspect the waveform. Space renders and plays the current edit.");
        ImGui::EndChild();
        return;
    }
    const bool current = editor.current_audio();
    if (ImPlot::BeginPlot("##waveform", ImVec2(-1.0f, height), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes("Time (seconds)", nullptr, ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0,
                               static_cast<double>(editor.rendered.audio.mono.size()) / editor.rendered.audio.sample_rate,
                               ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.05, 1.05, ImGuiCond_Always);
        ImPlot::SetNextFillStyle(current ? ImVec4(0.30f, 0.83f, 0.87f, 0.80f)
                                        : ImVec4(0.42f, 0.48f, 0.53f, 0.60f));
        ImPlot::PlotShaded("Amplitude", editor.rendered.times.data(), editor.rendered.minimum.data(),
                           editor.rendered.maximum.data(), static_cast<int>(editor.rendered.times.size()));
        ImPlot::EndPlot();
    }
}

void keyboard(Editor& editor) {
    const auto& io = ImGui::GetIO();
    const bool modifier = io.KeyCtrl || io.KeySuper;
    if (modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        editor.attempt([&]() { editor.save(); });
    }
    if (io.WantTextInput) {
        return;
    }
    if (modifier && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        editor.attempt([&]() { editor.undo(io.KeyShift); });
    }
    if (modifier && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        editor.attempt([&]() { editor.undo(true); });
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        editor.attempt([&]() { editor.erase(); });
    }
    // Shortcut routing reserves Space before ImGui's next navigation update;
    // a focused button must not activate from the same transport key press.
    if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) {
        editor.attempt([&]() {
            if (editor.playing || editor.play_after_render) {
                editor.stop();
                editor.message("Stopped playback.");
            } else {
                editor.play();
            }
        });
    }
}

void interface(Editor& editor) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("gtracker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (editor.document.pending() == gtracker::EditorDocument::Pending::none) keyboard(editor);

    ImGui::TextColored(ImVec4(0.31f, 0.83f, 0.87f, 1.0f), "gtracker");
    ImGui::SameLine();
    ImGui::TextDisabled(" /  ACOUSTIC PATTERN EDITOR");
    ImGui::SameLine();
    const float title_width = ImGui::CalcTextSize(editor.document.pattern().name.c_str()).x + 180.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - title_width));
    ImGui::Text("%s%s  |  r%llu", editor.document.pattern().name.c_str(), editor.document.dirty() ? " *" : "",
                static_cast<unsigned long long>(editor.document.pattern().revision));
    ImGui::Separator();

    ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - 146.0f));
    ImGui::InputText("##document_path", editor.path.data(), editor.path.size());
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(60.0f, 0.0f))) {
        editor.attempt([&]() { editor.load(); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(60.0f, 0.0f))) {
        editor.attempt([&]() { editor.save(); });
    }

    ImGui::BeginDisabled(!editor.document.can_undo());
    if (ImGui::Button("Undo")) {
        editor.attempt([&]() { editor.undo(); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.document.can_redo());
    if (ImGui::Button("Redo")) {
        editor.attempt([&]() { editor.undo(true); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 20.0f);
    if (ImGui::Button("+ Pluck")) {
        editor.attempt([&]() { editor.add("pluck"); });
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Damp")) {
        editor.attempt([&]() { editor.add("damp"); });
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Body hit")) {
        editor.attempt([&]() { editor.add("body_hit"); });
    }
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginDisabled(editor.rendering());
    if (ImGui::Button(editor.rendering() ? "Rendering..." : "Render")) {
        editor.attempt([&]() { editor.request_render(); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.rendering());
    if (ImGui::Button(editor.current_audio() ? "Play" : "Render + Play")) {
        editor.attempt([&]() { editor.play(); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        editor.stop();
        editor.message("Stopped playback.");
    }

    double bpm = editor.document.pattern().bpm;
    double length = editor.document.pattern().length_beats;
    auto edit_settings = [&](bool changed) {
        edit_control(editor, changed, [&](const std::string& key) {
            Pattern next = editor.document.pattern();
            next.bpm = bpm;
            next.length_beats = length;
            editor.commit(std::move(next), "Edited pattern", key);
            editor.cursor_beat = std::min(editor.cursor_beat, std::nextafter(editor.document.pattern().length_beats, 0.0));
        });
    };
    ImGui::TextUnformatted("BPM");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    edit_settings(ImGui::InputDouble("##bpm", &bpm, 0.0, 0.0, "%.1f"));
    ImGui::SameLine();
    ImGui::TextUnformatted("Beats");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(85.0f);
    edit_settings(ImGui::InputDouble("##length", &length, 0.0, 0.0, "%.2f"));
    ImGui::SameLine(0.0f, 18.0f);
    ImGui::TextUnformatted("Snap");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* snaps[] = {"1/4 beat", "1/2 beat", "1 beat"};
    int snap = editor.snap == 0.25f ? 0 : (editor.snap == 0.5f ? 1 : 2);
    if (ImGui::Combo("##snap", &snap, snaps, 3)) {
        editor.snap = snap == 0 ? 0.25f : (snap == 1 ? 0.5f : 1.0f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu strings  |  %zu events  |  cursor %.2f", editor.document.pattern().tuning_midi.size(),
                        editor.document.pattern().events.size(), editor.cursor_beat);

    const float waveform_height = 136.0f;
    const float main_height = std::max(255.0f, ImGui::GetContentRegionAvail().y - waveform_height - 65.0f);
    const float inspector_width = 268.0f;
    const float canvas_width = std::max(300.0f, ImGui::GetContentRegionAvail().x - inspector_width - 8.0f);
    ImGui::BeginChild("Tab", ImVec2(canvas_width, main_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("TAB  /  highest string first");
    ImGui::SameLine();
    ImGui::TextDisabled("  fret = pluck   x = damp   + = body hit");
    canvas(editor, ImGui::GetContentRegionAvail());
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Inspector", ImVec2(0.0f, main_height), ImGuiChildFlags_Borders);
    inspector(editor);
    ImGui::EndChild();

    waveform(editor, waveform_height);
    if (editor.status_error) {
        ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.38f, 1.0f), "%s", editor.status.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.96f, 0.74f, 0.37f, 1.0f), "%s", editor.status.c_str());
    }
    if (!editor.rendered.audio.mono.empty() && !editor.current_audio()) {
        ImGui::TextDisabled("Waveform: previous revision.  Space play/stop | Ctrl/Cmd+S save | Ctrl/Cmd+Z undo");
    } else {
        ImGui::TextDisabled("Space play/stop | Ctrl/Cmd+S save | Ctrl/Cmd+Z undo | Delete event");
    }
    unsaved_dialog(editor);
    ImGui::End();
}

struct Host {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool platform_backend = false;
    bool render_backend = false;
    bool imgui = false;
    bool implot = false;

    ~Host() {
        if (render_backend) {
            ImGui_ImplSDLRenderer3_Shutdown();
        }
        if (platform_backend) {
            ImGui_ImplSDL3_Shutdown();
        }
        if (implot) {
            ImPlot::DestroyContext();
        }
        if (imgui) {
            ImGui::DestroyContext();
        }
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }

    void initialize() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL video: ") + SDL_GetError());
        }
        window = SDL_CreateWindow("gtracker - acoustic pattern editor", 1152, 760,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) {
            throw std::runtime_error(std::string("Window: ") + SDL_GetError());
        }
        SDL_SetWindowMinimumSize(window, 980, 680);
        const char* render_driver = SDL_getenv("SDL_RENDER_DRIVER");
        const char* video_driver = SDL_GetCurrentVideoDriver();
        if (!render_driver && video_driver && std::string(video_driver) == "dummy") {
            render_driver = "software";
        }
        renderer = SDL_CreateRenderer(window, render_driver);
        if (!renderer) {
            throw std::runtime_error(std::string("Renderer: ") + SDL_GetError());
        }
        SDL_SetRenderVSync(renderer, 1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui = true;
        ImPlot::CreateContext();
        implot = true;
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImFontConfig font;
        font.SizePixels = 16.0f;
        io.Fonts->AddFontDefault(&font);
        theme();
        platform_backend = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        if (!platform_backend) {
            throw std::runtime_error("ImGui SDL3 platform backend initialization failed");
        }
        render_backend = ImGui_ImplSDLRenderer3_Init(renderer);
        if (!render_backend) {
            throw std::runtime_error("ImGui SDL renderer backend initialization failed");
        }
    }

    void screenshot(const std::filesystem::path& path) {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        if (!surface) {
            throw std::runtime_error(std::string("Screenshot readback: ") + SDL_GetError());
        }
        const bool saved = SDL_SaveBMP(surface, path.u8string().c_str());
        SDL_DestroySurface(surface);
        if (!saved) {
            throw std::runtime_error(std::string("Screenshot save: ") + SDL_GetError());
        }
    }
};

int run(const Arguments& args) {
    Host host;
    host.initialize();
    Editor editor(args);
    if (args.smoke) {
        editor.request_render();
    }
    const auto started = std::chrono::steady_clock::now();
    bool running = true;
    bool captured = false;
    int frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(host.window))) {
                editor.document.request_close();
            }
        }
        editor.poll_render();
        if (args.smoke && editor.render_failed) {
            throw std::runtime_error(editor.status);
        }
        if (editor.playing && editor.audio_stream && SDL_GetAudioStreamQueued(editor.audio_stream) == 0) {
            editor.playing = false;
            editor.message("Playback finished.");
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        interface(editor);
        if (editor.document.close_requested()) running = false;
        ImGui::Render();
        const auto& io = ImGui::GetIO();
        SDL_SetRenderScale(host.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(host.renderer, 11, 15, 21, 255);
        SDL_RenderClear(host.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), host.renderer);
        ++frames;
        const bool frame_limit = args.frames > 0 && frames >= args.frames &&
                                 (!args.smoke || editor.current_audio());
        if (!args.screenshot.empty() && (!running || frame_limit)) {
            host.screenshot(args.screenshot);
            captured = true;
        }
        SDL_RenderPresent(host.renderer);
        if (frame_limit) {
            running = false;
        }
        if (args.smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            throw std::runtime_error("UI smoke exceeded 30 seconds waiting for the offline render");
        }
        SDL_Delay(8);
    }
    if (!args.screenshot.empty() && !captured) {
        throw std::runtime_error("No screenshot was captured");
    }
    if (args.smoke) {
        if (!editor.current_audio() || frames < args.frames) {
            throw std::runtime_error("UI smoke closed before its render and frame checks completed");
        }
        std::cout << "gtracker_editor: smoke passed; frames=" << frames
                  << "; rendered_samples=" << editor.rendered.audio.mono.size()
                  << "; revision=" << editor.document.pattern().revision << '\n';
    }
    return 0;
}

} // namespace

#ifndef GT_EDITOR_TEST
int main(int argc, char* argv[]) {
    try {
        const Arguments args = arguments(argc, argv);
        if (args.help) {
            std::cout << "gtracker_editor [pattern.gtrk.json] [--script PATH]\n"
                         "                [--data-dir PATH] [--resource-dir PATH]\n"
                         "                [--smoke-test] [--frames N] [--screenshot PATH.bmp]\n\n"
                         "Default saves go to the per-user application data folder.\n"
                         "Bundled scripts resolve beside the executable; explicit paths use the working directory.\n"
                         "The smoke test renders a pattern and draws at least N frames (default 8).\n"
                         "Use SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software for headless smoke.\n"
                         "Screenshots are BMP files. --screenshot implies --frames 8 if omitted.\n"
                         "Space: render/play/stop. Ctrl/Cmd+S: save. Ctrl/Cmd+Z: undo. Delete: event.\n";
            return 0;
        }
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "gtracker_editor: " << error.what() << '\n';
        return 1;
    }
}
#endif
