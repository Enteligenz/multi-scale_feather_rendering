#pragma once

#include <lightwave.hpp>

#include "../core/plyparser.hpp"
#include "accel.hpp"

// Large parts of the code have been taken from
// https://gitlab.cs.uni-saarland.de/art/lightwave/-/blob/main/src/shapes/hairs.cpp?ref_type=heads
// which implements ideas from this paper:
// https://research.nvidia.com/sites/default/files/pubs/2018-08_Phantom-Ray-Hair-Intersector//Phantom-HPG%202018.pdf

namespace lightwave {

/**
 * @brief A shape consisting of many (potentially millions) of (bezier)
 * curves, which share a buffer. Since individual curves are rarely
 * needed (and would pose an excessive amount of overhead), collections of
 * curves are combined in a single shape.
 */
class Curves : public AccelerationStructure {
    /// @brief The file these curve joints were loaded from, for logging and debugging
    /// purposes.
    std::filesystem::path m_originalPath;

    protected:
    // Almost identically taken from the paper.
    struct RayConeIntersection { // ray.origin = Point{0}, ray.direction =
                                 // Vector{0,0,1}
        Point c0;                // curve(t) in RCC
        Vector cd;               // tangent(t) in RCC
        Float s;                 // the intersection's t parameter
        Float dt;                // unscaled delta(t) from the paper
        Float dp;
        Float dc;
        Float sp;

        RayConeIntersection(Point pointOnCurve, Vector tangentAtPoint) {
            c0 = pointOnCurve;
            cd = tangentAtPoint;
        }

        // Returns true for real intersections and false for phantom
        // intersections.
        inline bool intersect(const Float r, const Float dr) {
            const Float r2  = r * r;
            const Float drr = r * dr;

            Float ddd = sqr(cd.x()) + sqr(cd.y());
            dp        = sqr(c0.x()) + sqr(c0.y());

            const Float cdd = c0.x() * cd.x() +
                              c0.y() * cd.y(); // Has to be (c(t) -
                                               // o).dot(c'(t)) according to sp.
            const Float cxd  = c0.x() * cd.y() - c0.y() * cd.x();
            const Float cdz2 = sqr(cd.z());

            Float c = ddd;
            ddd += cdz2;
            Float b = cd.z() * (drr - cdd);
            Float a = 2 * drr * cdd + cxd * cxd - ddd * r2 + dp * cdz2;

            const Float det = b * b - a * c;
            s               = (b - safe_sqrt(det)) / c;
            dt              = (s * cd.z() - cdd) / ddd;
            dc              = s * s + dp;
            sp              = cdd / cd.z();
            dp += sp * sp;

            s += c0.z();
            sp += c0.z();
            return det > 0;
        }
    };

    // Parametric version of a cylinder 'tightly' fitting the hair,
    // used for early exit checks
    struct BoundingCylinder {
        Vector de;
        Point oe;
        Float maxDist;
    };

    std::vector<BoundingCylinder> m_boundingCylinders; // Precomputed
    std::vector<Bounds> m_boundingBoxes; // Will be cleared after creating the
                                         // acceleration structure
    /**
     * @brief Contains all control points in order, consecutively.
     * This means that the last control point of one segment is the first control
     * point of the following segment, meaning the segments overlap in this sense.
     * The nth segment starts at index n*m_numControlPoints - 1.
     */
    std::vector<Point> m_controlPoints;

    // These three vectors are only used to keep track of indices when
    // reordering. They slow the program down by quite a bit (~ 20% for some
    // scenes) and are pretty large but other solutions are WAY too ugly.
    std::vector<int> m_hairIndizes;
    std::vector<int> m_curveIndizes;
    std::vector<int> m_splitIndizes;

    Float m_rootRadius;
    Float m_tipRadius;
    using RadiusFunction = Float (Curves::*)(Float, int) const;
    RadiusFunction m_radiusFunction;

    // The number of control points per segment
    int32_t m_numControlPoints;
    // The number of segments per strand
    int32_t m_numCurves;
    // The number of strands
    int32_t m_hairCount;

