#include "gtracker/core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kLogSilence = -6.9077552789821370521; // log(0.001), -60 dB.
constexpr std::size_t kMaxEvents = 1000000;
constexpr std::size_t kMaxSlides = 16384;
constexpr std::size_t kMaxFretPoints = 65536;
constexpr std::uint32_t kZeroSeedReplacement = 0x6d2b79f5u;

template <typename Enum>
typename std::underlying_type<Enum>::type enum_value(const Enum& value) noexcept {
    // A C caller can supply an enum value outside C++'s permitted enum range.
    // Inspect its representation before interpreting it as a validated enum.
    typename std::underlying_type<Enum>::type raw{};
    std::memcpy(&raw, &value, sizeof(raw));
    return raw;
}

bool in_range(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_config(const gt_config& config) noexcept {
    if (config.sample_rate < 8000 || config.sample_rate > 192000 ||
        config.string_count < 1 || config.string_count > GT_MAX_STRINGS ||
        !in_range(config.decay_seconds, 0.02f, 60.0f) ||
        !in_range(config.brightness, 0.0f, 1.0f) ||
        !in_range(config.body_mix, 0.0f, 1.0f)) {
        return false;
    }
    for (std::uint32_t i = 0; i < config.string_count; ++i) {
        if (!in_range(config.tuning_hz[i], 20.0f, 2000.0f) ||
            config.tuning_hz[i] > static_cast<float>(config.sample_rate) * 0.25f) {
            return false;
        }
    }
    return true;
}

double event_frequency(const gt_config& config, const gt_event& event) noexcept {
    return static_cast<double>(config.tuning_hz[event.string_index]) *
           std::exp2(static_cast<double>(event.fret) / 12.0);
}

bool valid_event(const gt_config& config, const gt_event& event) noexcept {
    if (event.frame == std::numeric_limits<std::uint64_t>::max() ||
        !in_range(event.fret, 0.0f, 36.0f) ||
        !in_range(event.strength, 0.0f, 1.0f)) {
        return false;
    }
    switch (enum_value(event.type)) {
    case GT_EVENT_PLUCK:
        return event.string_index < config.string_count &&
               event_frequency(config, event) <= static_cast<double>(config.sample_rate) * 0.25;
    case GT_EVENT_DAMP:
        return event.string_index < config.string_count;
    case GT_EVENT_BODY_HIT:
        return true;
    default:
        return false;
    }
}

struct StringState {
    std::vector<double> delay_line;
    std::size_t write_index = 0;
    double delay_samples = 4.0;
    double previous_delayed = 0.0;
    double feedback = 0.0;
    bool active = false;
    double contact_fret = 0.0;
    double damping_factor = 1.0;
    std::size_t slide_index = kMaxSlides;
    std::size_t point_index = 0;
};

struct Slide {
    std::uint64_t frame = 0;
    std::uint32_t string_index = 0;
    std::vector<gt_fret_point> points;
};

struct BodyMode {
    double x = 0.0;
    double y = 0.0;
    double rotation_x = 0.0;
    double rotation_y = 0.0;
    double input_gain = 0.0;
    double weight = 0.0;
};

} // namespace

struct gt_synth {
    gt_config config{};
    gt_model model = GT_MODEL_BASELINE_0_1;
    std::array<StringState, GT_MAX_STRINGS> strings;
    std::array<BodyMode, 6> body;
    std::vector<gt_event> events;
    std::vector<Slide> slides;
    std::size_t slide_cursor = 0;
    std::size_t event_cursor = 0;
    std::uint64_t frame = 0;
    std::uint32_t rng_state = kZeroSeedReplacement;
};

