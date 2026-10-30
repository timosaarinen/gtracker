// Standalone GPL-2.0-or-later renderer adapter, distributed separately from gtracker.
// Uses libsidplayfp/reSIDfp; no synthesizer is reimplemented here.
#include "sid_configuration.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/sidplayfp.h>
#include <stdexcept>
#include <vector>
void le(std::ostream &out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>(value >> (8 * i)));
}
int main(int argc, char **argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("gtracker_sidrender SID WAV FRAMES [SOLO_VOICE_1_TO_3]");
        const int solo = argc == 5 ? std::stoi(argv[4]) : 0;
        if (argc == 5 && (solo < 1 || solo > 3))
            throw std::runtime_error("Solo voice must be 1, 2 or 3");
        const auto frames = std::stoul(argv[3]);
        if (frames > 32 * 1024 * 1024 || !frames)
            throw std::runtime_error("Render sample bound exceeded");
        SidTune tune(argv[1]);
        if (!tune.getStatus())
            throw std::runtime_error(tune.statusString());
        tune.selectSong(1);
        ReSIDfpBuilder builder("gtracker pinned reSIDfp");
        sidplayfp engine;
        configure_sid(engine,builder);
        if(!engine.load(&tune))throw std::runtime_error(engine.error());
        engine.initMixer(false);
        // Muting only changes output routing; all oscillators keep clocking.
        if (solo)
            for (unsigned voice = 0; voice < 3; ++voice)
                engine.mute(0, voice, voice + 1 != static_cast<unsigned>(solo));
        std::ofstream out(argv[2], std::ios::binary);
        out.write("RIFF", 4);
        le(out, 36 + frames * 2, 4);
        out.write("WAVEfmt ", 8);
        le(out, 16, 4);
        le(out, 1, 2);
        le(out, 1, 2);
        le(out, 48000, 4);
        le(out, 96000, 4);
        le(out, 2, 2);
        le(out, 16, 2);
        out.write("data", 4);
        le(out, frames * 2, 4);
        std::vector<short> buffer(static_cast<std::size_t>(engine.getBufSize(20000)) / sizeof(short) + 1024);
        unsigned long written = 0;
        while (written < frames) {
            const int samples = engine.play(20000);
            if (samples < 0)
                throw std::runtime_error(engine.error());
            const auto count = engine.mix(buffer.data(), static_cast<unsigned>(samples));
            const auto n = std::min<unsigned long>(count, frames - written);
            for (unsigned long i = 0; i < n; ++i)
                le(out, static_cast<std::uint16_t>(buffer[i]), 2);
            written += n;
        }
        out.close();
        if (!out)
            throw std::runtime_error("WAV write failed");
        engine.load(nullptr);
        std::cout << "libsidplayfp 3.1.1 / reSIDfp 1.2.3 deterministic serial tables; frames=" << frames
                  << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
