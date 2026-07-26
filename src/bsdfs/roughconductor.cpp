#include "fresnel.hpp"
#include "microfacet.hpp"
#include <lightwave.hpp>

namespace lightwave {

class RoughConductor : public Bsdf {
    ref<Texture> m_reflectance;
    ref<Texture> m_roughness;

public:
    RoughConductor(const Properties &properties) {
        m_reflectance = properties.get<Texture>("reflectance");
        m_roughness   = properties.get<Texture>("roughness");
    }

    Color albedo(const Point2 &uv) const override {
        return m_reflectance->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        // Using the squared roughness parameter results in a more gradual
        // transition from specular to rough. For numerical stability, we avoid
        // extremely specular distributions (alpha values below 10^-3)
        const auto alpha = std::max(Float(1e-3), sqr(m_roughness->scalar(uv)));

        const auto normal = (wi + wo).normalized();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        pdf *= microfacet::detReflection(normal, wo);

        const Float Gi =
            microfacet::anisotropicSmithG1(alpha, alpha, normal, wi);
        return {
            .value = m_reflectance->evaluate(uv) * float(Gi * pdf),
            .pdf   = pdf,
        };
        // hints:
        // * the microfacet normal can be computed from `wi' and `wo'
    }

    BsdfEval evaluate(const Vector &wo, const Vector &wi) const override {
        // Using the squared roughness parameter results in a more gradual
        // transition from specular to rough. For numerical stability, we avoid
        // extremely specular distributions (alpha values below 10^-3)
        Point2 uv = Point2(Float(0.0), Float(0.0));

        const auto alpha = std::max(Float(1e-3), sqr(m_roughness->scalar(uv)));

        const auto normal = (wi + wo).normalized();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        pdf *= microfacet::detReflection(normal, wo);

        const Float Gi =
            microfacet::anisotropicSmithG1(alpha, alpha, normal, wi);
        return {
            .value = m_reflectance->evaluate(uv) * float(Gi * pdf),
            .pdf   = pdf,
        };
        // hints:
        // * the microfacet normal can be computed from `wi' and `wo'
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        const auto alpha = std::max(Float(1e-3), sqr(m_roughness->scalar(uv)));

        const Vector normal =
            microfacet::sampleGGXVNDF(alpha, wo, rng.next2D());

        const Vector wi = reflect(wo, normal);
        if (!Frame::sameHemisphere(wi, wo))
            return BsdfSample::invalid();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfSample::invalid();
        pdf *= microfacet::detReflection(normal, wo);

        const Float Gi = microfacet::smithG1(alpha, normal, wi);
        return {
            .wi     = wi,
            .weight = m_reflectance->evaluate(uv) * Gi,
            .pdf = pdf,
        };
        // hints:
        // * do not forget to cancel out as many terms from your equations as possible!
        //   (the resulting sample weight is only a product of two factors)
    }

    std::string toString() const override {
        return tfm::format("RoughConductor[\n"
                           "  reflectance = %s,\n"
                           "  roughness = %s\n"
                           "]",
                           indent(m_reflectance), indent(m_roughness));
    }
};

} // namespace lightwave

REGISTER_BSDF(RoughConductor, "roughconductor")
