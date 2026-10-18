#include "gtracker/capture.hpp"
#include "gtracker/audio_source.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <thread>

namespace gtracker {
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::runtime_error sdl_error(const char* action) { return std::runtime_error(std::string(action) + ": " + SDL_GetError()); }
struct AudioScope {
    AudioScope() { if (!SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_EVENTS)) throw sdl_error("Initialize audio"); }
    ~AudioScope() { SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_EVENTS); }
};
using Stream = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>;
CaptureFormat format(SDL_AudioDeviceID device) {
    SDL_AudioSpec spec{};
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(device, &spec, &frames)) throw sdl_error("Read device format");
    return {spec.freq, spec.channels, SDL_GetAudioFormatName(spec.format), frames};
}
std::vector<RecordingDevice> enumerate() {
    int count = 0;
    auto* ids = SDL_GetAudioRecordingDevices(&count);
    if (!ids) throw sdl_error("Enumerate recording devices");
    std::unique_ptr<SDL_AudioDeviceID, decltype(&SDL_free)> owned(ids, SDL_free);
    std::vector<RecordingDevice> devices;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        if (!name) throw sdl_error("Read recording device name");
        devices.push_back({i, ids[i], name, format(ids[i])});
    }
    return devices;
}
SDL_AudioDeviceID select_device(const std::string& selector, std::string& name) {
    if (selector == "default") {
        name = "System default recording device";
        return SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    }
    const auto devices = enumerate();
    const bool numeric = !selector.empty() && std::all_of(selector.begin(), selector.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (numeric) {
        for (const auto& device : devices) if (std::to_string(device.index) == selector) { name = device.name; return device.runtime_id; }
    } else {
        const RecordingDevice* match = nullptr;
        for (const auto& device : devices) if (device.name == selector) {
            require(!match, "More than one input has this name; select its current index");
            match = &device;
        }
        if (match) { name = match->name; return match->runtime_id; }
    }
    throw std::runtime_error("Recording device not found: " + selector);
}
std::vector<std::int16_t> make_reference(const CaptureOptions& options, const CapturePlan& plan) {
    std::vector<std::int16_t> result(static_cast<std::size_t>(plan.total_frames), 0);
    const double beat_frames = 60.0 / options.pattern.bpm * plan.sample_rate;
    const auto click = [&](std::uint64_t start, bool accent) {
        const auto length = static_cast<std::uint64_t>(plan.sample_rate / 40); // 25 ms reference click.
        for (std::uint64_t n = 0; n < length && start + n < plan.total_frames; ++n) {
            const double phase = 6.283185307179586 * (accent ? 1760 : 1320) * static_cast<double>(n) / plan.sample_rate;
            const double envelope = std::pow(1.0 - static_cast<double>(n) / length, 2);
            result[static_cast<std::size_t>(start + n)] = static_cast<std::int16_t>(std::lround(std::sin(phase) * envelope * 5000));
        }
    };
    for (int i = 0; i < options.count_in_beats; ++i)
        click(plan.pre_roll_frames + static_cast<std::uint64_t>(std::llround(i * beat_frames)), i == 0);
    if (options.metronome) for (int i = 0; i < static_cast<int>(std::ceil(options.pattern.length_beats)); ++i)
        click(plan.pattern_zero_frame + static_cast<std::uint64_t>(std::llround(i * beat_frames)), i % 4 == 0);
    return result;
}
bool cancelled(const CaptureOptions& options, std::uint64_t frames) { return options.cancelled && options.cancelled(frames); }
void finish(CaptureResult& result) {
    result.interleaved.resize(static_cast<std::size_t>(result.captured_frames) * result.plan.channels);
    if (result.plan.total_frames && result.captured_frames == result.plan.total_frames && result.error.empty() && result.status != "cancelled") result.status = "complete";
}
void simulate(const CaptureOptions& options, CaptureResult& result) {
    const auto source = load_pcm_wav(options.simulated_input);
    result.source_kind = "file_simulation";
    result.driver = "none";
    result.device_name = options.simulated_input.filename().u8string();
    result.device_format = {source.sample_rate, source.channels, "PCM_S" + std::to_string(source.sample_width_bits) + "LE", 0};
    if (source.sample_width_bits == 8) result.device_format.encoding = "PCM_U8";
    result.simulation_input_sha256 = source.sha256;
    require(!options.sample_rate || options.sample_rate == source.sample_rate, "File simulation does not resample; use its source sample rate");
    require(!options.channels || options.channels == source.channels, "File simulation does not remap channels");
    result.plan = plan_capture(options, source.sample_rate, source.channels);
    result.format_conversion = source.sample_width_bits != 16;
    result.reference_mono = make_reference(options, result.plan);
    result.interleaved.resize(static_cast<std::size_t>(result.plan.total_frames) * source.channels);
    const auto start = std::chrono::steady_clock::now();
    while (result.captured_frames < result.plan.total_frames) {
        if (cancelled(options, result.captured_frames)) { result.status = "cancelled"; break; }
        if (options.simulate_disconnect_frame && result.captured_frames >= options.simulate_disconnect_frame) {
            result.status = "disconnected"; result.error = "Simulated input disconnect"; break;
        }
        if (result.captured_frames >= source.frames) { result.status = "source_exhausted"; result.error = "Simulation input ended before the requested capture"; break; }
        auto end = std::min({result.captured_frames + 512, result.plan.total_frames, source.frames});
        if (options.simulate_disconnect_frame) end = std::min(end, options.simulate_disconnect_frame);
        for (auto frame = result.captured_frames; frame < end; ++frame) for (int ch = 0; ch < source.channels; ++ch) {
            const auto index = static_cast<std::size_t>(frame) * source.channels + ch;
            const auto pcm = std::lround(static_cast<double>(source.interleaved[index]) * 32768.0);
            result.interleaved[index] = static_cast<std::int16_t>(std::clamp(pcm, -32768L, 32767L));
        }
        result.captured_frames = end;
        if (options.realtime_simulation) {
            const auto target = start + std::chrono::microseconds(static_cast<std::int64_t>(end * 1000000ULL / source.sample_rate));
            // Short waits let signal/cancel-file checks run at least every block.
            std::this_thread::sleep_until(target);
        }
    }
    finish(result);
}
void record(const CaptureOptions& options, CaptureResult& result) {
    AudioScope scope;
    result.source_kind = "sdl_recording";
    const char* driver = SDL_GetCurrentAudioDriver();
    result.driver = driver ? driver : "unknown";
    if (result.driver == "dummy" || result.driver == "disk") result.source_kind = "sdl_simulated_driver";
    const auto selected = select_device(options.device, result.device_name);
    const auto preferred = format(selected);
    const int rate = options.sample_rate ? options.sample_rate : preferred.sample_rate;
    const int channels = options.channels ? options.channels : preferred.channels;
    result.plan = plan_capture(options, rate, channels);
    result.reference_mono = make_reference(options, result.plan);
    result.interleaved.resize(static_cast<std::size_t>(result.plan.total_frames) * channels);
    const SDL_AudioSpec app{SDL_AUDIO_S16, channels, rate};
    Stream input(SDL_OpenAudioDeviceStream(selected, &app, nullptr, nullptr), SDL_DestroyAudioStream);
    if (!input) throw sdl_error("Open selected recording device");
    const auto logical = SDL_GetAudioStreamDevice(input.get());
    result.device_format = format(logical);
    const char* actual_name = SDL_GetAudioDeviceName(logical);
    if (actual_name) result.device_name = actual_name;
    SDL_AudioSpec device_spec{};
    if (!SDL_GetAudioDeviceFormat(logical, &device_spec, nullptr)) throw sdl_error("Read opened input format");
    result.format_conversion = device_spec.format != app.format || device_spec.freq != app.freq || device_spec.channels != app.channels;
    Stream output(nullptr, SDL_DestroyAudioStream);
    if (!options.silent && (options.count_in_beats || options.metronome)) {
        const SDL_AudioSpec reference{SDL_AUDIO_S16, 1, rate};
        output.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &reference, nullptr, nullptr));
        if (!output) throw sdl_error("Open count-in playback device (or use --silent)");
        if (!SDL_PutAudioStreamData(output.get(), result.reference_mono.data(), static_cast<int>(result.reference_mono.size() * sizeof(std::int16_t))))
            throw sdl_error("Queue count-in reference");
        if (!SDL_FlushAudioStream(output.get())) throw sdl_error("Flush count-in reference");
    }
    SDL_PumpEvents();
    SDL_Event event{};
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) > 0) {}
    if (cancelled(options, 0)) { result.status = "cancelled"; finish(result); return; }
    if (!SDL_ResumeAudioStreamDevice(input.get())) throw sdl_error("Start recording");
    if (output) {
        if (!SDL_ResumeAudioStreamDevice(output.get())) throw sdl_error("Start count-in playback");
        result.reference_played = true;
    }
    auto last_progress = std::chrono::steady_clock::now();
    const auto deadline = last_progress + std::chrono::seconds(10) + std::chrono::microseconds(static_cast<std::int64_t>(result.plan.total_frames * 1000000ULL / rate));
    const int frame_bytes = channels * static_cast<int>(sizeof(std::int16_t));
    while (result.captured_frames < result.plan.total_frames) {
        if (cancelled(options, result.captured_frames)) { result.status = "cancelled"; break; }
        SDL_PumpEvents();
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) > 0) {
            if (event.adevice.recording && event.type != SDL_EVENT_AUDIO_DEVICE_ADDED &&
                (selected == SDL_AUDIO_DEVICE_DEFAULT_RECORDING || event.adevice.which == selected || event.adevice.which == logical)) {
                result.status = "disconnected";
                result.error = "Selected input was removed or reconfigured; partial source retained";
                break;
            }
        }
        if (result.status == "disconnected") break;
        const int available = SDL_GetAudioStreamAvailable(input.get());
        if (available < 0) throw sdl_error("Query recorded samples");
        const auto frames = std::min<std::uint64_t>(static_cast<std::uint64_t>(available / frame_bytes), result.plan.total_frames - result.captured_frames);
        if (frames) {
            const auto samples = static_cast<int>(frames * channels);
            const int got = SDL_GetAudioStreamData(input.get(), result.interleaved.data() + result.captured_frames * channels, samples * static_cast<int>(sizeof(std::int16_t)));
            if (got < 0) throw sdl_error("Read recorded samples");
            require(got % frame_bytes == 0, "Input stream returned an incomplete PCM frame");
            result.captured_frames += static_cast<std::uint64_t>(got / frame_bytes);
            if (got) last_progress = std::chrono::steady_clock::now();
        }
        if (std::chrono::steady_clock::now() - last_progress > std::chrono::seconds(5)) throw std::runtime_error("Input made no sample progress for five seconds");
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Capture exceeded its bounded wall-clock deadline; partial source retained");
        SDL_Delay(2);
    }
    SDL_PauseAudioStreamDevice(input.get());
    finish(result);
}
} // namespace

