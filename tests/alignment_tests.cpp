#include "gtracker/alignment.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
gtracker::PcmAudio probe(int rate) {
    gtracker::PcmAudio result;
    result.sample_rate = rate; result.channels = 1;
    const auto pcm = gtracker::calibration_probe(rate);
    result.frames = pcm.size();
    for (auto sample : pcm) result.interleaved.push_back(static_cast<float>(sample / 32768.0));
    return result;
}
gtracker::PcmAudio delayed(const gtracker::PcmAudio& ref, std::int64_t lag, int channels = 1, bool invert = false) {
    gtracker::PcmAudio result;
    result.sample_rate = ref.sample_rate; result.channels = channels; result.frames = ref.frames + ref.sample_rate / 2;
    result.interleaved.resize(static_cast<std::size_t>(result.frames) * channels);
    for (std::uint64_t n = 0; n < result.frames; ++n) {
        const auto source = static_cast<std::int64_t>(n) - lag;
        const float value = source >= 0 && source < static_cast<std::int64_t>(ref.frames) ? ref.interleaved[static_cast<std::size_t>(source)] : 0;
        for (int ch = 0; ch < channels; ++ch)
            result.interleaved[static_cast<std::size_t>(n) * channels + ch] = 0.02f + (invert ? -0.7f : 0.7f) * value + (ch ? 1e-5f : 0);
    }
    return result;
}
}
int main() {
    try {
        for (int rate : {8000, 44100, 48000, 192000}) {
            const auto reference = probe(rate);
            check(gtracker::calibration_probe(rate) == gtracker::calibration_probe(rate), "probe must be deterministic");
            check(gtracker::calibration_probe(rate, 43) != gtracker::calibration_probe(rate), "probe seed must affect reference");
            for (int channels : {1, 2}) for (std::int64_t lag : {std::int64_t{0}, std::int64_t{137}, std::int64_t{-223}, static_cast<std::int64_t>(rate / 10 + 3)}) {
                gtracker::AlignmentOptions options;
                options.recorded_channel = channels - 1;
                const auto result = gtracker::estimate_file_delay(reference, delayed(reference, lag, channels, true), options);
                if (result.status != "matched" || result.candidate_offset_frames != lag)
                    throw std::runtime_error("Known file delay failed: rate " + std::to_string(rate) + " lag " + std::to_string(lag) + " status " + result.status + " candidate " + std::to_string(result.candidate_offset_frames));
                check(result.polarity == -1 && result.fine_score > 0.99, "gain/DC/inverted polarity diagnostics");
            }
        }
        auto reference = probe(48000);
        auto recording = delayed(reference, 9600);
        gtracker::AlignmentOptions options;
        options.max_offset_seconds = 0.2;
        check(gtracker::estimate_file_delay(reference, recording, options).status == "search_boundary", "search edge cannot certify an offset");
        auto silence = reference;
        std::fill(silence.interleaved.begin(), silence.interleaved.end(), 0);
        check(gtracker::estimate_file_delay(silence, silence).status == "no_signal", "silence cannot calibrate a clock");
        auto short_input = reference;
        short_input.frames = 100; short_input.interleaved.resize(100);
        check(gtracker::estimate_file_delay(reference, short_input).status == "insufficient_overlap", "short recording must not match a fragment");
        auto periodic = reference;
        const std::size_t period = 12000;
        for (std::size_t n = period; n < periodic.interleaved.size(); ++n) periodic.interleaved[n] = periodic.interleaved[n % period];
        check(gtracker::estimate_file_delay(periodic, delayed(periodic, 123)).status == "ambiguous", "periodic references must expose repeated-lag ambiguity");
        auto other_probe = reference;
        const auto other_pcm = gtracker::calibration_probe(48000, 999);
        for (std::size_t n = 0; n < other_pcm.size(); ++n) other_probe.interleaved[n] = static_cast<float>(other_pcm[n] / 32768.0);
        check(gtracker::estimate_file_delay(reference, other_probe).status != "matched", "matching envelopes alone cannot certify different waveforms");
        recording.sample_rate = 44100;
        rejects([&] { gtracker::estimate_file_delay(reference, recording); }, "unequal clocks/rates accepted");
        options.max_offset_seconds = std::numeric_limits<double>::infinity();
        rejects([&] { gtracker::estimate_file_delay(reference, reference, options); }, "unbounded search accepted");
        options = {};
        options.recorded_channel = 1;
        rejects([&] { gtracker::estimate_file_delay(reference, reference, options); }, "missing channel accepted");
        std::cout << "relative delay: signed offsets, rates/channels, gain/DC/polarity, silence, overlap and bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "alignment: " << error.what() << '\n'; return 1;
    }
}