namespace {

void reset_state(gt_synth& synth) noexcept {
    synth.event_cursor = 0;
    synth.slide_cursor = 0;
    synth.frame = 0;
    synth.rng_state = synth.config.seed == 0 ? kZeroSeedReplacement : synth.config.seed;
    for (auto& string : synth.strings) {
        std::fill(string.delay_line.begin(), string.delay_line.end(), 0.0);
        string.write_index = 0;
        string.previous_delayed = 0.0;
        string.feedback = 0.0;
        string.active = false;
        string.contact_fret = 0.0;
        string.damping_factor = 1.0;
        string.slide_index = kMaxSlides;
        string.point_index = 0;
    }
    for (auto& mode : synth.body) {
        mode.x = 0.0;
        mode.y = 0.0;
    }
}

double noise(gt_synth& synth) noexcept {
    auto value = synth.rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    synth.rng_state = value;
    return static_cast<double>(value >> 8) * (1.0 / 8388608.0) - 1.0;
}

void pluck(gt_synth& synth, const gt_event& event) noexcept {
    auto& string = synth.strings[event.string_index];
    const double frequency = event_frequency(synth.config, event);
    const double lowpass_delay = (1.0 - static_cast<double>(synth.config.brightness)) * 0.5;
    string.delay_samples = static_cast<double>(synth.config.sample_rate) / frequency - lowpass_delay;
    string.feedback = std::exp(kLogSilence / (frequency * synth.config.decay_seconds));
    string.write_index = 0;
    string.previous_delayed = 0.0;
    string.active = event.strength > 0.0f;
    string.contact_fret = event.fret;
    string.damping_factor = 1.0;

    // A seeded, zero-mean initial displacement. This baseline reinitializes the
    // addressed string on a pluck; independent ringing strings are untouched.
    double mean = 0.0;
    for (double& sample : string.delay_line) {
        sample = noise(synth);
        mean += sample;
    }
    mean /= static_cast<double>(string.delay_line.size());
    const double amplitude = static_cast<double>(event.strength) * 0.45;
    for (double& sample : string.delay_line) {
        sample = (sample - mean) * amplitude;
    }
}

void damp(gt_synth& synth, const gt_event& event) noexcept {
    auto& string = synth.strings[event.string_index];
    const double remaining = 1.0 - static_cast<double>(event.strength);
    for (double& sample : string.delay_line) {
        sample *= remaining;
    }
    string.previous_delayed *= remaining;
    string.feedback *= remaining;
    string.damping_factor *= remaining;
    if (event.strength == 1.0f) {
        string.active = false;
    }
}

void apply_event(gt_synth& synth, const gt_event& event) noexcept {
    switch (event.type) {
    case GT_EVENT_PLUCK:
        pluck(synth, event);
        break;
    case GT_EVENT_DAMP:
        damp(synth, event);
        break;
    case GT_EVENT_BODY_HIT:
        for (auto& mode : synth.body) {
            mode.x += static_cast<double>(event.strength) * mode.weight;
        }
        break;
    default:
        break; // Events are validated before entering the render queue.
    }
}

void update_contacts(gt_synth& synth) noexcept {
    while (synth.slide_cursor < synth.slides.size() && synth.slides[synth.slide_cursor].frame == synth.frame) {
        const auto index = synth.slide_cursor++;
        auto& string = synth.strings[synth.slides[index].string_index];
        string.slide_index = index;
        string.point_index = 0;
    }
    for (std::uint32_t index = 0; index < synth.config.string_count; ++index) {
        auto& string = synth.strings[index];
        if (string.slide_index == kMaxSlides) continue;
        const auto& slide = synth.slides[string.slide_index];
        const auto relative = synth.frame - slide.frame;
        while (string.point_index + 1 < slide.points.size() && slide.points[string.point_index + 1].frame_offset <= relative)
            ++string.point_index;
        const auto& point = slide.points[string.point_index];
        double fret = point.fret;
        if (string.point_index + 1 < slide.points.size()) {
            const auto& next = slide.points[string.point_index + 1];
            const double t = static_cast<double>(relative - point.frame_offset) / static_cast<double>(next.frame_offset - point.frame_offset);
            fret += (static_cast<double>(next.fret) - point.fret) * t;
        } else string.slide_index = kMaxSlides;
        if (fret != string.contact_fret) {
            string.contact_fret = fret;
            const double frequency = synth.config.tuning_hz[index] * std::exp2(fret / 12.0);
            const double filter_delay = (1.0 - static_cast<double>(synth.config.brightness)) * 0.5;
            string.delay_samples = synth.config.sample_rate / frequency - filter_delay;
            string.feedback = std::exp(kLogSilence / (frequency * synth.config.decay_seconds)) * string.damping_factor;
        }
    }
}

double render_string(StringState& string, double brightness) noexcept {
    if (!string.active) {
        return 0.0;
    }
    const std::size_t capacity = string.delay_line.size();
    double read_position = static_cast<double>(string.write_index) - string.delay_samples;
    if (read_position < 0.0) {
        read_position += static_cast<double>(capacity);
    }
    const auto first = static_cast<std::size_t>(read_position);
    const auto second = first + 1 == capacity ? 0 : first + 1;
    const double fraction = read_position - static_cast<double>(first);
    const double delayed = string.delay_line[first] * (1.0 - fraction) +
                           string.delay_line[second] * fraction;
    const double previous_weight = (1.0 - brightness) * 0.5;
    const double filtered = delayed * (1.0 - previous_weight) +
                            string.previous_delayed * previous_weight;
    string.previous_delayed = delayed;
    string.delay_line[string.write_index] = filtered * string.feedback;
    if (++string.write_index == capacity) {
        string.write_index = 0;
    }
    return delayed;
}

double render_body(gt_synth& synth, double strings) noexcept {
    double sum = 0.0;
    for (auto& mode : synth.body) {
        const double x = mode.rotation_x * mode.x - mode.rotation_y * mode.y +
                         strings * mode.input_gain;
        const double y = mode.rotation_y * mode.x + mode.rotation_x * mode.y;
        mode.x = x;
        mode.y = y;
        sum += x;
    }
    return sum;
}

} // namespace

