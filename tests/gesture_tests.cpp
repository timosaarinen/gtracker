#include "gtracker/core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Synth = std::unique_ptr<gt_synth, decltype(&gt_destroy)>;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Synth create(gt_config config, gt_model model = GT_MODEL_CONTACT_0_2) {
    gt_synth* synth = nullptr;
    check(gt_create_model(&config, model, &synth) == GT_OK && synth, "create explicit model");
    return Synth(synth, gt_destroy);
}
std::vector<float> render(gt_synth* synth, std::size_t count) {
    std::vector<float> result(count);
    check(gt_render(synth, result.data(), count) == GT_OK, "render contact performance");
    for (float value : result) check(std::isfinite(value) && std::abs(value) <= 1, "non-finite/unbounded contact output");
    return result;
}
bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}
double pitch(const std::vector<float>& audio, std::size_t start, std::size_t count, double expected) {
    double best = -1;
    std::size_t selected = 0;
    for (std::size_t lag = static_cast<std::size_t>(48000 / (expected * 1.1)); lag <= static_cast<std::size_t>(48000 / (expected * 0.9)); ++lag) {
        double xy = 0, xx = 0, yy = 0;
        for (std::size_t n = start; n < start + count; ++n) {
            const double x = audio[n], y = audio[n + lag];
            xy += x * y; xx += x * x; yy += y * y;
        }
        const double score = xx && yy ? xy / std::sqrt(xx * yy) : 0;
        if (score > best) { best = score; selected = lag; }
    }
    check(best > 0.8 && selected, "pitched ringing missing after slide");
    return 48000.0 / selected;
}
void retained_vibration_and_transport() {
    auto config = gt_default_config();
    config.body_mix = 0; config.decay_seconds = 10; config.brightness = 0.8f;
    auto synth = create(config);
    const gt_event pluck{0, GT_EVENT_PLUCK, 5, 0, 0.8f};
    gt_fret_point points[]{{0, 0}, {17001, 3.25f}, {40003, 12}};
    gt_slide slide{9601, 5, points, 3};
    check(gt_set_performance(synth.get(), &pluck, 1, &slide, 1) == GT_OK, "load continuous slide");
    const auto expected = render(synth.get(), 100000);
    check(std::abs(pitch(expected, 1200, 5000, config.tuning_hz[5]) / config.tuning_hz[5] - 1) < 0.015, "initial fret pitch");
    check(std::abs(pitch(expected, 70000, 20000, config.tuning_hz[5] * 2) / (config.tuning_hz[5] * 2) - 1) < 0.015, "final slide fret pitch");
    check(gt_reset(synth.get()) == GT_OK, "reset slide performance");
    std::vector<float> partitioned(expected.size());
    const std::size_t blocks[]{1, 17, 256, 4093, 11};
    for (std::size_t offset = 0, block = 0; offset < partitioned.size(); ++block) {
        const auto count = std::min(blocks[block % 5], partitioned.size() - offset);
        check(gt_render(synth.get(), partitioned.data() + offset, count) == GT_OK, "partition slide across knots");
        offset += count;
    }
    check(same(expected, partitioned), "slide depends on render block partition");
    check(gt_set_performance(synth.get(), &pluck, 1, &slide, 1) == GT_OK, "copy performance");
    points[1].fret = 36; slide.frame = 1;
    check(same(expected, render(synth.get(), expected.size())), "caller point storage was retained");

    auto baseline = create(config, GT_MODEL_BASELINE_0_1);
    check(gt_set_events(baseline.get(), &pluck, 1) == GT_OK, "baseline reference");
    const auto original = render(baseline.get(), expected.size());
    check(!std::memcmp(expected.data(), original.data(), 9602 * sizeof(float)), "slide changed samples before motion began");
    check(!same(expected, original), "continuous control was flattened to a static pluck");
    check(gt_set_events(synth.get(), &pluck, 1) == GT_OK, "clear curves using legacy API");
    check(same(original, render(synth.get(), original.size())), "empty contact model changes the baseline");
}
void silence_and_damping() {
    auto config = gt_default_config(); config.body_mix = 0;
    auto synth = create(config);
    const gt_fret_point points[]{{0, 0}, {30000, 12}};
    const gt_slide slide{0, 0, points, 2};
    check(gt_set_performance(synth.get(), nullptr, 0, &slide, 1) == GT_OK, "contact without excitation");
    const auto silence = render(synth.get(), 40000);
    check(std::all_of(silence.begin(), silence.end(), [](float value) { return value == 0; }), "contact curve excited a silent string");
    const gt_event events[]{{0, GT_EVENT_PLUCK, 0, 0, 0.8f}, {10001, GT_EVENT_DAMP, 0, 0, 1}};
    check(gt_set_performance(synth.get(), events, 2, &slide, 1) == GT_OK, "damp during changing contact");
    const auto muted = render(synth.get(), 40000);
    check(std::any_of(muted.begin(), muted.begin() + 10001, [](float value) { return value != 0; }), "slide has no ringing");
    check(std::all_of(muted.begin() + 10001, muted.end(), [](float value) { return value == 0; }), "slide undid full damping");
}
void rejection_and_atomicity() {
    const auto config = gt_default_config();
    auto synth = create(config);
    auto reference = create(config);
    gt_event event{0, GT_EVENT_PLUCK, 0, 0, 0.8f};
    gt_fret_point points[]{{0, 0}, {1000, 4}};
    gt_slide slide{100, 0, points, 2};
    check(gt_set_performance(synth.get(), &event, 1, &slide, 1) == GT_OK, "valid transaction");
    check(gt_set_performance(reference.get(), &event, 1, &slide, 1) == GT_OK, "reference transaction");
    check(same(render(synth.get(), 137), render(reference.get(), 137)), "initial state mismatch");
    points[1].frame_offset = 0;
    check(gt_set_performance(synth.get(), &event, 1, &slide, 1) == GT_INVALID_ARGUMENT, "duplicate curve times accepted");
    check(same(render(synth.get(), 4000), render(reference.get(), 4000)), "rejection reset or partially changed running state");
    points[1].frame_offset = 1000;
    auto baseline = create(config, GT_MODEL_BASELINE_0_1);
    check(gt_set_performance(baseline.get(), &event, 1, &slide, 1) == GT_INVALID_ARGUMENT, "baseline silently accepted unsupported gestures");
    gt_slide overlapping[]{slide, slide}; overlapping[1].frame = 101;
    check(gt_set_performance(synth.get(), &event, 1, overlapping, 2) == GT_INVALID_ARGUMENT, "overlapping same-string slides accepted");
    event.frame = 200;
    check(gt_set_performance(synth.get(), &event, 1, &slide, 1) == GT_INVALID_ARGUMENT, "ambiguous excitation during a slide accepted");
    event.frame = 100; event.fret = 1;
    check(gt_set_performance(synth.get(), &event, 1, &slide, 1) == GT_INVALID_ARGUMENT, "conflicting initial fret accepted");
    event.frame = 1100;
    check(gt_set_performance(synth.get(), &event, 1, &slide, 1) == GT_OK, "pluck at slide end should take precedence");
    slide.frame = std::numeric_limits<std::uint64_t>::max() - 500;
    check(gt_set_performance(synth.get(), nullptr, 0, &slide, 1) == GT_INVALID_ARGUMENT, "slide frame overflow accepted");
    slide.frame = 0; points[1].fret = std::numeric_limits<float>::quiet_NaN();
    check(gt_set_performance(synth.get(), nullptr, 0, &slide, 1) == GT_INVALID_ARGUMENT, "nonfinite curve accepted");
    gt_synth* bad = reinterpret_cast<gt_synth*>(1);
    check(gt_create_model(&config, static_cast<gt_model>(99), &bad) == GT_INVALID_ARGUMENT && !bad, "unknown model was accepted");
    check(std::string(gt_model_name(static_cast<gt_model>(99))) == "unknown_model", "unknown model diagnostic");
}
}
int main() {
    try {
        retained_vibration_and_transport();
        silence_and_damping();
        rejection_and_atomicity();
        std::cout << "contact gestures: retained vibration/pitch, exact timing, partition/reset/copy, damping and atomic rejection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "contact gesture: " << error.what() << '\n'; return 1;
    }
}