    // Number of sampled points when computing bounding boxes and cylinders
    static constexpr int32_t m_numCurveSamples = 20;
    // Max number of iterations when looking for an intersection
    static constexpr int32_t m_phantomIter = 30;
    // Number of sub-segments each segment is split into (paper uses 8)
    int32_t m_curveSplits = 16;

    Float m_invCurveSplits;
    static constexpr Float m_margin         = 1e-5;
    static constexpr Float m_deltaMargin    = 5e-5;

    // Parameters for when the sigmoid version of localRadius is used
    static constexpr Float m_localRadius_midPoint = Float(0.7); // Where the transition happens (0.7 = 70% along the hair)
    static constexpr Float m_localRadius_sharpness = Float(8.0); // Higher value = sharper transition

protected:
    // Below are multiple versions of the localRadius function, selected at class instance creation
    inline Float localRadiusLinear(const Float t, const int curveIndex) const { // Using linear
        const Float hairT = (t + curveIndex) / m_numCurves;
        return m_rootRadius + hairT * (m_tipRadius - m_rootRadius); // From ART's interpolateLinear()
    }
    inline Float localRadiusPow(const Float t, const int curveIndex) const { // Using pow
        const Float hairT = (t + curveIndex) / m_numCurves;
        const Float exponent = Float(3.0); // Adjust this to control sharpness of the tip
        const Float tPow = pow(hairT, exponent);
        return m_rootRadius + tPow * (m_tipRadius - m_rootRadius);
    }
    inline Float localRadiusSigmoid(const Float t, const int curveIndex) const { // Using sigmoid
        const Float hairT = (t + curveIndex) / m_numCurves;

        // Sigmoid-like function centered at midPoint
        const Float tSigmoid = Float(1.0) / (Float(1.0) + exp(m_localRadius_sharpness * (m_localRadius_midPoint - hairT)));

        return m_rootRadius + tSigmoid * (m_tipRadius - m_rootRadius);
    }

protected:
    std::pair<int, int> numberOfPrimitives() const override {
        return { m_hairCount * m_numCurves * m_curveSplits, 0 };
    }

    Bounds getBoundingBox(int primitiveIndex, bool forHighLOD=true) const override {
        return m_boundingBoxes[primitiveIndex];
    }

    /// @brief Gets the centroid of the bounding box as there is no clear
    /// centroid of a curve
    Point getCentroid(int primitiveIndex) const override {
        return m_boundingBoxes[primitiveIndex].center();
    }

    /// @brief t in range [0, 1] and curveIndex = 0 on first bezier curve, 1 on
    /// second, ...
    inline Float localRadius(const Float t, const int curveIndex) const {
        return (this->*m_radiusFunction)(t, curveIndex);
    }


    /// @brief t in range [0, 1] and curveIndex = 0 on first bezier curve, 1 on
    /// second, ...
    inline Float localSlant(const Float t, const int curveIndex) const {
        return (m_tipRadius - m_rootRadius) / m_numCurves;  // from ART's interpolateLinearPrime()
    }

    inline Point interpolateBezierCurve(const Float t, const std::array<Point, 4> &values) const {
        const Point b0 = values[0];
        const Point b1 = values[1];
        const Point b2 = values[2];
        const Point b3 = values[3];
        const Float t2 = sqr(t);
        return b0 - Float(3.)*t*(b0-b1) + Float(3.)*t2*(b0-Float(2.)*b1+b2) - t*t2*(b0-Float(3.)*b1+Float(3.)*b2-b3);
    }

    inline Point tangentToBezierCurve(const Float t, const std::array<Point, 4> &values) const {
        const Point b0 = values[0];
        const Point b1 = values[1];
        const Point b2 = values[2];
        const Point b3 = values[3];
        return - Float(3.)*(b0-b1) + Float(6.)*t*(b0-Float(2.)*b1+b2) - Float(3.)*t*t*(b0-Float(3.)*b1+Float(3.)*b2-b3);
    }

