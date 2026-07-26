#include <lightwave.hpp>

namespace lightwave {

class AreaLight final : public Light {
    ref<Instance> m_instance;

public:
    AreaLight(const Properties &properties)
    : Light(properties) {
        m_instance = properties.getChild<Instance>();
        m_instance->setLight(this);
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        auto areaSample = m_instance->sampleArea(rng);
        auto [distance, wi] =
            (areaSample.position - origin).lengthAndNormalized();
        auto absCosTheta = abs(areaSample.geometryNormal.dot(wi));
        auto invDet      = sqr(distance) / absCosTheta;
        auto pdf         = areaSample.pdf * invDet;
        auto E           = m_instance->emission()->evaluate(
            areaSample.uv, areaSample.shadingFrame().toLocal(-wi));
        return {
            .wi     = wi,
            .weight = E.value / pdf,
            .pdf = pdf,
            .distance = distance,
        };
    }

    bool canBeIntersected() const override { return m_instance->isVisible(); }

    std::string toString() const override {
        return tfm::format("AreaLight[\n"
                           "  instance = %s\n"
                           "]",
                           indent(m_instance));
    }
};

} // namespace lightwave

REGISTER_LIGHT(AreaLight, "area")
