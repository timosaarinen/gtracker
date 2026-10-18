#include "gtracker/renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace gtracker {
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void checked(gt_result result) {
    if (result != GT_OK) throw std::runtime_error(std::string("Native renderer: ") + gt_result_string(result));
}
}
struct NativeRenderer::State {
    std::unique_ptr<gt_synth, decltype(&gt_destroy)> synth{nullptr, gt_destroy};
    std::unique_ptr<ProductionProcessor> production;
    std::array<float, 1024> dry{};
    std::uint64_t instrument_frames = 0, frames = 0, cursor = 0;
    int rate = 48000, channels = 1;
    State(const NativePerformance& performance, std::uint64_t dry_frames, const ProductionSettings& settings) {
        validate_production(settings);
        require(performance.instrument.sample_rate >= 8000 && performance.instrument.sample_rate <= 192000, "Native renderer rate must be in 8000..192000");
        require(dry_frames > 0 && dry_frames <= static_cast<std::uint64_t>(performance.instrument.sample_rate) * 86400,
                "Native render duration must be positive and bounded to one day");
        require(performance.events.size() <= 1000000 && performance.slides.size() <= 16384, "Native performance exceeds event/slide limits");
        rate = static_cast<int>(performance.instrument.sample_rate);
        instrument_frames = dry_frames;
        channels = settings.enabled ? 2 : 1;
        for (const auto& event : performance.events) require(event.frame < dry_frames, "Event is outside the instrument timeline");
        std::vector<gt_slide> slides;
        slides.reserve(performance.slides.size());
        for (const auto& source : performance.slides) {
            require(source.frame < dry_frames && !source.points.empty() && source.points.back().frame_offset <= dry_frames - source.frame,
                    "Slide is outside the instrument timeline");
            slides.push_back({source.frame, source.string_index, source.points.data(), source.points.size()});
        }
        gt_synth* raw = nullptr;
        checked(gt_create_model(&performance.instrument, performance.model, &raw));
        synth.reset(raw);
        const auto loaded = gt_set_performance(synth.get(), performance.events.data(), performance.events.size(), slides.data(), slides.size());
        require(loaded != GT_INVALID_ARGUMENT, "Performance curves/events conflict after sample rounding, exceed limits or are unsorted");
        checked(loaded);
        const auto tail = static_cast<std::uint64_t>(std::ceil(production_tail_seconds(settings) * rate));
        require(dry_frames <= std::numeric_limits<std::uint64_t>::max() - tail, "Native render duration overflow");
        frames = dry_frames + tail;
        if (settings.enabled) production = std::make_unique<ProductionProcessor>(rate, settings);
    }
};
NativeRenderer::NativeRenderer(const NativePerformance& p, std::uint64_t frames, const ProductionSettings& s) : state_(std::make_unique<State>(p, frames, s)) {}
NativeRenderer::~NativeRenderer() = default;
NativeRenderer::NativeRenderer(NativeRenderer&&) noexcept = default;
NativeRenderer& NativeRenderer::operator=(NativeRenderer&&) noexcept = default;
int NativeRenderer::sample_rate() const { require(static_cast<bool>(state_), "Moved renderer"); return state_->rate; }
int NativeRenderer::channels() const { require(static_cast<bool>(state_), "Moved renderer"); return state_->channels; }
std::uint64_t NativeRenderer::total_frames() const { require(static_cast<bool>(state_), "Moved renderer"); return state_->frames; }
std::uint64_t NativeRenderer::position() const { require(static_cast<bool>(state_), "Moved renderer"); return state_->cursor; }
void NativeRenderer::reset() {
    require(static_cast<bool>(state_), "Moved renderer");
    checked(gt_reset(state_->synth.get()));
    if (state_->production) state_->production->reset();
    state_->cursor = 0;
}
std::size_t NativeRenderer::read(float* output, std::size_t capacity) {
    require(static_cast<bool>(state_), "Moved renderer");
    require(capacity <= 65536, "Native renderer block request exceeds 65536 frames");
    auto& s = *state_;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(capacity, s.frames - s.cursor));
    require(count == 0 || output, "Native renderer needs output storage");
    for (std::size_t offset = 0; offset < count; offset += s.dry.size()) {
        const auto block = std::min(s.dry.size(), count - offset);
        const auto dry_count = s.cursor < s.instrument_frames
            ? static_cast<std::size_t>(std::min<std::uint64_t>(block, s.instrument_frames - s.cursor)) : 0;
        float* target = s.production ? s.dry.data() : output + offset;
        if (dry_count) checked(gt_render(s.synth.get(), target, dry_count));
        std::fill(target + dry_count, target + block, 0);
        if (s.production) s.production->process(target, output + offset * 2, block);
        s.cursor += block;
    }
    return count;
}
} // namespace gtracker
