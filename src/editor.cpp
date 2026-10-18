#include "gtracker/project.hpp"
#include "gtracker/editor_document.hpp"
#include "gtracker/editor_recovery.hpp"
#include "gtracker/io.hpp"
#include "gtracker/take.hpp"
#include "gtracker/timeline.hpp"
#include "gtracker/proposals.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <implot.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
    std::filesystem::path take;
    std::filesystem::path script;
    std::filesystem::path screenshot;
    std::filesystem::path data_dir;
    std::filesystem::path resource_dir;
    int frames = 0;
    bool smoke = false;
    bool help = false;
    bool recovery = true;
};

Arguments arguments(int argc, char* argv[]) {
    Arguments result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> const char* {
            if (++i >= argc) {
                throw std::invalid_argument("missing value after " + argument);
            }
            if (argv[i][0] == '\0') throw std::invalid_argument("empty value after " + argument);
            return argv[i];
        };
        if (argument == "--script") {
            result.script = std::filesystem::u8path(value());
        } else if (argument == "--take") {
            result.take = std::filesystem::u8path(value());
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
        } else if (argument == "--no-recovery") {
            result.recovery = false;
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
    if (!result.document.empty() && !result.take.empty()) {
        throw std::invalid_argument("Choose a pattern path or --take; a take supplies its own editable pattern.");
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
    double end_beat = 0;
    std::vector<double> times;
    std::vector<double> beats;
    std::vector<double> minimum;
    std::vector<double> maximum;
};

struct RenderJob {
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> completed_frames{0}, total_frames{0};
};

struct FretEntry {
    std::string id;
    int value = 0, digits = 0;
    double time = -1;
    std::uint64_t generation = 0;
};

CompletedRender render_snapshot(const Pattern& pattern, const gtracker::RenderControl& control = {}) {
    CompletedRender result;
    result.audio = gtracker::render_pattern(pattern, {}, control);
    result.end_beat = static_cast<double>(result.audio.frames()) / result.audio.sample_rate * pattern.bpm / 60.0;
    const std::size_t count = std::min<std::size_t>(2048, result.audio.frames());
    result.times.reserve(count);
    result.beats.reserve(count);
    result.minimum.reserve(count);
    result.maximum.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (control.cancelled && control.cancelled()) throw gtracker::RenderCancelled();
        const std::size_t begin = i * result.audio.frames() / count;
        const std::size_t end = (i + 1) * result.audio.frames() / count;
        auto bounds = std::minmax_element(result.audio.samples.begin() + begin * result.audio.channels,
                                          result.audio.samples.begin() + end * result.audio.channels);
        const double seconds = static_cast<double>(begin + end) * 0.5 / result.audio.sample_rate;
        result.times.push_back(seconds);
        result.beats.push_back(seconds * pattern.bpm / 60.0);
        result.minimum.push_back(*bounds.first);
        result.maximum.push_back(*bounds.second);
    }
    return result;
}

struct SourcePreview {
    gtracker::Take take;
    std::vector<double> beats;
    std::vector<double> minimum;
    std::vector<double> maximum;
};

SourcePreview load_source_preview(const std::filesystem::path& path) {
    SourcePreview preview;
    preview.take = gtracker::load_take(path);
    const auto& audio = preview.take.audio;
    const auto count = std::min<std::size_t>(2048, static_cast<std::size_t>(audio.frames));
    preview.beats.reserve(count);
    preview.minimum.reserve(count);
    preview.maximum.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto begin = i * audio.frames / count;
        const auto end = (i + 1) * audio.frames / count;
        const auto bounds = std::minmax_element(audio.interleaved.begin() + static_cast<std::ptrdiff_t>(begin * audio.channels),
                                                audio.interleaved.begin() + static_cast<std::ptrdiff_t>(end * audio.channels));
        preview.beats.push_back(preview.take.source_frame_to_beat(static_cast<double>(begin + end) * 0.5));
        preview.minimum.push_back(*bounds.first);
        preview.maximum.push_back(*bounds.second);
    }
    return preview;
}

template<std::size_t N>
void set_text(std::array<char, N>& destination, const std::string& text) {
    if (text.size() >= N) throw std::invalid_argument("Editor paths must fit within 1023 UTF-8 bytes.");
    std::fill(destination.begin(), destination.end(), '\0');
    std::copy(text.begin(), text.end(), destination.begin());
}

struct Editor {
    gtracker::EditorDocument document{initial_pattern()};
    std::filesystem::path data_directory;
    std::filesystem::path resource_directory;
    std::uint64_t pending_generation = 0;
    std::uint64_t rendered_generation = 0;
    std::uint64_t rendered_revision = 0;
    std::future<CompletedRender> pending;
    std::shared_ptr<RenderJob> render_job;
    std::future<gtracker::StreamRenderResult> pending_export;
    std::shared_ptr<RenderJob> export_job;
    std::optional<gtracker::StreamRenderResult> export_result;
    std::filesystem::path export_destination, export_replace_path;
    bool open_export_panel = false;
    std::uint64_t exported_revision = 0;
    std::string export_error;
    CompletedRender rendered;
    std::optional<SourcePreview> source;
    std::optional<SourcePreview> pending_source;
    std::optional<gtracker::NoteProposals> proposals;
    std::vector<int> proposal_alternatives; // -1 skips a note until explicitly chosen.
    float proposal_strength = 0.6f;
    struct AudioOutput { SDL_AudioDeviceID id; std::string name; };
    std::vector<AudioOutput> audio_outputs;
    SDL_AudioDeviceID audio_output = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    std::string audio_output_name = "System default";
    bool audio_outputs_queried = false, audio_output_available = true;
    SDL_AudioStream* audio_stream = nullptr;
    int audio_rate = 0;
    int audio_channels = 0;
    int selected = 0;
    int selected_gesture = -1;
    int cursor_lane = 0;
    double cursor_beat = 0.0;
    float snap = 0.5f;
    bool status_error = false;
    bool render_failed = false;
    bool play_after_render = false;
    bool playing = false;
    bool source_from_zero = true;
    gtracker::BeatViewport view;
    gtracker::TimelineGeometry timeline_geometry;
    gtracker::TimelineGeometry tab_geometry;
    bool follow_view_fit = true;
    bool view_all_context = false;
    bool auto_wave_gain = true;
    bool tab_keyboard_focus = false;
    FretEntry fret_entry;
    bool recovery_enabled = true, open_recovery_panel = false;
    std::optional<gtracker::RecoveryCopies> recovery;
    gtracker::RecoveryListing recovery_listing;
    std::filesystem::path recovery_save_suggestion;
    std::string recovery_error;
    std::string status = "Select a fret number, edit the inspector, then Render and Play.";
    std::array<char, 1024> path{};
    std::array<char, 1024> script_path{};
    std::array<char, 1024> take_path{};
    std::array<char, 1024> proposal_path{};
    std::array<char, 1024> export_path{};
    std::array<char, 1024> confirmation_save_path{};

    explicit Editor(const Arguments& args) {
        data_directory = editor_data_directory(args);
        resource_directory = editor_resource_directory(args);
        set_text(export_path,(data_directory/"render.wav").u8string());
        for (const auto& argument : {args.document, args.take, args.script}) {
            if (argument.u8string().size() >= path.size()) throw std::invalid_argument("Editor launch paths must fit within 1023 UTF-8 bytes.");
        }
        if (!args.take.empty()) source = load_source_preview(args.take);
        const auto document_path = source ? source->take.pattern_path : args.document;
        const std::string document_text = document_path.empty()
            ? (data_directory / "sketch.gtrk.json").u8string() : document_path.u8string();
        const std::string script_text = args.script.empty()
            ? (resource_directory / "scripts/soften.lua").u8string() : args.script.u8string();
        set_text(path, document_text);
        set_text(script_path, script_text);
        if (!args.take.empty()) set_text(take_path, source->take.manifest_path.u8string());
        if (!document_path.empty()) {
            document = gtracker::EditorDocument(source ? source->take.pattern : gtracker::load_pattern(document_path), document_path);
        }
        if (!args.script.empty()) {
            Pattern edited = document.pattern();
            gtracker::apply_lua(edited, args.script);
            commit(std::move(edited), "Applied Lua script");
        }
        if (document.pattern().events.empty()) {
            selected = -1;
        } else {
            const auto& event = document.pattern().events.front();
            cursor_beat = event.beat;
            cursor_lane = event.type == "body_hit" ? static_cast<int>(document.pattern().tuning_midi.size()) : event.string_index;
        }
        fit_timeline(source.has_value());
        recovery_enabled = args.recovery;
        if (recovery_enabled) {
            try {
                refresh_recoveries();
                if (recovery && recovery->contains(document.filename())) {
                    recovery_save_suggestion = recovery->suggested_save_path(); restore_active_path();
                }
            }
            catch (const std::exception& error) { recovery_error = error.what(); message("Recovery unavailable: "+recovery_error,true); }
        }
    }

    ~Editor() {
        if (recovery && (!document.dirty() || document.close_requested())) {
            try { recovery->clear_owned(document.filename()); } catch (const std::exception&) { /* Keep the copy if cleanup fails. */ }
        }
        if (render_job) render_job->cancelled.store(true, std::memory_order_relaxed);
        if (export_job) export_job->cancelled.store(true, std::memory_order_relaxed);
        // The worker owns its snapshot. Request cancellation, then join without
        // allowing a worker to outlive its application resources.
        if (pending.valid()) {
            pending.wait();
        }
        if (pending_export.valid()) pending_export.wait();
        if (audio_stream) {
            SDL_DestroyAudioStream(audio_stream);
        }
    }

    void message(std::string text, bool error = false) {
        status = std::move(text);
        status_error = error;
    }

    bool rendering() const { return pending.valid(); }
    bool exporting() const { return pending_export.valid(); }
    bool current_audio() const {
        return rendered_generation == document.generation() && !rendered.audio.samples.empty();
    }

    void stop() {
        if (audio_stream) {
            SDL_ClearAudioStream(audio_stream);
            SDL_PauseAudioStreamDevice(audio_stream);
        }
        playing = false;
        play_after_render = false;
    }