    bool intersect(int primitiveIndex, const Ray &ray, Intersection &its,
        Payload &p, Sampler &rng, bool useHighLOD=true) const override {
            const int splitIndex = m_splitIndizes[primitiveIndex];
            const int curveIndex = m_curveIndizes[primitiveIndex];
            const int hairIndex  = m_hairIndizes[primitiveIndex];
            const int cpIndex    = hairIndex * m_numControlPoints + 3 * curveIndex;
    
            const Float tstart = splitIndex * m_invCurveSplits;
            const Float tend   = (splitIndex + 1) * m_invCurveSplits;
    
            if (!intersectsBoundingCylinder(tstart, primitiveIndex, ray))
                return false;
    
            const Point w0               = m_controlPoints.at(cpIndex);
            const Point w1               = m_controlPoints.at(cpIndex + 1);
            const Point w2               = m_controlPoints.at(cpIndex + 2);
            const Point w3               = m_controlPoints.at(cpIndex + 3);
            const std::array<Point, 4> w = { w0, w1, w2, w3 };
    
            Transform rccToLocal = rcc_to_loc_transform(ray, w3);
    
            // If deltaT(start) < 0 and deltaT(end) > 0, ignore interval
            if (delT(tstart, curveIndex, w, rccToLocal) < 0 &&
                delT(tend, curveIndex, w, rccToLocal) > 0)
                return false;
    
            const Point start = interpolateBezierCurve(tstart, w);
            const Point end   = interpolateBezierCurve(tend, w);
    
            // Start iterations at 'better' end and afterwards, try the 'worse' one.
            const Float t1 = ray.direction.dot(end - start) > 0 ? tstart : tend;
            const Float t2 = ray.direction.dot(end - start) > 0 ? tend : tstart;
            if (intersectionIteration(
                    t1, curveIndex, hairIndex, ray, its, rccToLocal, w))
                return true;
            if (intersectionIteration(
                    t2, curveIndex, hairIndex, ray, its, rccToLocal, w))
                return true;
    
            return false;
    }

    /// @brief Returns false if the ray misses the bounding cylinder of the
    /// curve.
    inline bool intersectsBoundingCylinder(const Float tstart,
                                           const int primitiveIndex,
                                           const Ray &ray) const {
        const BoundingCylinder bCylinder = m_boundingCylinders[primitiveIndex];
        const Vector n                   = bCylinder.de.cross(ray.direction);

        const Float distRayCylindAxis = abs(n.dot(ray.origin - bCylinder.oe));
        return distRayCylindAxis < bCylinder.maxDist;
    }

    /// @brief Prepares a transform that will transform a coordinate
    /// into the ray-centric coordinate system and back.
    inline Transform rcc_to_loc_transform(const Ray &ray, const Point &w3) const {
        const Vector d = ray.direction;
        Vector q       = Vector(w3).cross(d);

        // If w3 and d are (nearly) collinear we choose an arbitrary q
        q = (q.length() < m_margin) ? Frame(d).tangent : q.normalized();
        const Vector c = q.cross(d);

        Matrix3x3 changeOfBasis; // This is an orthonormal matrix!
        changeOfBasis.setColumn(0, q);
        changeOfBasis.setColumn(1, c);
        changeOfBasis.setColumn(2, d);

        Transform rcc_to_loc(changeOfBasis);
        rcc_to_loc.translate(Vector(ray.origin));

        return rcc_to_loc;
    }

