#pragma once
#include "gtracker/hash.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace gtracker::sid {
class LiveSynth {
  SDL_Process *process = nullptr;
  SDL_IOStream *input = nullptr, *output = nullptr;
  bool ready = false;
  void write(const void *bytes, size_t n) {
    const auto *p = static_cast<const unsigned char *>(bytes);
    const auto deadline = SDL_GetTicks() + 3000;
    while (n) {
      const auto sent = SDL_WriteIO(input, p, n);
      p += sent;
      n -= sent;
      if (!sent) {
        if (SDL_GetTicks() > deadline ||
            SDL_GetIOStatus(input) != SDL_IO_STATUS_NOT_READY)
          throw std::runtime_error("SID stream write stopped or timed out");
        SDL_Delay(1);
      }
    }
    if (!SDL_FlushIO(input))
      throw std::runtime_error("SID stream flush failed");
  }
  void read(void *bytes, size_t n) {
    auto *p = static_cast<unsigned char *>(bytes);
    auto deadline = SDL_GetTicks() + 3000;
    while (n) {
      auto got = SDL_ReadIO(output, p, n);
      p += got;
      n -= got;
      if (!got) {
        if (SDL_GetTicks() > deadline ||
            SDL_GetIOStatus(output) != SDL_IO_STATUS_NOT_READY)
          throw std::runtime_error("SID stream stopped or timed out");
        SDL_Delay(1);
      }
    }
  }
  void send(uint32_t n) {
    unsigned char b[4];
    for (int i = 0; i < 4; ++i)
      b[i] = static_cast<unsigned char>(n >> (8 * i));
    write(b, 4);
  }
  uint32_t receive() {
    unsigned char b[4];
    read(b, 4);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 |
           uint32_t(b[3]) << 24;
  }

public:
  LiveSynth(const LiveSynth&)=delete;
  LiveSynth& operator=(const LiveSynth&)=delete;
  explicit LiveSynth(const std::filesystem::path &tools) {
#ifdef _WIN32
    auto exe =
        std::filesystem::absolute(tools / "gtracker_sidstream.exe").u8string();
#else
    auto exe =
        std::filesystem::absolute(tools / "gtracker_sidstream").u8string();
#endif
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(exe)))
      throw std::runtime_error("Realtime SID helper missing. Run "
                               "tools/sid_backend.py prepare-video.");
    std::ifstream receipt(tools / "stream.json");
    std::string manifest(std::istreambuf_iterator<char>(receipt), {});
    if (manifest.find(gtracker::sha256_file(std::filesystem::u8path(exe))) ==
        std::string::npos)
      throw std::runtime_error(
          "Realtime SID helper checksum mismatch; rebuild with prepare-video");
    const char *args[] = {exe.c_str(), nullptr};
    process = SDL_CreateProcess(args, true);
    if (!process)
      throw std::runtime_error(SDL_GetError());
    input = SDL_GetProcessInput(process);
    output = SDL_GetProcessOutput(process);
  }
  ~LiveSynth() {
    if (process) {
      SDL_KillProcess(process, true);
      SDL_WaitProcess(process, true, nullptr);
      SDL_DestroyProcess(process);
    }
  }
  void load(const std::filesystem::path &file) {
    if (!ready) {
      if (receive() != 0x47545331)
        throw std::runtime_error("SID stream protocol mismatch");
      ready = true;
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> data(std::istreambuf_iterator<char>(in), {});
    if (data.size() < 126 || data.size() > 65536)
      throw std::runtime_error("Invalid SID stream input");
    send(1);
    send(uint32_t(data.size()));
    write(data.data(), data.size());
    if (receive() != 1)
      throw std::runtime_error("SID load failed");
  }
  size_t read_frames(float *out, size_t frames) {
    if (frames > 4096)
      throw std::runtime_error("SID block too large");
    send(2);
    send(uint32_t(frames));
    if (receive() != frames)
      throw std::runtime_error("SID frame mismatch");
    unsigned char bytes[8192];
    read(bytes, frames * 2);
    for (size_t i = 0; i < frames; ++i)
      out[i] = static_cast<int16_t>(uint16_t(bytes[i * 2]) |
                                    uint16_t(bytes[i * 2 + 1]) << 8) /
               32768.0f;
    return frames;
  }
};
} // namespace gtracker::sid