std::vector<RecordingDevice> recording_devices() { AudioScope scope; return enumerate(); }
CapturePlan plan_capture(const CaptureOptions& options, int rate, int channels) {
    validate_pattern(options.pattern);
    require(rate >= 8000 && rate <= 192000, "Capture sample rate must be in 8000..192000");
    require(channels == 1 || channels == 2, "Capture needs mono/stereo; explicitly choose --channels 1 or 2 for a multichannel device");
    require(std::isfinite(options.pre_roll_seconds) && options.pre_roll_seconds >= 0 && options.pre_roll_seconds <= 10, "Pre-roll must be in 0..10 seconds");
    require(std::isfinite(options.tail_seconds) && options.tail_seconds >= 0 && options.tail_seconds <= 30, "Tail must be in 0..30 seconds");
    require(options.count_in_beats >= 0 && options.count_in_beats <= 16, "Count-in must be in 0..16 beats");
    CapturePlan plan;
    plan.sample_rate = rate; plan.channels = channels;
    plan.pre_roll_frames = static_cast<std::uint64_t>(std::llround(options.pre_roll_seconds * rate));
    plan.count_in_frames = static_cast<std::uint64_t>(std::llround(options.count_in_beats * 60.0 / options.pattern.bpm * rate));
    plan.pattern_zero_frame = plan.pre_roll_frames + plan.count_in_frames;
    plan.musical_frames = static_cast<std::uint64_t>(std::ceil(options.pattern.length_beats * 60.0 / options.pattern.bpm * rate));
    plan.tail_frames = static_cast<std::uint64_t>(std::llround(options.tail_seconds * rate));
    plan.total_frames = plan.pattern_zero_frame + plan.musical_frames + plan.tail_frames;
    require(plan.total_frames <= static_cast<std::uint64_t>(rate) * 120, "Capture exceeds the short-take limit of 120 seconds");
    require(plan.total_frames * channels <= 32 * 1024 * 1024, "Capture exceeds the 32 Mi-sample preview limit");
    return plan;
}
CaptureResult capture_audio(const CaptureOptions& options) {
    require(options.device.empty() != options.simulated_input.empty(), "Choose exactly one explicit --device or --simulate input");
    require(options.simulated_input.empty() || options.device.empty(), "Device and file simulation are mutually exclusive");
    require(!options.simulated_input.empty() || (!options.realtime_simulation && !options.simulate_disconnect_frame), "Simulation flags require --simulate");
    CaptureResult result;
    result.source_kind = options.simulated_input.empty() ? "sdl_recording" : "file_simulation";
    try {
        if (!options.simulated_input.empty()) simulate(options, result);
        else record(options, result);
    } catch (const std::exception& error) {
        result.status = "failed"; result.error = error.what();
        finish(result);
    }
    return result;
}
} // namespace gtracker
