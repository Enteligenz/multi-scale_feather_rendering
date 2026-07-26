#pragma once

#include <lightwave.hpp>

#include "fresnel.hpp"
#include "microfacet.hpp"

#include "../textures/constant.hpp"

namespace lightwave {

struct DiffuseLobe {
    Color color;

    BsdfEval evaluate(const Vector &wo, const Vector &wi) const {
        if (!Frame::sameHemisphere(wo, wi))
            return BsdfEval::invalid();
        const Float cosTerm = Frame::absCosTheta(wi);
        return {
            .value = color * float(cosTerm * InvPi),
            .pdf = cosTerm * InvPi,
        };
        // hints:
        // * copy your diffuse bsdf evaluate here
        // * you do not need to query a texture, the albedo is given by `color`
    }

    BsdfSample sample(const Vector &wo, Sampler &rng) const {
        const Vector wi = squareToCosineHemisphere(rng.next2D());
        return {
            .wi     = wi * (Frame::cosTheta(wi) > Float(0.0) ? Float(+1.0) : Float(-1.0)),
            .weight = color,
            .pdf = Frame::cosTheta(wi) * InvPi,
        };
        // hints:
        // * copy your diffuse bsdf evaluate here
        // * you do not need to query a texture, the albedo is given by `color`
    }
};

struct MetallicLobe {
    Float alpha;
    Color color;

    BsdfEval evaluate(const Vector &wo, const Vector &wi) const {
        const auto normal = (wi + wo).normalized();

        // VNDF PDF
        Float pdf = microfacet::pdfGGXVNDF(alpha, normal, wo);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        pdf *= microfacet::detReflection(normal, wo);

        const Float Gi =
            microfacet::anisotropicSmithG1(alpha, alpha, normal, wi);
        return {
            .value = color * float(Gi * pdf),
            .pdf = pdf,
        };
        // hints:
        // * copy your roughconductor bsdf evaluate here
        // * you do not need to query textures
        //   * the reflectance is given by `color'
        //   * the variable `alpha' is already provided for you
    }

    BsdfSample sample(const Vector &wo, Sampler &rng) const {
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
            .weight = color * Gi,
            .pdf = pdf,
        };
        // hints:
        // * copy your roughconductor bsdf sample here
        // * you do not need to query textures
        //   * the reflectance is given by `color'
        //   * the variable `alpha' is already provided for you
    }
};

class Principled : public Bsdf {
    ref<Texture> m_baseColor;
    ref<Texture> m_roughness;
    ref<Texture> m_metallic;
    ref<Texture> m_specular;

    struct Combination {
        Float diffuseSelectionProb;
        DiffuseLobe diffuse;
        MetallicLobe metallic;
    };

    Combination combine(const Point2 &uv, const Vector &wo) const {
        const auto baseColor = m_baseColor->evaluate(uv);
        const auto alpha = std::max(Float(1e-3), sqr(m_roughness->scalar(uv)));
        const auto specular = m_specular->scalar(uv);
        const auto metallic = m_metallic->scalar(uv);
        const auto F =
            specular * schlick((1 - metallic) * Float(0.08), Frame::cosTheta(wo));

        const DiffuseLobe diffuseLobe = {
            .color = (1 - F) * (1 - metallic) * baseColor,
        };
        const MetallicLobe metallicLobe = {
            .alpha = alpha,
            .color = F * Color(1) + (1 - F) * metallic * baseColor,
        };

        const Float diffuseAlbedo = diffuseLobe.color.mean();
        const Float totalAlbedo =
            diffuseLobe.color.mean() + metallicLobe.color.mean();
        return {
            .diffuseSelectionProb =
                totalAlbedo > 0 ? diffuseAlbedo / totalAlbedo : Float(1.0),
            .diffuse  = diffuseLobe,
            .metallic = metallicLobe,
        };
    }

public:
    Principled(const Properties &properties) {
        m_baseColor = properties.get<Texture>("baseColor");
        m_roughness = properties.get<Texture>("roughness");
        m_metallic  = properties.get<Texture>("metallic");
        m_specular  = properties.get<Texture>("specular");
    }

    /// @brief For testing in feathers.
    Principled(const Color &color) {
        m_baseColor = std::make_shared<ConstantTexture>(color);
        m_roughness = std::make_shared<ConstantTexture>(Color(0.5f));
        m_metallic  = std::make_shared<ConstantTexture>(Color(0.0f));
        m_specular  = std::make_shared<ConstantTexture>(Color(0.0f));
    }

    Color albedo(const Point2 &uv) const override {
        return m_baseColor->evaluate(uv);
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        PROFILE("Principled")

        const auto combination = combine(uv, wo);
        const auto diffuse  = combination.diffuse.evaluate(wo, wi);
        const auto metallic = combination.metallic.evaluate(wo, wi);
        const auto pdf = diffuse.pdf * combination.diffuseSelectionProb +
                         metallic.pdf * (1 - combination.diffuseSelectionProb);
        if (!(pdf > 0))
            return BsdfEval::invalid();
        return {
            .value = diffuse.value + metallic.value,
            .pdf = pdf,
        };
        // hint: evaluate `combination.diffuse` and `combination.metallic` and
        // combine their results
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        PROFILE("Principled")

        const auto combination = combine(uv, wo);
        const auto pickedDiffuse =
            rng.next() < combination.diffuseSelectionProb;
        const auto sample = pickedDiffuse
                                ? combination.diffuse.sample(wo, rng)
                                : combination.metallic.sample(wo, rng);
        if (sample.isInvalid())
            return sample;
        const auto diffuse  = combination.diffuse.evaluate(wo, sample.wi);
        const auto metallic = combination.metallic.evaluate(wo, sample.wi);
        const auto pdf      = diffuse.pdf * combination.diffuseSelectionProb +
                         metallic.pdf * (1 - combination.diffuseSelectionProb);
        if (!(pdf > 0))
            return BsdfSample::invalid();
        return {
            .wi     = sample.wi,
            .weight = (diffuse.value + metallic.value) / pdf,
            .pdf    = pdf,
        };
        // hint: sample either `combination.diffuse` (probability
        // `combination.diffuseSelectionProb`) or `combination.metallic`
    }

    std::string toString() const override {
        return tfm::format("Principled[\n"
                           "  baseColor = %s,\n"
                           "  roughness = %s,\n"
                           "  metallic  = %s,\n"
                           "  specular  = %s,\n"
                           "]",
                           indent(m_baseColor), indent(m_roughness),
                           indent(m_metallic), indent(m_specular));
    }
};

} // namespace lightwave