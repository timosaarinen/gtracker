// Exercise the real native widgets and shortcut/modal wiring with synthetic
// input. This is explicitly separate from physical window/device qualification.
#define GT_EDITOR_TEST
#include "../src/editor.cpp"

#include <functional>
#include <fstream>
#include <iterator>
#include "gtracker/hash.hpp"

namespace {
void check_ui(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void frame(Host& host, const std::function<void()>& draw,
           const std::function<void()>& input = [] {}) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    auto& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    io.AddFocusEvent(true);
    input();
    ImGui::NewFrame();
    draw();
    ImGui::Render();
    SDL_SetRenderDrawColor(host.renderer, 11, 15, 21, 255);
    SDL_RenderClear(host.renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), host.renderer);
    SDL_RenderPresent(host.renderer);
}

void grouped_widget(Host& host, const Arguments& args) {
    Editor editor(args);
    ImVec2 a, b;
    auto draw = [&] {
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(500, 200));
        ImGui::Begin("Edit control regression");
        keyboard(editor);
        float brightness = editor.document.pattern().brightness;
        ImGui::SetNextItemWidth(300);
        const bool changed = ImGui::SliderFloat("Brightness", &brightness, 0, 1);
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        edit_control(editor, changed, [&](const std::string& key) {
            auto next = editor.document.pattern();
            next.brightness = brightness;
            editor.commit(std::move(next), "Edited instrument", key);
        });
        ImGui::End();
    };
    frame(host, draw);
    frame(host, draw);
    auto mouse = [&](float fraction, bool down) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(a.x + 10 + fraction * 280, (a.y + b.y) * 0.5f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    };
    const float original = editor.document.pattern().brightness;
    frame(host, draw, [&] { mouse(0.2f, true); });
    frame(host, draw, [&] { mouse(0.4f, true); });
    frame(host, draw, [&] { mouse(0.8f, true); });
    frame(host, draw, [&] { mouse(0.8f, false); });
    const float edited = editor.document.pattern().brightness;
    check_ui(edited > 0.7f && editor.document.dirty(), "synthetic slider drag did not edit");
    frame(host, draw, [&] {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_Z, true);
    });
    check_ui(editor.document.pattern().brightness == original && !editor.document.can_undo(),
             "Ctrl+Z did not undo the whole drag");
    check_ui(!editor.document.dirty(), "widget undo did not restore savepoint");
    frame(host, draw, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Z, false); });
    frame(host, draw, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Y, true); });
    check_ui(editor.document.pattern().brightness == edited, "Ctrl+Y did not redo the whole drag");
    frame(host, draw, [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Y, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    });
    frame(host, draw, [&] { mouse(0.3f, true); });
    frame(host, draw, [&] { mouse(0.3f, false); });
    editor.undo();
    check_ui(editor.document.pattern().brightness == edited, "second widget activation merged with first drag");
}

void guarded_interface(Host& host, const Arguments& args) {
    Editor editor(args);
    auto next = editor.document.pattern();
    next.bpm = 110;
    editor.commit(std::move(next), "Edited pattern");
    editor.document.request_close();
    auto draw = [&] { interface(editor); };
    frame(host, draw);
    check_ui(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId), "unsaved modal was not drawn");
    check_ui(!editor.document.close_requested(), "window close bypassed unsaved modal");
    // The first auto-sized popup frame measures its contents before display.
    frame(host, draw);
    host.screenshot(std::filesystem::current_path() / "editor-unsaved-dialog.bmp");
    frame(host, draw, [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_S, true);
    });
    check_ui(gtracker::load_pattern(args.document).bpm == 90 && editor.document.dirty(),
             "background save shortcut ran through modal");
    frame(host, draw, [&] {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_S, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiKey_Escape, true);
    });
    check_ui(editor.document.pending() == gtracker::EditorDocument::Pending::none &&
             !editor.document.close_requested() && editor.document.dirty(),
             "Escape did not cancel guarded close safely");
    frame(host, draw, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); });
}

void render_and_dummy_audio(const Arguments& args) {
    Editor editor(args);
    editor.request_render(true);
    auto next = editor.document.pattern();
    next.bpm = 115;
    editor.commit(std::move(next), "Edited pattern during render");
    editor.pending.wait();
    editor.poll_render();
    check_ui(!editor.current_audio() && !editor.playing && !editor.play_after_render,
             "stale render played after an edit");
    check_ui(!editor.render_failed, "discarding a stale render reported failure");
    editor.request_render();
    editor.pending.wait();
    editor.poll_render();
    check_ui(editor.current_audio(), "fresh render did not become current");
    const auto generation = editor.document.generation();
    editor.commit(editor.document.pattern(), "No-op");
    check_ui(editor.current_audio() && editor.document.generation() == generation,
             "no-op edit invalidated audio");
    editor.play();
    check_ui(editor.playing && editor.audio_stream && SDL_GetAudioStreamQueued(editor.audio_stream) > 0,
             "dummy audio did not receive rendered PCM");
    editor.stop();
    check_ui(!editor.playing && SDL_GetAudioStreamQueued(editor.audio_stream) == 0, "stop left audio queued");
    editor.play();
    editor.undo();
    check_ui(!editor.playing && !editor.current_audio() && SDL_GetAudioStreamQueued(editor.audio_stream) == 0,
             "undo did not stop/invalidate playback");
}

