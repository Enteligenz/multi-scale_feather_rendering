//#include "colorramp.hpp"
#include <lightwave/colorramp.hpp>

#include <algorithm>

namespace lightwave
{

    ColorStop::ColorStop(Float position, const Color& color)
        :position(position), color(color) {}

    void ColorRamp::addColorStop(Float position, const Color& color) {
        stops.emplace_back(position, color);
        std::sort(stops.begin(), stops.end(), [](const ColorStop& a, const ColorStop& b) {
            return a.position < b.position;
        });
    }

    Color ColorRamp::getColorAt(Float position) const {
        // if (stops.empty()) {
        //     // TODO
        // }

        // Clamp position
        if (position <= stops.front().position) return stops.front().color;
        if (position >= stops.back().position) return stops.back().color;

        for (size_t i = 0; i < stops.size() - 1; ++i) {
            const ColorStop& a = stops[i];
            const ColorStop& b = stops[i + 1];
            if (position >= a.position && position <= b.position) {
                Float t = (position - a.position) / (b.position - a.position);
                return lerp(a.color, b.color, t);
            }
        }

        return stops.back().color; // should never reach here, but just in case
    }
    
} // namespace lightwave
