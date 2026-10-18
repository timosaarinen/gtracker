#pragma once

#include "gtracker/project.hpp"
#include <functional>

namespace gtracker {

struct CaptureFormat {
    int sample_rate = 0;
    int channels = 0;
    std::string encoding;
    int buffer_frames = 0; // Device service quantum, never a latency measurement.
};
struct RecordingDevice {
    int index = 0;
    std::uint32_t runtime_id = 0;
    std::string name;
    CaptureFormat preferred;
};
struct CaptureOptions {
    Pattern pattern;
    std::string device; // Explicit "default", zero-based index, or exact name.
    std::filesystem::path simulated_input; // Mutually exclusive with device.
    int sample_rate = 0; // Zero retains device recommendation/file rate.
    int channels = 0; // Zero retains mono/stereo; multichannel devices need an override.
    double pre_roll_seconds = 1;
    double tail_seconds = 2;
    int count_in_beats = 4;
    bool metronome = false;
    bool calibration_reference = false;
    bool silent = false;
    bool realtime_simulation = false;
    std::uint64_t simulate_disconnect_frame = 0; // File simulation only.
    std::function<bool(std::uint64_t)> cancelled;
};
struct CapturePlan {
    int sample_rate = 0;
    int channels = 0;
    std::uint64_t pre_roll_frames = 0;
    std::uint64_t count_in_frames = 0;
    std::uint64_t pattern_zero_frame = 0;
    std::uint64_t musical_frames = 0;
    std::uint64_t tail_frames = 0;
    std::uint64_t total_frames = 0;
};
struct CaptureResult {
    std::string status = "failed";
    std::string error;
    std::string source_kind;
    std::string driver;
    std::string device_name;
    CaptureFormat device_format;
    CapturePlan plan;
    bool format_conversion = false;
    bool reference_played = false;
    std::string simulation_input_sha256;
    std::uint64_t captured_frames = 0;
    std::vector<std::int16_t> interleaved;
    std::vector<std::int16_t> reference_mono;
};

// These functions belong to the SDL I/O layer, never the instrument core.
std::vector<RecordingDevice> recording_devices();
CapturePlan plan_capture(const CaptureOptions& options, int rate, int channels);
// Synchronous servicing on the caller's worker/main thread. No file writes,
// inference or musical DSP run on an audio callback. File simulation opens no device.
CaptureResult capture_audio(const CaptureOptions& options);

} // namespace gtracker