void writable_defaults(const Arguments& args) {
    Arguments fresh = args;
    fresh.document.clear();
    Editor editor(fresh);
    const auto save_path = std::filesystem::u8path(editor.path.data());
    check_ui(save_path.parent_path() == std::filesystem::absolute(args.data_dir),
             "new document save path is not in user data");
    check_ui(std::filesystem::u8path(editor.script_path.data()) ==
             std::filesystem::absolute(args.resource_dir) / "scripts/soften.lua",
             "bundled script did not resolve from resource directory");
    editor.run_script();
    const auto edited = editor.document.pattern();
    editor.save();
    check_ui(!editor.document.dirty(), "default save did not succeed");
    fresh.document = save_path;
    Editor reopened(fresh);
    check_ui(gtracker::pattern_json(reopened.document.pattern()) == gtracker::pattern_json(edited),
             "user-data save/reopen changed musical content");
}

void render_cancellation(Host& host, const Arguments& args) {
    Editor editor(args);
    editor.request_render(); editor.pending.wait(); editor.poll_render();
    const auto previous = editor.rendered.audio.samples;
    const auto generation = editor.document.generation();
    editor.request_render(true);
    ImVec2 a, b;
    auto draw = [&] {
        ImGui::SetNextWindowPos(ImVec2(20,20)); ImGui::SetNextWindowSize(ImVec2(400,120));
        ImGui::Begin("Render cancellation regression"); render_button(editor);
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax(); ImGui::End();
    };
    frame(host,draw); frame(host,draw);
    frame(host,draw,[&] { ImGui::GetIO().AddMousePosEvent((a.x+b.x)*.5f,(a.y+b.y)*.5f); ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true); });
    frame(host,draw,[&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false); });
    check_ui(editor.render_job && editor.render_job->cancelled.load() && !editor.play_after_render, "real Cancel render button did not cancel pending playback intent");
    editor.pending.wait(); editor.poll_render();
    check_ui(!editor.rendering() && !editor.render_failed && !editor.playing && editor.current_audio() && editor.rendered.audio.samples == previous,
             "cancelled render replaced/lost the previous preview or reported failure");
    check_ui(editor.document.generation() == generation && !editor.document.dirty(), "render cancellation edited score/history");
    editor.request_render(); const auto job = editor.render_job;
    editor.pending.wait();
    check_ui(job->completed_frames.load() == job->total_frames.load() && job->total_frames.load() > 0, "editor progress did not reach generated frame count");
    editor.cancel_render(); // A result ready between frames must still honor explicit cancellation.
    editor.poll_render();
    check_ui(editor.rendered.audio.samples == previous && !editor.render_failed, "late cancellation published the unwanted result");
    editor.request_render(); editor.pending.wait(); editor.poll_render();
    check_ui(editor.current_audio() && !editor.render_job, "cancelled worker could not be followed by a new render");
}

