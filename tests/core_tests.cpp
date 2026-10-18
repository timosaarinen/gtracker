#include "gtracker/core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using Synth = std::unique_ptr<gt_synth, decltype(&gt_destroy)>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Synth create(const gt_config& config) {
    gt_synth* raw = nullptr;
    require(gt_create(&config, &raw) == GT_OK && raw != nullptr, "create synth");
    return Synth(raw, &gt_destroy);
}

std::vector<float> render(gt_synth* synth, std::size_t frames) {
    std::vector<float> output(frames);
    require(gt_render(synth, output.data(), output.size()) == GT_OK, "render samples");
    for (float value : output) {
        require(std::isfinite(value) && std::abs(value) <= 1.0f, "finite bounded output");
    }
    return output;
}

double energy(const std::vector<float>& signal, std::size_t begin, std::size_t end) {
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(signal[i]) * signal[i];
    }
    return sum / static_cast<double>(end - begin);
}

bool identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

void defaults_and_silence() {
    const auto config = gt_default_config();
    require(config.sample_rate == 48000 && config.string_count == 6, "default instrument");
    require(std::abs(config.tuning_hz[0] - 329.627557f) < 0.001f, "high E first");
    require(std::abs(config.tuning_hz[5] - 82.406889f) < 0.001f, "low E last");
    auto synth = create(config);
    const auto output = render(synth.get(), 1024);
    require(std::all_of(output.begin(), output.end(), [](float v) { return v == 0.0f; }),
            "new synth is silent");
    require(gt_render(synth.get(), nullptr, 0) == GT_OK, "empty render");
    require(gt_set_events(synth.get(), nullptr, 0) == GT_OK, "empty performance");
    gt_destroy(nullptr);
}

void deterministic_performance() {
    const auto config = gt_default_config();
    auto synth = create(config);
    const std::vector<gt_event> events{
        {0, GT_EVENT_PLUCK, 5, 0.0f, 0.8f},
        {791, GT_EVENT_PLUCK, 2, 3.5f, 0.6f},
        {1533, GT_EVENT_BODY_HIT, 0, 0.0f, 0.7f},
        {2501, GT_EVENT_DAMP, 5, 0.0f, 0.8f},
        {3901, GT_EVENT_PLUCK, 5, 7.0f, 0.5f}
    };
    require(gt_set_events(synth.get(), events.data(), events.size()) == GT_OK, "load performance");
    const auto reference = render(synth.get(), 16000);
    require(energy(reference, 0, 4000) > 0.00001, "performance makes meaningful sound");
    require(gt_reset(synth.get()) == GT_OK, "reset synth");
    require(identical(reference, render(synth.get(), reference.size())), "reset restores exact seed/state");

    require(gt_reset(synth.get()) == GT_OK, "reset before partitioned render");
    std::vector<float> partitioned(reference.size());
    std::size_t position = 0;
    constexpr std::size_t chunks[]{1, 17, 256, 793, 64, 7, 1024};
    std::size_t chunk = 0;
    while (position < partitioned.size()) {
        const auto count = std::min(chunks[chunk++ % 7], partitioned.size() - position);
        require(gt_render(synth.get(), partitioned.data() + position, count) == GT_OK, "partitioned render");
        position += count;
    }
    require(identical(reference, partitioned), "render partition must not affect output");

    auto other_config = config;
    other_config.seed = config.seed + 1;
    auto other = create(other_config);
    require(gt_set_events(other.get(), events.data(), events.size()) == GT_OK, "load other seed");
    require(!identical(reference, render(other.get(), reference.size())), "seed affects excitation");
    other_config.seed = 0;
    auto zero_seed = create(other_config);
    require(gt_set_events(zero_seed.get(), events.data(), events.size()) == GT_OK, "zero seed is valid");
    const auto zero_output = render(zero_seed.get(), 4096);
    require(energy(zero_output, 0, zero_output.size()) > 0.00001, "zero seed produces sound");
}

