#include "fresnel.hpp"
#include "microfacet.hpp"
#include <lightwave.hpp>

// Based on paper: "A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence"
// https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html

namespace lightwave {

class IridescentMicrofacet : public Bsdf {
    ref<Texture> m_reflectance;
    ref<Texture> m_roughness;

    Float m_Dinc;
    Float m_eta2;
    Float m_eta3;
    Float m_kappa3;

private:
    /// @brief XYZ to CIE 1931 RGB color space (using neutral E illuminant)
    const Matrix3x3 XYZ_TO_RGB = Matrix3x3 {2.3706743, -0.5138850, 0.0052982, -0.9000405, 1.4253036, -0.0146949, -0.4706338, 0.0885814, 1.0093968 }.transpose();

    /// @brief Depolarization function for natural light.
    Float depol(const Vector2 &polV) const { return Float(0.5f) * (polV.x() + polV.y()); }
    /// @brief Depolarization function for natural light.
    Vector depolColor(const Vector &colS, const Vector &colP) const { return Float(0.5f) * (colS + colP); }

    /**
     * Evaluation XYZ sensitivity curves in Fourier space.
     */
    Vector evalSensitivity(Float opd, Float shift) const {
        // Use Gaussian fits, given by 3 parameters: val, pos and var
        Float phase = Float(2.0f) * Pi * opd * Float(1.0e-6f);
        Vector val = Vector(Float(5.4856e-13), Float(4.4201e-13), Float(5.2481e-13));
        Vector pos = Vector(Float(1.6810e+06), Float(1.7953e+06), Float(2.2084e+06));
        Vector var = Vector(Float(4.3278e+09), Float(9.3046e+09), Float(6.6121e+09));
        Vector xyz = val * sqrt(Float(2.0f) * Pi * var) * cos(pos * phase + Vector(shift)) * exp(-var * phase * phase);
        xyz.x()   += Float(9.7470e-14) * sqrt(Float(2.0f) * Pi * Float(4.5282e+09)) * std::cos(Float(2.2399e+06) * phase + shift) * std::exp(Float(-4.5282e+09) * phase * phase);
        return xyz / Float(1.0685e-7);
    }

    /**
     * Computes the I term for evaluate() and sample().
     * @param L Corresponds to wi.
     * @param V Corresponds to wo.
     * @param N Corresponds to normal.
     */
    Vector computeIridescenceTerm(const Vector &L, const Vector &V, const Vector &N) const {
        // Force eta_2 -> 1.0 when Dinc -> 0.0
        float eta_2 = std::lerp(Float(1.0f), m_eta2, smoothstep(Float(0.0f), Float(0.03f), m_Dinc));
        
        // Compute dot products
        Float NdotL = N.dot(L);
        Float NdotV = N.dot(V);
        if (NdotL < Float(0.0f) || NdotV < Float(0.0f)) return Vector(Float(0.0f));
        Vector H = (L + V).normalized();
        Float NdotH = N.dot(H);
        float cosTheta1 = H.dot(L);
        float cosTheta2 = sqrt(Float(1.0f) - sqr(Float(1.0f) / eta_2) * (Float(1.0f) - sqr(cosTheta1)));
        
        // First interface
        Vector2 R12, phi12;
        fresnelDielectric(cosTheta1, Float(1.0f), eta_2, R12, phi12);
        Vector2 T121  = Vector2(Float(1.0f)) - R12;
        Vector2 phi21 = Vector2(Pi) - phi12;

        // Second interface
        Vector2 R23, phi23;
        fresnelConductor(cosTheta2, eta_2, m_eta3, m_kappa3, R23, phi23);

        // Phase shift
        Float OPD = m_Dinc * cosTheta2;
        Vector2 phi2 = phi21 + phi23;

        // Compound terms
        Vector I = Vector(Float(0.0f));
        Vector2 R123 = R12 * R23;
        Vector2 r123 = sqrt(R123);
        Vector2 Rs   = sqr(T121) * R23 / (Vector2(Float(1.0f)) - R123);

        // Reflectance term for m=0 (DC term amplitude)
        Vector2 C0 = R12 + Rs;
        Vector S0  = evalSensitivity(Float(0.0f), Float(0.0f));
        I += depol(C0) * S0;

        // Reflectance term for m>0 (pairs of diracs)
        Vector2 Cm = Rs - T121;
        for (int m = 1; m <= 3; ++m) {
            Cm *= r123;
            Vector SmS = Float(2.0f) * evalSensitivity(m * OPD, m * phi2.x());
            Vector SmP = Float(2.0f) * evalSensitivity(m * OPD, m * phi2.y());
            I += depolColor(Cm.x() * SmS, Cm.y() * SmP);
        }

        // Convert back to RGB reflectance
        I = clamp(XYZ_TO_RGB * I, Vector(Float(0.0f)), Vector(Float(1.0f)));
        return I;
    }

    // -----------------------------------------------------
    // GGX distribution function
    Float GGX(Float NdotH, Float a) const {
        Float a2 = sqr(a);
        return a2 / (Pi * sqr(sqr(NdotH) * (a2 - 1) + 1));
    }