std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void source_workflow(Host& host, const Arguments& args, const std::filesystem::path& fixture) {
    const auto directory = args.document.parent_path() / "take-copy";
    std::filesystem::copy(fixture.parent_path(), directory, std::filesystem::copy_options::recursive);
    const auto manifest = directory / "take.json";
    const auto source_before = file_bytes(directory / "source.wav");
    Arguments source_args = args;
    source_args.document.clear();
    source_args.take = manifest;
    Editor editor(source_args);
    check_ui(editor.source && editor.source->take.audio.channels == 2 && editor.source->take.audio.sample_rate == 44100,
             "stereo take did not load at its original rate");
    check_ui(editor.source_playback_range().first == 44100, "source playback did not honor aligned beat zero");
    check_ui(editor.source->take.source_frame_to_beat(static_cast<double>(editor.source_playback_range().first)) == 0,
             "source playback and display used different frame mappings");
    editor.source_from_zero = false;
    check_ui(editor.source_playback_range().first == 0, "retained pre-roll was not available");
    editor.source_from_zero = true;
    editor.request_render(true);
    editor.play_source();
    check_ui(editor.playing && !editor.play_after_render && editor.audio_channels == 2 && editor.audio_rate == 44100,
             "source playback format/transport intent was lost");
    editor.pending.wait();
    editor.poll_render();
    check_ui(editor.playing && editor.audio_channels == 2 && editor.audio_rate == 44100 && editor.current_audio(),
             "finishing a render interrupted source playback");
    auto draw = [&] { interface(editor); };
    frame(host, draw);
    frame(host, draw);
    host.screenshot(std::filesystem::current_path() / "editor-source-comparison.bmp");
    editor.play();
    check_ui(editor.audio_channels == 1 && editor.audio_rate == 48000, "synth A/B did not reopen the mono renderer stream");
    editor.play_source();
    check_ui(editor.audio_channels == 2 && editor.audio_rate == 44100, "source A/B did not restore source rate/channels");
    auto next = editor.document.pattern();
    next.bpm = 100;
    editor.commit(std::move(next), "Edited source pattern");
    check_ui(!editor.playing && !editor.current_audio(), "edit left old comparison audio playing/current");
    editor.save();
    Editor reopened(source_args);
    check_ui(reopened.source && reopened.document.pattern().bpm == 100 && reopened.source->take.alignment.bpm == 90,
             "take save/reopen lost source timing or edited pattern");
    check_ui(reopened.document.pattern().revision != reopened.source->take.source_pattern_revision,
             "old metadata job was presented as the current pattern revision");
    const auto separate = directory / "separate-pattern.json";
    set_text(reopened.path, separate.u8string());
    reopened.attempt([&] { reopened.save(); });
    check_ui(reopened.status_error && !std::filesystem::exists(separate), "save silently broke the take association");
    reopened.detach_source();
    reopened.save();
    check_ui(std::filesystem::is_regular_file(separate), "detached pattern could not be saved separately");

    Editor guarded(args);
    auto edited = guarded.document.pattern();
    edited.bpm = 105;
    guarded.commit(std::move(edited), "Before source load");
    set_text(guarded.take_path, manifest.u8string());
    guarded.load_source();
    check_ui(guarded.pending_source && !guarded.source && guarded.document.pattern().bpm == 105,
             "pending take load replaced unsaved content");
    guarded.resolve(gtracker::EditorDocument::Decision::cancel);
    check_ui(!guarded.pending_source && !guarded.source && guarded.document.dirty(), "cancel applied the pending source");
    guarded.load_source();
    guarded.resolve(gtracker::EditorDocument::Decision::discard);
    check_ui(guarded.source && guarded.document.pattern().bpm == 100, "guarded source load did not attach the chosen pattern");
    edited = guarded.document.pattern();
    edited.bpm = 110;
    guarded.commit(std::move(edited), "Before ordinary load");
    set_text(guarded.path, args.document.u8string());
    guarded.load();
    check_ui(guarded.source && guarded.document.pending() == gtracker::EditorDocument::Pending::load,
             "ordinary load cleared source before resolving unsaved edits");
    set_text(guarded.confirmation_save_path, guarded.document.filename().u8string());
    guarded.resolve(gtracker::EditorDocument::Decision::save);
    check_ui(!guarded.source && guarded.document.pattern().bpm == 90 && !guarded.document.dirty(),
             "save/load did not detach the previous take safely");
    check_ui(gtracker::load_pattern(directory / "pattern.gtrk.json").bpm == 110, "guarded take edit was not saved before loading");
    check_ui(file_bytes(directory / "source.wav") == source_before, "A/B/edit/save changed original source bytes");
}

