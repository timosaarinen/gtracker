#include "gtracker/production.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace gtracker {
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
bool bounded(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
float quiet(float value) { return std::abs(value) < 1e-20f ? 0.0f : value; }
struct Delay {
    std::vector<float> samples;
    std::size_t cursor = 0;
    void init(std::size_t count) { samples.assign(std::max<std::size_t>(1, count), 0); cursor = 0; }
    void reset() { std::fill(samples.begin(), samples.end(), 0); cursor = 0; }
    float read() const { return samples[cursor]; }
    void write(float value) { samples[cursor] = quiet(value); if (++cursor == samples.size()) cursor = 0; }
};
struct Comb {
    Delay delay;
    float feedback = 0, lowpass = 0, damping = 0;
    float tick(float input) {
        const float delayed = delay.read();
        lowpass = quiet((1 - damping) * delayed + damping * lowpass);
        // Unit DC gain prevents a long decay from multiplying steady input.
        delay.write((1 - feedback) * input + feedback * lowpass);
        return delayed;
    }
    void reset() { delay.reset(); lowpass = 0; }
};
struct Allpass {
    Delay delay;
    float tick(float input) {
        constexpr float gain = 0.5f;
        const float output = delay.read() - gain * input;
        delay.write(input + gain * output);
        return output;
    }
};
std::size_t delay_frames(double milliseconds, int rate) {
    return static_cast<std::size_t>(std::llround(milliseconds * rate / 1000));
}
}

void validate_production(const ProductionSettings& s) {
    require(s.version == 1 && s.algorithm == "room_v1", "Unsupported production version/algorithm; expected v1 room_v1");
    require(bounded(s.wet, 0, 1), "Production wet must be in 0..1");
    require(bounded(s.decay_seconds, 0.1f, 8), "Production decay_seconds must be in 0.1..8");
    require(bounded(s.damping, 0, 0.95f), "Production damping must be in 0..0.95");
    require(bounded(s.predelay_ms, 0, 200), "Production predelay_ms must be in 0..200");
    require(bounded(s.width, 0, 1), "Production width must be in 0..1");
    require(bounded(s.output_gain_db, -24, 6), "Production output_gain_db must be in -24..6");
}
double production_tail_seconds(const ProductionSettings& s) {
    validate_production(s);
    return s.enabled && s.wet > 0 ? s.predelay_ms / 1000.0 + 1.5 * s.decay_seconds + 0.15 : 0;
}

struct ProductionProcessor::State {
    ProductionSettings settings;
    std::array<std::array<Comb, 4>, 2> combs;
    std::array<std::array<Allpass, 2>, 2> diffusers;
    Delay predelay;
    bool has_predelay = false;
    float gain = 1;
    State(int rate, const ProductionSettings& s) : settings(s) {
        validate_production(s);
        require(rate >= 8000 && rate <= 192000, "Production sample rate must be in 8000..192000");
        gain = std::pow(10.0f, s.output_gain_db / 20.0f);
        const auto pre = delay_frames(s.predelay_ms, rate);
        has_predelay = pre != 0;
        predelay.init(pre);
        // Original tuning; topology follows Schroeder/Moorer comb + allpass DSP.
        constexpr double comb_ms[2][4] = {{29.7, 37.1, 41.1, 43.7}, {30.7, 38.3, 42.5, 45.1}};
        constexpr double diffuse_ms[2][2] = {{5.0, 1.7}, {5.7, 2.3}};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (std::size_t i = 0; i < 4; ++i) {
                auto& comb = combs[channel][i];
                const auto count = std::max<std::size_t>(1, delay_frames(comb_ms[channel][i], rate));
                comb.delay.init(count);
                comb.feedback = static_cast<float>(std::pow(0.001, static_cast<double>(count) / rate / s.decay_seconds));
                comb.damping = s.damping;
            }
            for (std::size_t i = 0; i < 2; ++i) diffusers[channel][i].delay.init(delay_frames(diffuse_ms[channel][i], rate));
        }
    }
    void reset() {
        predelay.reset();
        for (auto& channel : combs) for (auto& comb : channel) comb.reset();
        for (auto& channel : diffusers) for (auto& diffuser : channel) diffuser.delay.reset();
    }
};
ProductionProcessor::ProductionProcessor(int rate, const ProductionSettings& settings) : state_(std::make_unique<State>(rate, settings)) {}
ProductionProcessor::~ProductionProcessor() = default;
ProductionProcessor::ProductionProcessor(ProductionProcessor&&) noexcept = default;
ProductionProcessor& ProductionProcessor::operator=(ProductionProcessor&&) noexcept = default;
void ProductionProcessor::reset() { require(static_cast<bool>(state_), "Moved production processor"); state_->reset(); }
void ProductionProcessor::process(const float* mono, float* stereo, std::size_t frames) {
    require(static_cast<bool>(state_), "Moved production processor");
    require(frames == 0 || (mono && stereo && mono != stereo), "Production requires separate mono input and stereo output buffers");
    for (std::size_t i = 0; i < frames; ++i) require(std::isfinite(mono[i]) && std::abs(mono[i]) <= 16, "Production input must be finite and within +/-16");
    auto& state = *state_;
    const auto& s = state.settings;
    for (std::size_t i = 0; i < frames; ++i) {
        const float input = mono[i];
        if (!s.enabled) { stereo[i * 2] = input; stereo[i * 2 + 1] = input; continue; }
        float delayed = input;
        if (state.has_predelay) { delayed = state.predelay.read(); state.predelay.write(input); }
        float wet[2]{};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (auto& comb : state.combs[channel]) wet[channel] += comb.tick(delayed) * 0.25f;
            for (auto& diffuser : state.diffusers[channel]) wet[channel] = diffuser.tick(wet[channel]);
        }
        const float mid = (wet[0] + wet[1]) * 0.5f;
        const float side = (wet[0] - wet[1]) * (s.width * 0.5f);
        stereo[i * 2] = ((1 - s.wet) * input + s.wet * (mid + side)) * state.gain;
        stereo[i * 2 + 1] = ((1 - s.wet) * input + s.wet * (mid - side)) * state.gain;
    }
}
} // namespace gtracker
