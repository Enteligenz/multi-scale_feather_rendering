#include <lightwave.hpp>

namespace lightwave {

class PointLight final : public Light {
    Point m_position;
    Color m_intensity;

public:
    PointLight(const Properties &properties)
    : Light(properties) {
        m_position  = properties.get<Point>("position");
        m_intensity = properties.get<Color>("power") / (4 * Pi);
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        auto [distance, wi] = (m_position - origin).lengthAndNormalized();
        return {
            .wi     = wi,
            .weight = m_intensity / sqr(distance),
            .pdf = Infinity,
            .distance = distance,
        };
    }

    bool canBeIntersected() const override { return false; }

    std::string toString() const override {
        return tfm::format("PointLight[\n"
                           "]");
    }
};

} // namespace lightwave

REGISTER_LIGHT(PointLight, "point")