void slide_authoring(Host& host, const Arguments& original) {
    Arguments args = original;
    args.document = original.document.parent_path() / std::filesystem::u8path(u8"slide-滑音.gtrk.json");
    gtracker::save_pattern(gtracker::load_pattern(original.resource_dir / "examples/campfire.gtrk.json"), args.document);
    Editor editor(args);
    editor.set_model("contact_0_2");
    editor.cursor_beat = 0;
    editor.cursor_lane = 5;
    editor.add_slide();
    auto linked = editor.document.pattern().gestures.front();
    linked.points.front().fret = 3;
    editor.replace_slide(0, linked, {});
    check_ui(editor.document.pattern().events.front().fret == 3, "initial curve edit did not update shared pluck fret");
    auto linked_event = editor.document.pattern().events.front();
    linked_event.fret = 4;
    editor.replace_event(0, linked_event, {});
    check_ui(editor.document.pattern().gestures.front().points.front().fret == 4, "pluck edit did not update shared initial curve fret");
    editor.undo(); editor.undo(); editor.undo();
    check_ui(editor.document.pattern().gestures.empty() && editor.document.pattern().events.front().fret == 0,
             "linked fret edits were not independently undoable");
    editor.cursor_beat = 0.25;
    editor.cursor_lane = 5;
    editor.add_slide();
    const auto id = editor.document.pattern().gestures.front().id;
    check_ui(editor.selected_gesture == 0 && editor.selected == -1, "new slide selection missing");

    bool focus = true;
    const auto controls = [&] {
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(450, 680));
        ImGui::Begin("Slide control regression");
        if (focus) { ImGui::SetKeyboardFocusHere(); focus = false; }
        slide_inspector(editor);
        ImGui::End();
    };
    frame(host, controls);
    frame(host, controls);
    frame(host, controls, [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    });
    frame(host, controls, [&] {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8("0.31234567890123");
    });
    frame(host, controls, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true); });
    frame(host, controls, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false); });
    check_ui(std::abs(editor.document.pattern().gestures.front().beat - 0.31234567890123) < 1e-14,
             "real slide beat widget lost microtiming or did not commit");
    editor.undo();
    check_ui(editor.document.pattern().gestures.size() == 1 && editor.document.pattern().gestures.front().beat == 0.25,
             "slide numeric entry did not undo as one operation");
    editor.undo(true);
    const auto edited_beat = editor.document.pattern().gestures.front().beat;

    const ImVec2 origin(48, 80), size(800, 420);
    const auto tab = [&] {
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(900, 600));
        ImGui::Begin("Slide canvas regression");
        keyboard(editor);
        ImGui::SetCursorScreenPos(origin);
        canvas(editor, size);
        ImGui::End();
    };
    editor.selected_gesture = -1;
    frame(host, tab);
    const float x = origin.x + 64 + static_cast<float>((edited_beat + 0.5) / 8) * (size.x - 84);
    const float y = origin.y + 32 + 5.5f * ((size.y - 42) / 7);
    frame(host, tab, [&] {
        ImGui::GetIO().AddMousePosEvent(x, y);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    });
    frame(host, tab, [&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); });
    check_ui(editor.selected_gesture == 0 && editor.selected == -1, "tab could not select continuous slide");
    frame(host, tab, [&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true); });
    check_ui(editor.document.pattern().gestures.empty(), "Delete did not remove selected curve");
    frame(host, tab, [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Z, true);
    });
    check_ui(editor.document.pattern().gestures.size() == 1 && editor.document.pattern().gestures.front().id == id, "keyboard undo lost slide ID");
    frame(host, tab, [&] {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Z, false);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    });
    editor.attempt([&] { editor.set_model("baseline_0_1"); });
    check_ui(editor.status_error && editor.document.pattern().model == "contact_0_2", "baseline selection flattened curves");
    editor.save();
    Editor reopened(args);
    check_ui(reopened.document.pattern().schema_version == 2 && reopened.document.pattern().gestures.front().beat == edited_beat,
             "editor save/reopen lost curve format or microtiming");
    reopened.selected = -1;
    reopened.selected_gesture = 0;
    reopened.request_render();
    reopened.pending.wait();
    reopened.poll_render();
    check_ui(reopened.rendered.audio.samples == gtracker::render_pattern(reopened.document.pattern()).samples, "editor used different curve renderer from native CLI/library");
    frame(host, [&] { interface(reopened); });
    frame(host, [&] { interface(reopened); });
    host.screenshot(std::filesystem::current_path() / "editor-slide-controls.bmp");
    reopened.set_model("retained_0_5");
    check_ui(gtracker::supports_slides(reopened.document.pattern()) && reopened.document.pattern().gestures.front().id == id,
             "retained model selection dropped slide support/identity");
    reopened.undo();
    check_ui(reopened.document.pattern().model == "contact_0_2", "undo did not restore the contact model");
    reopened.undo(true);
    reopened.save();
    Editor retained(args);
    check_ui(retained.document.pattern().model == "retained_0_5", "retained model did not reopen");
    retained.request_render(); retained.pending.wait(); retained.poll_render();
    check_ui(retained.rendered.audio.samples == gtracker::render_pattern(retained.document.pattern()).samples,
             "editor silently fell back from the retained model");
    frame(host, [&] { interface(retained); });
    frame(host, [&] { interface(retained); });
    host.screenshot(std::filesystem::current_path() / "editor-retained-controls.bmp");
}

void production_authoring(Host& host, const Arguments& original) {
    Arguments args = original;
    args.document = original.document.parent_path() / "production.gtrk.json";
    std::filesystem::copy_file(original.resource_dir / "examples/campfire.gtrk.json", args.document,
                               std::filesystem::copy_options::overwrite_existing);
    Editor editor(args);
    const auto dry = gtracker::render_pattern(editor.document.pattern());
    ImVec2 checkbox;
    auto controls = [&] {
        ImGui::SetNextWindowPos(ImVec2(30, 30));
        ImGui::SetNextWindowSize(ImVec2(430, 570));
        ImGui::Begin("Production control regression");
        checkbox = ImGui::GetCursorScreenPos();
        production_inspector(editor);
        ImGui::End();
    };
    frame(host, controls); frame(host, controls);
    frame(host, controls, [&] {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(checkbox.x + 6, checkbox.y + 6);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    });
    frame(host, controls, [&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); });
    check_ui(editor.document.pattern().production && editor.document.pattern().production->enabled && editor.document.pattern().schema_version == 2,
             "real room checkbox did not create explicit production settings");
    editor.undo();
    check_ui(!editor.document.pattern().production && editor.document.pattern().schema_version == 1 && !editor.document.dirty(),
             "room checkbox undo failed to restore the original v1 savepoint");
    editor.undo(true);
    auto settings = *editor.document.pattern().production;
    settings.wet = 0.2f; editor.set_production(settings, "room-drag");
    settings.wet = 0.3f; editor.set_production(settings, "room-drag");
    editor.document.end_edit();
    editor.undo();
    check_ui(editor.document.pattern().production->wet == gtracker::ProductionSettings{}.wet, "room drag did not undo as one operation");
    editor.undo(true);
    editor.save();
    Editor reopened(args);
    check_ui(reopened.document.pattern().production->wet == 0.3f, "saved room mix did not reopen");
    reopened.request_render(); reopened.pending.wait(); reopened.poll_render();
    check_ui(reopened.rendered.audio.channels == 2 && reopened.rendered.audio.samples == gtracker::render_pattern(reopened.document.pattern()).samples,
             "editor and CLI/library production render differ");
    reopened.play();
    check_ui(reopened.playing && reopened.audio_channels == 2 && reopened.audio_rate == 48000, "wet playback did not open stereo input");
    reopened.stop();
    settings = *reopened.document.pattern().production; settings.enabled = false;
    reopened.set_production(settings);
    check_ui(!reopened.current_audio(), "room bypass kept stale audio marked current");
    reopened.request_render(); reopened.pending.wait(); reopened.poll_render();
    check_ui(reopened.rendered.audio.samples == dry.samples, "editor bypass changed exact dry PCM");
    reopened.play();
    check_ui(reopened.audio_channels == 1, "dry playback did not restore mono stream");
    reopened.stop();
    frame(host, controls); frame(host, controls);
    host.screenshot(std::filesystem::current_path() / "editor-production-controls.bmp");
}

