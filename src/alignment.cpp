#include "gtracker/alignment.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gtracker {
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double coefficient(double sx, double sy, double xx, double yy, double xy, std::size_t count) {
    if (count < 2) return 0;
    const double vx = std::max(0.0, xx - sx * sx / count);
    const double vy = std::max(0.0, yy - sy * sy / count);
    if (vx < 1e-15 || vy < 1e-15) return 0;
    return std::clamp((xy - sx * sy / count) / std::sqrt(vx * vy), -1.0, 1.0);
}
std::vector<double> envelope(const PcmAudio& audio, int channel, int step, std::uint64_t frames) {
    std::vector<double> values(static_cast<std::size_t>(frames / step));
    for (std::size_t bin = 0; bin < values.size(); ++bin) {
        double sum = 0;
        for (int n = 0; n < step; ++n)
            sum += std::abs(static_cast<double>(audio.interleaved[(bin * step + n) * audio.channels + channel]));
        values[bin] = sum / step;
    }
    return values;
}
double coarse_match(const std::vector<double>& x, const std::vector<double>& y, int lag) {
    const int first = std::max(0, -lag);
    const int end = std::min(static_cast<int>(x.size()), static_cast<int>(y.size()) - lag);
    if (end <= first || static_cast<std::size_t>(end - first) * 5 < x.size() * 4) return -1;
    double sx = 0, sy = 0, xx = 0, yy = 0, xy = 0;
    for (int i = first; i < end; ++i) {
        const auto a = x[static_cast<std::size_t>(i)];
        const auto b = y[static_cast<std::size_t>(i + lag)];
        sx += a; sy += b; xx += a * a; yy += b * b; xy += a * b;
    }
    return coefficient(sx, sy, xx, yy, xy, static_cast<std::size_t>(end - first));
}
void validate(const PcmAudio& audio, int channel) {
    require(audio.sample_rate >= 8000 && audio.sample_rate <= 192000, "Delay estimation needs an 8..192 kHz source");
    require(audio.channels == 1 || audio.channels == 2, "Delay estimation needs mono/stereo PCM");
    require(channel >= 0 && channel < audio.channels, "Selected delay-estimation channel does not exist");
    require(audio.frames && audio.frames <= 32 * 1024 * 1024ULL / audio.channels && audio.interleaved.size() == audio.frames * audio.channels,
            "Delay estimation received an incomplete or oversized preview");
    for (float sample : audio.interleaved) require(std::isfinite(sample), "Delay estimation cannot use non-finite audio");
}
bool has_signal(const PcmAudio& audio, int channel, std::uint64_t frames) {
    double sum = 0, squares = 0;
    for (std::uint64_t n = 0; n < frames; ++n) {
        const double value = audio.interleaved[static_cast<std::size_t>(n) * audio.channels + channel];
        sum += value; squares += value * value;
    }
    return frames && squares / frames - std::pow(sum / frames, 2) >= 1e-8;
}
} // namespace

std::vector<std::int16_t> calibration_probe(int sample_rate, std::uint32_t seed) {
    require(sample_rate >= 8000 && sample_rate <= 192000, "Probe sample rate must be in 8000..192000");
    std::vector<std::int16_t> result(static_cast<std::size_t>(sample_rate) * 2, 0);
    std::uint32_t state = seed ? seed : 0x6d2b79f5U;
    // Irregular burst spacing avoids the repeated-beat ambiguity of a metronome.
    for (double seconds : {0.15, 0.48, 0.98, 1.41, 1.80}) {
        const auto start = static_cast<std::size_t>(std::llround(seconds * sample_rate));
        const auto length = static_cast<std::size_t>(sample_rate / 40);
        for (std::size_t n = 0; n < length; ++n) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double noise = static_cast<double>(state & 0xffffU) / 32767.5 - 1;
            const double fade = std::sin(3.141592653589793 * static_cast<double>(n) / length);
            result[start + n] = static_cast<std::int16_t>(std::lround(noise * fade * fade * 4000));
        }
    }
    return result;
}