void timing_and_damping() {
    auto config = gt_default_config();
    config.body_mix = 0.0f; // A body resonance can correctly outlive a string mute.
    auto synth = create(config);
    const gt_event delayed{137, GT_EVENT_PLUCK, 0, 0.0f, 0.8f};
    require(gt_set_events(synth.get(), &delayed, 1) == GT_OK, "load delayed event");
    const auto output = render(synth.get(), 2000);
    require(std::all_of(output.begin(), output.begin() + 137, [](float v) { return v == 0.0f; }),
            "no sound before scheduled frame");
    require(energy(output, 137, 1000) > 0.00001, "sound after scheduled frame");

    const gt_event muted[]{
        {0, GT_EVENT_PLUCK, 0, 0.0f, 0.8f},
        {1500, GT_EVENT_DAMP, 0, 0.0f, 1.0f}
    };
    require(gt_set_events(synth.get(), muted, 2) == GT_OK, "load mute");
    const auto muted_output = render(synth.get(), 4000);
    require(energy(muted_output, 0, 1500) > 0.00001, "pluck before mute");
    require(energy(muted_output, 1500, 4000) == 0.0, "full damping silences at exact frame");

    gt_event partial[]{muted[0], muted[1]};
    partial[1].strength = 0.7f;
    require(gt_set_events(synth.get(), partial, 2) == GT_OK, "load partial damping");
    const auto partial_output = render(synth.get(), 4000);
    require(gt_set_events(synth.get(), muted, 1) == GT_OK, "load undamped reference");
    const auto undamped_output = render(synth.get(), 4000);
    require(energy(partial_output, 1600, 3500) < energy(undamped_output, 1600, 3500) * 0.05,
            "partial damping removes ringing energy");

    const gt_event same_frame[]{
        {0, GT_EVENT_PLUCK, 0, 0.0f, 0.8f},
        {0, GT_EVENT_DAMP, 0, 0.0f, 1.0f}
    };
    require(gt_set_events(synth.get(), same_frame, 2) == GT_OK, "same-frame events allowed");
    require(energy(render(synth.get(), 512), 0, 512) == 0.0, "same-frame supplied ordering");

    // Damping one string must not erase a different string's state.
    const gt_event independent[]{
        {0, GT_EVENT_PLUCK, 1, 0.0f, 0.8f},
        {100, GT_EVENT_DAMP, 0, 0.0f, 1.0f}
    };
    require(gt_set_events(synth.get(), independent, 2) == GT_OK, "independent string performance");
    require(energy(render(synth.get(), 2000), 200, 2000) > 0.00001, "other string keeps ringing");
}

void body_and_config_extremes() {
    auto config = gt_default_config();
    config.body_mix = 1.0f;
    auto synth = create(config);
    const gt_event hit{0, GT_EVENT_BODY_HIT, 0, 0.0f, 0.8f};
    require(gt_set_events(synth.get(), &hit, 1) == GT_OK, "load body tap");
    const auto body = render(synth.get(), 48000);
    require(energy(body, 0, 2000) > 0.00001, "body tap is audible");
    require(energy(body, 40000, 48000) < energy(body, 0, 2000) * 0.0001, "body tap decays");

    for (std::uint32_t rate : {8000u, 192000u}) {
        config.sample_rate = rate;
        config.string_count = 12;
        config.brightness = rate == 8000 ? 0.0f : 1.0f;
        config.decay_seconds = rate == 8000 ? 0.02f : 60.0f;
        for (std::uint32_t i = 0; i < config.string_count; ++i) {
            config.tuning_hz[i] = 20.0f + static_cast<float>(i) * 10.0f;
        }
        auto extreme = create(config);
        std::vector<gt_event> chord;
        for (std::uint32_t i = 0; i < config.string_count; ++i) {
            chord.push_back({0, GT_EVENT_PLUCK, i, 36.0f, 1.0f});
        }
        require(gt_set_events(extreme.get(), chord.data(), chord.size()) == GT_OK, "twelve-string chord");
        const auto samples = render(extreme.get(), 12000);
        require(energy(samples, 0, 2000) > 0.00001, "boundary configuration renders");
    }
}

double estimate_frequency(const std::vector<float>& signal, double sample_rate, double expected) {
    const auto minimum = static_cast<std::size_t>(sample_rate / (expected * 1.10));
    const auto maximum = static_cast<std::size_t>(sample_rate / (expected * 0.90));
    double best = -1.0;
    std::size_t best_lag = minimum;
    for (std::size_t lag = minimum; lag <= maximum; ++lag) {
        double correlation = 0.0;
        double first_energy = 0.0;
        double second_energy = 0.0;
        for (std::size_t i = 4000; i < 20000; ++i) {
            correlation += static_cast<double>(signal[i]) * signal[i + lag];
            first_energy += static_cast<double>(signal[i]) * signal[i];
            second_energy += static_cast<double>(signal[i + lag]) * signal[i + lag];
        }
        correlation /= std::sqrt(first_energy * second_energy);
        if (correlation > best) {
            best = correlation;
            best_lag = lag;
        }
    }
    require(best > 0.80, "pluck has a periodic pitched tail");
    return sample_rate / static_cast<double>(best_lag);
}

