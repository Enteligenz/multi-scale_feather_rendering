#pragma once

#include <lightwave/color.hpp>

#include <vector>

namespace lightwave
{

    struct ColorStop {
        Float position;
        Color color;
        ColorStop(Float position, const Color& color);
    };

    /// @brief Replicates Blender's Color Ramp.
    class ColorRamp {
        public:
            void addColorStop(Float position, const Color& color);
            Color getColorAt(Float position) const;

        private:
            std::vector<ColorStop> stops;
    };
    
} // namespace lightwave
