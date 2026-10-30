// GPL-2.0-or-later. Shared realtime/offline emulator configuration.
#pragma once
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/sidplayfp.h>
#include <stdexcept>
inline void configure_sid(sidplayfp &engine, ReSIDfpBuilder &builder) {
  builder.filter6581Curve(0.5);
  builder.filter6581Range(0.5);
  builder.enableOld6581caps(false);
  builder.offset6581(0);
  builder.dacLeakage(1);
  builder.combinedWaveformsStrength(SidConfig::AVERAGE);
  auto config = engine.config();
  config.frequency = 48000;
  config.defaultC64Model = SidConfig::PAL;
  config.forceC64Model = true;
  config.defaultSidModel = SidConfig::MOS6581;
  config.forceSidModel = true;
  config.powerOnDelay = 0;
  config.sidEmulation = &builder;
  config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
  if (!engine.config(config))
    throw std::runtime_error(engine.error());
}
