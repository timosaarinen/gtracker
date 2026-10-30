// GPL-2.0-or-later. Persistent, bounded PCM service using the export emulator.
#include "sid_configuration.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/sidplayfp.h>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
uint32_t read32() {
  uint32_t n = 0;
  for (int i = 0; i < 4; ++i) {
    int c = std::cin.get();
    if (c < 0)
      throw 1;
    n |= uint32_t(c) << (8 * i);
  }
  return n;
}
void put32(uint32_t n) {
  for (int i = 0; i < 4; ++i)
    std::cout.put(char(n >> (8 * i)));
}
int main() {
  try {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    ReSIDfpBuilder builder("gtracker pinned reSIDfp");
    sidplayfp engine;
    configure_sid(engine, builder);
    // Warm the analogue tables before accepting requests. Original silent PSID:
    // load/init/play $1000, one RTS, PAL/6581, no ROMs or third-party song
    // data.
    std::vector<uint8_t> warm(127, 0);
    warm[0] = 'P';
    warm[1] = 'S';
    warm[2] = 'I';
    warm[3] = 'D';
    warm[5] = 2;
    warm[7] = 124;
    warm[10] = 0x10;
    warm[12] = 0x10;
    warm[15] = 1;
    warm[17] = 1;
    warm[119] = 0x14;
    warm[125] = 0x10;
    warm[126] = 0x60;
    {
      SidTune silent(warm.data(), static_cast<uint32_t>(warm.size()));
      if (!engine.load(&silent))
        return 8;
      engine.initMixer(false);
      engine.load(nullptr);
    }
    std::unique_ptr<SidTune> tune;
    std::vector<short> pending(4096);
    unsigned first = 0, last = 0;
    put32(0x47545331);
    std::cout.flush();
    for (;;) {
      auto command = read32();
      if (command == 1) {
        auto size = read32();
        if (size > 65536 || size < 126)
          return 3;
        std::vector<uint8_t> bytes(size);
        std::cin.read(reinterpret_cast<char *>(bytes.data()), size);
        if (!std::cin)
          return 4;
        engine.load(nullptr);
        tune = std::make_unique<SidTune>(bytes.data(), size);
        tune->selectSong(1);
        if (!tune->getStatus() || !engine.load(tune.get()))
          return 5;
        engine.initMixer(false);
        first = last = 0;
        put32(1);
        std::cout.flush();
      } else if (command == 2) {
        auto count = read32();
        if (!tune || count > 4096)
          return 6;
        put32(count);
        while (count) {
          if (first == last) {
            auto n = engine.play(20000);
            if (n < 0)
              return 7;
            last = engine.mix(pending.data(), unsigned(n));
            first = 0;
          }
          auto n = std::min(count, last - first);
          for (unsigned i = 0; i < n; ++i) {
            uint16_t v = uint16_t(pending[first++]);
            std::cout.put(char(v));
            std::cout.put(char(v >> 8));
          }
          count -= n;
        }
        std::cout.flush();
      } else
        return 0;
    }
  } catch (...) {
    return 0;
  }
}
