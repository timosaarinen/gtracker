#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
namespace gtracker {
// Synthesis/IPC runs on one producer, never in an audio callback. Queue is
// bounded.
class LiveAudio {
  SDL_AudioStream *stream = nullptr;
  std::thread worker;
  std::atomic<bool> cancelled{false}, paused{false}, finished{true};
  std::atomic<uint64_t> produced{0};
  int channels = 1;
  SDL_AudioDeviceID selected_device = 0;
  mutable std::mutex mutex;
  std::string failure;

public:
  ~LiveAudio() {
    stop();
    if (stream)
      SDL_DestroyAudioStream(stream);
  }
  void stop() {
    cancelled = true;
    if (worker.joinable())
      worker.join();
    if (stream) {
      SDL_PauseAudioStreamDevice(stream);
      SDL_ClearAudioStream(stream);
    }
    finished = true;
    paused = false;
  }
  void start(std::function<size_t(float *, size_t)> read, int count = 1,
             SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
    stop();
    if (stream && (channels != count || selected_device != device)) {
      SDL_DestroyAudioStream(stream);
      stream = nullptr;
    }
    channels = count;
    selected_device = device;
    if (!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
      throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec spec{SDL_AUDIO_F32, channels, 48000};
    if (!stream)
      stream = SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
    if (!stream)
      throw std::runtime_error(SDL_GetError());
    cancelled = false;
    finished = false;
    produced = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      failure.clear();
    }
    worker = std::thread([this, read = std::move(read)] {
      try {
        std::array<float, 1024> block{};
        bool started = false;
        while (!cancelled) {
          if (paused || SDL_GetAudioStreamQueued(stream) >=
                            2048 * channels * int(sizeof(float))) {
            SDL_Delay(2);
            continue;
          }
          const auto n = read(block.data(), 512);
          if (!n)
            break;
          if (cancelled)
            break;
          if (!SDL_PutAudioStreamData(stream, block.data(),
                                      int(n * channels * sizeof(float))))
            throw std::runtime_error(SDL_GetError());
          produced += n;
          if (!started) {
            if (!paused)
              SDL_ResumeAudioStreamDevice(stream);
            started = true;
          }
        }
        if (!cancelled)
          SDL_FlushAudioStream(stream);
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(mutex);
        failure = e.what();
      }
      finished = true;
    });
  }
  void toggle() {
    paused = !paused;
    if (stream) {
      if (paused)
        SDL_PauseAudioStreamDevice(stream);
      else
        SDL_ResumeAudioStreamDevice(stream);
    }
  }
  uint64_t position() const {
    auto queued = stream ? std::max(0, SDL_GetAudioStreamQueued(stream)) /
                               int(channels * sizeof(float))
                         : 0;
    auto n = produced.load();
    return n > uint64_t(queued) ? n - queued : 0;
  }
  bool stopping() const { return cancelled; }
  bool done() const {
    return finished && (!stream || SDL_GetAudioStreamQueued(stream) == 0);
  }
  std::string error() const {
    std::lock_guard<std::mutex> lock(mutex);
    return failure;
  }
};
} // namespace gtracker