void proposal_workflow(Host& host, const Arguments& original, const std::filesystem::path& fixture) {
    const auto directory = original.document.parent_path() / "proposal-copy";
    std::filesystem::copy(fixture.parent_path(), directory, std::filesystem::copy_options::recursive);
    Arguments args = original; args.document.clear(); args.take = directory / "take.json";
    Editor editor(args);
    const auto source_before = file_bytes(directory / "source.wav");
    const auto metadata_before = file_bytes(directory / "analysis.json");
    const auto generation = editor.document.generation();
    set_text(editor.proposal_path, (directory / "proposals.json").u8string());
    editor.load_proposals();
    check_ui(editor.proposals && editor.proposals->notes.size() == 4 && editor.document.generation() == generation && !editor.document.dirty(),
             "loading proposals edited the source score or lost the actual generated candidates");
    check_ui(std::all_of(editor.proposal_alternatives.begin(), editor.proposal_alternatives.end(), [](int value) { return value == -1; }),
             "proposal fingerings were silently preselected");
    editor.attempt([&] { editor.accept_proposals(); });
    check_ui(editor.status_error && editor.document.pattern().events.empty(), "empty proposal selection edited the score");
    for (std::size_t i = 0; i < editor.proposals->notes.size(); ++i)
        editor.proposal_alternatives[i] = static_cast<int>(editor.proposals->notes[i].alternatives.size())-1;
    ImVec2 a, b;
    auto draw = [&] {
        ImGui::SetNextWindowPos(ImVec2(20,20)); ImGui::SetNextWindowSize(ImVec2(620,690));
        ImGui::Begin("Note proposal review regression");
        proposal_inspector(editor);
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax();
        ImGui::End();
    };
    frame(host,draw); frame(host,draw);
    host.screenshot(std::filesystem::current_path() / "editor-proposal-review.bmp");
    frame(host,draw,[&] { ImGui::GetIO().AddMousePosEvent((a.x+b.x)*.5f,(a.y+b.y)*.5f); ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true); });
    frame(host,draw,[&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false); });
    check_ui(editor.document.pattern().events.size() == 4 && editor.document.dirty(), "real Accept selected plucks button did not commit the chosen batch");
    for (const auto& event : editor.document.pattern().events) check_ui(event.string_index == 5, "chosen string alternatives were ignored");
    const auto first_id = editor.document.pattern().events.front().id;
    check_ui(!gtracker::note_proposals_match(*editor.proposals,editor.document.pattern(),editor.source->take), "applied proposals stayed fresh for a second import");
    editor.undo(); check_ui(editor.document.pattern().events.empty(), "proposal batch did not undo in one operation");
    editor.undo(true); check_ui(editor.document.pattern().events.front().id == first_id, "proposal redo lost stable event IDs");
    editor.save();
    Editor reopened(args);
    check_ui(reopened.document.pattern().events.size() == 4, "accepted proposals did not survive source take save/reopen");
    reopened.request_render(); reopened.pending.wait(); reopened.poll_render(); reopened.play();
    check_ui(reopened.current_audio() && reopened.playing, "accepted notes did not render/queue through the shared instrument");
    reopened.play_source(); check_ui(reopened.audio_rate == 16000 && reopened.audio_channels == 1, "proposal A/B changed original source format");
    reopened.stop();
    check_ui(file_bytes(directory / "source.wav") == source_before && file_bytes(directory / "analysis.json") == metadata_before,
             "review/accept/save altered source PCM or the preserved metadata job");

    const auto corrupt = original.document.parent_path() / "proposal-corrupt-copy";
    std::filesystem::copy(fixture.parent_path(), corrupt, std::filesystem::copy_options::recursive);
    args.take = corrupt / "take.json"; Editor guarded(args);
    set_text(guarded.proposal_path, (corrupt / "proposals.json").u8string()); guarded.load_proposals(); guarded.proposal_alternatives[0] = 0;
    { std::fstream audio(corrupt / "source.wav",std::ios::binary|std::ios::in|std::ios::out); audio.seekp(-1,std::ios::end); audio.put('\x01'); }
    guarded.attempt([&] { guarded.accept_proposals(); });
    check_ui(guarded.status_error && guarded.document.pattern().events.empty() && !guarded.document.dirty(), "source changed after loading but stale proposals were accepted");
}

