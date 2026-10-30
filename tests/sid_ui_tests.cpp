// Synthetic native ImGui input, separate from physical interaction evidence.
#include "../src/sid/editor.cpp"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
namespace gtracker::sid {
struct EditorTest {
    static void run(Editor &editor, SDL_Renderer *renderer) {
        auto &s = *editor.state;
        auto check = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        auto frame = [&](ImGuiKey key = ImGuiKey_None, bool down = false) {
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            auto &io = ImGui::GetIO();
            io.DeltaTime = 1.0f / 60;
            io.ConfigInputTrickleEventQueue = false;
            io.AddFocusEvent(true);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, down);
            ImGui::NewFrame();
            editor.draw();
            ImGui::Render();
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        };
        auto press = [&](ImGuiKey key) {
            frame(key, true);
            frame(key, false);
        };
        frame();
        frame();
        // Minimum-size synthetic layout evidence at normal and 125% style/text scale.
        for (float scale : {1.0f, 1.25f}) {
            ImGui::GetIO().FontGlobalScale = scale;
            if (scale > 1)
                ImGui::GetStyle().ScaleAllSizes(scale);
            frame();
            frame();
            SDL_Surface *image = SDL_RenderReadPixels(renderer, nullptr);
            check(image != nullptr, "SID layout capture failed");
            const auto output = s.data / (scale > 1 ? "sid-minimum-125.bmp" : "sid-minimum.bmp");
            check(SDL_SaveBMP(image, output.u8string().c_str()), "SID layout capture could not save");
            SDL_DestroySurface(image);
        }
        ImGui::GetIO().FontGlobalScale = 1;
        ImGui::GetStyle().ScaleAllSizes(0.8f);
        // Missing backend fails asynchronously while the document remains editable.
        s.backend.tools = s.data / "missing-tools";
        press(ImGuiKey_Z);
        check(cell(s.current(), 0, 0)->note == 60, "Z should enter scientific C4");
        check(s.cursor.row == 1, "note must advance edit step");
        press(ImGuiKey_A);
        check(cell(s.current(), 1, 0)->type == EventType::off, "A should enter OFF");
        s.stop();
        if (s.job.valid())
            s.job.wait();
        frame();
        s.edit = false;
        const auto before = serialize(s.doc.song);
        press(ImGuiKey_X);
        check(serialize(s.doc.song) == before, "Edit off must not mutate");
        s.stop();
        if (s.job.valid())
            s.job.wait();
        frame();
        s.edit = true;
        s.cursor = {0, 0, 0};
        s.anchor = {1, 0, 0};
        s.selection = true;
        s.copy();
        s.cursor = {62, 1, 0};
        s.selection = false;
        s.paste();
        check(cell(s.current(), 62, 1)->note == 60 && cell(s.current(), 63, 1)->type == EventType::off,
              "rectangular paste semantics");
        s.doc.undo();
        check(!cell(s.current(), 62, 1), "paste should undo as one action");
        s.doc.redo();
        s.cursor = {62, 1, 0};
        s.anchor = s.cursor;
        s.selection = false;
        s.shift(12);
        check(cell(s.current(), 62, 1)->note == 72, "transpose selected note");
        s.cursor = {0, 0, 0};
        s.selection = false;
        press(ImGuiKey_Delete);
        check(!cell(s.current(), 0, 0), "Delete clears entire event");
        s.doc.undo();
        s.save_to(s.data / "roundtrip.sidtrk.json");
        check(serialize(load(s.path)) == serialize(s.doc.song), "save/reopen lost musical data");
        // A modal owns Escape; it must not toggle Edit in that frame.
        const bool edit_before = s.edit;
        s.file("save");
        frame();
        press(ImGuiKey_Escape);
        check(s.edit == edit_before, "modal Escape leaked to Edit");
        s.doc.change([](Song &song) { song.name = "Unsaved"; });
        s.ask_close = true;
        frame();
        check(!s.close, "dirty close must prompt");
        press(ImGuiKey_Escape);
        check(!s.close && s.doc.dirty(), "cancel close lost changes");
        s.stop();
        const auto real_tools = std::filesystem::absolute(s.resources / ".local/sid/tools");
        if (std::filesystem::exists(real_tools / "backend.json")) {
            s.backend.tools = real_tools;
            s.audition(60);
            std::cout << s.status << '\n';
            const auto audition_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(s.job.valid() && std::chrono::steady_clock::now()<audition_deadline){SDL_Delay(5);frame();}
            check(s.playing_audition && !s.job.valid(),
                  "Realtime audition should start without a complete render");
            s.stop();
            s.audition(60);
            check(!s.job.valid() && s.transport.active && s.transport.realtime,
                  "repeated note should reuse compiled player without preparation");
            s.stop();
            s.path.clear();
            s.doc.opened(new_song());
            s.pattern = 0;
            s.occurrence = 0;
            s.doc.change([](Song &song) {
                song.patterns[0].voices[0] = {{0, EventType::note, 60, 1}, {32, EventType::off, 60, 1}};
            });
            s.start(false, true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (s.job.valid() && std::chrono::steady_clock::now() < deadline) {
                SDL_Delay(10);
                frame();
            }
            check(!s.job.valid() && s.transport.active, "native song transport did not start");
            const auto sound_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(s.transport.position()==0 && std::chrono::steady_clock::now()<sound_deadline)SDL_Delay(5);
            check(s.transport.live.error().empty(), "realtime producer failed");
            const auto consumed = s.transport.position();
            check(consumed > 0, "PCM callback did not advance");
            s.transport.toggle();
            const auto paused = s.transport.position();
            SDL_Delay(30);
            check(s.transport.position() == paused, "pause changed PCM position");
            s.transport.toggle();
            s.stop();
            check(!s.transport.active, "Stop failed");
            s.start(true);
            while (s.job.valid() && std::chrono::steady_clock::now() < deadline) {
                SDL_Delay(10);
                frame();
            }
            check(s.transport.active && s.transport.loop_end > s.transport.loop_start,
                  "native pattern loop did not prepare");
            check(s.transport.realtime, "pattern loop must synthesize live");
            s.stop();
            s.prepared.reset();
            s.start(false, true);
            s.mutate([](Song &song) { song.name = "Changed during render"; });
            while (s.job.valid() && std::chrono::steady_clock::now() < deadline) {
                SDL_Delay(10);
                frame();
            }
            check(!s.transport.active, "stale job autoplayed after edit");
        } else
            std::cout << "Pinned backend not available: real SID transport subset skipped\n";
    }
};
} // namespace gtracker::sid
int main(int argc, char **argv) {
    try {
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        auto *window = SDL_CreateWindow("SID native UI tests", 980, 680, 0);
        auto *renderer = SDL_CreateRenderer(window, "software");
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImFontConfig font;
        font.SizePixels = 16;
        ImGui::GetIO().Fonts->AddFontDefault(&font);
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);
        {
            gtracker::sid::Editor editor(
                {}, argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::current_path(),
                std::filesystem::temp_directory_path() / "gtracker-sid-ui-tests");
            gtracker::sid::EditorTest::run(editor, renderer);
        }
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "SID native synthetic UI checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