    /// @brief Contains the entire root-finding logic (i.e.
    /// intersection-finding) and returns true iff an intersection is found.
    /// Starting at t = tstart, we use the RayConeIntersection struct from the
    /// paper to update t until we're either close enough or our iterations run
    /// dry.
    inline bool intersectionIteration(const Float tstart, const int curveIndex,
                                      const int hairIndex, const Ray &ray,
                                      Intersection &its,
                                      const Transform &rccToLocal,
                                      const std::array<Point, 4> &cp) const {
        Float t          = tstart;
        Float prevDeltaT = 0, prevT;
        for (int k = 0; k < m_phantomIter; k++) {
            const Point rccStart =
                rccToLocal.inverse(interpolateBezierCurve(t, cp));
            const Vector rccTangent =
                rccToLocal.inverse((Vector) tangentToBezierCurve(t, cp));
            RayConeIntersection coneInter(rccStart, rccTangent);

            const bool realRoot = coneInter.intersect(
                localRadius(t, curveIndex), localSlant(t, curveIndex));
            const Float deltaT = clamp(coneInter.dt, Float(-0.5), Float(0.5));

            prevT = t;
            if (k == 0) { // We want to do atleast 2 iterations
                prevDeltaT = deltaT;
                continue;
            }

            // When we have a 0-crossing for delta(t), "it's more stable to do
            // regula falsi", otherwise we just add deltaT
            if (deltaT * prevDeltaT >= 0)
                t += deltaT;
            else {
                if (k % 4 == 0)
                    t = (t + prevT) / 2;
                else
                    t = (deltaT * prevT - prevDeltaT * t) /
                        (deltaT - prevDeltaT);
            }

            // Buttend handling
            const Float buffer = min(Float(0.1), 2 * m_invCurveSplits);
            const Float lower  = max(Float(0.), tstart - buffer);
            const Float upper  = min(Float(1.), tstart + m_invCurveSplits + buffer);
            if (t < lower || t > upper) {
                // If we neither hit the root nor the tip, then the 'buttend'
                // lies between 2 segments and is thus occluded.
                if (!(curveIndex == 0 && t < Float(0.)) &&
                    !(curveIndex == m_numCurves - 1 && t > Float(1.)))
                    return false;

                const bool tbool =
                    (t >= 1.) ? true
                               : false; // Safely cast t into a bool. (True if t
                                        // > 1 and False if t < 0)
                t = clamp(t, Float(0.), Float(1.));
                const Vector tangent =
                    Vector(tangentToBezierCurve(t, cp)).normalized();
                if (tangent.length() == Float(0.0)) {
                    logger(EWarn, "Curve tangent has length 0! Intersection will return false as a fix");
                    return false;
                }
                const Point endPoint = tbool ? cp[3] : cp[0];

                const Float denom = tangent.dot(ray.direction);
                if (abs(denom) < m_margin)
                    return false;

                const Float tbar = (endPoint - ray.origin).dot(tangent) / denom;
                if (tbar < Epsilon || tbar > its.t)
                    return false;

                const Point hitPoint = ray(tbar);
                if (abs((hitPoint - endPoint).length()) >=
                    localRadius(t, curveIndex))
                    return false;

                const Vector gNormal = (tbool ? 1. : -1.) * tangent;

                its.t  = tbar;
                its.uv = Point2{ Float(1.) * curveIndex / m_numCurves, Float(1.) * hairIndex }; // Note: This used to use (m_numCurves - 1), but it broke since for feathers I usually have m_numCurves=1.
                its.position       = hitPoint;
                its.geometryNormal = gNormal;
                its.shadingNormal  = its.geometryNormal;
                its.tangent        = hitPoint - endPoint;
                its.pdf            = 0;

                assert_finite(its.t,
                              { logger(EError, "offending shape: %s", this); });
                if (its.tangent.length() == Float(0.0)) {
                    logger(EWarn, "Curve tangent has length 0! Intersection will return false as a fix.");
                    return false;
                }
            }

            // Stopping the iteration when we are precise enough
            if (abs(deltaT) < m_deltaMargin) {
                const Float s = coneInter.s;
                if (!realRoot || s < Epsilon || s >= its.t)
                    return false;

                if (abs(deltaT - prevDeltaT) > m_margin)
                    t = (deltaT * prevT - prevDeltaT * t) /
                        (deltaT - prevDeltaT);

                const Float hairT   = (t + curveIndex) / m_numCurves;
                const Point onCurve = interpolateBezierCurve(t, cp);
                const Point onCone  = ray(s);
                const Vector n      = onCone - onCurve;
                const Vector gNormal =
                    (n.length() > Float(1e-8) ? n.normalized() : -ray.direction);
                const Vector tangentCurve =
                    Vector(tangentToBezierCurve(t, cp)).normalized();
                // const Vector tanBitan = tangentCurve.cross(-ray.direction); // unused
                Vector sNormal        = gNormal;

                // Try this one if the geometric normal doesn't look right!
                // sNormal = (gNormal +
                // 0.875*gNormal.dot(ray.direction)*ray.direction).normalized();

                // https://github.com/blender/blender/blob/ddbc34829feb0ab46b2723d87e6602d76117b9c7/intern/cycles/kernel/geom/curve_intersect.h
                // Generates the same normals as Curve Info -> Tangent Normals,
                // no idea wtf that is though.

                its.t  = s;
                its.uv = Point2{ hairT, Float(1.) * hairIndex }; // v is used for the hash to differentiate between curves
                its.pdf            = 0;
                its.position       = onCone;
                its.geometryNormal = gNormal; // Is -ray.direction for Curve
                                              // type 'Rounded Ribbons'
                its.shadingNormal = sNormal;
                its.tangent =
                    (tangentCurve - tangentCurve.dot(gNormal) * gNormal)
                        .normalized(); // Projection of the tangent to the curve
                                       // onto the cone

                assert_finite(its.t,
                              { logger(EError, "offending shape: %s", this); });
                if (its.tangent.length() == Float(0.0)) {
                    logger(EWarn, "Curve tangent has length 0! Intersection will return false as a fix.");
                    return false;
                }
                return true;
            }

            prevDeltaT = deltaT;
        }
        return false;
    }