void keyboard_entry(Host& host, const Arguments& args) {
    Editor editor(args);
    auto empty = editor.document.pattern(); empty.events.clear(); empty.gestures.clear();
    editor.document = gtracker::EditorDocument(empty); editor.selected = -1; editor.fit_timeline(false);
    ImVec2 origin, input_a, input_b, number_a, number_b;
    double number=1;
    std::array<char,64> text{};
    auto draw = [&] {
        ImGui::SetNextWindowPos(ImVec2(20,20)); ImGui::SetNextWindowSize(ImVec2(760,460));
        ImGui::Begin("Keyboard tab regression");
        if (editor.document.pending() == gtracker::EditorDocument::Pending::none) keyboard(editor);
        origin = ImGui::GetCursorScreenPos(); canvas(editor,ImVec2(700,360));
        ImGui::InputText("A path field",text.data(),text.size()); input_a=ImGui::GetItemRectMin(); input_b=ImGui::GetItemRectMax();
        ImGui::InputDouble("A numeric field",&number); number_a=ImGui::GetItemRectMin(); number_b=ImGui::GetItemRectMax();
        unsaved_dialog(editor);
        ImGui::End();
    };
    auto key = [&](ImGuiKey value) {
        frame(host,draw,[&] { ImGui::GetIO().AddKeyEvent(value,true); });
        frame(host,draw,[&] { ImGui::GetIO().AddKeyEvent(value,false); });
    };
    auto click = [&](double beat,int lane) {
        const float x=editor.tab_geometry.beat_x(beat), y=origin.y+32+(lane+.5f)*(360-42)/7;
        frame(host,draw,[&] { ImGui::GetIO().AddMousePosEvent(x,y); ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true); });
        frame(host,draw,[&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false); });
    };
    frame(host,draw); frame(host,draw); click(1,0);
    check_ui(editor.tab_keyboard_focus && editor.cursor_beat == 1 && editor.cursor_lane == 0,"canvas click did not focus keyboard entry at the clicked cell");
    key(ImGuiKey_1); const auto id=editor.document.pattern().events.front().id; key(ImGuiKey_2);
    check_ui(editor.document.pattern().events.size() == 1 && editor.document.pattern().events.front().fret == 12 && editor.document.pattern().events.front().id == id,
             "two-digit entry duplicated the event or lost its stable ID");
    editor.undo(); check_ui(editor.document.pattern().events.empty() && !editor.document.dirty(),"two fret digits did not undo in one operation");
    editor.undo(true); const auto generation=editor.document.generation(); key(ImGuiKey_RightArrow);
    check_ui(editor.cursor_beat == 1.5 && editor.selected == -1 && editor.document.generation() == generation,"arrow navigation edited data or failed to move the tab cursor");
    key(ImGuiKey_3); key(ImGuiKey_6);
    check_ui(editor.document.pattern().events.back().fret == 36,"highest supported two-digit fret was rejected");
    key(ImGuiKey_3); key(ImGuiKey_7);
    check_ui(editor.status_error && editor.document.pattern().events.back().fret == 3,"invalid fret37 was silently accepted");
    editor.undo(); check_ui(editor.document.pattern().events.back().fret == 36,"invalid-prefix undo did not restore the previous complete cell");
    key(ImGuiKey_2); for (int i=0;i<55;++i) frame(host,draw); key(ImGuiKey_3);
    check_ui(editor.document.pattern().events.back().fret == 3,"expired fret entry unexpectedly became23");
    editor.undo(); check_ui(editor.document.pattern().events.back().fret == 2,"a later entry coalesced into an old undo operation");
    editor.undo(); check_ui(editor.document.pattern().events.back().fret == 36,"prefix timeout lost the earlier cell");
    key(ImGuiKey_DownArrow); key(ImGuiKey_X);
    check_ui(editor.document.pattern().events.back().type == "damp" && editor.document.pattern().events.back().string_index == 1,"X did not enter damping on the current string");
    const auto damp_id=editor.document.pattern().events.back().id;
    const auto count=editor.document.pattern().events.size(); key(ImGuiKey_0);
    check_ui(editor.document.pattern().events.size() == count && editor.document.pattern().events.back().id == damp_id && editor.document.pattern().events.back().type == "pluck",
             "typing over a damp cell duplicated the event or lost its ID");
    key(ImGuiKey_X);
    key(ImGuiKey_B); check_ui(editor.cursor_lane == 6 && editor.document.pattern().events.back().type == "body_hit","B did not enter a body hit at the cursor beat");
    const auto before_input=gtracker::pattern_json(editor.document.pattern());
    frame(host,draw,[&] { ImGui::GetIO().AddMousePosEvent((input_a.x+input_b.x)*.5f,(input_a.y+input_b.y)*.5f); ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true); });
    frame(host,draw,[&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false); ImGui::GetIO().AddInputCharactersUTF8("23"); });
    key(ImGuiKey_2); key(ImGuiKey_Delete); key(ImGuiKey_RightArrow);
    check_ui(!editor.tab_keyboard_focus && gtracker::pattern_json(editor.document.pattern()) == before_input,"tab keys edited musical data while a path field was active");
    frame(host,draw,[&] { ImGui::GetIO().AddMousePosEvent((number_a.x+number_b.x)*.5f,(number_a.y+number_b.y)*.5f); ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true); });
    frame(host,draw,[&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false); });
    key(ImGuiKey_5); key(ImGuiKey_LeftArrow);
    check_ui(gtracker::pattern_json(editor.document.pattern()) == before_input,"tab digits/navigation edited music while a numeric field was active");
    editor.document.request_close(); frame(host,draw); frame(host,draw); key(ImGuiKey_6);
    check_ui(gtracker::pattern_json(editor.document.pattern()) == before_input && editor.document.pending() == gtracker::EditorDocument::Pending::close,
             "unsaved-work dialog let a tab digit edit the document");
    key(ImGuiKey_Escape);
    check_ui(editor.document.pending() == gtracker::EditorDocument::Pending::none,"Escape did not cancel the keyboard test's unsaved-work dialog");
    key(ImGuiKey_Escape); click(1.5,0); key(ImGuiKey_Escape);
    check_ui(!editor.tab_keyboard_focus,"Escape did not leave keyboard tab entry");
    const auto escaped=gtracker::pattern_json(editor.document.pattern()); key(ImGuiKey_4);
    check_ui(gtracker::pattern_json(editor.document.pattern()) == escaped,"unfocused digit edited a fret");
    click(1.5,0); frame(host,draw,[&] { ImGui::GetIO().AddFocusEvent(false); });
    frame(host,draw); key(ImGuiKey_5);
    check_ui(!editor.tab_keyboard_focus && gtracker::pattern_json(editor.document.pattern()) == escaped,"window focus loss did not release musical keyboard input");

    auto linked=empty; linked.schema_version=2; linked.model="contact_0_2";
    linked.events={{"original-id","pluck",.333333333333,0,2.5f,.7f}};
    linked.gestures={{"original-curve","slide",.333333333333,0,{{0,2.5f},{.5,7}}}};
    editor.document=gtracker::EditorDocument(linked); editor.cursor_lane=0; editor.selected=0; editor.selected_gesture=-1;
    click(.333333333333,0); key(ImGuiKey_Keypad1); key(ImGuiKey_Keypad2);
    check_ui(editor.document.pattern().events.front().id == "original-id" && editor.document.pattern().events.front().beat == linked.events.front().beat &&
             editor.document.pattern().events.front().fret == 12 && editor.document.pattern().gestures.front().points.front().fret == 12,
             "keypad entry quantized source timing, replaced IDs or broke shared initial contact");
    editor.undo(); check_ui(editor.document.pattern().events.front().fret == 2.5f && editor.document.pattern().gestures.front().points.front().fret == 2.5f,
                            "linked keyboard contact/fret edit did not undo together");
    const auto linked_generation=editor.document.generation(); key(ImGuiKey_RightArrow);
    check_ui(editor.cursor_beat == linked.events.front().beat+.5 && editor.document.generation() == linked_generation,"arrow movement quantized an unsnapped existing cursor");
    editor.request_render(); editor.pending.wait(); editor.poll_render();
    frame(host,[&] { interface(editor); }); frame(host,[&] { interface(editor); });
    host.screenshot(std::filesystem::current_path()/"editor-keyboard-main.bmp");
    auto ambiguous=linked; ambiguous.events.push_back(ambiguous.events.front()); ambiguous.events.back().id="another-pluck";
    editor.document=gtracker::EditorDocument(ambiguous); editor.cursor_beat=linked.events.front().beat; editor.cursor_lane=0; editor.selected=-1;
    frame(host,draw); frame(host,draw);
    const auto ambiguous_before=gtracker::pattern_json(editor.document.pattern()); key(ImGuiKey_4);
    check_ui(editor.status_error && gtracker::pattern_json(editor.document.pattern()) == ambiguous_before,"keyboard entry silently chose among coincident events");
    key(ImGuiKey_Home); check_ui(editor.cursor_beat == 0,"Home did not reach beat zero");
    key(ImGuiKey_End); check_ui(editor.cursor_beat == 7.5,"End did not reach the final snap position inside the pattern");
    frame(host,draw); host.screenshot(std::filesystem::current_path()/"editor-keyboard-entry.bmp");
}

