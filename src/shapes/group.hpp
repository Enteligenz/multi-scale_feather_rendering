#pragma once

#include <lightwave.hpp>

#include "accel.hpp"

namespace lightwave {

/**
 * @brief A group is a shape that results from the union of an arbitrary amount
 * of individual shapes. This allows us to avoid manually iterating over all
 * objects in the scene whenever we need to find an intersection, and also
 * provides noticeable speed-up by using an acceleration structure under the
 * hood.
 */
class Group final : public AccelerationStructure {
    std::vector<ref<Shape>> m_children;

protected:
    std::pair<int, int> numberOfPrimitives() const override;

    bool intersect(int primitiveIndex, const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng, bool useHighLOD=true) const override;

    Bounds getBoundingBox(int primitiveIndex, bool forHighLOD=true) const override;

    Point getCentroid(int primitiveIndex) const override ;

public:
    Group(const Properties &properties);

    /// @brief Alternative constructor for use in feathers class.
    /// Combines the three component types of a feather into one group.
    Group(const ref<Shape> &spine, const ref<Shape> &barbs, const ref<Shape> &barbules);

    void markAsVisible() override;

    AreaSample sampleArea(Sampler &rng) const override;

    void passInfoToFeathers(ref<Bsdf> bsdf) override ;

    std::string toString() const override;
};

} // namespace lightwave