    // Smith GGX geometric functions
    Float smithG1_GGX(Float NdotV, Float a) const {
        Float a2 = sqr(a);
        return 2 / (1 + sqrt(1 + a2 * (1 - sqr(NdotV)) / sqr(NdotV)));
    }
    
    Float smithG_GGX(Float NdotL, Float NdotV, Float a) const {
        return smithG1_GGX(NdotL, a) * smithG1_GGX(NdotV, a);
    }
    // -----------------------------------------------------

public:
    IridescentMicrofacet(const Properties &properties) {
        m_reflectance = properties.get<Texture>("reflectance");
        m_roughness   = properties.get<Texture>("roughness");   // min: 0.01, max: 1.0 (this is alpha)

        m_Dinc   = Float(properties.get<float>("dinc", 0.5f));   // min: 0.0,  max: 10.0
        m_eta2   = Float(properties.get<float>("eta2", 2.0f));   // min: 1.0,  max: 5.0
        m_eta3   = Float(properties.get<float>("eta3", 3.0f));   // min: 1.0,  max: 5.0
        m_kappa3 = Float(properties.get<float>("kappa3", 0.0f)); // min: 0.0,  max: 5.0
    }

    Color albedo(const Point2 &uv) const override {
        return m_reflectance->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        // Using the squared roughness parameter results in a more gradual
        // transition from specular to rough. For numerical stability, we avoid
        // extremely specular distributions (alpha values below 10^-3)
        const auto m_alpha = std::max(Float(1e-3), m_roughness->scalar(uv));

        const auto normal = (wi + wo).normalized();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(m_alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        pdf *= microfacet::detReflection(normal, wo); // This is D

        const Float Gi =
            microfacet::anisotropicSmithG1(m_alpha, m_alpha, normal, wi);

        const Vector I = computeIridescenceTerm(wi, wo, Vector(0.0f, 0.0f, 1.0f)); // (L, V, N); L should be wi, V should be wo

        // TODO maybe test using their microfacet code instead of ours
        // Compute dot products
        // Float NdotL = normal.dot(wi);
        // Float NdotV = normal.dot(wo);
        // if (NdotL < Float(0.0f) || NdotV < Float(0.0f))
        //     return BsdfEval::invalid();
        // Vector H = (wi + wo).normalized();
        // Float NdotH = normal.dot(H);

        // Float pdf = GGX(NdotH, m_alpha);
	    // Float Gi = smithG_GGX(NdotL, NdotV, m_alpha);
	    // return D*G*I / (4*NdotL*NdotV); /////

        return {
            .value = m_reflectance->evaluate(uv) * float(Gi * pdf) * Color(I),
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

        const auto m_alpha = std::max(Float(1e-3), m_roughness->scalar(uv));

        const auto normal = (wi + wo).normalized();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(m_alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        pdf *= microfacet::detReflection(normal, wo); // D

        const Float Gi =
            microfacet::anisotropicSmithG1(m_alpha, m_alpha, normal, wi); // G

        const Vector I = computeIridescenceTerm(wi, wo, Vector(0.0f, 0.0f, 1.0f)); // (L, V, N); L should be wi, V should be wo

        return {
            .value = m_reflectance->evaluate(uv) * float(Gi * pdf) * Color(I),
            .pdf   = pdf,
        };
        // hints:
        // * the microfacet normal can be computed from `wi' and `wo'
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        const auto m_alpha = std::max(Float(1e-3), m_roughness->scalar(uv));

        const Vector normal =
            microfacet::sampleGGXVNDF(m_alpha, wo, rng.next2D());

        const Vector wi = reflect(wo, normal);
        if (!Frame::sameHemisphere(wi, wo))
            return BsdfSample::invalid();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(m_alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfSample::invalid();
        pdf *= microfacet::detReflection(normal, wo);

        const Float Gi = microfacet::smithG1(m_alpha, normal, wi);

        const Vector I = computeIridescenceTerm(wi, wo, Vector(0.0f, 0.0f, 1.0f)); // (L, V, N); L should be wi, V should be wo

        return {
            .wi     = wi,
            .weight = m_reflectance->evaluate(uv) * Gi * Color(I),
            .pdf = pdf,
        };
        // hints:
        // * do not forget to cancel out as many terms from your equations as possible!
        //   (the resulting sample weight is only a product of two factors)
    }

    std::string toString() const override {
        return tfm::format("IridescentMicrofacet[\n"
                           "  reflectance = %s,\n"
                           "  roughness = %s,\n"
                           "  Dinc = %s,\n"
                           "  eta2 = %s,\n"
                           "  eta3 = %s,\n"
                           "  kappa3 = %s\n"
                           "]",
                           indent(m_reflectance), indent(m_roughness), indent(m_Dinc), indent(m_eta2),
                           indent(m_eta3), indent(m_kappa3));
    }
};

} // namespace lightwave

REGISTER_BSDF(IridescentMicrofacet, "iridescentmicrofacet")