void approximate_pitch() {
    auto config = gt_default_config();
    config.body_mix = 0.0f;
    config.brightness = 0.7f;
    auto synth = create(config);
    for (const gt_event event : {gt_event{0, GT_EVENT_PLUCK, 4, 0.0f, 0.8f},
                                 gt_event{0, GT_EVENT_PLUCK, 4, 12.0f, 0.8f},
                                 gt_event{0, GT_EVENT_PLUCK, 0, 3.5f, 0.8f}}) {
        require(gt_set_events(synth.get(), &event, 1) == GT_OK, "load pitch test");
        const auto signal = render(synth.get(), 24000);
        const double expected = config.tuning_hz[event.string_index] * std::exp2(event.fret / 12.0);
        const double estimated = estimate_frequency(signal, config.sample_rate, expected);
        require(std::abs(estimated / expected - 1.0) < 0.015, "pitch within 1.5 percent of target");
    }
}

void invalid_arguments() {
    const auto config = gt_default_config();
    gt_synth* raw = reinterpret_cast<gt_synth*>(static_cast<std::uintptr_t>(1));
    require(gt_create(nullptr, &raw) == GT_INVALID_ARGUMENT && raw == nullptr, "invalid create clears result");
    require(gt_create(&config, nullptr) == GT_INVALID_ARGUMENT, "null create destination");
    require(gt_reset(nullptr) == GT_INVALID_ARGUMENT, "null reset");
    require(gt_render(nullptr, nullptr, 0) == GT_INVALID_ARGUMENT, "null renderer");
    require(gt_set_events(nullptr, nullptr, 0) == GT_INVALID_ARGUMENT, "null event destination");
    require(std::strcmp(gt_result_string(GT_OK), "ok") == 0, "result string");

    for (int variant = 0; variant < 11; ++variant) {
        auto bad = config;
        switch (variant) {
        case 0: bad.sample_rate = 7999; break;
        case 1: bad.sample_rate = 192001; break;
        case 2: bad.string_count = 0; break;
        case 3: bad.string_count = 13; break;
        case 4: bad.tuning_hz[0] = 0.0f; break;
        case 5: bad.tuning_hz[0] = std::numeric_limits<float>::infinity(); break;
        case 6: bad.decay_seconds = -1.0f; break;
        case 7: bad.brightness = std::numeric_limits<float>::quiet_NaN(); break;
        case 8: bad.body_mix = 1.01f; break;
        case 9: bad.decay_seconds = 61.0f; break;
        case 10: bad.tuning_hz[0] = 2001.0f; break;
        }
        require(gt_create(&bad, &raw) == GT_INVALID_ARGUMENT && raw == nullptr, "invalid configuration");
    }

    auto synth = create(config);
    const gt_event valid{0, GT_EVENT_PLUCK, 0, 0.0f, 0.8f};
    require(gt_set_events(synth.get(), &valid, 1) == GT_OK, "load valid event before failure");
    require(gt_render(synth.get(), nullptr, 1) == GT_INVALID_ARGUMENT, "null audio destination");
    require(gt_set_events(synth.get(), nullptr, 1) == GT_INVALID_ARGUMENT, "null events");
    require(gt_set_events(synth.get(), &valid, 1000001) == GT_INVALID_ARGUMENT, "bounded event allocation");
    for (int variant = 0; variant < 8; ++variant) {
        auto bad = valid;
        switch (variant) {
        case 0: bad.type = static_cast<gt_event_type>(0); break;
        case 1: bad.string_index = config.string_count; break;
        case 2: bad.fret = -1.0f; break;
        case 3: bad.fret = 36.01f; break;
        case 4: bad.strength = std::numeric_limits<float>::quiet_NaN(); break;
        case 5: bad.strength = 1.1f; break;
        case 6: bad.fret = std::numeric_limits<float>::infinity(); break;
        case 7: bad.frame = std::numeric_limits<std::uint64_t>::max(); break;
        }
        require(gt_set_events(synth.get(), &bad, 1) == GT_INVALID_ARGUMENT, "invalid event");
    }
    const gt_event unsorted[]{
        {10, GT_EVENT_PLUCK, 0, 0.0f, 0.5f},
        {9, GT_EVENT_PLUCK, 1, 0.0f, 0.5f}
    };
    require(gt_set_events(synth.get(), unsorted, 2) == GT_INVALID_ARGUMENT, "reject unsorted events");
    require(energy(render(synth.get(), 2048), 0, 2048) > 0.00001, "failed load preserves prior performance");

    auto low_rate = config;
    low_rate.sample_rate = 8000;
    auto low_synth = create(low_rate);
    auto too_high = valid;
    too_high.fret = 36.0f;
    require(gt_set_events(low_synth.get(), &too_high, 1) == GT_INVALID_ARGUMENT,
            "reject target pitch above rate/4");
}

} // namespace

int main() {
    try {
        defaults_and_silence();
        deterministic_performance();
        timing_and_damping();
        body_and_config_extremes();
        approximate_pitch();
        invalid_arguments();
        std::cout << "core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
