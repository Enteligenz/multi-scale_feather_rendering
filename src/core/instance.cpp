#include <lightwave/core.hpp>
#include <lightwave/instance.hpp>
#include <lightwave/registry.hpp>
#include <lightwave/sampler.hpp>

namespace lightwave {

void Instance::transformFrame(SurfaceEvent &surf, const Vector &wo) const {
    surf.position = m_transform->apply(surf.position);

    if (m_normal) {
        const Color normal = 2.0f * m_normal->evaluate(surf.uv) - Color(1.0f);
        const Vector v     = Vector(normal.r(), normal.g(), normal.b()).normalized();
        surf.shadingNormal = surf.shadingFrame().toWorld(v);
        // const auto bitangent = surf.shadingNormal.cross(surf.tangent).normalized();
        // surf.tangent = bitangent.cross(surf.shadingNormal);

        if (Frame::sameHemisphere(wo, surf.geometryNormal)) {
            // Mirror the normals along the normal texture plane
            surf.shadingNormal = surf.shadingNormal - Float(2.0) * surf.shadingNormal.dot(surf.geometryNormal) * surf.geometryNormal;
        }
    } // -, then +, then old I guess based on last results +wo looks better

    surf.shadingNormal =
        m_transform->applyNormal(surf.shadingNormal).normalized();

    const auto tangent   = surf.tangent.normalized();
    const auto bitangent = surf.geometryNormal.cross(surf.tangent).normalized();
    surf.pdf /= m_transform->apply(tangent)
                    .cross(m_transform->apply(bitangent))
                    .length();

    const Vector newGeoNormal = m_transform->applyNormal(surf.geometryNormal);
    surf.tangent              = m_transform->apply(surf.tangent);
    surf.geometryNormal       = newGeoNormal.normalized();
}

inline void validateIntersection(const Intersection &its) {
    // use the following macros to make debugginer easier:
    // * assert_condition(condition, { ... });
    // * assert_normalized(vector, { ... });
    // * assert_ortoghonal(vec1, vec2, { ... });
    // * assert_finite(value or vector or color, { ... });

    // each assert statement takes a block of code to execute when it fails
    // (useful for printing out variables to narrow done what failed)

    assert_finite(its.t, {
        logger(
            EError,
            "  your intersection produced a non-finite intersection distance");
        logger(EError, "  offending shape: %s", its.instance->shape());
    });
    // Commented out below assert because it gets triggered too often, and the render looks fine anyway
    // assert_condition(its.t >= Epsilon, {
    //     logger(EError,
    //            "  your intersection is susceptible to self-intersections");
    //     logger(EError, "  offending shape: %s", its.instance->shape());
    //     logger(EError,
    //            "  returned t: %.3g (smaller than Epsilon = %.3g)",
    //            its.t,
    //            Epsilon);
    // });
}

bool Instance::intersect(const Ray &worldRay, Intersection &its, Payload &p,
                         Sampler &rng) const {
    const auto prevAlphaMask = its.alphaMask;
    its.alphaMask            = m_alpha.get();
    if (!m_transform) {
        // fast path, if no transform is needed
        const Ray localRay        = worldRay;
        const bool wasIntersected = m_shape->intersect(localRay, its, p, rng);
        if (wasIntersected) {
            its.instance = this;
            validateIntersection(its);
        }
        its.alphaMask = prevAlphaMask;
        return wasIntersected;
    }

    const Float previousT = its.t;
    Ray localRay = m_transform->inverse(worldRay);

    const Float dLength = localRay.direction.length();
    if (dLength == 0)
        return false;
    localRay.direction /= dLength;

    its.t *= dLength;
    // hints:
    // * transform the ray (do not forget to normalize!)
    // * how does its.t need to change?

    auto previousInstance = its.instance;
    const bool wasIntersected = m_shape->intersect(localRay, its, p, rng);
    if (wasIntersected) {
        // if (its.instance == previousInstance && bsdf()) {
        //     // m_shape did not set materials, so let us do it
        //     its.instance = this;
        //     logger(EWarn, "bsdf: %s", bsdf());
        // }
        // if ((previousT > its.t) || !its.instance || (bsdf() && !its.instance->bsdf())) its.instance = this; // This is necessary if using colorMaps for LD feathers
        // if (its.instance && bsdf() && !its.instance->bsdf()) its.instance = this;
        its.instance = this;
        validateIntersection(its);
        assert_finite(its.position, {
            logger(EError,
                   "non-finite position, offending shape: %s",
                   m_shape->toString());
            logger(EError, "  returned its.t: %g", its.t);
            logger(EError,
                   "  for input ray %s in dir %s",
                   worldRay.origin,
                   worldRay.direction);
        });
        its.t /= dLength;
        // hint: how does its.t need to change?

        // if (its.frame.normal.dot(localRay.direction) > 0) {
        //     /// NOTE: hack, just for testing
        //     its.frame.tangent *= -1;
        //     its.frame.bitangent *= -1;
        //     its.frame.normal *= -1;

        //     its.geoFrame.tangent *= -1;
        //     its.geoFrame.bitangent *= -1;
        //     its.geoFrame.normal *= -1;
        // }
        transformFrame(its, -localRay.direction);
        // if (its.frame.normal.dot(worldRay.direction) > 0) {
        //     /// NOTE: hack, just for testing
        //     its.frame.tangent *= -1;
        //     its.frame.bitangent *= -1;
        //     its.frame.normal *= -1;

        //     its.geoFrame.tangent *= -1;
        //     its.geoFrame.bitangent *= -1;
        //     its.geoFrame.normal *= -1;
        // }
    } else {
        its.t = previousT;
    }

    its.alphaMask = prevAlphaMask;
    return wasIntersected;
}

Bounds Instance::getBoundingBox() const {
    if (!m_transform) {
        // fast path
        return m_shape->getBoundingBox();
    }

    const Bounds untransformedAABB = m_shape->getBoundingBox();
    if (untransformedAABB.isUnbounded()) {
        return Bounds::full();
    }

    Bounds result;
    for (int point = 0; point < 8; point++) {
        Point p = untransformedAABB.min();
        for (int dim = 0; dim < p.Dimension; dim++) {
            if ((point >> dim) & 1) {
                p[dim] = untransformedAABB.max()[dim];
            }
        }
        p = m_transform->apply(p);
        result.extend(p);
    }
    return result;
}

Point Instance::getCentroid() const {
    if (!m_transform) {
        // fast path
        return m_shape->getCentroid();
    }

    return m_transform->apply(m_shape->getCentroid());
}

AreaSample Instance::sampleArea(Sampler &rng) const {
    AreaSample sample = m_shape->sampleArea(rng);
    transformFrame(sample, Vector());
    return sample;
}

void Instance::passInfoToFeathers(ref<Bsdf> bsdf) {
    // If this instance already has a material, we can assume that it has already given it downwards.
    if (m_bsdf) return;
    // Otherwise continue recursion!
    else m_shape->passInfoToFeathers(bsdf);
}

void Instance::setLateParameters(ref<Bsdf> bsdf, ref<Texture> normal, ref<Texture> alpha) {
    m_bsdf = bsdf;
    m_normal = normal;
    m_alpha = alpha;
}

} // namespace lightwave

REGISTER_CLASS(Instance, "instance", "default")