    /// @brief Calculates the delta(t) from the paper if that's all we need
    inline Float delT(const Float t, int curveIndex,
                      const std::array<Point, 4> &w,
                      const Transform &rccToLocal) const {
        const Point start = rccToLocal.inverse(interpolateBezierCurve(t, w));
        const Vector tangent =
            rccToLocal.inverse((Vector) tangentToBezierCurve(t, w));

        RayConeIntersection coneInter(start, tangent);
        coneInter.intersect(localRadius(t, curveIndex),
                            localSlant(t, curveIndex));
        return coneInter.dt;
    }

    protected:
    /// @brief Computes the bounding box and cylinder for each hair segment
    inline void populateBoundingObjects() {
        for (int hairIndex = 0; hairIndex < m_hairCount; hairIndex++) {
            int cpIndex      = hairIndex * m_numControlPoints;
            int segmentIndex = hairIndex * m_numCurves;

            for (int i = 0; i < m_numControlPoints - 1; i += 3) {
                const Point w0               = m_controlPoints[cpIndex];
                const Point w1               = m_controlPoints[cpIndex + 1];
                const Point w2               = m_controlPoints[cpIndex + 2];
                const Point w3               = m_controlPoints[cpIndex + 3];
                const std::array<Point, 4> w = { w0, w1, w2, w3 };

                // We split each curve segment into parts to find all delta(T)
                // solutions and get a better bounding box fit
                Point start, end = interpolateBezierCurve(0, w);
                for (int numSplit = 0; numSplit < m_curveSplits; numSplit++) {
                    start = end;
                    end   = interpolateBezierCurve(
                        (numSplit + Float(1.)) * m_invCurveSplits, w);

                    Bounds aabb = Bounds::empty();
                    const Point oe =
                        Float(0.5) *
                        (start + interpolateBezierCurve(
                                     (numSplit + Float(0.5)) * m_invCurveSplits, w));
                    const Vector de = (end - start).normalized();
                    Float maxDist   = 0;

                    // We sample points on the curve to approximate the max
                    // distance from the curve to the line 'oe + lambda * de'
                    // Instead of using the control points, we use the sampled
                    // points for a tighter bounding box
                    for (int j = 0; j < m_numCurveSamples; j++) {
                        Float t = (m_numCurveSamples > 1)
                                      ? (Float(1.) * j) / (m_numCurveSamples - 1)
                                      : 0;
                        t       = (numSplit + t) * m_invCurveSplits;

                        // Building bounding box
                        const Float radius = localRadius(t, i / 3);
                        const Point sample = interpolateBezierCurve(t, w);
                        aabb.extend(sample + Vector(radius));
                        aabb.extend(sample - Vector(radius));

                        // Sampling bounding cylinder's maxDist
                        const Vector toLine = oe - interpolateBezierCurve(t, w);
                        const Float distInterpolToLine =
                            (toLine - toLine.dot(de) * de).length();
                        if (distInterpolToLine > maxDist)
                            maxDist = distInterpolToLine;
                    }
                    maxDist += localRadius(numSplit * m_invCurveSplits, i / 3);

                    m_boundingCylinders[m_curveSplits * segmentIndex +
                                        numSplit] = { .de      = de,
                                                      .oe      = oe,
                                                      .maxDist = maxDist };
                    m_boundingBoxes[m_curveSplits * segmentIndex + numSplit] =
                        aabb;
                }
                cpIndex += 3;
                segmentIndex++;
            }
        }
    }