    void close_audio_output() {
        stop();
        if (audio_stream) SDL_DestroyAudioStream(audio_stream);
        audio_stream = nullptr;
        audio_rate = audio_channels = 0;
    }

    void initialize_audio() {
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("Audio initialization: ")+SDL_GetError());
    }

    void refresh_audio_outputs() {
        audio_outputs_queried = true;
        initialize_audio();
        int count = 0;
        std::unique_ptr<SDL_AudioDeviceID,decltype(&SDL_free)> devices(SDL_GetAudioPlaybackDevices(&count),SDL_free);
        if (!devices) throw std::runtime_error(std::string("Audio output discovery: ")+SDL_GetError());
        std::vector<AudioOutput> found;
        for (int i=0;i<count;++i) {
            const auto name = SDL_GetAudioDeviceName(devices.get()[i]);
            if (name) found.push_back({devices.get()[i],name});
        }
        audio_outputs = std::move(found);
        audio_output_available = audio_output == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ||
            std::any_of(audio_outputs.begin(),audio_outputs.end(),[&](const auto& output) { return output.id == audio_output; });
        if (!audio_output_available) {
            close_audio_output();
            message("Selected audio output is unavailable. Choose an output and press Play.",true);
        }
    }

    void select_audio_output(SDL_AudioDeviceID id) {
        std::string name = "System default";
        if (id != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
            const auto found = std::find_if(audio_outputs.begin(),audio_outputs.end(),[&](const auto& output) { return output.id == id; });
            if (found == audio_outputs.end() || !SDL_GetAudioDeviceName(id))
                throw std::runtime_error("That audio output is unavailable. Refresh the output list.");
            name = found->name;
        }
        if (id == audio_output && audio_output_available) return;
        close_audio_output();
        audio_output = id; audio_output_name = std::move(name); audio_output_available = true;
        message("Audio output: "+audio_output_name+". Press Play when ready.");
    }

    void audio_device_removed(const SDL_AudioDeviceEvent& event) {
        if (event.recording || audio_output == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK || event.which != audio_output) return;
        close_audio_output(); audio_output_available = false;
        audio_outputs.erase(std::remove_if(audio_outputs.begin(),audio_outputs.end(),[&](const auto& output) { return output.id == event.which; }),audio_outputs.end());
        message("Selected audio output disconnected. Refresh outputs and choose a device.",true);
    }

    void changed() {
        if (render_job) render_job->cancelled.store(true, std::memory_order_relaxed);
        stop();
        if (selected >= static_cast<int>(document.pattern().events.size())) {
            selected = static_cast<int>(document.pattern().events.size()) - 1;
        }
        if (selected_gesture >= static_cast<int>(document.pattern().gestures.size())) selected_gesture = -1;
        if (selected_gesture >= 0) selected = -1;
        sync_timeline();
    }

    gtracker::BeatViewport timeline_extent() const {
        gtracker::BeatViewport result{0, document.pattern().length_beats};
        if (source) {
            result.first = std::min(result.first, source->take.source_frame_to_beat(0));
            result.last = std::max(result.last, source->take.source_frame_to_beat(static_cast<double>(source->take.audio.frames)));
        }
        result.last = std::max(result.last, rendered.end_beat);
        return result;
    }
    void fit_timeline(bool all_context) {
        follow_view_fit = true;
        view_all_context = all_context;
        const auto range = all_context ? timeline_extent() : gtracker::BeatViewport{0, document.pattern().length_beats};
        view.fit(range.first, range.last);
    }
    void sync_timeline() {
        if (follow_view_fit) fit_timeline(view_all_context);
        else {
            const auto bounds = timeline_extent();
            view.constrain(bounds.first, bounds.last);
        }
    }
    void zoom_timeline(double factor, double anchor) {
        const auto bounds = timeline_extent();
        view.zoom(factor, anchor, bounds.first, bounds.last);
        follow_view_fit = false;
    }
    void pan_timeline(double beats) {
        const auto bounds = timeline_extent();
        view.pan(beats, bounds.first, bounds.last);
        follow_view_fit = false;
    }

    void set_production(const gtracker::ProductionSettings& settings, const std::string& key = {}) {
        Pattern next = document.pattern();
        next.schema_version = 2;
        next.production = settings;
        commit(std::move(next), "Edited production", key);
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
        end_fret_entry();
        if (document.undo(redo)) {
            changed();
            message(redo ? "Redid document edit." : "Undid document edit.");
        }
    }

    void loaded() {
        const bool recovered = recovery && recovery->contains(document.filename());
        clear_recovery(document.filename());
        recovery_save_suggestion = recovered ? recovery->suggested_save_path() : std::filesystem::path{};
        end_fret_entry(); tab_keyboard_focus = false;
        proposals.reset(); proposal_alternatives.clear();
        source = std::move(pending_source);
        pending_source.reset();
        restore_active_path();
        if (source) set_text(take_path, source->take.manifest_path.u8string());
        selected = document.pattern().events.empty() ? -1 : 0;
        selected_gesture = -1;
        cursor_beat = 0.0;
        cursor_lane = 0;
        if (selected >= 0) {
            const auto& event = document.pattern().events.front();
            cursor_beat = event.beat;
            cursor_lane = event.type == "body_hit" ? static_cast<int>(document.pattern().tuning_midi.size()) : event.string_index;
        }
        fit_timeline(source.has_value());
        changed();
        message(recovered ? "Opened a recovery score copy. Save to the suggested new filename after reviewing it." : "Loaded " + document.pattern().name + ".");
    }

    void load() {
        if (exporting()) throw std::runtime_error("Finish or cancel WAV export before loading another document.");
        if (document.pending() != gtracker::EditorDocument::Pending::none) return;
        pending_source.reset();
        const auto before = document.generation();
        document.request_load(std::filesystem::u8path(path.data()));
        if (document.generation() != before) loaded();
        else restore_active_path();
    }

    void restore_active_path() {
        set_text(path, (!recovery_save_suggestion.empty() ? recovery_save_suggestion :
            (document.filename().empty() ? data_directory / "sketch.gtrk.json" : document.filename())).u8string());
    }

    void load_source() {
        if (exporting()) throw std::runtime_error("Finish or cancel WAV export before opening another source take.");
        if (document.pending() != gtracker::EditorDocument::Pending::none) return;
        auto preview = load_source_preview(std::filesystem::u8path(take_path.data()));
        if (preview.take.pattern_path.u8string().size() >= path.size() || preview.take.manifest_path.u8string().size() >= take_path.size())
            throw std::invalid_argument("This take's display paths exceed 1023 UTF-8 bytes.");
        const auto before = document.generation();
        document.request_load(preview.take.pattern_path);
        pending_source = std::move(preview);
        if (document.generation() != before) loaded();
        else restore_active_path();
    }

    std::filesystem::path save_destination(const char* value) const {
        if (!value[0]) throw std::invalid_argument("Enter a pattern path before saving.");
        auto destination = std::filesystem::u8path(value);
        if (exporting()) gtracker::require_distinct_output(destination,{export_destination});
        if (source) {
            if (std::filesystem::weakly_canonical(destination) != source->take.pattern_path)
                throw std::invalid_argument("Save the attached pattern in its take bundle. Detach source first to save a separate pattern copy.");
            destination = source->take.pattern_path;
        }
        return destination;
    }

    void detach_source() {
        if (document.pending() != gtracker::EditorDocument::Pending::none) return;
        document.end_edit();
        stop();
        source.reset();
        proposals.reset(); proposal_alternatives.clear();
        message("Source detached; its files are preserved. Save writes the pattern only.");
    }

    void load_proposals() {
        if (!source) throw std::runtime_error("Open a preserved source take before loading note proposals.");
        auto loaded = gtracker::load_note_proposals(std::filesystem::u8path(proposal_path.data()), source->take);
        const bool current = gtracker::note_proposals_match(loaded, document.pattern(), source->take);
        proposal_alternatives.assign(loaded.notes.size(), -1);
        proposals = std::move(loaded);
        message(current ? "Loaded monophonic candidates. Review timing and choose each fingering explicitly." : "Loaded a stale proposal job. Repeat analysis against the current pattern before accepting.");
    }

    void accept_proposals() {
        if (!source || !proposals) throw std::runtime_error("Open a source take and proposal job first.");
        const auto verified = gtracker::load_take(source->take.manifest_path);
        if (gtracker::pattern_json(verified.pattern) != gtracker::pattern_json(document.pattern()))
            throw std::runtime_error("The saved take pattern differs from the editor. Save/review it before accepting a proposal job.");
        std::vector<gtracker::ProposalChoice> choices;
        for (std::size_t i = 0; i < proposal_alternatives.size(); ++i)
            if (proposal_alternatives[i] >= 0) choices.push_back({i, static_cast<std::size_t>(proposal_alternatives[i])});
        auto next = gtracker::accept_note_proposals(document.pattern(), verified, *proposals, choices, proposal_strength);
        commit(std::move(next), "Accepted selected note proposals as one undoable edit.");
        std::fill(proposal_alternatives.begin(), proposal_alternatives.end(), -1);
    }

    void resolve(gtracker::EditorDocument::Decision decision) {
        const auto before = document.generation();
        const auto destination = decision == gtracker::EditorDocument::Decision::save
            ? save_destination(confirmation_save_path.data()) : std::filesystem::path{};
        document.resolve(decision, destination);
        if (!document.dirty() || document.close_requested()) clear_recovery(document.filename());
        if (document.generation() != before) loaded();
        if (decision == gtracker::EditorDocument::Decision::cancel) pending_source.reset();
    }

    void save() {
        end_fret_entry();
        document.save(save_destination(path.data()));
        clear_recovery(document.filename()); recovery_save_suggestion.clear();
        restore_active_path();
        message("Saved " + document.filename().filename().u8string() + ".");
    }

    void refresh_recoveries() {
        if (!recovery_enabled) return;
        if (!recovery) recovery.emplace(data_directory);
        recovery_listing = recovery->list();
        recovery_error.clear();
    }

    void clear_recovery(const std::filesystem::path& preserve = {}) {
        if (!recovery) return;
        try { recovery->clear_owned(preserve); }
        catch (const std::exception& error) { recovery_error = error.what(); }
    }

    void checkpoint_recovery(double seconds) {
        if (!recovery) return;
        try {
            if (recovery->update(document,seconds)) recovery_error.clear();
        } catch (const std::exception& error) {
            const std::string detail = error.what();
            if (detail != recovery_error) message("Recovery copy failed: "+detail,true);
            recovery_error = detail;
        }
    }

    void open_recovery(const std::filesystem::path& copy) {
        if (!recovery || !recovery->contains(copy)) throw std::runtime_error("Choose a recovery score from this data folder.");
        set_text(path,copy.u8string()); load();
    }

    void run_script() {
        Pattern next = document.pattern();
        gtracker::apply_lua(next, std::filesystem::u8path(script_path.data()));
        commit(std::move(next), "Applied Lua script");
    }

    std::string new_id(const char* prefix = "event-") const {
        const auto& pattern = document.pattern();
        std::size_t suffix = pattern.events.size() + pattern.gestures.size() + 1;
        for (;;) {
            const std::string id = prefix + std::to_string(suffix++);
            if (std::none_of(pattern.events.begin(), pattern.events.end(),
                             [&](const Event& event) { return event.id == id; }) &&
                std::none_of(pattern.gestures.begin(), pattern.gestures.end(),
                             [&](const gtracker::Gesture& gesture) { return gesture.id == id; })) {
                return id;
            }
        }
    }

    void end_fret_entry() {
        if (fret_entry.time >= 0) document.end_edit();
        fret_entry = {};
    }

    bool at_cursor(const Event& event) const {
        const int lane = event.type == "body_hit" ? static_cast<int>(document.pattern().tuning_midi.size()) : event.string_index;
        return lane == cursor_lane && std::abs(event.beat-cursor_beat) <= 1e-9;
    }

    void move_cursor(int beats, int lanes, int edge = 0) {
        end_fret_entry();
        document.end_edit();
        const auto& pattern = document.pattern();
        const double proposed = cursor_beat + beats * static_cast<double>(snap);
        if (edge < 0) cursor_beat = 0;
        else if (edge > 0) cursor_beat = std::floor(std::nextafter(pattern.length_beats,0.0)/snap)*snap;
        else if (proposed >= 0 && proposed < pattern.length_beats) cursor_beat = proposed;
        else if (proposed < 0) cursor_beat = 0;
        cursor_lane = std::clamp(cursor_lane+lanes,0,static_cast<int>(pattern.tuning_midi.size()));
        selected = -1; selected_gesture = -1;
        int matches = 0;
        for (std::size_t i = 0; i < pattern.events.size(); ++i) if (at_cursor(pattern.events[i])) {
            selected = static_cast<int>(i); ++matches;
        }
        if (matches > 1) selected = -1;
        if (selected < 0) for (std::size_t i = 0; i < pattern.gestures.size(); ++i)
            if (pattern.gestures[i].string_index == cursor_lane && std::abs(pattern.gestures[i].beat-cursor_beat) <= 1e-9) { selected_gesture = static_cast<int>(i); break; }
        if (cursor_beat < view.first || cursor_beat > view.last) {
            const auto bounds = timeline_extent();
            view.pan(cursor_beat < view.first ? cursor_beat-view.first : cursor_beat-view.last, bounds.first, bounds.last);
            follow_view_fit = false;
        }
    }

    int cursor_event() const {
        const auto& events = document.pattern().events;
        if (selected >= 0 && selected < static_cast<int>(events.size()) && at_cursor(events[static_cast<std::size_t>(selected)])) return selected;
        int found = -1;
        for (std::size_t i = 0; i < events.size(); ++i) if (at_cursor(events[i])) {
            if (found >= 0) throw std::runtime_error("Several events share this cell. Select the event to edit first.");
            found = static_cast<int>(i);
        }
        return found;
    }

    void enter_fret(int digit, double time) {
        const auto& pattern = document.pattern();
        if (cursor_lane < 0 || cursor_lane >= static_cast<int>(pattern.tuning_midi.size())) throw std::runtime_error("Frets belong on string lanes. B adds a body hit.");
        int index = cursor_event();
        const bool continuing = fret_entry.time >= 0 && time-fret_entry.time <= .8 && fret_entry.digits == 1 &&
            fret_entry.generation == document.generation() && index >= 0 && pattern.events[static_cast<std::size_t>(index)].id == fret_entry.id;
        const int value = continuing ? fret_entry.value*10+digit : digit;
        if (value > 36) { end_fret_entry(); throw std::runtime_error("Frets are 0..36. The first digit remains."); }
        if (!continuing) { end_fret_entry(); document.end_edit(); }
        Event event;
        if (index >= 0) event = pattern.events[static_cast<std::size_t>(index)];
        else { event.id = new_id(); event.beat = cursor_beat; event.string_index = cursor_lane; event.strength = .65f; }
        if (event.type != "pluck") event.strength = .65f;
        event.type = "pluck"; event.fret = static_cast<float>(value);
        const std::string key = "tab-fret-" + event.id;
        if (index >= 0) replace_event(static_cast<std::size_t>(index),event,key);
        else {
            Pattern next = pattern;
            for (auto& gesture : next.gestures) if (gesture.beat == event.beat && gesture.string_index == event.string_index) gesture.points.front().fret = event.fret;
            next.events.push_back(event);
            commit(std::move(next),"Entered fret",key);
            index = static_cast<int>(document.pattern().events.size())-1;
        }
        selected = index; selected_gesture = -1;
        fret_entry = {event.id,value,continuing ? 2 : 1,time,document.generation()};
        if (fret_entry.digits == 2) document.end_edit();
    }

    void enter_mark(bool body) {
        end_fret_entry(); document.end_edit();
        if (body) cursor_lane = static_cast<int>(document.pattern().tuning_midi.size());
        else if (cursor_lane >= static_cast<int>(document.pattern().tuning_midi.size())) throw std::runtime_error("Choose a string lane for damping.");
        const int index = cursor_event();
        if (index < 0) { add(body ? "body_hit" : "damp"); return; }
        Event event = document.pattern().events[static_cast<std::size_t>(index)];
        event.type = body ? "body_hit" : "damp";
        event.strength = body ? .65f : 1.0f;
        replace_event(static_cast<std::size_t>(index),event,{});
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
        selected_gesture = -1;
        selected = static_cast<int>(document.pattern().events.size()) - 1;
    }

    void set_model(const char* model) {
        Pattern next = document.pattern();
        next.schema_version = 2;
        next.model = model;
        commit(std::move(next), "Selected explicit v2 instrument model");
    }

    void replace_event(std::size_t index, Event event, const std::string& key) {
        Pattern next = document.pattern();
        const auto& before = next.events.at(index);
        if (event.type == "pluck" && before.beat == event.beat &&
            before.string_index == event.string_index && (before.type != "pluck" || before.fret != event.fret)) {
            bool shared = false;
            for (auto& gesture : next.gestures) if (gesture.beat == event.beat && gesture.string_index == event.string_index) {
                gesture.points.front().fret = event.fret;
                shared = true;
            }
            if (shared) for (auto& other : next.events) if (other.type == "pluck" && other.beat == event.beat && other.string_index == event.string_index)
                other.fret = event.fret;
        }
        next.events[index] = event;
        commit(std::move(next), "Edited event", key);
        cursor_beat = event.beat;
        cursor_lane = event.type == "body_hit" ? static_cast<int>(document.pattern().tuning_midi.size()) : event.string_index;
    }

    void replace_slide(std::size_t index, gtracker::Gesture gesture, const std::string& key) {
        if (gesture.points.empty()) throw std::invalid_argument("A slide requires fret points.");
        Pattern next = document.pattern();
        const auto& before = next.gestures.at(index);
        if (before.beat == gesture.beat && before.string_index == gesture.string_index &&
            before.points.front().fret != gesture.points.front().fret) {
            for (auto& event : next.events) if (event.type == "pluck" && event.beat == gesture.beat && event.string_index == gesture.string_index)
                event.fret = gesture.points.front().fret;
        }
        next.gestures[index] = gesture;
        commit(std::move(next), "Edited slide curve", key);
        cursor_beat = gesture.beat;
        cursor_lane = gesture.string_index;
    }

    void add_slide() {
        const auto& pattern = document.pattern();
        if (!gtracker::supports_slides(pattern)) throw std::runtime_error("Enable slides before adding a contact curve.");
        if (cursor_lane < 0 || cursor_lane >= static_cast<int>(pattern.tuning_midi.size())) throw std::runtime_error("Choose a string lane for the slide.");
        Pattern next = pattern;
        gtracker::Gesture gesture;
        gesture.id = new_id("slide-");
        gesture.beat = std::clamp(cursor_beat, 0.0, std::nextafter(pattern.length_beats, 0.0));
        gesture.string_index = cursor_lane;
        double duration = std::min(1.0, pattern.length_beats - gesture.beat);
        double last_contact = -1;
        float fret = 0;
        for (const auto& event : pattern.events) if (event.type == "pluck" && event.string_index == cursor_lane) {
            if (event.beat <= gesture.beat && event.beat >= last_contact) { last_contact = event.beat; fret = event.fret; }
            if (event.beat > gesture.beat) duration = std::min(duration, event.beat - gesture.beat);
        }
        for (const auto& existing : pattern.gestures) if (existing.string_index == cursor_lane) {
            const double end = existing.beat + existing.points.back().beat;
            if (existing.beat <= gesture.beat && end > gesture.beat) throw std::runtime_error("The string already has a slide at this beat.");
            if (end <= gesture.beat && end > last_contact) { last_contact = end; fret = existing.points.back().fret; }
            if (existing.beat > gesture.beat) duration = std::min(duration, existing.beat - gesture.beat);
        }
        gesture.points = {{0, fret}, {duration, fret <= 34 ? fret + 2 : fret - 2}};
        next.gestures.push_back(std::move(gesture));
        commit(std::move(next), "Added continuous slide");
        selected = -1;
        selected_gesture = static_cast<int>(document.pattern().gestures.size()) - 1;
    }

    void erase() {
        const auto& pattern = document.pattern();
        if (selected_gesture >= 0 && selected_gesture < static_cast<int>(pattern.gestures.size())) {
            Pattern next = pattern;
            next.gestures.erase(next.gestures.begin() + selected_gesture);
            commit(std::move(next), "Deleted slide");
            selected_gesture = -1;
            return;
        }
        if (selected < 0 || selected >= static_cast<int>(pattern.events.size())) {
            return;
        }
        Pattern next = pattern;
        next.events.erase(next.events.begin() + selected);
        commit(std::move(next), "Deleted event");
    }

    std::filesystem::path checked_export_destination(const std::filesystem::path& value) const {
        if (value.empty()) throw std::runtime_error("Enter a WAV output path.");
        auto destination = std::filesystem::weakly_canonical(std::filesystem::absolute(value));
        auto extension = destination.extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".wav") throw std::runtime_error("WAV export needs a .wav filename.");
        std::vector<std::filesystem::path> inputs{document.filename(),std::filesystem::u8path(path.data()),
            std::filesystem::u8path(script_path.data()),std::filesystem::u8path(proposal_path.data())};
        if (source) inputs.insert(inputs.end(),{source->take.source_path,source->take.pattern_path,source->take.manifest_path,source->take.analysis_path});
        gtracker::require_distinct_output(destination,inputs);
        return destination;
    }

    void start_export(const std::filesystem::path& value) {
        if (exporting()) return;
        const auto destination = checked_export_destination(value);
        // Use one immutable score and resolved output. Edits can continue, but
        // loading another source/document waits until this output is settled.
        end_fret_entry(); document.end_edit();
        const Pattern snapshot = document.pattern();
        gtracker::validate_pattern(snapshot);
        auto job = std::make_shared<RenderJob>();
        export_job = job; export_destination = destination; exported_revision = snapshot.revision;
        export_result.reset(); export_error.clear();
        try {
            pending_export = std::async(std::launch::async,[snapshot,destination,job] {
                gtracker::StreamOptions stream;
                stream.cancelled = [job] { return job->cancelled.load(std::memory_order_relaxed); };
                stream.progress = [job](const gtracker::RenderProgress& progress) {
                    job->total_frames.store(progress.processing_frames,std::memory_order_relaxed);
                    job->completed_frames.store(progress.processed_frames,std::memory_order_relaxed);
                };
                if (stream.cancelled()) throw gtracker::RenderCancelled();
                return gtracker::render_pattern_wav(snapshot,destination,{},stream);
            });
        } catch (...) { export_job.reset(); throw; }
        message("Exporting revision "+std::to_string(exported_revision)+" to "+destination.filename().u8string()+"...");
    }

    void request_export() {
        if (exporting()) return;
        const auto destination = checked_export_destination(std::filesystem::u8path(export_path.data()));
        if (std::filesystem::exists(destination)) { export_replace_path = destination; return; }
        start_export(destination);
    }

    void replace_export() {
        if (export_replace_path.empty()) return;
        start_export(export_replace_path);
        export_replace_path.clear();
    }

    void cancel_export() {
        if (!exporting() || !export_job) return;
        export_job->cancelled.store(true,std::memory_order_relaxed);
        message("WAV export cancellation requested; an already completed export is kept.");
    }

    void poll_export() {
        if (!exporting() || pending_export.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        export_job.reset();
        try {
            // The native exporter has already atomically published this file.
            // A cancellation racing with completion cannot retract that success.
            export_result = pending_export.get();
            message("Exported revision "+std::to_string(exported_revision)+" to "+export_destination.filename().u8string()+
                ". Clipped samples: "+std::to_string(export_result->clipped_samples)+".");
        } catch (const gtracker::RenderCancelled&) { message("WAV export cancelled; previous output retained."); }
        catch (const std::exception& error) { export_error = error.what(); message("WAV export failed: "+export_error,true); }
    }

    void request_render(bool then_play = false) {
        if (rendering()) {
            return;
        }
        end_fret_entry(); document.end_edit();
        const Pattern snapshot = document.pattern();
        gtracker::validate_pattern(snapshot);
        pending_generation = document.generation();
        play_after_render = then_play;
        render_failed = false;
        auto job = std::make_shared<RenderJob>();
        render_job = job;
        try {
            pending = std::async(std::launch::async, [snapshot, job]() {
                gtracker::RenderControl control;
                control.cancelled = [job] { return job->cancelled.load(std::memory_order_relaxed); };
                control.progress = [job](std::uint64_t completed, std::uint64_t total) {
                    job->total_frames.store(total, std::memory_order_relaxed);
                    job->completed_frames.store(completed, std::memory_order_relaxed);
                };
                return render_snapshot(snapshot, control);
            });
        } catch (...) {
            render_job.reset(); play_after_render = false; throw;
        }
        message("Rendering revision " + std::to_string(snapshot.revision) + "...");
    }

    void cancel_render() {
        if (!rendering() || !render_job) return;
        render_job->cancelled.store(true, std::memory_order_relaxed);
        play_after_render = false;
        message("Cancelling render; the previous completed preview is retained.");
    }

    void poll_render() {
        if (!rendering() || pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            return;
        }
        const auto job = std::move(render_job);
        try {
            CompletedRender completed = pending.get();
            if (job && job->cancelled.load(std::memory_order_relaxed)) throw gtracker::RenderCancelled();
            if (pending_generation != document.generation()) {
                play_after_render = false;
                message("Render finished for an older edit and was discarded. Render the current pattern.");
                return;
            }
            rendered = std::move(completed);
            rendered_generation = document.generation();
            rendered_revision = document.pattern().revision;
            char text[256];
            std::snprintf(text, sizeof(text), "Rendered revision %llu in %.1f ms (%s). Peak %.3f; RMS %.3f; clipped %llu.",
                          static_cast<unsigned long long>(rendered_revision), rendered.audio.elapsed_ms,
                          rendered.audio.channels == 2 ? "stereo" : "mono", rendered.audio.peak, rendered.audio.rms,
                          static_cast<unsigned long long>(rendered.audio.clipped_samples));
            message(text);
            if (play_after_render) {
                play_after_render = false;
                play();
            }
        } catch (const gtracker::RenderCancelled&) {
            render_failed = false;
            play_after_render = false;
            message(pending_generation == document.generation() ? "Render cancelled; previous completed preview retained." :
                "Obsolete render cancelled. Render the current pattern.");
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
        queue_audio(rendered.audio.samples.data(), rendered.audio.samples.size(), rendered.audio.sample_rate, rendered.audio.channels);
        message("Playing rendered revision " + std::to_string(rendered_revision) + ".");
    }

    void play_source() {
        if (!source) return;
        const auto& take = source->take;
        const auto range = source_playback_range();
        const auto begin = static_cast<std::size_t>(range.first) * take.audio.channels;
        const auto count = static_cast<std::size_t>(range.second - range.first) * take.audio.channels;
        queue_audio(take.audio.interleaved.data() + begin, count, take.audio.sample_rate, take.audio.channels);
        message("Playing the original source selection; native channel count and timing retained.");
    }

    std::pair<std::uint64_t, std::uint64_t> source_playback_range() const {
        if (!source) throw std::runtime_error("No source take is attached.");
        const auto& timing = source->take.alignment;
        auto begin = timing.selection_start_frame;
        if (source_from_zero) {
            const auto zero = static_cast<std::int64_t>(timing.pattern_zero_frame) + timing.input_offset_frames;
            if (zero > 0) begin = std::max(begin, static_cast<std::uint64_t>(zero));
        }
        if (begin >= timing.selection_end_frame)
            throw std::runtime_error("This source selection ends before beat zero. Disable From beat 0 or adjust the take alignment.");
        return {begin, timing.selection_end_frame};
    }

    void queue_audio(const float* samples, std::size_t count, int rate, int channels) {
        if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()) / sizeof(float))
            throw std::runtime_error("Audio exceeds this prototype's playback queue limit.");
        stop();
        if (!audio_output_available || (audio_output != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && !SDL_GetAudioDeviceName(audio_output))) {
            close_audio_output(); audio_output_available = false;
            throw std::runtime_error("Selected audio output is unavailable. Refresh outputs and choose a device.");
        }
        if (!audio_stream || audio_rate != rate || audio_channels != channels) {
            if (audio_stream) {
                SDL_DestroyAudioStream(audio_stream);
                audio_stream = nullptr;
            }
            initialize_audio();
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_F32;
            spec.channels = channels;
            spec.freq = rate;
            audio_stream = SDL_OpenAudioDeviceStream(audio_output, &spec, nullptr, nullptr);
            if (!audio_stream) {
                throw std::runtime_error(std::string("Audio output: ") + SDL_GetError());
            }
            audio_rate = spec.freq;
            audio_channels = channels;
        }
        const auto bytes = count * sizeof(float);
        if (!SDL_PutAudioStreamData(audio_stream, samples, static_cast<int>(bytes)) ||
            !SDL_FlushAudioStream(audio_stream) || !SDL_ResumeAudioStreamDevice(audio_stream)) {
            const std::string error = SDL_GetError();
            close_audio_output();
            throw std::runtime_error("Audio playback: " + error);
        }
        playing = true;
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

void tab_keyboard(Editor& editor) {
    const auto& io = ImGui::GetIO();
    const double time = ImGui::GetTime();
    if (io.AppFocusLost) { editor.tab_keyboard_focus = false; editor.end_fret_entry(); return; }
    if (editor.fret_entry.time >= 0 && (time-editor.fret_entry.time > .8 || !editor.tab_keyboard_focus)) editor.end_fret_entry();
    if (editor.document.pending() != gtracker::EditorDocument::Pending::none) { editor.end_fret_entry(); return; }
    if (!editor.tab_keyboard_focus) return;
    if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopup)) { editor.end_fret_entry(); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab,false) || ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
        editor.tab_keyboard_focus = false; editor.end_fret_entry(); return;
    }
    if (io.KeyCtrl || io.KeySuper || io.KeyAlt || io.KeyShift) { editor.end_fret_entry(); return; }
    const auto route = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat;
    if (ImGui::Shortcut(ImGuiKey_LeftArrow,route)) editor.move_cursor(-1,0);
    if (ImGui::Shortcut(ImGuiKey_RightArrow,route)) editor.move_cursor(1,0);
    if (ImGui::Shortcut(ImGuiKey_UpArrow,route)) editor.move_cursor(0,-1);
    if (ImGui::Shortcut(ImGuiKey_DownArrow,route)) editor.move_cursor(0,1);
    if (ImGui::Shortcut(ImGuiKey_Home,route)) editor.move_cursor(0,0,-1);
    if (ImGui::Shortcut(ImGuiKey_End,route)) editor.move_cursor(0,0,1);
    for (int digit = 0; digit <= 9; ++digit)
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0+digit),false) || ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0+digit),false))
            editor.attempt([&] { editor.enter_fret(digit,time); });
    if (ImGui::IsKeyPressed(ImGuiKey_X,false)) editor.attempt([&] { editor.enter_mark(false); });
    if (ImGui::IsKeyPressed(ImGuiKey_B,false)) editor.attempt([&] { editor.enter_mark(true); });
}

