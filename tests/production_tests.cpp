#include "gtracker/production.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function, const char* message) {
    bool failed = false; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
double energy(const std::vector<float>& audio, std::size_t first, std::size_t last) {
    double result = 0; for (std::size_t i = first; i < last; ++i) result += static_cast<double>(audio[i]) * audio[i];
    return result;
}
void impulse(int rate) {
    gtracker::ProductionSettings s;
    s.enabled = true; s.wet = 1; s.decay_seconds = 0.8f; s.width = 1;
    const auto frames = static_cast<std::size_t>(rate * 2);
    std::vector<float> input(frames), whole(frames * 2), partitioned(frames * 2);
    input[0] = 1;
    gtracker::ProductionProcessor processor(rate, s);
    processor.process(input.data(), whole.data(), frames);
    processor.reset();
    std::size_t offset = 0;
    for (std::size_t step = 1; offset < frames; step = step * 17 % 997 + 1) {
        const auto count = std::min(step, frames - offset);
        processor.process(input.data() + offset, partitioned.data() + offset * 2, count);
        offset += count;
    }
    check(whole == partitioned, "room changed at block/song boundaries or reset lost determinism");
    const auto first = std::find_if(whole.begin(), whole.end(), [](float value) { return value != 0; });
    check(first != whole.end() && static_cast<double>(first - whole.begin()) / 2 / rate >= 0.047,
          "wet sound precedes pre-delay and earliest reflection");
    double difference = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        check(std::isfinite(whole[i * 2]) && std::abs(whole[i * 2]) < 1, "unstable/nonfinite impulse");
        check(std::isfinite(whole[i * 2 + 1]) && std::abs(whole[i * 2 + 1]) < 1, "unstable/nonfinite right impulse");
        difference += std::abs(whole[i * 2] - whole[i * 2 + 1]);
    }
    check(difference > 0.01, "stereo room collapsed into duplicated mono");
    const double early = energy(whole, 0, static_cast<std::size_t>(rate)); // First half-second.
    const double late = energy(whole, static_cast<std::size_t>(rate * 3), whole.size());
    check(early > 1e-6 && late < early * 1e-6, "room tail failed to decay after silence");
    s.width = 0;
    gtracker::ProductionProcessor mono_width(rate, s);
    mono_width.process(input.data(), whole.data(), frames);
    for (std::size_t i = 0; i < frames; ++i) check(whole[i * 2] == whole[i * 2 + 1], "zero width is not mono compatible");
}
}
int main() {
    try {
        for (int rate : {8000, 44100, 48000, 192000}) impulse(rate);
        gtracker::ProductionSettings s;
        s.output_gain_db = 6; // A true bypass skips even gain.
        std::vector<float> input{0.25f, -0.5f, 0, 1}, output(8);
        gtracker::ProductionProcessor bypass(48000, s);
        bypass.process(input.data(), output.data(), input.size());
        for (std::size_t i = 0; i < input.size(); ++i) check(output[i * 2] == input[i] && output[i * 2 + 1] == input[i], "bypass changed samples");
        bypass.process(nullptr, nullptr, 0);
        rejects([&] { bypass.process(input.data(), input.data(), input.size()); }, "in-place mono/stereo alias accepted");
        input[1] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { bypass.process(input.data(), output.data(), input.size()); }, "nonfinite input accepted");
        for (float damping : {0.0f, 0.95f}) {
            s.enabled = true; s.wet = 1; s.decay_seconds = 8; s.damping = damping;
            std::vector<float> constant(48000 * 12, 1), stereo(constant.size() * 2);
            gtracker::ProductionProcessor processor(48000, s);
            processor.process(constant.data(), stereo.data(), constant.size());
            for (float value : stereo) check(std::isfinite(value) && std::abs(value) < 10, "longest decay/constant input is unstable");
            check(stereo.back() > 1 && stereo.back() < 2.2f, "long-decay gain staging lost the bounded DC response");
        }
        s.wet = std::numeric_limits<float>::infinity();
        rejects([&] { gtracker::ProductionProcessor bad(48000, s); }, "nonfinite settings accepted");
        s.wet = 0.2f; s.algorithm = "future";
        rejects([&] { gtracker::ProductionProcessor bad(48000, s); }, "future production algorithm silently substituted");
        std::cout << "production: reset/partition, stereo/mono, pre-delay, decay, bypass and bounded stability passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
