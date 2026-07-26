#include <lightwave.hpp>

namespace lightwave {

class DirectLightIntegrator : public SamplingIntegrator {
public:
    DirectLightIntegrator(const Properties &properties)
        : SamplingIntegrator(properties) {}

    Color Li(const Ray &ray, Sampler &rng) override {
        Payload p;
        Intersection its = m_scene->intersect(ray, p, rng);
        auto L = Color(0);
        L += its.evaluateEmission().value;
        if (!its) return L;

        if (const auto lightSample = m_scene->sampleLight(rng)) {
            if (!lightSample.light->canBeIntersected()) {
                const auto directSample =
                    lightSample.light->sampleDirect(its.position, rng);

                Ray shadowRay;
                shadowRay.origin    = its.position;
                shadowRay.direction = directSample.wi;
                if (!m_scene->intersect(shadowRay, p, directSample.distance,
                                        rng)) { // unoccluded
                    auto B = its.evaluateBsdf(shadowRay.direction, !p.currentComingFromHD);
                    L +=
                        B.value * directSample.weight / lightSample.probability;
                }
            }
        }

        if (auto B = its.sampleBsdf(rng, !p.currentComingFromHD)) {
            auto its2 = m_scene->intersect(Ray(its.position, B.wi), p, rng);
            L += B.weight * its2.evaluateEmission().value;
        } else {
            // bsdf sampling failed, L is unchanged
        }

        return L;
    }

    std::string toString() const override {
        return tfm::format("DirectLightIntegrator[\n"
                           "  sampler = %s,\n"
                           "  image = %s,\n"
                           "]",
                           indent(m_sampler), indent(m_image));
    }
};

} // namespace lightwave

REGISTER_INTEGRATOR(DirectLightIntegrator, "direct")
