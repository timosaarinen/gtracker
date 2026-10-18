#include "gtracker/proposals.hpp"
#include "gtracker/editor_document.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid/stale/ambiguous proposal acceptance succeeded");
}
}
int main() {
    try {
        gtracker::Take take;
        take.id = "original-source"; take.audio.sample_rate = 48000; take.audio.channels = 2;
        take.audio.frames = 240000; take.audio.sha256 = std::string(64, 'a');
        take.alignment = {24000, 17, "manual", 120, 8, 0, 240000};
        take.pattern.bpm = 120;
        gtracker::NoteProposals p;
        p.job_id = "explicit-reviewed-job"; p.take_id = take.id; p.source_sha256 = take.audio.sha256;
        p.source_frames = take.audio.frames; p.sample_rate = 48000; p.channels = 2; p.channel = 1;
        p.source_pattern = take.pattern; p.alignment = take.alignment; p.status = "proposals";
        p.engine = "gtracker.monophonic-yin"; p.engine_version = "0.1.0";
        p.notes = {{"n1", 48000, 54000, 2400, 64, .98, 1, 2, {{0,0}, {1,5}}},
                   {"n2", 72123, 78000, 2400, 59, .95, .9, 4, {{1,0}, {2,4}}}};
        const auto original = gtracker::pattern_json(take.pattern);
        const auto accepted = gtracker::accept_note_proposals(take.pattern, take, p, {{0,1}, {1,0}}, .55f);
        check(accepted.events.size() == 2 && accepted.events[0].string_index == 1 && accepted.events[0].fret == 5,
              "Explicit fingering choice was replaced/ignored");
        check(accepted.events[1].beat == take.source_frame_to_beat(72123) && accepted.events[1].strength == .55f,
              "Source-sample timing/manual offset or explicit strength was quantized");
        check(gtracker::pattern_json(take.pattern) == original && accepted.revision == take.pattern.revision,
              "Proposal helper mutated the source/revision");
        check(gtracker::render_pattern(accepted).peak > 0, "Accepted native plucks did not render");
        gtracker::EditorDocument document(take.pattern);
        document.commit(accepted);
        const auto first_id = document.pattern().events.front().id;
        check(document.undo() && document.pattern().events.empty(), "Accepted batch did not undo in one step");
        check(document.undo(true) && document.pattern().events.front().id == first_id, "Proposal redo lost note IDs");
        rejects([&] { gtracker::accept_note_proposals(document.pattern(), take, p, {{0,0}}); });
        rejects([&] { gtracker::accept_note_proposals(take.pattern, take, p, {}); });
        rejects([&] { gtracker::accept_note_proposals(take.pattern, take, p, {{0,0},{0,1}}); });
        rejects([&] { gtracker::accept_note_proposals(take.pattern, take, p, {{0,99}}); });
        auto changed = take.pattern; changed.brightness = .9f;
        rejects([&] { gtracker::accept_note_proposals(changed, take, p, {{0,0}}); });
        for (int variant = 0; variant < 8; ++variant) {
            auto invalid = p;
            switch (variant) {
            case 0: invalid.source_sha256[0] = 'b'; break;
            case 1: invalid.alignment.input_offset_frames = 18; break;
            case 2: invalid.notes[0].onset_frame = 0; break;
            case 3: invalid.notes[0].periodicity = 2; break;
            case 4: invalid.notes[0].alternatives[0].fret = 9; break;
            case 5: invalid.notes[1].id = invalid.notes[0].id; break;
            case 6: invalid.channel = 2; break;
            case 7: invalid.engine_version = "future"; break;
            }
            rejects([&] { gtracker::accept_note_proposals(take.pattern, take, invalid, {{0,0}}); });
        }
        auto curved = take.pattern; curved.schema_version = 2; curved.model = "contact_0_2";
        curved.gestures = {{"existing", "slide", .75, 0, {{0,0},{.75,2}}}};
        auto conflicted = p; conflicted.source_pattern = curved;
        rejects([&] { gtracker::accept_note_proposals(curved, take, conflicted, {{0,0}}); });
        std::cout << "proposals: explicit choices, source timing, atomic copy, one-step undo, stale/identity/conflict rejection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
