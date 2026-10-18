#include "gtracker/editor_document.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using gtracker::EditorDocument;
using gtracker::Pattern;
using Decision = EditorDocument::Decision;
using Pending = EditorDocument::Pending;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}

void tempo(EditorDocument& document, double bpm, const std::string& key = {}) {
    auto next = document.pattern();
    next.bpm = bpm;
    document.commit(std::move(next), key);
}

void history(const Pattern& original, const std::filesystem::path& directory) {
    EditorDocument document(original);
    check(!document.dirty() && !document.can_undo(), "initial content should be clean");
    const auto generation = document.generation();
    check(!document.commit(original), "no-op must not create history");
    check(document.generation() == generation, "no-op invalidated render");
    tempo(document, 100);
    check(document.dirty() && document.can_undo(), "edit was not tracked");
    check(document.undo(), "undo unavailable");
    check(document.pattern().bpm == 90 && !document.dirty(), "undo to savepoint stayed dirty");
    check(document.pattern().revision == original.revision + 2, "undo did not advance revision");
    const auto snapshot = gtracker::pattern_json(document.pattern());
    rejects([&] { tempo(document, 0); }, "invalid tempo accepted");
    check(gtracker::pattern_json(document.pattern()) == snapshot && document.can_redo(),
          "failed edit changed document or destroyed redo");
    check(document.undo(true) && document.pattern().bpm == 100, "redo failed");
    document.save(directory / "savepoint.json");
    check(!document.dirty(), "save did not mark content clean");
    check(document.undo() && document.dirty(), "undo away from savepoint stayed clean");
    check(document.undo(true) && !document.dirty(), "redo to saved content stayed dirty");

    EditorDocument grouped(original);
    for (int i = 1; i <= 50; ++i) tempo(grouped, 90 + i, "bpm-control");
    grouped.end_edit();
    check(grouped.undo() && grouped.pattern().bpm == 90, "drag did not undo as one operation");
    check(!grouped.can_undo(), "drag left intermediate undo steps");
    check(grouped.undo(true) && grouped.pattern().bpm == 140, "drag redo lost final value");
    tempo(grouped, 150, "bpm-control");
    grouped.end_edit();
    check(grouped.undo() && grouped.pattern().bpm == 140, "separate drags merged");
    tempo(grouped, 141, "bpm-control");
    tempo(grouped, 140, "bpm-control");
    grouped.end_edit();
    check(grouped.undo() && grouped.pattern().bpm == 90, "round-trip drag left useless history");

    EditorDocument controls(original);
    tempo(controls, 100, "bpm");
    auto next = controls.pattern();
    next.brightness = 0.3f;
    controls.commit(next, "brightness");
    check(controls.undo() && controls.pattern().bpm == 100, "different controls were coalesced");
    check(controls.pattern().brightness == original.brightness, "wrong property restored");
    tempo(controls, 110, "bpm");
    controls.save(directory / "mid-drag.json");
    tempo(controls, 120, "bpm");
    check(controls.undo() && !controls.dirty(), "save did not split the edit group");

    EditorDocument bounded(original);
    for (int i = 1; i <= 150; ++i) tempo(bounded, 90 + i);
    int undos = 0;
    while (bounded.undo()) ++undos;
    check(undos == 128, "history bound was not maintained");
    check(bounded.pattern().bpm == 112, "history bound restored wrong state");
}

void leave_guards(const Pattern& original, const std::filesystem::path& directory) {
    const auto old_file = directory / "current.json";
    const auto new_file = directory / "next.json";
    auto other = original;
    other.name = "Next document";
    other.bpm = 120;
    gtracker::save_pattern(original, old_file);
    gtracker::save_pattern(other, new_file);
    const auto target_bytes = gtracker::pattern_json(gtracker::load_pattern(new_file));

    EditorDocument document(original, old_file);
    tempo(document, 105);
    document.request_close();
    check(document.pending() == Pending::close && !document.close_requested(), "dirty close was not guarded");
    document.resolve(Decision::cancel);
    check(document.pending() == Pending::none && document.dirty(), "cancel lost the current edit");
    document.request_load(new_file);
    check(document.pattern().bpm == 105 && document.pending_path() == new_file, "dirty load replaced current content");
    document.request_load(directory / "ignored.json");
    document.request_close();
    check(document.pending() == Pending::load && document.pending_path() == new_file,
          "a repeated request replaced the pending target");
    document.resolve(Decision::save);
    check(document.pattern().name == other.name && !document.dirty() && !document.can_undo(), "save/load transition failed");
    check(gtracker::load_pattern(old_file).bpm == 105, "Save and Load did not save current file");
    check(gtracker::pattern_json(gtracker::load_pattern(new_file)) == target_bytes,
          "Save and Load overwrote requested load target");
    document.request_close();
    check(document.close_requested(), "clean close was blocked");

    EditorDocument discard(original, old_file);
    tempo(discard, 110);
    discard.request_load(new_file);
    discard.resolve(Decision::discard);
    check(discard.pattern().bpm == 120 && !discard.dirty(), "discard/load failed");
    check(gtracker::load_pattern(old_file).bpm == 105, "discard wrote unsaved edits");
    tempo(discard, 125);
    discard.request_close();
    discard.resolve(Decision::discard);
    check(discard.close_requested(), "discard/close failed");

    EditorDocument close_saved(original);
    tempo(close_saved, 115);
    close_saved.request_close();
    const auto save_as = directory / "new" / "saved.json";
    close_saved.resolve(Decision::save, save_as);
    check(close_saved.close_requested() && gtracker::load_pattern(save_as).bpm == 115,
          "save-as before close lost the edit");
}

void failures(const Pattern& original, const std::filesystem::path& directory) {
    EditorDocument document(original, directory / "safe.json");
    tempo(document, 100);
    document.request_close();
    const auto snapshot = gtracker::pattern_json(document.pattern());
    rejects([&] { document.resolve(Decision::save, directory); }, "save over directory succeeded");
    check(document.pending() == Pending::close && !document.close_requested() && document.dirty(),
          "failed save dismissed protection");
    check(gtracker::pattern_json(document.pattern()) == snapshot && document.can_undo(),
          "failed save changed document/history");
    document.resolve(Decision::cancel);
    document.request_load(directory / "missing.json");
    rejects([&] { document.resolve(Decision::discard); }, "missing file loaded");
    check(document.pending() == Pending::load && document.dirty() && document.can_undo(),
          "failed load lost current document/history");
    check(gtracker::pattern_json(document.pattern()) == snapshot, "failed load changed pattern");
    document.resolve(Decision::cancel);
    check(document.undo() && !document.dirty(), "failed operations damaged undo/savepoint");

    auto exhausted = original;
    exhausted.revision = 9007199254740991ULL;
    EditorDocument limit(exhausted);
    rejects([&] { tempo(limit, 100); }, "revision limit exceeded");
    check(!limit.can_undo() && !limit.dirty() && limit.generation() == 1, "revision failure mutated document");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected source root");
        const auto original = gtracker::load_pattern(std::filesystem::path(argv[1]) / "examples/campfire.gtrk.json");
        const auto directory = std::filesystem::current_path() / "editor-document-test-output";
        std::filesystem::create_directories(directory);
        history(original, directory);
        leave_guards(original, directory);
        failures(original, directory);
        std::filesystem::remove_all(directory);
        std::cout << "editor document: grouped history, savepoints and guarded load/close passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "editor document: " << error.what() << '\n';
        return 1;
    }
}
