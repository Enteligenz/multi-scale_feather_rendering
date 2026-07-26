#include <lightwave.hpp>
#pragma once

namespace lightwave {

class Checkerboard : public Texture {
    Vector2 m_scale;
    Color m_color0;
    Color m_color1;

public:
    Checkerboard(const Properties &properties) {
        m_scale  = properties.get<Vector2>("scale");
        m_color0 = properties.get<Color>("color0", Color(0));
        m_color1 = properties.get<Color>("color1", Color(1));
    }

    /// @brief Test for use in feathers.
    Checkerboard() {
        m_scale  = Vector2(64);
        m_color0 = Color(0);
        m_color1 = Color(1);
    }

    Color evaluate(const Point2 &uv) const override {
        const auto p = Vector2(uv) * m_scale;
        return ((int(p.x()) ^ int(p.y())) & 1) ? m_color1 : m_color0;
    }

    std::string toString() const override {
        return tfm::format("Checkerboard[\n"
                           "]");
    }
};

} // namespace lightwave


REGISTER_TEXTURE(Checkerboard, "checkerboard")