DelayEstimate estimate_file_delay(const PcmAudio& reference, const PcmAudio& recorded, const AlignmentOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    validate(reference, options.reference_channel); validate(recorded, options.recorded_channel);
    require(reference.sample_rate == recorded.sample_rate, "Relative delay needs equal source rates; resampling alone does not calibrate clocks");
    require(std::isfinite(options.max_offset_seconds) && options.max_offset_seconds >= 0.001 && options.max_offset_seconds <= 2,
            "Delay search must be bounded to 1..2000 milliseconds");
    require(std::isfinite(options.window_seconds) && options.window_seconds >= 0.1 && options.window_seconds <= 5,
            "Reference window must be in 0.1..5 seconds");
    DelayEstimate result;
    const int rate = reference.sample_rate;
    result.max_offset_frames = static_cast<std::int64_t>(std::llround(options.max_offset_seconds * rate));
    result.reference_window_frames = std::min(reference.frames, static_cast<std::uint64_t>(std::llround(options.window_seconds * rate)));
    result.coarse_step_frames = std::max(1, (rate + 999) / 1000);
    const auto finish = [&] {
        result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };
    if (result.reference_window_frames < static_cast<std::uint64_t>(rate / 10)) { result.status = "insufficient_overlap"; return finish(); }
    if (recorded.frames * 5 < result.reference_window_frames * 4) { result.status = "insufficient_overlap"; return finish(); }
    const int step = result.coarse_step_frames;
    const auto recorded_window = std::min(recorded.frames, result.reference_window_frames + static_cast<std::uint64_t>(result.max_offset_frames) + step);
    if (!has_signal(reference, options.reference_channel, result.reference_window_frames) || !has_signal(recorded, options.recorded_channel, recorded_window)) {
        result.status = "no_signal"; return finish();
    }
    const auto x = envelope(reference, options.reference_channel, step, result.reference_window_frames);
    const auto y = envelope(recorded, options.recorded_channel, step, recorded_window);
    const int range = static_cast<int>((result.max_offset_frames + step - 1) / step);
    std::vector<double> scores(static_cast<std::size_t>(range * 2 + 1), -1);
    int best = 0;
    double best_score = -1;
    for (int lag = -range; lag <= range; ++lag) {
        const double score = coarse_match(x, y, lag);
        scores[static_cast<std::size_t>(lag + range)] = score;
        if (score > best_score) { best_score = score; best = lag; }
    }
    if (best_score < 0) { result.status = "insufficient_overlap"; return finish(); }
    result.coarse_score = std::max(0.0, best_score);
    const int exclude = static_cast<int>(std::ceil(rate * 0.02 / step));
    for (int lag = -range; lag <= range; ++lag) if (std::abs(lag - best) > exclude)
        result.alternative_score = std::max(result.alternative_score, scores[static_cast<std::size_t>(lag + range)]);
    if (best_score < 0.6) return finish();

    // Refine a high-energy <=2048-sample anchor at native sample resolution.
    // Mean removal + normalization handles gain and DC offset. Absolute score
    // permits an inverted cable/microphone polarity, which is reported separately.
    const auto length = std::min<std::uint64_t>(2048, static_cast<std::uint64_t>(rate * 0.03));
    result.refinement_window_frames = length;
    double energy = 0, largest = -1;
    for (std::uint64_t frame = 0; frame < result.reference_window_frames; ++frame) {
        const double sample = reference.interleaved[static_cast<std::size_t>(frame) * reference.channels + options.reference_channel];
        energy += sample * sample;
        if (frame >= length) {
            const double old = reference.interleaved[static_cast<std::size_t>(frame - length) * reference.channels + options.reference_channel];
            energy -= old * old;
        }
        if (frame + 1 >= length && energy > largest) { largest = energy; result.refinement_start_frame = frame + 1 - length; }
    }
    if (largest < length * 1e-8) return finish();
    const auto first = result.refinement_start_frame;
    const auto center = static_cast<std::int64_t>(best) * step;
    const auto low = std::max(-result.max_offset_frames, center - step * 2);
    const auto high = std::min(result.max_offset_frames, center + step * 2);
    for (auto lag = low; lag <= high; ++lag) {
        const auto recorded_first = static_cast<std::int64_t>(first) + lag;
        if (recorded_first < 0 || static_cast<std::uint64_t>(recorded_first) + length > recorded.frames) continue;
        double sx = 0, sy = 0, xx = 0, yy = 0, xy = 0;
        for (std::uint64_t n = 0; n < length; ++n) {
            const double a = reference.interleaved[static_cast<std::size_t>(first + n) * reference.channels + options.reference_channel];
            const double b = recorded.interleaved[static_cast<std::size_t>(recorded_first + static_cast<std::int64_t>(n)) * recorded.channels + options.recorded_channel];
            sx += a; sy += b; xx += a * a; yy += b * b; xy += a * b;
        }
        const double score = coefficient(sx, sy, xx, yy, xy, static_cast<std::size_t>(length));
        if (std::abs(score) > result.fine_score) {
            result.fine_score = std::abs(score); result.polarity = score < 0 ? -1 : 1; result.candidate_offset_frames = lag;
        }
    }
    if (result.fine_score < 0.6 || best_score - result.alternative_score < 0.08) result.status = "ambiguous";
    else if (std::abs(result.candidate_offset_frames) == result.max_offset_frames) result.status = "search_boundary";
    else result.status = "matched";
    return finish();
}
} // namespace gtracker
