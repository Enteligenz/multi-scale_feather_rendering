#include "accel.hpp"
#include <lightwave/camera.hpp>

namespace lightwave {

bool AccelerationStructure::intersect(const Ray &ray, Intersection &its, Payload &p, Sampler &rng) const {
    if (m_primitiveIndices.empty())
        return false; // exit early if no children exist

    // if (m_useLOD) logger(EWarn, "%f * %i (%f) + %f * %f² (%f) + %f / %f (%f) > %f",
    //     m_a, p.depth, (m_a * Float(p.depth)),
    //     m_b, p.traveledDistance, m_b * p.traveledDistance,
    //     m_c, p.pdf, m_c * p.pdf,
    //     m_thresholdLOD);

    // Compute pixel footprint
    Float footprint = 0;
    Float tEstimate = 0;
    if (p.camera) {
        tEstimate = intersectAABB(getBoundingBox(), ray);
        // tEstimate = clamp(tEstimate, Float(1), Infinity);
        footprint = p.camera->computePixelFootprint(tEstimate + p.totalDistance);
    }
    bool lightPDFResult = p.lightPDF > Float(0.9);
    // Float usedFootprint = p.isForNEE ? p.lastFootprint : footprint; // This line here fixes "tattoo" artifacts (though can't fix it if we use (!lightPDFResult && p.isForNEE))
    Float totalDist = p.traveledDistance + tEstimate;
    totalDist = totalDist * totalDist; // We want to use the squared distance

    // if (m_useLOD && (p.pdf / (totalDist + Float(1) + Epsilon) < m_thresholdLOD)) { // Low detail using combined switch [1]
    // if (m_useLOD && (p.pdf < m_thresholdLOD || p.wasRough)) { // Low detail using PDF only switch [2]
    // if (m_useLOD && (totalDist > m_thresholdLOD)) { // Low detail using TraveledDistance only switch [3]
    // if (m_useLOD && (Float(1) / (totalDist + Float(1)) < m_thresholdLOD || p.wasRough)) { // Low detail using old TraveledDistance only switch [3.5] (old)
    // if (m_useLOD && p.depth >= int(m_thresholdLOD)) { // Low detail using bounce depth as switch [4]
    if (m_useLOD) { // Always low detail [5]
    // if (m_useLOD && (footprint >= m_thresholdLOD || p.wasRough || !p.isComingFromHD || (!lightPDFResult && p.isForNEE))) { // Footprint method [6]
    // if (m_useLOD && (m_a * p.depth + m_b * totalDist * totalDist + (m_c / p.pdf)) > m_thresholdLOD) { // Derived from Philipp Ziegler's Bsc Thesis [7]
    // if (m_useLOD && (p.pdf / (Float(0.0125) * totalDist + Float(1) + Epsilon) < m_thresholdLOD)) { // Low detail using combined switch with weighted-down distance [8]

        if (intersectAABB(rootNode(false).aabb, ray) <
        its.t) // test root bounding box for potential hit
        // TODO record old feather id in case we get something different sometime?
            return intersectNode(rootNode(false), ray, its, p, rng, m_primitiveIndicesLow, m_nodesLow, false);
        return false;
    } else { // High detail
        if (intersectAABB(rootNode(true).aabb, ray) <
        its.t) // test root bounding box for potential hit
            return intersectNode(rootNode(true), ray, its, p, rng, m_primitiveIndices, m_nodes, true);
        return false;
    }
}

} // namespace lightwave