#include "gtracker/project.hpp"
#include "gtracker/editor_document.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "source directory required");
        const auto original = gtracker::load_pattern(std::filesystem::path(argv[1]) / "examples/campfire.gtrk.json");
        const auto dry = gtracker::render_pattern(original);
        auto pattern = original;
        gtracker::ProductionSettings settings; settings.enabled = true;
        pattern.production = settings;
        rejects([&] { gtracker::validate_pattern(pattern); }, "production silently migrated a v1 document");
        pattern.schema_version = 2;
        const auto wet = gtracker::render_pattern(pattern);
        check(wet.channels == 2 && wet.frames() == dry.frames() + static_cast<std::size_t>(std::ceil(wet.production_tail_seconds * wet.sample_rate)), "stereo/tail frame count is wrong");
        check(wet.peak > 0 && wet.clipped_samples == 0 && wet.end_peak < 1e-5f, "fixture output clipped, silent or tail truncated");
        const auto reopened = gtracker::parse_pattern_json(gtracker::pattern_json(pattern));
        check(gtracker::render_pattern(reopened).samples == wet.samples, "production save/reopen is not reproducible");
        check(gtracker::production_json(gtracker::parse_production_json(gtracker::production_json(settings))) == gtracker::production_json(settings), "standalone settings failed to round-trip");
        gtracker::RenderOptions bypass; bypass.bypass_production = true;
        const auto bypassed = gtracker::render_pattern(pattern, bypass);
        check(bypassed.channels == 1 && bypassed.samples == dry.samples && bypassed.production_tail_seconds == 0, "dry bypass changed the baseline");
        gtracker::RenderOptions overridden; overridden.production_override = settings;
        check(gtracker::render_pattern(original, overridden).samples == wet.samples, "CLI override differs from saved settings");
        const auto identity = wet.identity;
        check(identity.size() == 64 && gtracker::render_identity(reopened) == identity, "identity not stable after reopen");
        for (int field = 0; field < 8; ++field) {
            auto changed = pattern;
            auto& s = *changed.production;
            switch (field) {
                case 0: s.enabled = false; break;
                case 1: s.wet += 0.01f; break;
                case 2: s.decay_seconds += 0.1f; break;
                case 3: s.damping += 0.01f; break;
                case 4: s.predelay_ms += 1; break;
                case 5: s.width -= 0.1f; break;
                case 6: s.output_gain_db -= 1; break;
                case 7: changed.seed += 1; break;
            }
            check(gtracker::render_identity(changed) != identity, "audible production/seed change missing from identity");
        }
        check(gtracker::render_identity(pattern, bypass) != identity, "bypass missing from identity");
        auto options = gtracker::RenderOptions{}; options.tail_seconds = 1;
        check(gtracker::render_identity(pattern, options) != identity, "tail missing from identity");
        options.sample_rate = 44100;
        check(gtracker::render_identity(pattern, options) != gtracker::render_identity(pattern, {48000, 1}), "rate missing from identity");
        gtracker::EditorDocument document(original);
        check(document.commit(pattern), "production edit not recorded");
        check(document.undo() && !document.pattern().production && document.pattern().schema_version == 1, "production undo lost v1");
        check(document.undo(true) && document.pattern().production->enabled, "production redo lost settings");
        auto bad = settings; bad.version = 2;
        rejects([&] { gtracker::production_json(bad); }, "future version accepted");
        auto text = gtracker::production_json(settings); text.insert(text.find('{') + 1, "\"unknown\": 1,");
        rejects([&] { gtracker::parse_production_json(text); }, "unknown settings silently dropped");
        std::cout << "production project: settings, save/reopen, bypass, stereo tail, identity and undo passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