void timeline_navigation(Host& host, const Arguments& original, const std::filesystem::path& manifest) {
    Arguments args = original;
    args.document.clear(); args.take = manifest;
    Editor editor(args);
    const auto document = gtracker::pattern_json(editor.document.pattern());
    const auto source = file_bytes(editor.source->take.source_path);
    const auto generation = editor.document.generation();
    const auto draw = [&] { interface(editor); };
    const auto aligned = [&] {
        for (double beat : {0.0, 0.333333333333, 4.0, 8.0})
            check_ui(std::abs(editor.tab_geometry.beat_x(beat) - editor.timeline_geometry.beat_x(beat)) < 0.001,
                     "tab and waveform do not share screen-pixel beat positions");
    };
    frame(host, draw); frame(host, draw); aligned();
    check_ui(editor.view.first < 0 && editor.view.last > editor.document.pattern().length_beats, "source context did not include pre-roll/tail");
    editor.fit_timeline(false);
    frame(host, draw); aligned();
    check_ui(editor.view.first == 0 && editor.view.last == 8, "pattern fit did not restore musical bounds");
    const float middle = (editor.tab_geometry.left + editor.tab_geometry.right) * 0.5f;
    frame(host, draw, [&] { ImGui::GetIO().AddMousePosEvent(middle, 300); });
    frame(host, draw, [&] { ImGui::GetIO().AddMouseWheelEvent(0, 1); });
    frame(host, draw); aligned();
    check_ui(editor.view.span() < 8 && !editor.follow_view_fit, "tab wheel did not zoom the shared viewport");
    const double zoomed_first = editor.view.first;
    frame(host, draw, [&] {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    });
    frame(host, draw, [&] {
        ImGui::GetIO().AddMousePosEvent(middle + 40, 300);
    });
    frame(host, draw, [&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, false); });
    frame(host, draw); aligned();
    check_ui(editor.view.first < zoomed_first, "middle drag did not pan the tab");
    const double before_wave_zoom = editor.view.span();
    frame(host, draw, [&] { ImGui::GetIO().AddMousePosEvent(middle, 580); });
    frame(host, draw, [&] { ImGui::GetIO().AddMouseWheelEvent(0, 1); });
    frame(host, draw); aligned();
    check_ui(editor.view.span() < before_wave_zoom, "plot wheel did not retain interactive zoom");

    int width = 0, height = 0;
    SDL_GetWindowSize(host.window, &width, &height);
    check_ui(SDL_SetWindowSize(host.window, 980, 680), "resize dummy window");
    frame(host, draw); frame(host, draw); aligned();
    check_ui(editor.timeline_geometry.right < 980 && editor.timeline_geometry.right > editor.timeline_geometry.left,
             "shared geometry did not follow window resize");
    host.screenshot(std::filesystem::current_path() / "editor-timeline-compact.bmp");
    check_ui(SDL_SetWindowSize(host.window, width, height), "restore dummy window size");
    editor.fit_timeline(true);
    frame(host, draw); frame(host, draw); aligned();
    const float context = editor.tab_geometry.beat_x(-0.75);
    for (int i = 0; i < 2; ++i) {
        frame(host, draw, [&] {
            ImGui::GetIO().AddMousePosEvent(context, 300);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        });
        frame(host, draw, [&] { ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); });
    }
    check_ui(gtracker::pattern_json(editor.document.pattern()) == document && editor.document.generation() == generation && !editor.document.dirty(),
             "viewport navigation or context double-click edited musical data/history");
    frame(host,draw,[&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_4,true); });
    frame(host,draw,[&] { ImGui::GetIO().AddKeyEvent(ImGuiKey_4,false); });
    check_ui(gtracker::pattern_json(editor.document.pattern()) == document && !editor.tab_keyboard_focus,
             "retained-context click enabled editing at an old in-pattern cursor");
    check_ui(file_bytes(editor.source->take.source_path) == source, "view navigation altered preserved audio");
}
}