    // void reorderPrimitives(const std::vector<int> &newOrder) override {
    //     std::vector<BoundingCylinder> updatedCylinders;
    //     std::vector<Bounds> updatedBoundingBoxes;
    //     std::vector<int> updatedHairIndizies;
    //     std::vector<int> updatedCurveIndizies;
    //     std::vector<int> updatedSplitIndizies;
    //     for (int index : newOrder) {
    //         updatedCylinders.push_back(m_boundingCylinders[index]);
    //         updatedBoundingBoxes.push_back(m_boundingBoxes[index]);
    //         updatedHairIndizies.push_back(m_hairIndizes[index]);
    //         updatedCurveIndizies.push_back(m_curveIndizes[index]);
    //         updatedSplitIndizies.push_back(m_splitIndizes[index]);
    //     }
    //     m_boundingCylinders = updatedCylinders;
    //     m_boundingBoxes     = updatedBoundingBoxes;
    //     m_hairIndizes       = updatedHairIndizies;
    //     m_curveIndizes      = updatedCurveIndizies;
    //     m_splitIndizes      = updatedSplitIndizies;
    // }

public:
    Curves(const Properties &properties) {
        m_originalPath  = properties.get<std::filesystem::path>("filename");
        m_curveSplits = properties.get<int32_t>("curvesplits", 16);
        m_invCurveSplits = Float(1.) / m_curveSplits;
        m_rootRadius = properties.get<float>("rootradius", 0.0005);
        m_tipRadius = properties.get<float>("tipradius", 0.0001);

        // Select which version of localRadius to use
        std::string radiusFuncTypeStr = properties.get<std::string>("radiusfunction", "sigmoid");
        std::transform(radiusFuncTypeStr.begin(), radiusFuncTypeStr.end(), 
                       radiusFuncTypeStr.begin(), ::tolower);
        if (radiusFuncTypeStr == "linear") {
            m_radiusFunction = &Curves::localRadiusLinear;
        } else if (radiusFuncTypeStr == "power" || radiusFuncTypeStr == "pow") {
            m_radiusFunction = &Curves::localRadiusPow;
        } else if (radiusFuncTypeStr == "sigmoid") {
            m_radiusFunction = &Curves::localRadiusSigmoid;
        } else { // Default case and error handling
            logger(EWarn, "Unknown radius function type: %s. Using sigmoid as default.",
                radiusFuncTypeStr);
            m_radiusFunction = &Curves::localRadiusSigmoid;
        }

        int32_t numSegments; // We don't need this value here.
        std::vector<Vector> normals; // We also don't need this here.
        readCurvePLY(m_originalPath, m_controlPoints, normals, m_numControlPoints, numSegments);
        // logger(EInfo, "loaded ply with %d curve joints each",
        //     m_numControlPoints);
        // for (const auto &p : m_controlPoints) {
        //     logger(EInfo, "point %s", p);
        // }

        const int cpSize = m_controlPoints.size();
        m_numCurves = (m_numControlPoints - 1) / 3;
        m_hairCount = cpSize / m_numControlPoints;

        logger(EInfo, "loaded ply with %s strands, %s segments per strand and %s control points per segment",
            m_hairCount, m_numCurves, m_numControlPoints);

        m_boundingBoxes =
            std::vector<Bounds>(m_hairCount * m_numCurves * m_curveSplits);
        m_boundingCylinders = std::vector<BoundingCylinder>(
            m_hairCount * m_numCurves * m_curveSplits);

        m_hairIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);
        m_curveIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);
        m_splitIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);

        for (int i = 0; i < m_hairCount * m_numCurves * m_curveSplits; i++) {
            m_hairIndizes[i] = i / (m_numCurves * m_curveSplits);
            m_curveIndizes[i] =
                (i % (m_numCurves * m_curveSplits)) / m_curveSplits;
            m_splitIndizes[i] = i % m_curveSplits;
        }

        populateBoundingObjects();

        buildAccelerationStructure();
        m_boundingBoxes.clear();
    }

    /// @brief Alternative constructor, specifically for use in the Feathers class.
    Curves(const std::vector<Point> &controlPoints, const std::filesystem::path &filePath,
           const int32_t curveSplits, const Float rootRadius, const Float tipRadius,
           std::string radiusFuncTypeStr, const int32_t numControlPoints)
          : m_originalPath(filePath), m_controlPoints(controlPoints), m_rootRadius(rootRadius),
            m_tipRadius(tipRadius), m_numControlPoints(numControlPoints), m_curveSplits(curveSplits) {
        m_invCurveSplits = Float(1.) / m_curveSplits;

        // Select which version of localRadius to use
        std::transform(radiusFuncTypeStr.begin(), radiusFuncTypeStr.end(), 
                       radiusFuncTypeStr.begin(), ::tolower);
        if (radiusFuncTypeStr == "linear") {
            m_radiusFunction = &Curves::localRadiusLinear;
        } else if (radiusFuncTypeStr == "power" || radiusFuncTypeStr == "pow") {
            m_radiusFunction = &Curves::localRadiusPow;
        } else if (radiusFuncTypeStr == "sigmoid") {
            m_radiusFunction = &Curves::localRadiusSigmoid;
        } else { // Default case and error handling
            logger(EWarn, "Unknown radius function type: %s. Using sigmoid as default.",
                radiusFuncTypeStr);
            m_radiusFunction = &Curves::localRadiusSigmoid;
        }

        const int cpSize = m_controlPoints.size();
        m_numCurves = (m_numControlPoints - 1) / 3;
        m_hairCount = cpSize / m_numControlPoints;

        logger(EInfo, "loaded ply with %s strands, %s segments per strand and %s control points per segment",
            m_hairCount, m_numCurves, m_numControlPoints);

        m_boundingBoxes =
            std::vector<Bounds>(m_hairCount * m_numCurves * m_curveSplits);
        m_boundingCylinders = std::vector<BoundingCylinder>(
            m_hairCount * m_numCurves * m_curveSplits);

        m_hairIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);
        m_curveIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);
        m_splitIndizes =
            std::vector<int>(m_hairCount * m_numCurves * m_curveSplits);

        for (int i = 0; i < m_hairCount * m_numCurves * m_curveSplits; i++) {
            m_hairIndizes[i] = i / (m_numCurves * m_curveSplits);
            m_curveIndizes[i] =
                (i % (m_numCurves * m_curveSplits)) / m_curveSplits;
            m_splitIndizes[i] = i % m_curveSplits;
        }

        populateBoundingObjects();

        buildAccelerationStructure();
        m_boundingBoxes.clear();
    }

    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override {
        PROFILE("Curve collection")
        return AccelerationStructure::intersect(ray, its, p, rng);
    }

    AreaSample sampleArea(Sampler &rng) const override{
        NOT_IMPLEMENTED
    }

    std::string toString() const override {
        return tfm::format("Curve collection[\n"
                           "  control points per curve = %d,\n"
                           "  filename = \"%s\"\n"
                           "]",
                           m_numControlPoints,
                           m_originalPath.generic_string());
    }
};

} // namespace lightwave
