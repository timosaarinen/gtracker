#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace gtracker {
// Artistic production, separate from instrument/performance parameters.
struct ProductionSettings {
    int version = 1;
    std::string algorithm = "room_v1";
    bool enabled = false;
    float wet = 0.18f;
    float decay_seconds = 1.2f; // Low-frequency feedback target T60, not measured room dimensions.
    float damping = 0.35f;
    float predelay_ms = 18;
    float width = 0.9f;
    float output_gain_db = 0;
};
void validate_production(const ProductionSettings& settings);
double production_tail_seconds(const ProductionSettings& settings);

// One persistent mono-in/stereo-out chain. No allocation in process(). Keep an
// instance across musical boundaries; reset() is an explicit transport restart.
class ProductionProcessor {
public:
    ProductionProcessor(int sample_rate, const ProductionSettings& settings);
    ~ProductionProcessor();
    ProductionProcessor(ProductionProcessor&&) noexcept;
    ProductionProcessor& operator=(ProductionProcessor&&) noexcept;
    ProductionProcessor(const ProductionProcessor&) = delete;
    ProductionProcessor& operator=(const ProductionProcessor&) = delete;
    void reset();
    void process(const float* mono, float* stereo, std::size_t frames);
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace gtracker