void canvas(Editor& editor, ImVec2 size, const gtracker::TimelineGeometry* shared = nullptr) {
    size.x = std::max(size.x, 200.0f);
    size.y = std::max(size.y, 170.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tab_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool panning = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !clicked) {
        editor.tab_keyboard_focus = false; editor.end_fret_entry();
    }
    const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(14, 22, 30, 255));

    const int strings = static_cast<int>(editor.document.pattern().tuning_midi.size());
    const int lanes = strings + 1;
    const gtracker::TimelineGeometry geometry = shared ? *shared
        : gtracker::TimelineGeometry{editor.view, origin.x + 64.0f, origin.x + size.x - 20.0f};
    editor.tab_geometry = geometry;
    const float left = geometry.left;
    const float right = geometry.right;
    const float top = origin.y + 32.0f;
    const float lane_height = (size.y - 42.0f) / static_cast<float>(lanes);
    const double pixels_per_beat = static_cast<double>(right - left) / geometry.view.span();
    auto lane_y = [&](int lane) { return top + (static_cast<float>(lane) + 0.5f) * lane_height; };
    auto beat_x = [&](double beat) {
        return geometry.beat_x(beat);
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

    draw->PushClipRect(ImVec2(left - 14, origin.y), ImVec2(right + 14, origin.y + size.y), true);
    if (geometry.view.first < 0)
        draw->AddRectFilled(ImVec2(left, top), ImVec2(std::min(right, beat_x(0)), origin.y + size.y), IM_COL32(7, 12, 18, 145));
    if (geometry.view.last > editor.document.pattern().length_beats)
        draw->AddRectFilled(ImVec2(std::max(left, beat_x(editor.document.pattern().length_beats)), top), ImVec2(right, origin.y + size.y), IM_COL32(7, 12, 18, 145));

    double grid = 1.0;
    while (grid * pixels_per_beat > 150.0 && grid > 1.0 / 64) grid *= 0.5;
    while (grid * pixels_per_beat < 34.0) {
        grid *= 2.0;
    }
    for (double beat = std::ceil(geometry.view.first / grid) * grid; beat <= geometry.view.last + 1e-9; beat += grid) {
        const float x = beat_x(beat);
        const bool bar = std::abs(std::remainder(beat, 4.0)) < 0.001;
        draw->AddLine(ImVec2(x, top - 3.0f), ImVec2(x, origin.y + size.y - 6.0f),
                       bar ? IM_COL32(60, 89, 107, 255) : IM_COL32(33, 50, 64, 255));
        char label[32];
        std::snprintf(label, sizeof(label), "%g", std::abs(beat) < 1e-12 ? 0.0 : beat);
        draw->AddText(ImVec2(x - 3.0f, origin.y + 7.0f), bar ? cyan : quiet_color, label);
    }
    for (double boundary : {0.0, editor.document.pattern().length_beats}) {
        if (boundary >= geometry.view.first && boundary <= geometry.view.last)
            draw->AddLine(ImVec2(beat_x(boundary), top), ImVec2(beat_x(boundary), origin.y + size.y - 6), IM_COL32(107, 125, 135, 180), 1.5f);
    }

    const float cursor_x = beat_x(editor.cursor_beat);
    draw->AddLine(ImVec2(cursor_x, top), ImVec2(cursor_x, origin.y + size.y - 7.0f),
                   IM_COL32(246, 188, 94, 75));
    const float cursor_y = lane_y(std::clamp(editor.cursor_lane,0,strings));
    draw->AddRect(ImVec2(cursor_x-12,cursor_y-lane_height*.4f), ImVec2(cursor_x+12,cursor_y+lane_height*.4f),
                  editor.tab_keyboard_focus ? cyan : IM_COL32(246,188,94,100),3,0,1.5f);
    int hit = -1;
    int gesture_hit = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (std::size_t i = 0; i < editor.document.pattern().gestures.size(); ++i) {
        const auto& gesture = editor.document.pattern().gestures[i];
        if (gesture.beat > geometry.view.last || gesture.beat + gesture.points.back().beat < geometry.view.first) continue;
        const bool selected = static_cast<int>(i) == editor.selected_gesture;
        const auto color = selected ? amber : IM_COL32(187, 143, 76, 210);
        const float y = lane_y(gesture.string_index);
        float minimum = gesture.points.front().fret, maximum = minimum;
        for (const auto& point : gesture.points) { minimum = std::min(minimum, point.fret); maximum = std::max(maximum, point.fret); }
        const auto point_y = [&](float fret) {
            return y + 6.0f + (maximum > minimum ? (0.5f - (fret - minimum) / (maximum - minimum)) * std::min(12.0f, lane_height * 0.3f) : 0);
        };
        ImVec2 previous(beat_x(gesture.beat), point_y(gesture.points.front().fret));
        for (const auto& point : gesture.points) {
            const ImVec2 position(beat_x(gesture.beat + point.beat), point_y(point.fret));
            draw->AddLine(previous, position, color, selected ? 2.5f : 1.5f);
            draw->AddCircleFilled(position, selected ? 3.5f : 2.5f, color);
            previous = position;
        }
        const float x0 = beat_x(gesture.beat), x1 = previous.x;
        draw->AddTriangleFilled(previous, ImVec2(previous.x - 7, previous.y - 4), ImVec2(previous.x - 7, previous.y + 4), color);
        draw->AddText(ImVec2((x0 + x1) * 0.5f - 3, y - 14), color, "/");
        if (hovered && mouse.x >= x0 - 5 && mouse.x <= x1 + 5 && std::abs(mouse.y - y) <= lane_height * 0.43f)
            gesture_hit = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < editor.document.pattern().events.size(); ++i) {
        const auto& event = editor.document.pattern().events[i];
        if (event.beat < geometry.view.first - 14 / pixels_per_beat || event.beat > geometry.view.last + 14 / pixels_per_beat) continue;
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
    draw->PopClipRect();

    if (hovered && hit >= 0) {
        const auto& event = editor.document.pattern().events[static_cast<std::size_t>(hit)];
        ImGui::BeginTooltip();
        ImGui::Text("%s | beat %.3f | strength %.2f", event.type.c_str(), event.beat, event.strength);
        ImGui::EndTooltip();
    } else if (hovered && gesture_hit >= 0) {
        const auto& gesture = editor.document.pattern().gestures[static_cast<std::size_t>(gesture_hit)];
        ImGui::BeginTooltip();
        ImGui::Text("Slide %g / %g | beat %.6f | %zu points", static_cast<double>(gesture.points.front().fret),
                    static_cast<double>(gesture.points.back().fret), gesture.beat, gesture.points.size());
        ImGui::EndTooltip();
    }
    if (clicked) {
        editor.end_fret_entry(); editor.tab_keyboard_focus = false;
        editor.document.end_edit();
        if (hit >= 0) {
            editor.tab_keyboard_focus = true;
            editor.selected = hit;
            editor.selected_gesture = -1;
            const auto& event = editor.document.pattern().events[static_cast<std::size_t>(hit)];
            editor.cursor_beat = event.beat;
            editor.cursor_lane = event.type == "body_hit" ? strings : event.string_index;
        } else if (gesture_hit >= 0) {
            editor.tab_keyboard_focus = true;
            editor.selected = -1;
            editor.selected_gesture = gesture_hit;
            const auto& gesture = editor.document.pattern().gestures[static_cast<std::size_t>(gesture_hit)];
            editor.cursor_beat = gesture.beat;
            editor.cursor_lane = gesture.string_index;
        } else if (mouse.y >= top && mouse.x >= left && mouse.x <= right) {
            const double beat = geometry.x_beat(mouse.x);
            if (beat < 0 || beat >= editor.document.pattern().length_beats) {
                editor.selected = -1; editor.selected_gesture = -1;
                editor.message("Retained context is outside the editable pattern. Choose a pattern beat to add an event.");
                return;
            }
            editor.cursor_beat = std::clamp(std::round(beat / editor.snap) * editor.snap,
                                            0.0, std::nextafter(editor.document.pattern().length_beats, 0.0));
            editor.cursor_lane = std::clamp(static_cast<int>((mouse.y - top) / lane_height), 0, strings);
            editor.selected = -1;
            editor.selected_gesture = -1;
            editor.tab_keyboard_focus = true;
            if (double_clicked) {
                editor.attempt([&]() { editor.add(editor.cursor_lane == strings ? "body_hit" : "pluck"); });
            }
        }
    }
    if (hovered) {
        const auto& io = ImGui::GetIO();
        if (io.MouseWheel != 0) {
            if (io.KeyShift) editor.pan_timeline(-io.MouseWheel * geometry.view.span() * 0.1);
            else editor.zoom_timeline(std::pow(0.8, io.MouseWheel), geometry.x_beat(io.MousePos.x));
        }
        if (io.MouseWheelH != 0) editor.pan_timeline(-io.MouseWheelH * geometry.view.span() * 0.1);
    }
    if (panning && !ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && ImGui::GetIO().MouseDelta.x != 0)
        editor.pan_timeline(-static_cast<double>(ImGui::GetIO().MouseDelta.x) / (right - left) * geometry.view.span());
    tab_keyboard(editor);
}

void slide_inspector(Editor& editor) {
    auto gesture = editor.document.pattern().gestures[static_cast<std::size_t>(editor.selected_gesture)];
    ImGui::PushID(gesture.id.c_str());
    ImGui::TextDisabled("%s", gesture.id.c_str());
    ImGui::TextWrapped("Contact moves while the string rings. Add a pluck to excite the string.");
    const auto commit = [&](const std::string& key) {
        editor.replace_slide(static_cast<std::size_t>(editor.selected_gesture), gesture, key);
    };
    const auto edit = [&](bool changed) { edit_control(editor, changed, commit); };
    ImGui::TextUnformatted("Start beat");
    ImGui::SetNextItemWidth(-1);
    edit(ImGui::InputDouble("##slide_beat", &gesture.beat, editor.snap, 1.0, "%.15g"));
    int string = gesture.string_index + 1;
    ImGui::TextUnformatted("String (1 = highest lane)");
    ImGui::SetNextItemWidth(-1);
    const bool string_changed = ImGui::InputInt("##slide_string", &string);
    if (string_changed) gesture.string_index = std::clamp(string, 1, static_cast<int>(editor.document.pattern().tuning_midi.size())) - 1;
    edit(string_changed);
    int remove = -1;
    if (ImGui::CollapsingHeader("Fret points", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t i = 0; i < gesture.points.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Point %zu | beat offset", i + 1);
            ImGui::BeginDisabled(i == 0);
            ImGui::SetNextItemWidth(-1);
            edit(ImGui::InputDouble("##point_beat", &gesture.points[i].beat, editor.snap, 1.0, "%.15g"));
            ImGui::EndDisabled();
            ImGui::TextUnformatted("Fret");
            ImGui::SetNextItemWidth(-76);
            const bool fret_changed = ImGui::InputFloat("##point_fret", &gesture.points[i].fret, 1, 5, "%.6g");
            if (fret_changed) gesture.points[i].fret = std::clamp(gesture.points[i].fret, 0.0f, 36.0f);
            edit(fret_changed);
            if (i == 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("A pluck at this same start shares the initial fret and is updated in the same undoable edit.");
            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0 || gesture.points.size() <= 2);
            if (ImGui::Button("Remove")) remove = static_cast<int>(i);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (remove >= 0) {
            gesture.points.erase(gesture.points.begin() + remove);
            editor.attempt([&] { commit({}); });
        }
        ImGui::BeginDisabled(gesture.points.size() >= 256);
        if (ImGui::Button("+ Point", ImVec2(-1, 0))) {
            std::size_t gap = 1;
            for (std::size_t i = 2; i < gesture.points.size(); ++i)
                if (gesture.points[i].beat - gesture.points[i - 1].beat > gesture.points[gap].beat - gesture.points[gap - 1].beat) gap = i;
            const auto a = gesture.points[gap - 1], b = gesture.points[gap];
            gesture.points.insert(gesture.points.begin() + static_cast<std::ptrdiff_t>(gap), {(a.beat + b.beat) * 0.5, (a.fret + b.fret) * 0.5f});
            editor.attempt([&] { commit({}); });
        }
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Delete slide", ImVec2(-1, 0))) editor.attempt([&] { editor.erase(); });
    ImGui::PopID();
}

void production_inspector(Editor& editor) {
    auto settings = editor.document.pattern().production.value_or(gtracker::ProductionSettings{});
    auto edit = [&](bool changed) {
        edit_control(editor, changed, [&](const std::string& key) { editor.set_production(settings, key); });
    };
    edit(ImGui::Checkbox("Room enabled", &settings.enabled));
    ImGui::TextDisabled("room_v1 / stereo when enabled");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Artistic algorithmic space, separate from the physical instrument. Disabling bypasses the whole chain, including gain, for an exact mono dry reference. Editing stores explicit v2 settings.");
    auto slider = [&](const char* label, const char* id, float& value, float low, float high, const char* format) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        edit(ImGui::SliderFloat(id, &value, low, high, format));
    };
    slider("Wet / dry", "##room_wet", settings.wet, 0, 1, "%.2f");
    slider("Decay target (seconds)", "##room_decay", settings.decay_seconds, 0.1f, 8, "%.2f");
    slider("High-frequency damping", "##room_damping", settings.damping, 0, 0.95f, "%.2f");
    slider("Pre-delay (ms)", "##room_predelay", settings.predelay_ms, 0, 200, "%.1f");
    slider("Stereo width", "##room_width", settings.width, 0, 1, "%.2f");
    slider("Output gain (dB)", "##room_gain", settings.output_gain_db, -24, 6, "%.1f");
    ImGui::TextWrapped("Render again after editing. Original source playback stays unchanged.");
}

void proposal_inspector(Editor& editor) {
    ImGui::TextWrapped("Optional single-note research proposals. Chords and articulations are unsupported.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##proposal_path", editor.proposal_path.data(), editor.proposal_path.size());
    ImGui::BeginDisabled(!editor.source);
    if (ImGui::Button("Load proposals", ImVec2(-1, 0))) editor.attempt([&] { editor.load_proposals(); });
    ImGui::EndDisabled();
    if (!editor.source) ImGui::TextWrapped("Open a source take first. Generate a job with analysis/propose.py in a source checkout.");
    if (!editor.source || !editor.proposals) return;
    const auto& proposals = *editor.proposals;
    const bool current = gtracker::note_proposals_match(proposals, editor.document.pattern(), editor.source->take);
    ImGui::TextWrapped("Job: %s", proposals.job_id.c_str());
    ImGui::Text("Channel %d | %zu candidates", proposals.channel+1, proposals.notes.size());
    if (!current) ImGui::TextWrapped("Stale score snapshot. Repeat the proposal job after saving current edits.");
    if (proposals.notes.empty()) ImGui::TextWrapped("No confident candidates in this single-note experiment. This does not establish silence.");
    ImGui::TextWrapped("Fingerings are alternatives, not detected strings. Periodicity is a diagnostic, not a probability. Acceptance adds plucks only.");
    ImGui::BeginDisabled(!current);
    for (std::size_t index = 0; index < proposals.notes.size(); ++index) {
        const auto& note = proposals.notes[index];
        const double beat = editor.source->take.source_frame_to_beat(static_cast<double>(note.onset_frame));
        ImGui::PushID(static_cast<int>(index));
        const bool inside = beat >= 0 && beat < editor.document.pattern().length_beats;
        if (ImGui::TreeNodeEx("note", ImGuiTreeNodeFlags_None, "%s | beat %.3f", note.id.c_str(), beat)) {
            ImGui::Text("MIDI %.2f | periodicity %.2f", note.pitch_midi, note.periodicity);
            ImGui::Text("Onset window: ~%.1f ms", note.onset_uncertainty_frames * 1000.0 / proposals.sample_rate);
            ImGui::Text("Pitch spread: %.1f cents", note.pitch_spread_cents);
            if (!inside) ImGui::TextWrapped("Outside the pattern. Adjust alignment explicitly before a new job.");
            if (note.alternatives.empty()) ImGui::TextWrapped("No compatible string/fret in this tuning.");
            ImGui::BeginDisabled(!inside || note.alternatives.empty());
            const int selected = editor.proposal_alternatives[index];
            std::string label = "Skip this note";
            if (selected >= 0) {
                const auto& alternative = note.alternatives[static_cast<std::size_t>(selected)];
                char buffer[96]; std::snprintf(buffer, sizeof(buffer), "String %d, fret %.2f", alternative.string_index+1, alternative.fret); label = buffer;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##fingering", label.c_str())) {
                if (ImGui::Selectable("Skip this note", selected < 0)) editor.proposal_alternatives[index] = -1;
                for (std::size_t i = 0; i < note.alternatives.size(); ++i) {
                    const auto& alternative = note.alternatives[i];
                    char buffer[96]; std::snprintf(buffer, sizeof(buffer), "String %d, fret %.2f", alternative.string_index+1, alternative.fret);
                    if (ImGui::Selectable(buffer, selected == static_cast<int>(i))) editor.proposal_alternatives[index] = static_cast<int>(i);
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::TreeNode("Research limits")) {
        for (const auto& limitation : proposals.limitations) ImGui::TextWrapped("%s", limitation.c_str());
        ImGui::TreePop();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##proposal_strength", &editor.proposal_strength, 0, 1, "New pluck strength %.2f");
    ImGui::TextWrapped("Strength is your authoring choice; it is not inferred from the recording.");
    const auto selected_count = std::count_if(editor.proposal_alternatives.begin(), editor.proposal_alternatives.end(), [](int value) { return value >= 0; });
    ImGui::Text("%zu / %zu notes selected", static_cast<std::size_t>(selected_count), proposals.notes.size());
    ImGui::BeginDisabled(selected_count == 0);
    if (ImGui::Button("Accept selected plucks", ImVec2(-1, 0))) editor.attempt([&] { editor.accept_proposals(); });
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void open_recovery_button(Editor& editor,const gtracker::RecoveryCopy& copy) {
    if (ImGui::Button("Open recovered copy",ImVec2(-1,0))) editor.attempt([&] { editor.open_recovery(copy.path); });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",copy.path.u8string().c_str());
}

void recovery_inspector(Editor& editor) {
    if (!editor.recovery_enabled) { ImGui::TextWrapped("Recovery copies are disabled for this session (--no-recovery)."); return; }
    ImGui::TextWrapped("Unsaved scores get a recovery copy every five seconds while the app runs. Originals stay unchanged.");
    if (!editor.recovery_error.empty()) ImGui::TextWrapped("Last recovery error: %s",editor.recovery_error.c_str());
    if (ImGui::Button("Refresh recovery copies",ImVec2(-1,0))) editor.attempt([&] { editor.refresh_recoveries(); });
    if (editor.recovery && !editor.recovery->owned_path().empty()) ImGui::TextWrapped("This session has a recovery copy. Save or explicit discard clears it.");
    if (editor.recovery_listing.unreadable) ImGui::TextWrapped("%zu unreadable/future copies were kept on disk.",editor.recovery_listing.unreadable);
    if (editor.recovery_listing.truncated) ImGui::TextWrapped("Showing a bounded recent list; more files may exist in the recovery folder.");
    ImGui::TextWrapped("Open a copy to review the score and save to a new filename. Source audio and take metadata stay in their original bundles.");
    ImGui::TextDisabled("Recent copies, newest first.");
    for (std::size_t i=0;i<editor.recovery_listing.copies.size();++i) {
        const auto& copy=editor.recovery_listing.copies[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator(); ImGui::TextWrapped("%s",copy.name.c_str());
        ImGui::TextDisabled("Revision %llu",static_cast<unsigned long long>(copy.revision));
        open_recovery_button(editor,copy);
        ImGui::PopID();
    }
    if (editor.recovery) ImGui::TextWrapped("Folder: %s",editor.recovery->directory().u8string().c_str());
}

void export_button(Editor& editor) {
    if (editor.exporting()) {
        if (ImGui::Button("Cancel WAV export",ImVec2(-1,0))) editor.cancel_export();
    } else if (ImGui::Button("Export current score",ImVec2(-1,0))) editor.attempt([&] { editor.request_export(); });
}

void export_inspector(Editor& editor) {
    ImGui::TextWrapped("Export a score snapshot as 48 kHz, 16-bit PCM WAV, with its production settings and decay tail.");
    ImGui::BeginDisabled(editor.exporting());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##export_path",editor.export_path.data(),editor.export_path.size());
    ImGui::EndDisabled();
    export_button(editor);
    if (editor.exporting() && editor.export_job) {
        const auto total = editor.export_job->total_frames.load(std::memory_order_relaxed);
        const auto completed = editor.export_job->completed_frames.load(std::memory_order_relaxed);
        if (total) ImGui::ProgressBar(static_cast<float>(static_cast<double>(completed)/total),ImVec2(-1,0));
        else ImGui::TextUnformatted("Preparing WAV...");
        ImGui::TextWrapped("Exporting revision %llu. Further edits belong to the next export.",static_cast<unsigned long long>(editor.exported_revision));
    }
    if (!editor.export_error.empty()) ImGui::TextWrapped("Last export error: %s",editor.export_error.c_str());
    if (editor.export_result) {
        const auto& result = *editor.export_result;
        ImGui::TextWrapped("Wrote revision %llu: %.2f seconds, %s. Peak %.3f; clipped %llu.",
            static_cast<unsigned long long>(editor.exported_revision),static_cast<double>(result.frames)/result.sample_rate,
            result.channels == 2 ? "stereo" : "mono",result.peak,static_cast<unsigned long long>(result.clipped_samples));
        ImGui::TextWrapped("%s",editor.export_destination.u8string().c_str());
    }
    ImGui::TextWrapped("Attached source audio is protected. Saving the score remains a separate action.");
}

void export_replace_dialog(Editor& editor) {
    if (editor.export_replace_path.empty()) return;
    if (!ImGui::IsPopupOpen("Replace WAV?")) ImGui::OpenPopup("Replace WAV?");
    ImGui::SetNextWindowSize(ImVec2(520,0),ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Replace WAV?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Replace the existing WAV at this path?");
        ImGui::TextWrapped("%s",editor.export_replace_path.u8string().c_str());
        ImGui::TextWrapped("The previous file is kept if export fails or cancellation is handled before completion.");
        if (ImGui::Button("Replace",ImVec2(120,0))) {
            editor.attempt([&] { editor.replace_export(); });
            if (editor.export_replace_path.empty()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel",ImVec2(120,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            editor.export_replace_path.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void audio_output_options(Editor& editor) {
    if (ImGui::Selectable("System default",editor.audio_output == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK))
        editor.attempt([&] { editor.select_audio_output(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK); });
    for (const auto& output : editor.audio_outputs) {
        ImGui::PushID(static_cast<int>(output.id));
        if (ImGui::Selectable(output.name.c_str(),editor.audio_output == output.id))
            editor.attempt([&] { editor.select_audio_output(output.id); });
        ImGui::PopID();
    }
}

void audio_output_inspector(Editor& editor) {
    if (!editor.audio_outputs_queried) editor.attempt([&] { editor.refresh_audio_outputs(); });
    if (ImGui::Button("Refresh outputs",ImVec2(-1,0))) editor.attempt([&] { editor.refresh_audio_outputs(); });
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##audio_output",editor.audio_output_name.c_str())) {
        audio_output_options(editor);
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s",editor.audio_output_name.c_str());
    if (!editor.audio_output_available) ImGui::TextWrapped("Selected output is unavailable. Refresh and choose a device.");
    ImGui::TextWrapped("Changing output stops playback. This session's choice applies to source and synth; press Play to resume.");
}

void inspector(Editor& editor) {
    const bool slide = editor.selected_gesture >= 0 && editor.selected_gesture < static_cast<int>(editor.document.pattern().gestures.size());
    ImGui::TextColored(ImVec4(0.31f, 0.83f, 0.87f, 1.0f), slide ? "SLIDE" : "EVENT");
    ImGui::Separator();
    if (slide) slide_inspector(editor);
    else if (editor.selected < 0 || editor.selected >= static_cast<int>(editor.document.pattern().events.size())) {
        ImGui::TextWrapped(editor.tab_keyboard_focus ? "Use arrows and digits in the active tab. Select a note or slide for detailed controls." :
                           "Select a fret number, slide or body hit. Click a cell to type frets; double-click to add an event.");
    } else {
        Event event = editor.document.pattern().events[static_cast<std::size_t>(editor.selected)];
        ImGui::PushID(event.id.c_str());
        ImGui::TextDisabled("%s", event.id.c_str());
        auto edit = [&](bool changed) {
            edit_control(editor, changed, [&](const std::string& key) {
                editor.replace_event(static_cast<std::size_t>(editor.selected), event, key);
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
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A slide at this same beat shares the initial fret and is updated in the same undoable edit.");
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
        const char* models[]{"Baseline 0.1", "Contact 0.2 (experimental)", "Retained plucks 0.5 (experimental)"};
        const char* model_ids[]{"baseline_0_1", "contact_0_2", "retained_0_5"};
        int model = static_cast<int>(gtracker::instrument_model(editor.document.pattern().model));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##instrument_model", &model, models, 3))
            editor.attempt([&] { editor.set_model(model_ids[model]); });
        ImGui::TextDisabled("Pattern format v%d", editor.document.pattern().schema_version);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Existing v1 files keep the original baseline. Retained 0.5 keeps weighted vibration on replucks; zero strength does not mute it. Select a model explicitly to use v2. Existing curves cannot be flattened by choosing baseline.");
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
    if (ImGui::TreeNode("Audio output")) {
        audio_output_inspector(editor);
        ImGui::TreePop();
    }
    const bool focus_export = editor.open_export_panel;
    if (focus_export) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    editor.open_export_panel = false;
    if (ImGui::TreeNode("WAV export")) {
        if (focus_export) ImGui::SetScrollHereY(0);
        export_inspector(editor);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Production / room")) {
        production_inspector(editor);
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
    if (ImGui::TreeNode("Source take")) {
        ImGui::TextWrapped("Open a preserved take bundle to compare its original audio with this instrument.");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##take_path", editor.take_path.data(), editor.take_path.size());
        if (ImGui::Button("Open take", ImVec2(-1.0f, 0.0f))) editor.attempt([&]() { editor.load_source(); });
        if (editor.source) {
            const auto& take = editor.source->take;
            ImGui::TextWrapped("%s", take.source_name.c_str());
            ImGui::Text("%d Hz, %d channels, %d-bit PCM", take.audio.sample_rate, take.audio.channels, take.audio.sample_width_bits);
            ImGui::Text("Source tempo: %.2f BPM", take.alignment.bpm);
            ImGui::Text("Input offset: %lld frames (%s)", static_cast<long long>(take.alignment.input_offset_frames), take.alignment.offset_status.c_str());
            ImGui::TextWrapped("Source timing is %s. Its metadata job has no transcript; optional note proposals are reviewed separately.", take.alignment.offset_status.c_str());
            if (editor.document.pattern().revision != take.source_pattern_revision)
                ImGui::TextWrapped("The metadata job describes an older pattern revision.");
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Note proposals")) {
        proposal_inspector(editor);
        ImGui::TreePop();
    }
    const bool focus_recovery = editor.open_recovery_panel;
    if (focus_recovery) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
    editor.open_recovery_panel = false;
    if (ImGui::TreeNode("Recovery copies")) {
        if (focus_recovery) ImGui::SetScrollHereY(0);
        recovery_inspector(editor);
        ImGui::TreePop();
    }
}

gtracker::TimelineGeometry waveform(Editor& editor, float height) {
    editor.sync_timeline();
    const auto origin = ImGui::GetCursorScreenPos();
    gtracker::TimelineGeometry geometry{editor.view, origin.x + 64, origin.x + ImGui::GetContentRegionAvail().x - 20};
    const bool has_render = !editor.rendered.audio.samples.empty();
    const bool current = editor.current_audio();
    const bool source = editor.source.has_value();
    const auto before = editor.view;
    const auto extent = editor.timeline_extent();
    auto& input = ImPlot::GetInputMap();
    const auto saved_input = input;
    input.Pan = ImGuiMouseButton_Middle;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(16, 10));
    if (ImPlot::BeginPlot("##waveform", ImVec2(-1.0f, height), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | (source ? 0 : ImPlotFlags_NoLegend))) {
        ImPlot::SetupAxes("Pattern beat", nullptr, ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisLimits(ImAxis_X1, editor.view.first, editor.view.last, ImPlotCond_Once);
        ImPlot::SetupAxisLinks(ImAxis_X1, &editor.view.first, &editor.view.last);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, extent.first, extent.last);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, std::min(1.0 / 64, extent.span()), extent.span());
        const double peak = std::max(source ? editor.source->take.audio.peak : 0.0, has_render ? static_cast<double>(editor.rendered.audio.peak) : 0.0);
        const double amplitude = editor.auto_wave_gain && (has_render || source) ? std::clamp(peak * 1.15, 0.01, 1.05) : 1.05;
        ImPlot::SetupAxisLimits(ImAxis_Y1, -amplitude, amplitude, ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, "% 7.3f");
        if (source) ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Horizontal);
        ImPlot::SetupFinish();
        const auto position = ImPlot::GetPlotPos();
        const auto size = ImPlot::GetPlotSize();
        const auto limits = ImPlot::GetPlotLimits();
        geometry = {{limits.X.Min, limits.X.Max}, position.x, position.x + size.x};
        if (source) {
            const auto& preview = *editor.source;
            ImPlot::SetNextFillStyle(ImVec4(0.96f, 0.74f, 0.37f, 0.45f));
            ImPlot::PlotShaded("Source", preview.beats.data(), preview.minimum.data(), preview.maximum.data(), static_cast<int>(preview.beats.size()));
        }
        const double boundaries[] = {0, editor.document.pattern().length_beats};
        ImPlot::SetNextLineStyle(ImVec4(0.55f, 0.59f, 0.63f, 0.65f));
        ImPlot::PlotInfLines("Pattern bounds", boundaries, 2);
        if (has_render) {
            ImPlot::SetNextFillStyle(current ? ImVec4(0.30f, 0.83f, 0.87f, 0.80f)
                                            : ImVec4(0.42f, 0.48f, 0.53f, 0.60f));
            ImPlot::PlotShaded(current ? "Synth" : "Synth (previous)",
                               editor.rendered.beats.data(), editor.rendered.minimum.data(),
                               editor.rendered.maximum.data(), static_cast<int>(editor.rendered.times.size()));
        }
        if (!has_render && !source) ImPlot::PlotText("Render to inspect audio", (geometry.view.first + geometry.view.last) * 0.5, 0);
        ImPlot::EndPlot();
        if (std::abs(editor.view.first - before.first) > 1e-12 || std::abs(editor.view.last - before.last) > 1e-12)
            editor.follow_view_fit = false;
    }
    ImPlot::PopStyleVar();
    input = saved_input;
    editor.timeline_geometry = geometry;
    return geometry;
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
        editor.end_fret_entry();
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

void render_button(Editor& editor) {
    if (editor.rendering()) {
        const bool cancelling = editor.render_job && editor.render_job->cancelled.load(std::memory_order_relaxed);
        ImGui::BeginDisabled(cancelling);
        if (ImGui::Button(cancelling ? "Cancelling..." : "Cancel render")) editor.cancel_render();
        ImGui::EndDisabled();
    } else if (ImGui::Button("Render")) {
        editor.attempt([&] { editor.request_render(); });
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
    if (!editor.recovery_listing.copies.empty() || editor.recovery_listing.unreadable || !editor.recovery_error.empty()) {
        const auto label = editor.recovery_listing.copies.empty() ? std::string("Recovery copies need attention") :
            "Review recovery copies ("+std::to_string(editor.recovery_listing.copies.size())+")";
        if (ImGui::Button(label.c_str())) editor.open_recovery_panel = true;
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
    ImGui::SameLine();
    const bool slides_enabled = gtracker::supports_slides(editor.document.pattern());
    if (ImGui::Button(slides_enabled ? "+ Slide" : "Enable slides")) {
        editor.attempt([&] { if (slides_enabled) editor.add_slide(); else editor.set_model("contact_0_2"); });
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(slides_enabled ? "Add a continuous fret curve on the current string. It retains ringing vibration." : "Select the experimental contact model and v2 format. Undo restores the previous model; the original baseline remains selectable.");
    ImGui::SameLine(0.0f, 20.0f);
    render_button(editor);
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.rendering());
    const char* play_label = editor.source ? (editor.current_audio() ? "Play synth" : "Render + Play synth")
                                           : (editor.current_audio() ? "Play" : "Render + Play");
    if (ImGui::Button(play_label)) {
        editor.attempt([&]() { editor.play(); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        editor.stop();
        editor.message("Stopped playback.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Export WAV")) editor.open_export_panel = true;

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
    const auto strings = editor.document.pattern().tuning_midi.size(), events = editor.document.pattern().events.size(), slides = editor.document.pattern().gestures.size();
    ImGui::TextDisabled("%zu string%s | %zu event%s, %zu slide%s | cursor %.2f", strings, strings == 1 ? "" : "s",
                        events, events == 1 ? "" : "s", slides, slides == 1 ? "" : "s", editor.cursor_beat);

    if (editor.source) {
        if (ImGui::Button("Play source")) editor.attempt([&]() { editor.play_source(); });
        ImGui::SameLine();
        if (ImGui::Checkbox("From beat 0", &editor.source_from_zero)) {
            editor.stop();
            editor.message("Source playback range changed. Play source to compare.");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start at the mapped beat-zero frame within the selected source. Disable to hear retained pre-roll. The original file is unchanged.");
        ImGui::SameLine();
        const bool detach = ImGui::Button("Detach");
        ImGui::SameLine();
        const auto& take = editor.source->take;
        ImGui::TextDisabled("%s | %.1f kHz, %d ch | source %.1f BPM | metadata job", take.source_name.c_str(),
                            take.audio.sample_rate / 1000.0, take.audio.channels, take.alignment.bpm);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextWrapped("%s", take.source_name.c_str());
            ImGui::Text("%d-bit PCM; input offset %lld frames (%s)", take.audio.sample_width_bits,
                        static_cast<long long>(take.alignment.input_offset_frames), take.alignment.offset_status.c_str());
            ImGui::TextUnformatted("Input/output clock alignment is unmeasured. Optional single-note proposals are reviewed separately.");
            if (editor.document.pattern().revision != take.source_pattern_revision)
                ImGui::TextUnformatted("The metadata job describes an older pattern revision.");
            ImGui::EndTooltip();
        }
        if (detach) editor.detach_source();
    }

    const float waveform_height = 136.0f;
    const float main_height = std::max(350.0f, ImGui::GetContentRegionAvail().y - 65.0f);
    const float inspector_width = 268.0f;
    const float canvas_width = std::max(300.0f, ImGui::GetContentRegionAvail().x - inspector_width - 8.0f);
    ImGui::BeginChild("Timeline", ImVec2(canvas_width, main_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled(editor.tab_keyboard_focus ? "TAB / slide (keyboard)" : "TAB / slide");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Highest string first. Fret = pluck, / = slide, x = damp, + = body hit. Wheel zooms; middle-drag pans. Shift+wheel pans the tab.");
    ImGui::SameLine();
    if (ImGui::Button("Pattern")) editor.fit_timeline(false);
    ImGui::SameLine();
    if (ImGui::Button("Context")) editor.fit_timeline(true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the whole preserved source and rendered decay tail on the same beat axis.");
    ImGui::SameLine();
    if (ImGui::Button("-##zoom")) editor.zoom_timeline(2, (editor.view.first + editor.view.last) * 0.5);
    ImGui::SameLine();
    if (ImGui::Button("+##zoom")) editor.zoom_timeline(0.5, (editor.view.first + editor.view.last) * 0.5);
    ImGui::SameLine();
    ImGui::Checkbox("Auto scale", &editor.auto_wave_gain);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit the wave's display scale. Played/exported samples keep their original level.");
    const auto canvas_origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 canvas_size(available.x, std::max(170.0f, available.y - waveform_height - 8));
    // Measure/draw the plot first, then use its exact data rectangle for the tab.
    // Both widgets therefore share current-frame bounds even during plot zoom.
    ImGui::SetCursorScreenPos(ImVec2(canvas_origin.x, canvas_origin.y + canvas_size.y + 8));
    const auto geometry = waveform(editor, waveform_height);
    ImGui::SetCursorScreenPos(canvas_origin);
    canvas(editor, canvas_size, &geometry);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Inspector", ImVec2(0.0f, main_height), ImGuiChildFlags_Borders);
    inspector(editor);
    ImGui::EndChild();

    if (editor.rendering() && editor.render_job && !editor.status_error) {
        const auto completed = editor.render_job->completed_frames.load(std::memory_order_relaxed);
        const auto total = editor.render_job->total_frames.load(std::memory_order_relaxed);
        if (editor.render_job->cancelled.load(std::memory_order_relaxed))
            ImGui::TextColored(ImVec4(0.96f, 0.74f, 0.37f, 1.0f), "Cancelling render; previous completed preview retained.");
        else if (total && completed >= total)
            ImGui::TextColored(ImVec4(0.96f, 0.74f, 0.37f, 1.0f), "Audio rendered. Preparing waveform preview...");
        else
            ImGui::TextColored(ImVec4(0.96f, 0.74f, 0.37f, 1.0f), "Rendering: %.1f%%. Cancel render to stop.", total ? 100.0 * completed / total : 0.0);
    } else if (editor.status_error) {
        ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.38f, 1.0f), "%s", editor.status.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.96f, 0.74f, 0.37f, 1.0f), "%s", editor.status.c_str());
    }
    if (editor.tab_keyboard_focus) {
        ImGui::TextDisabled("Arrows move | 0-9 fret | X damp | B body hit | Esc leaves tab input");
    } else if (!editor.rendered.audio.samples.empty() && !editor.current_audio()) {
        ImGui::TextDisabled("Waveform: previous revision.  Space play/stop | Ctrl/Cmd+S save | Ctrl/Cmd+Z undo");
    } else {
        ImGui::TextDisabled("Space play/stop | Ctrl/Cmd+S save | Ctrl/Cmd+Z undo | Delete selection");
    }
    unsaved_dialog(editor);
    export_replace_dialog(editor);
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
            if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) editor.audio_device_removed(event.adevice);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(host.window))) {
                editor.document.request_close();
            }
        }
        editor.poll_render();
        editor.poll_export();
        editor.checkpoint_recovery(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
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
                  << "; rendered_samples=" << editor.rendered.audio.samples.size()
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
                         "                [--take PATH] (instead of a pattern path)\n"
                         "                [--data-dir PATH] [--resource-dir PATH] [--no-recovery]\n"
                         "                [--smoke-test] [--frames N] [--screenshot PATH.bmp]\n\n"
                         "Default saves go to the per-user application data folder.\n"
                         "Unsaved scores get recovery copies there every five seconds; --no-recovery disables this session.\n"
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
