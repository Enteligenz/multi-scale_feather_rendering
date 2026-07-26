#include <lightwave.hpp>

namespace lightwave {

/// @brief Test Material that samples each direction equally.
class IsotropicBsdf : public Bsdf {
    ref<Texture> m_albedo;

public:
    IsotropicBsdf(const Properties &properties) {
        m_albedo = properties.get<Texture>("albedo");
    }

    Color albedo(const Point2 &uv) const override {
        return m_albedo->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        return {
            .value = m_albedo->evaluate(uv) * float(Float(1.0) / (Float(4.0) * Pi)),
            .pdf   = Float(1.0) / (Float(4.0) * Pi),
        };
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        const Vector wi = squareToUniformSphere(rng.next2D());
        return {
            .wi     = wi,
            .weight = m_albedo->evaluate(uv),
            .pdf = Float(1.0) / (Float(4.0) * Pi),
        };
    }

    std::string toString() const override {
        return tfm::format("IsotropicBsdf[\n"
                           "  albedo = %s\n"
                           "]",
                           indent(m_albedo));
    }
};

} // namespace lightwave

REGISTER_BSDF(IsotropicBsdf, "isotropicbsdf")
