#include <lightwave.hpp>

namespace lightwave {

class Diffuse : public Bsdf {
    ref<Texture> m_albedo;

public:
    Diffuse(const Properties &properties) {
        m_albedo = properties.get<Texture>("albedo");
    }

    Color albedo(const Point2 &uv) const override {
        return m_albedo->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        if (!Frame::sameHemisphere(wo, wi))
            return BsdfEval::invalid();
        const Float cosTerm = Frame::absCosTheta(wi);
        return {
            .value = float(cosTerm) * m_albedo->evaluate(uv) * float(InvPi),
            .pdf   = cosTerm * InvPi,
        };
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        const Vector wi = squareToCosineHemisphere(rng.next2D());
        return {
            .wi     = wi * (Frame::cosTheta(wo) > Float(0.0) ? Float(+1.0) : Float(-1.0)),
            .weight = m_albedo->evaluate(uv),
            .pdf = Frame::cosTheta(wi) * InvPi,
        };
    }

    std::string toString() const override {
        return tfm::format("Diffuse[\n"
                           "  albedo = %s\n"
                           "]",
                           indent(m_albedo));
    }
};

} // namespace lightwave

REGISTER_BSDF(Diffuse, "diffuse")
