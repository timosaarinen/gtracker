// Exercise the real native widgets and shortcut/modal wiring with synthetic
// input. This is explicitly separate from physical window/device qualification.
#define GT_EDITOR_TEST
#include "../src/editor.cpp"

#include <functional>

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
}

int main(int argc, char** argv) {
    try {
        check_ui(argc == 2, "Expected source root");
        const auto directory = std::filesystem::current_path() / "editor-ui-test-output";
        std::filesystem::create_directories(directory);
        Arguments args;
        args.document = directory / std::filesystem::u8path(u8"yö-音楽.gtrk.json");
        std::filesystem::copy_file(std::filesystem::path(argv[1]) / "examples/campfire.gtrk.json", args.document,
                                   std::filesystem::copy_options::overwrite_existing);
        Host host;
        host.initialize();
        grouped_widget(host, args);
        guarded_interface(host, args);
        render_and_dummy_audio(args);
        std::filesystem::remove_all(directory);
        std::cout << "editor UI: synthetic controls/modal, stale rendering and dummy playback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "editor UI: " << error.what() << '\n';
        return 1;
    }
}
