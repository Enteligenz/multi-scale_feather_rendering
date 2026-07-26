#include <lightwave.hpp>

namespace lightwave {

    /// @brief Test Material that fakes subsurface scattering.
class FakeSubsurfaceScattering : public Bsdf {
    ref<Texture> m_albedo;
    ref<Texture> m_translucency;

public:
    FakeSubsurfaceScattering(const Properties &properties) {
        m_albedo = properties.get<Texture>("albedo");
        m_translucency = properties.get<Texture>("translucency"); // probability that ray does not change sides (as usual diffuse)
    }

    Color albedo(const Point2 &uv) const override {
        return m_albedo->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        Float pdfDir = m_translucency->scalar(uv);
        Color evalColor = m_albedo->evaluate(uv);
        if (!Frame::sameHemisphere(wo, wi)) {
            pdfDir = Float(1.0) - pdfDir;
            evalColor = Color(1.0f);
        }
        const Float cosTerm = abs(Frame::cosTheta(wi));
        return {
            .value = float(cosTerm) * evalColor * float(InvPi),
            .pdf   = cosTerm * InvPi * pdfDir,
        };
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        Vector wi = squareToCosineHemisphere(rng.next2D()) * (Frame::cosTheta(wo) > Float(0.0) ? Float(+1.0) : Float(-1.0)); // wi same side
        Float pdfDir = m_translucency->scalar(uv);
        Color evalColor = m_albedo->evaluate(uv);
        if (rng.next() < (Float(1.0) - pdfDir)) {
            pdfDir = Float(1.0) - pdfDir;
            wi = -wi;
            evalColor = Color(1.0f);
        }
        return {
            .wi     = wi,
            .weight = evalColor,
            .pdf = Frame::cosTheta(wi) * InvPi * pdfDir,
        };
    }

    std::string toString() const override {
        return tfm::format("FakeSubsurfaceScattering[\n"
                           "  albedo = %s\n"
                           "]",
                           indent(m_albedo));
    }
};

} // namespace lightwave

REGISTER_BSDF(FakeSubsurfaceScattering, "fakesss")
