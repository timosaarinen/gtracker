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
    check_ui(reopened.rendered.audio.mono == gtracker::render_pattern(reopened.document.pattern()).mono, "editor used different curve renderer from native CLI/library");
    frame(host, [&] { interface(reopened); });
    frame(host, [&] { interface(reopened); });
    host.screenshot(std::filesystem::current_path() / "editor-slide-controls.bmp");
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
    check_ui(file_bytes(editor.source->take.source_path) == source, "view navigation altered preserved audio");
}
}

int main(int argc, char** argv) {
    try {
        check_ui(argc == 3, "Expected source root and generated take manifest");
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
        writable_defaults(args);
        source_workflow(host, args, std::filesystem::path(argv[2]));
        slide_authoring(host, args);
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
