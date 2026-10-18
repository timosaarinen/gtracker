#include "gtracker/timeline.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        gtracker::BeatViewport view;
        view.zoom(0.5, 2, -1.5, 11);
        check(view.first == 1 && view.last == 5, "zoom must retain the anchor's pixel fraction");
        view.pan(-100, -1.5, 11);
        check(view.first == -1.5 && view.last == 2.5, "pre-roll pan bound");
        view.pan(100, -1.5, 11);
        check(view.first == 7 && view.last == 11, "retained tail pan bound");
        view.zoom(1e10, 8, -1.5, 11);
        check(view.first == -1.5 && view.last == 11, "zoom-out must fit preserved context");
        view.zoom(1e-10, 0, -1.5, 11);
        check(std::abs(view.span() - 1.0 / 64) < 1e-12, "zoom-in bound");
        const auto before = view;
        bool rejected = false;
        try { view.zoom(std::numeric_limits<double>::quiet_NaN(), 0, -1.5, 11); } catch (const std::exception&) { rejected = true; }
        check(rejected && view.first == before.first && view.last == before.last, "bad zoom changed the view");
        view.fit(0, 0.00001); view.constrain(0, 0.00001);
        check(view.first == 0 && view.last == 0.00001, "short valid patterns must still fit");
        for (float width : {400.0f, 790.0f, 1200.0f}) {
            gtracker::TimelineGeometry geometry{{-1.5, 11}, 84, 84 + width};
            for (double beat : {-1.5, 0.0, 0.333333333333, 4.0, 8.0, 11.0})
                check(std::abs(geometry.x_beat(geometry.beat_x(beat)) - beat) < 2e-6, "shared beat/pixel mapping lost alignment");
        }
        std::cout << "timeline: anchored zoom, bounded pan, context and shared beat/pixel mapping passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
