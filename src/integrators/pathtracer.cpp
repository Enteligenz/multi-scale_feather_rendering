#include <lightwave.hpp>

namespace lightwave {

class PathTracer : public SamplingIntegrator {
    int m_maxDepth;
    bool m_nee;
    bool m_mis;

    Float miWeight(Float a, Float b) const {
        if (std::isinf(a))
            return 1;
        return a / (a + b);
    }

    Color nextEventEstimation(const Intersection &its, Payload &p, Sampler &rng) const {
        PROFILE("NEE")
        p.isForNEE = true;

        auto lightSample = m_scene->sampleLight(rng);
        if (!lightSample)
            return Color(0);

        auto directSample = lightSample.light->sampleDirect(its.position, rng);
        if (!directSample || m_scene->intersect(
                Ray(its.position, directSample.wi), p, directSample.distance, rng))
            // occluded
            return Color(0);

        auto B = its.evaluateBsdf(directSample.wi, !p.currentComingFromHD);
        const Float misWeight =
            m_mis && lightSample.light->canBeIntersected()
                ? miWeight(lightSample.probability * directSample.pdf, B.pdf)
                : 1;
        return B.value * misWeight * directSample.weight /
               lightSample.probability;
    }

public:
    PathTracer(const Properties &properties) : SamplingIntegrator(properties) {
        m_maxDepth = properties.get<int>("depth", 2);
        m_nee      = properties.get<bool>("nee", true);
        m_mis      = properties.get<bool>("mis", false);
    }

    Color Li(const Ray &cameraRay, Sampler &rng) override {
        PROFILE("Li")

        auto L           = Color(0);
        auto throughput  = Color(1);
        auto ray         = cameraRay;
        auto prevBsdfPdf = Infinity;
        Payload p;
        p.camera = m_scene->camera();
        
        // int depth;
        for (int depth = 1;; depth++) {
            p.depth = depth;
            auto its = m_scene->intersect(ray, p, rng);
            p.isComingFromHD = p.currentComingFromHD;
            p.lastFeatherID = p.currentFeatherID;
            p.lastObjID = p.currentObjID;
            if (auto E = its.evaluateEmission()) {
                p.lightPDF = E.generalPDF;
                const auto misWeight =
                    std::isinf(prevBsdfPdf) || !m_nee || (E.pdf == 0) ? 1
                    : m_mis ? miWeight(prevBsdfPdf, E.pdf)
                            : 0;
                L += throughput * misWeight * E.value;
            }

            if (!its || (depth >= m_maxDepth))
                break;

            if (m_nee) {
                Payload pCopy = p;
                // logger(EWarn, "depth %i | pdf: %f, traveledDistance: %f, combination: %f", pCopy.depth, pCopy.pdf, p.traveledDistance, pCopy.pdf / (pCopy.traveledDistance + Epsilon));
                // Note for above print: product is 100000000.000000 if pdf=1 and traveledDistance=0
                L += throughput * nextEventEstimation(its, pCopy, rng);
            }

            auto B = its.sampleBsdf(rng, !p.currentComingFromHD);
            if (!B)
                break;

            throughput *= B.weight;
            prevBsdfPdf = B.pdf;
            ray = Ray(its.position, B.wi);

            // Update LOD information
            p.pdf = B.pdf;
            p.traveledDistance += its.t;
            p.totalDistance += its.t;
            if (B.pdf < Float(10)) {
                p.totalDistance = Infinity;
                p.wasRough = true;
            }
            p.lastFootprint = m_scene->camera()->computePixelFootprint(p.traveledDistance);
        }

        return L;
    }

    std::string toString() const override {
        return tfm::format(
            "PathTracer[\n"
            "  sampler = %s,\n"
            "  image = %s,\n"
            "]",
            indent(m_sampler),
            indent(m_image));
    }
};

} // namespace lightwave

REGISTER_INTEGRATOR(PathTracer, "pathtracer")