extern "C" {

const char* gt_result_string(gt_result result) {
    switch (enum_value(result)) {
    case GT_OK: return "ok";
    case GT_INVALID_ARGUMENT: return "invalid argument";
    case GT_OUT_OF_MEMORY: return "out of memory";
    case GT_INTERNAL_ERROR: return "internal error";
    default: return "unknown result";
    }
}

gt_config gt_default_config(void) {
    gt_config config{};
    config.sample_rate = 48000;
    config.string_count = 6;
    config.seed = 42;
    config.tuning_hz[0] = 329.627557f;
    config.tuning_hz[1] = 246.941651f;
    config.tuning_hz[2] = 195.997718f;
    config.tuning_hz[3] = 146.832384f;
    config.tuning_hz[4] = 110.0f;
    config.tuning_hz[5] = 82.406889f;
    config.decay_seconds = 2.5f;
    config.brightness = 0.6f;
    config.body_mix = 0.15f;
    return config;
}

gt_result gt_create(const gt_config* config, gt_synth** out_synth) {
    return gt_create_model(config, GT_MODEL_BASELINE_0_1, out_synth);
}

const char* gt_model_name(gt_model model) {
    switch (enum_value(model)) {
    case GT_MODEL_BASELINE_0_1: return "baseline_0_1";
    case GT_MODEL_CONTACT_0_2: return "contact_0_2";
    default: return "unknown_model";
    }
}

gt_result gt_create_model(const gt_config* config, gt_model model, gt_synth** out_synth) {
    if (out_synth == nullptr) {
        return GT_INVALID_ARGUMENT;
    }
    *out_synth = nullptr;
    const auto model_value = enum_value(model);
    if (config == nullptr || !valid_config(*config) || (model_value != GT_MODEL_BASELINE_0_1 && model_value != GT_MODEL_CONTACT_0_2)) {
        return GT_INVALID_ARGUMENT;
    }
    try {
        auto synth = std::make_unique<gt_synth>();
        synth->config = *config;
        synth->model = model;
        for (std::uint32_t i = 0; i < config->string_count; ++i) {
            const auto capacity = static_cast<std::size_t>(
                std::ceil(static_cast<double>(config->sample_rate) / config->tuning_hz[i])) + 4;
            synth->strings[i].delay_line.resize(capacity, 0.0);
        }
        // Illustrative synthetic modes, not measurements of a particular guitar.
        constexpr std::array<double, 6> frequencies{97.0, 191.0, 287.0, 433.0, 683.0, 1049.0};
        constexpr std::array<double, 6> decay{0.35, 0.28, 0.24, 0.18, 0.14, 0.10};
        constexpr std::array<double, 6> weights{0.32, 0.24, 0.17, 0.12, 0.09, 0.06};
        for (std::size_t i = 0; i < synth->body.size(); ++i) {
            auto& mode = synth->body[i];
            const double radius = std::exp(kLogSilence / (config->sample_rate * decay[i]));
            const double angle = 2.0 * kPi * frequencies[i] / config->sample_rate;
            mode.rotation_x = radius * std::cos(angle);
            mode.rotation_y = radius * std::sin(angle);
            mode.input_gain = (1.0 - radius) * weights[i] * 0.7;
            mode.weight = weights[i];
        }
        reset_state(*synth);
        *out_synth = synth.release();
        return GT_OK;
    } catch (const std::bad_alloc&) {
        return GT_OUT_OF_MEMORY;
    } catch (...) {
        return GT_INTERNAL_ERROR;
    }
}

void gt_destroy(gt_synth* synth) {
    delete synth;
}

gt_result gt_set_events(gt_synth* synth, const gt_event* events, size_t count) {
    return gt_set_performance(synth, events, count, nullptr, 0);
}

gt_result gt_set_performance(gt_synth* synth, const gt_event* events, size_t count,
                             const gt_slide* slides, size_t slide_count) {
    if (synth == nullptr || (count != 0 && events == nullptr) || count > kMaxEvents) {
        return GT_INVALID_ARGUMENT;
    }
    if ((slide_count && (!slides || synth->model != GT_MODEL_CONTACT_0_2)) || slide_count > kMaxSlides) return GT_INVALID_ARGUMENT;
    for (std::size_t i = 0; i < count; ++i) {
        if (!valid_event(synth->config, events[i]) ||
            (i != 0 && events[i].frame < events[i - 1].frame)) {
            return GT_INVALID_ARGUMENT;
        }
    }
    std::array<std::uint64_t, GT_MAX_STRINGS> ends{};
    std::array<bool, GT_MAX_STRINGS> used{};
    std::size_t total_points = 0;
    for (std::size_t i = 0; i < slide_count; ++i) {
        const auto& slide = slides[i];
        if (!slide.points || slide.point_count < 2 || slide.point_count > 256 || slide.string_index >= synth->config.string_count ||
            (i && slide.frame < slides[i - 1].frame) || slide.points[0].frame_offset != 0 ||
            (used[slide.string_index] && slide.frame < ends[slide.string_index]) || total_points > kMaxFretPoints - slide.point_count)
            return GT_INVALID_ARGUMENT;
        total_points += slide.point_count;
        for (std::size_t p = 0; p < slide.point_count; ++p) {
            const auto& point = slide.points[p];
            if (!in_range(point.fret, 0, 36) || (p && point.frame_offset <= slide.points[p - 1].frame_offset) ||
                point.frame_offset >= std::numeric_limits<std::uint64_t>::max() - slide.frame ||
                synth->config.tuning_hz[slide.string_index] * std::exp2(static_cast<double>(point.fret) / 12.0) > synth->config.sample_rate * 0.25)
                return GT_INVALID_ARGUMENT;
        }
        const auto end = slide.frame + slide.points[slide.point_count - 1].frame_offset;
        used[slide.string_index] = true; ends[slide.string_index] = end;
        // Events are sorted, so inspect only plucks in this slide's interval.
        if (count) {
            const auto first = std::lower_bound(events, events + count, slide.frame,
                [](const gt_event& event, std::uint64_t frame) { return event.frame < frame; });
            for (auto event = first; event != events + count && event->frame < end; ++event) {
                if (enum_value(event->type) == GT_EVENT_PLUCK && event->string_index == slide.string_index &&
                    (event->frame != slide.frame || event->fret != slide.points[0].fret)) return GT_INVALID_ARGUMENT;
            }
        }
    }
    try {
        std::vector<gt_event> replacement;
        if (count != 0) {
            replacement.assign(events, events + count);
        }
        std::vector<Slide> replacement_slides;
        replacement_slides.reserve(slide_count);
        for (std::size_t i = 0; i < slide_count; ++i) {
            Slide copied;
            copied.frame = slides[i].frame; copied.string_index = slides[i].string_index;
            copied.points.assign(slides[i].points, slides[i].points + slides[i].point_count);
            replacement_slides.push_back(std::move(copied));
        }
        synth->events.swap(replacement);
        synth->slides.swap(replacement_slides);
        reset_state(*synth);
        return GT_OK;
    } catch (const std::bad_alloc&) {
        return GT_OUT_OF_MEMORY;
    } catch (...) {
        return GT_INTERNAL_ERROR;
    }
}

gt_result gt_reset(gt_synth* synth) {
    if (synth == nullptr) {
        return GT_INVALID_ARGUMENT;
    }
    reset_state(*synth);
    return GT_OK;
}

gt_result gt_render(gt_synth* synth, float* mono, size_t frames) {
    if (synth == nullptr || (frames != 0 && mono == nullptr) ||
        frames > std::numeric_limits<std::uint64_t>::max() - synth->frame) {
        return GT_INVALID_ARGUMENT;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        if (synth->model == GT_MODEL_CONTACT_0_2) update_contacts(*synth);
        while (synth->event_cursor < synth->events.size() &&
               synth->events[synth->event_cursor].frame == synth->frame) {
            apply_event(*synth, synth->events[synth->event_cursor++]);
        }
        double strings = 0.0;
        for (std::uint32_t index = 0; index < synth->config.string_count; ++index) {
            strings += render_string(synth->strings[index], synth->config.brightness);
        }
        const double body = render_body(*synth, strings);
        const double mixed = strings * 0.7 + body * synth->config.body_mix;
        // Gentle output protection for dense chords/hits. This is a production
        // safeguard, not part of the physical model or a room-reverb simulation.
        mono[i] = static_cast<float>(std::tanh(mixed));
        ++synth->frame;
    }
    return GT_OK;
}

} // extern "C"
