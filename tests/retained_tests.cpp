#include "gtracker/core.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Synth = std::unique_ptr<gt_synth, decltype(&gt_destroy)>;
Synth create(gt_config config, gt_model model) {
    gt_synth* raw = nullptr; check(gt_create_model(&config, model, &raw) == GT_OK, "create model"); return Synth(raw, gt_destroy);
}
std::vector<float> render(gt_synth* synth, std::size_t frames, std::size_t block = 997) {
    std::vector<float> result(frames);
    for (std::size_t offset = 0; offset < frames; offset += block)
        check(gt_render(synth, result.data() + offset, std::min(block, frames - offset)) == GT_OK, "render");
    return result;
}
std::vector<float> performance(const gt_config& config, gt_model model, const std::vector<gt_event>& events, std::size_t frames) {
    auto synth = create(config, model);
    check(gt_set_events(synth.get(), events.data(), events.size()) == GT_OK, "set events");
    return render(synth.get(), frames);
}
}
int main() {
    try {
        auto config = gt_default_config(); config.sample_rate = 24000; config.string_count = 1;
        config.tuning_hz[0] = 110; config.body_mix = 0; config.decay_seconds = 4;
        const gt_event pluck{0, GT_EVENT_PLUCK, 0, 0, 0.7f};
        const auto initial = performance(config, GT_MODEL_BASELINE_0_1, {pluck}, 48000);
        check(performance(config, GT_MODEL_RETAINED_0_5, {pluck}, 48000) == initial, "first retained pluck changed the preserved initial excitation");
        auto zero = pluck; zero.frame = 12000; zero.strength = 0;
        check(performance(config, GT_MODEL_RETAINED_0_5, {pluck, zero}, 48000) == initial,
              "zero-strength retained pluck erased or excited an already ringing string");
        check(performance(config, GT_MODEL_BASELINE_0_1, {pluck, zero}, 48000) != initial,
              "legacy zero-strength pluck semantics unexpectedly changed");
        auto consume_rng = pluck; consume_rng.strength = 0;
        const auto second_noise = performance(config, GT_MODEL_BASELINE_0_1, {consume_rng, pluck}, 48000);
        const auto retained = performance(config, GT_MODEL_RETAINED_0_5, {pluck, pluck}, 48000);
        check(retained != second_noise, "retained repluck still replaced the old vibration");
        for (std::size_t i = 0; i < retained.size(); ++i) {
            const double expected = std::tanh((1 - 0.5 * pluck.strength) * std::atanh(initial[i]) + std::atanh(second_noise[i]));
            check(std::abs(retained[i] - expected) < 2e-7, "retained excitation does not preserve the specified weighted old state");
        }
        std::vector<gt_event> events{pluck, {6000, GT_EVENT_PLUCK, 0, 0, 0.5f}, {36000, GT_EVENT_DAMP, 0, 0, 1}};
        const gt_fret_point points[] = {{0, 0}, {6000, 2.5f}, {12000, 7}};
        const gt_slide slide{12000, 0, points, 3};
        auto synth = create(config, GT_MODEL_RETAINED_0_5);
        check(gt_set_performance(synth.get(), events.data(), events.size(), &slide, 1) == GT_OK, "retained model refused the contact API");
        const auto whole = render(synth.get(), 48000, 48000);
        check(gt_reset(synth.get()) == GT_OK && render(synth.get(), 48000, 127) == whole, "retained state/reset changed across block boundaries");
        for (std::size_t i = 36000; i < whole.size(); ++i) check(whole[i] == 0, "full damping did not silence retained excitation");
        events.assign(2048, pluck);
        for (auto& event : events) event.strength = 1;
        const auto dense = performance(config, GT_MODEL_RETAINED_0_5, events, 24000);
        for (float value : dense) check(std::isfinite(value) && std::abs(value) <= 1, "dense retained excitation became unbounded/nonfinite");
        config.body_mix = 0.2f;
        const gt_event tap{0, GT_EVENT_BODY_HIT, 0, 0, 0.7f};
        const auto body = performance(config, GT_MODEL_RETAINED_0_5, {tap}, 24000 * 40);
        check(std::all_of(body.end() - 24000, body.end(), [](float sample) { return sample == 0; }), "retained numeric tail did not become silent");
        check(performance(config, GT_MODEL_RETAINED_0_5, {}, 1000) == std::vector<float>(1000), "retained model excited itself without events");
        std::cout << "retained: explicit excitation memory, zero-strength contact, old semantics, slides, reset, damping and stability passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
