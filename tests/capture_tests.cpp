#include "gtracker/capture.hpp"
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
}
int main() {
    try {
        gtracker::CaptureOptions options;
        options.pattern.bpm = 90;
        options.pattern.length_beats = 8;
        for (int rate : {44100, 48000}) for (int channels : {1, 2}) {
            const auto plan = gtracker::plan_capture(options, rate, channels);
            check(plan.pre_roll_frames == static_cast<std::uint64_t>(rate), "pre-roll sample clock");
            check(plan.pattern_zero_frame == static_cast<std::uint64_t>(rate) + static_cast<std::uint64_t>(std::llround(4.0 * 60 / 90 * rate)), "count-in beat zero");
            check(plan.total_frames == plan.pattern_zero_frame + plan.musical_frames + plan.tail_frames, "retained context/tail length");
        }
        rejects([&] { gtracker::plan_capture(options, 7999, 1); }, "bad rate accepted");
        rejects([&] { gtracker::plan_capture(options, 48000, 3); }, "implicit channel loss accepted");
        options.calibration_reference = true;
        options.count_in_beats = 1;
        rejects([&] { gtracker::plan_capture(options, 48000, 1); }, "truncated calibration reference accepted");
        options.calibration_reference = false;
        options.count_in_beats = 4;
        options.pre_roll_seconds = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { gtracker::plan_capture(options, 48000, 1); }, "nonfinite plan accepted");
        options.pre_roll_seconds = 0;
        options.pattern.length_beats = 256;
        options.pattern.bpm = 20;
        rejects([&] { gtracker::plan_capture(options, 48000, 1); }, "unbounded capture accepted");
        options.pattern.length_beats = 64;
        options.pattern.bpm = 40; // 96 s + count-in/tail, valid in time but excessive stereo preview at 192 kHz.
        rejects([&] { gtracker::plan_capture(options, 192000, 2); }, "unbounded sample allocation accepted");
        options.device.clear();
        rejects([&] { gtracker::capture_audio(options); }, "implicit recording input accepted");
        std::cout << "capture planning: source choice, frame clock, channel and duration/allocation bounds passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "capture plan: " << error.what() << '\n';
        return 1;
    }
}
