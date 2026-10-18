#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gtracker {
// View state only: never changes musical timing, source samples or undo history.
struct BeatViewport {
    double first = 0;
    double last = 8;
    double span() const { return last - first; }
    void fit(double begin, double end) {
        if (!std::isfinite(begin) || !std::isfinite(end) || !std::isfinite(end - begin) || end <= begin)
            throw std::invalid_argument("Timeline bounds must be finite and increasing");
        first = begin; last = end;
    }
    void constrain(double begin, double end) {
        if (!std::isfinite(begin) || !std::isfinite(end) || !std::isfinite(end - begin) || end <= begin)
            throw std::invalid_argument("Timeline extent must be finite and increasing");
        const double width = std::clamp(span(), std::min(1.0 / 64, end - begin), end - begin);
        first = std::clamp(first, begin, end - width);
        last = first + width;
    }
    void zoom(double factor, double anchor, double begin, double end) {
        if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(anchor))
            throw std::invalid_argument("Timeline zoom must be finite and positive");
        constrain(begin, end);
        anchor = std::clamp(anchor, first, last);
        const double position = std::clamp((anchor - first) / span(), 0.0, 1.0);
        const double width = std::clamp(span() * factor, std::min(1.0 / 64, end - begin), end - begin);
        first = anchor - position * width;
        last = first + width;
        constrain(begin, end);
    }
    void pan(double beats, double begin, double end) {
        if (!std::isfinite(beats)) throw std::invalid_argument("Timeline pan must be finite");
        constrain(begin, end);
        const double width = span();
        first = std::clamp(first + beats, begin, end - width);
        last = first + width;
    }
};

struct TimelineGeometry {
    BeatViewport view;
    float left = 0;
    float right = 1;
    float beat_x(double beat) const {
        return static_cast<float>(left + (beat - view.first) / view.span() * (static_cast<double>(right) - left));
    }
    double x_beat(float x) const {
        return view.first + (static_cast<double>(x) - left) / (static_cast<double>(right) - left) * view.span();
    }
};
} // namespace gtracker
