#pragma once

#include "gtracker/core.h"
#include "gtracker/production.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gtracker {
// Native runtime inputs. Authoring layers compile beat positions into these
// absolute frames; this in-memory structure is not a serialization format.
struct ScheduledSlide {
    std::uint64_t frame = 0;
    std::uint32_t string_index = 0;
    std::vector<gt_fret_point> points;
};
struct NativePerformance {
    gt_config instrument = gt_default_config();
    gt_model model = GT_MODEL_BASELINE_0_1;
    std::vector<gt_event> events;
    std::vector<ScheduledSlide> slides;
};

// A dependency-free continuous instrument + optional production renderer.
// Input schedules are copied by the core during construction; blocks allocate
// no memory. Keep this instance alive across pattern/loop boundaries.
class NativeRenderer {
public:
    NativeRenderer(const NativePerformance& performance, std::uint64_t instrument_frames,
                   const ProductionSettings& production = {});
    ~NativeRenderer();
    NativeRenderer(NativeRenderer&&) noexcept;
    NativeRenderer& operator=(NativeRenderer&&) noexcept;
    NativeRenderer(const NativeRenderer&) = delete;
    NativeRenderer& operator=(const NativeRenderer&) = delete;
    int sample_rate() const;
    int channels() const;
    std::uint64_t total_frames() const;
    std::uint64_t position() const;
    // Writes at most capacity_frames complete interleaved frames; returns zero
    // at EOF. A request is limited to 65,536 frames. Null is valid for zero work.
    std::size_t read(float* output, std::size_t capacity_frames);
    void reset();
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace gtracker