int main(int argc, char** argv) {
    try {
        check_ui(argc == 4, "Expected source root, generated take and proposal manifests");
        const auto directory = std::filesystem::current_path() / "editor-ui-test-output";
        std::filesystem::create_directories(directory);
        Arguments args;
        args.data_dir = directory / "data";
        args.resource_dir = std::filesystem::path(argv[1]);
        args.document = directory / std::filesystem::u8path(u8"yö-音楽.gtrk.json");
        std::filesystem::copy_file(std::filesystem::path(argv[1]) / "examples/campfire.gtrk.json", args.document,
                                   std::filesystem::copy_options::overwrite_existing);
        Host host;
        host.initialize();
        grouped_widget(host, args);
        guarded_interface(host, args);
        render_and_dummy_audio(args);
        render_cancellation(host, args);
        writable_defaults(args);
        source_workflow(host, args, std::filesystem::path(argv[2]));
        slide_authoring(host, args);
        production_authoring(host, args);
        proposal_workflow(host, args, std::filesystem::path(argv[3]));
        keyboard_entry(host,args);
        timeline_navigation(host, args, std::filesystem::path(argv[2]));
        check_ui(std::filesystem::weakly_canonical(directory).parent_path() == std::filesystem::current_path(),
                 "test cleanup target escaped its working directory");
        std::filesystem::remove_all(directory);
        std::cout << "editor UI: synthetic controls/modal, stale rendering and dummy playback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "editor UI: " << error.what() << '\n';
        return 1;
    }
}
