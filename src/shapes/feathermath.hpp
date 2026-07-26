/**
 * @file mathfeathers.hpp
 * @brief Contains utility math functions written for feather-related code.
 */

#pragma once

#include <lightwave/math.hpp>

#include <Eigen/Dense>
#include <vector>
#include <random>
#include <iostream>
#include <iomanip>

namespace lightwave {
    static constexpr Float angleSumThreshold = Float(3.0) * Pi / Float(2.0);

    // MARK: - vector math

    /// @brief This function helps move a vector along a curve by changing its angle as needed.
    /// @param vectorToTransport The vector that should be transported.
    /// @param oldTangent Tangent that the vector previously used.
    /// @param newTangent Tangent that the vector should use now.
    inline Vector parallelTransport(const Vector &vectorToTransport, const Vector &oldTangent, const Vector &newTangent) {
        // If tangents are nearly identical, no transport needed
        if ((newTangent - oldTangent).length() < Epsilon) return vectorToTransport;

        Vector t1 = oldTangent.normalized();
        Vector t2 = newTangent.normalized();

        // If tangents are opposite, we need special handling
        if ((t1 + t2).length() < Epsilon) {
            // Find any vector perpendicular to t1
            Vector perp;
            if (abs(t1[0]) < Float(0.9)) perp = Vector(Float(1.0), Float(0.0), Float(0.0)).cross(t1).normalized();
            else perp = Vector(Float(0.0), Float(1.0), Float(0.0)).cross(t1).normalized();

            // Rotate 180 degrees around the perpendicular axis
            return vectorToTransport * (Float(-1.0));
        }

        // Standard parallel transport
        Vector rotationAxis = t1.cross(t2).normalized();
        Float cosAngle = t1.dot(t2);
        Float angle = std::acos(std::clamp(cosAngle, Float(-1.0), Float(1.0)));

        if (angle < Epsilon) return vectorToTransport; // No rotation needed

        // Rodrigues' rotation formula
        Vector v = vectorToTransport;
        Vector k = rotationAxis;
        Float cosA = std::cos(angle);
        Float sinA = std::sin(angle);

        return v * cosA + k.cross(v) * sinA + k * (k.dot(v)) * (Float(1.0) - cosA);
    }

    // MARK: - rotation

    /// @brief Rotates a vector by a quaternion (4x4 matrix).
    inline Vector rotateVectorByQuaternion(const Vector &v, const Vector4 &q) {
        Vector qVec(q[1], q[2], q[3]);
        Vector cross1 = qVec.cross(v);
        Vector cross2 = qVec.cross(cross1);
        return v + Float(2.0) * (q[0] * cross1 + cross2);
    }

    /**
     * @brief Builds the unit quaternion that rotates from onto to.
     * Expects both input vectors to be normalized.
     */
    inline Vector4 quatFromTo(const Vector &from, const Vector &to) {
        Float d = from.dot(to);
        if (d > Float(0.999999))
            return Vector4(Float(1), Float(0), Float(0), Float(0)); // Identity

        if (d < Float(-0.999999)) {
            // 180° rotation, so pick any perpendicular axis
            Vector axis = (std::abs(from[0]) < std::abs(from[2]))
                ? Vector(Float(0), -from[2], from[1])
                : Vector(-from[1], from[0], Float(0));
            axis = axis.normalized();
            return Vector4(Float(0), axis[0], axis[1], axis[2]);
        }

        Float w = Float(1) + d;
        Vector xyz = from.cross(to);
        return Vector4(w, xyz[0], xyz[1], xyz[2]).normalized();
    }

    /// @brief Multiplies two quaternions.
    inline Vector4 multiplyQuaternions(const Vector4& q1, const Vector4& q2) {
        return Vector4(
            q1[0]*q2[0] - q1[1]*q2[1] - q1[2]*q2[2] - q1[3]*q2[3],
            q1[0]*q2[1] + q1[1]*q2[0] + q1[2]*q2[3] - q1[3]*q2[2],
            q1[0]*q2[2] - q1[1]*q2[3] + q1[2]*q2[0] + q1[3]*q2[1],
            q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1] + q1[3]*q2[0]
        );
    }

    /**
     * @brief First computes the rotation needed for rotating one vector (e.g. direction of a hair) onto another,
     * then tries to fit another set of vectors with a second rotation (e.g. normals of old and new hair surfaces).
     * (e.g. for feathers: Ensure that the rotated hair curves in the direction of the target surface, if it curves.)
     * @param originalDir Original vector.
     * @param newDir Vector we want to rotate onto.
     * @param originalNormal Original direction of second set of vectors
     * (e.g. normal of the surface the original hair is attached to).
     * @param newNormal New direction of second set of vectors
     * (e.g. normal of the surface the new hair is attached to.)
     */
    inline Matrix4x4 computeDoubleRotation(const Vector &originalDir, const Vector &newDir,
                                           const Vector &originalNormal, const Vector &newNormal) {
        Float dotProduct = originalDir.dot(newDir);
        Vector xyz; // Perpendicular vector
        Vector4 q1;

        if (dotProduct < Float(-0.999999)) { // Vectors are nearly opposite (need 180° rotation)
            if (abs(originalDir[0]) > abs(originalDir[2])) xyz = Vector(-originalDir[1], originalDir[0], Float(0.0));
            else xyz = Vector(Float(0.0), -originalDir[2],originalDir[1]);
            xyz = xyz.normalized();
            q1 = Vector4(Float(0.0), xyz[0], xyz[1], xyz[2]);
        } else if (dotProduct > Float(0.999999)) q1 = Vector4(Float(1.0), Float(0.0), Float(0.0), Float(0.0)); // Vectors are already aligned
        else { // General case: Compute rotation quaternion
            Float w = Float(1.0) + dotProduct;
            xyz = originalDir.cross(newDir);
            q1 = Vector4(w, xyz[0], xyz[1], xyz[2]).normalized();
        }

        // Rotation - Adjust normal orientation (rotate around aligned direction)
        // Apply first rotation to see where normal ended up
        Vector rotatedNormal = rotateVectorByQuaternion(originalNormal, q1);

        // Project both normals onto plane perpendicular to newDir
        Vector projRotatedN = (rotatedNormal - rotatedNormal.dot(newDir) * newDir).normalized();
        Vector projTargetN = (newNormal - newNormal.dot(newDir) * newDir).normalized();

        // Rotation - Calculate rotation around newDir axis
        Float normalDot = projRotatedN.dot(projTargetN);
        Vector4 q2;
        if (normalDot < Float(-0.999999)) q2 = Vector4(Float(0.0), newDir[0], newDir[1], newDir[2]);
        else if (normalDot > Float(0.999999)) q2 = Vector4(Float(1.0), Float(0.0), Float(0.0), Float(0.0));
        else {
            Vector crossProd = projRotatedN.cross(projTargetN);
            Float angle = std::acos(std::clamp(normalDot, Float(-1.0), Float(1.0)));
            if (crossProd.dot(newDir) < 0) angle = -angle;
            
            Float halfAngle = angle * Float(0.5);
            Float sinHalf = std::sin(halfAngle);
            q2 = Vector4(std::cos(halfAngle), 
                        newDir[0] * sinHalf, 
                        newDir[1] * sinHalf, 
                        newDir[2] * sinHalf);
        }

        Vector4 qFinal = multiplyQuaternions(q2, q1);

        // Rotation - Convert qFinal into 4x4 matrix
        Float xx = qFinal[1] * qFinal[1], yy = qFinal[2] * qFinal[2], zz = qFinal[3] * qFinal[3];
        Float xy = qFinal[1] * qFinal[2], xz = qFinal[1] * qFinal[3], yz = qFinal[2] * qFinal[3];
        Float wx = qFinal[0] * qFinal[1], wy = qFinal[0] * qFinal[2], wz = qFinal[0] * qFinal[3];

        Matrix4x4 rotationMatrix = Matrix4x4 {
            Float(1.0) - Float(2.0)*(yy + zz),  Float(2.0)*(xy - wz),               Float(2.0)*(xz + wy),               Float(0.0),
            Float(2.0)*(xy + wz),               Float(1.0) - Float(2.0)*(xx + zz),  Float(2.0)*(yz - wx),               Float(0.0),
            Float(2.0)*(xz - wy),               Float(2.0)*(yz + wx),               Float(1.0) - Float(2.0)*(xx + yy),  Float(0.0),
            Float(0.0),                         Float(0.0),                         Float(0.0),                         Float(1.0)
        };
        return rotationMatrix;
    }

    /**
     * @brief Transforms given curve so that its first control point p0 is in the coordinate origin and the curve tip points straight up along the Z-axis.
     * Then rotates it further to ensure it lies in the XY-plane
     * This is used in curve comparison so that curves can be compared more easily.
     * @param cps Pointer to the first point of the curve in the list of control points.
     */
    inline std::array<Point, 4> canonicalize(const Point *cps) {
        // 1. Translate p0 to origin
        Vector offset(cps[0][0], cps[0][1], cps[0][2]);
        Point shifted[4];
        for (int i = 0; i < 4; ++i)
            shifted[i] = cps[i] - offset;

        // 2. Rotate chord (p3) onto +Z
        Vector chord = Vector(shifted[3][0], shifted [3][1], shifted[3][2]);
        Float chordLen = chord.length();

        std::array<Point, 4> result;
        if (chordLen < Float(1e-8)) {
            // Degenerate: zero-length chord, return as-is
            for (int i = 0; i < 4; ++i) result[i] = shifted[i];
            return result;
        }

        Vector4 q1 = quatFromTo(chord / chordLen, Vector(0, 0, 1));
        for (int i = 0; i < 4; ++i) {
            Vector v(shifted[i][0], shifted[i][1], shifted[i][2]);
            Vector rv = rotateVectorByQuaternion(v, q1);
            result[i] = Point(rv);
        }

        // 3. Rotate around Z to bring p1 into the XZ-plane (Y component of p1 -> 0)
        Vector p1(result[1][0], result[1][1], result[1][2]);
        Float p1xy = sqrt(p1[0] * p1[0] + p1[1] * p1[1]); // Distance from Z-axis

        if (p1xy > Float(1e-8)) {
            // Angle needed to rotate p1 into XZ-plane (so that p1.y = 0, p1.x > 0)
            Float angle = -atan2(p1[1], p1[0]);
            Float cosA = std::cos(angle);
            Float sinA = std::sin(angle);

            // Rotate all points around Z by this angle
            for (int i = 0; i < 4; ++i) {
                Float x = result[i][0];
                Float y = result[i][1];
                result[i] = Point(
                    cosA * x - sinA * y,
                    sinA * x + cosA * y,
                    result[i][2]
                );
            }
        }

        // If p1xy is near zero, P1 lies on the Z-axis (straight curve), so any rotation is equivalent

        return result;
    }

    // MARK: - Bézier Math
    /**
     * @brief Fits a cubic bezier curve to vertices using least squares optimization.
     * The start and end points are fixed, only the control handles are optimized.
     * @param vertices The actual hair vertices to fit the curve to
     * @param params Parameter values (0-1) corresponding to each vertex
     * @param w0 Fixed start point
     * @param w3 Fixed end point
     * @param w1 Output: optimized first control handle
     * @param w2 Output: optimized second control handle
     */
    inline void fitCubicBezier(const std::vector<Point>& vertices, 
                        const std::vector<Float>& params,
                        const Point& w0, const Point& w3,
                        Point& w1, Point& w2) {
        const size_t n = vertices.size();
        if (n < 3) {
            // Fallback to simple tangent-based approach
            Point tangent = (w3 - w0) / Float(3.0);
            w1 = w0 + tangent;
            w2 = w3 - tangent;
            return;
        }
        
        // Set up least squares system: A * x = b; solve separately for each component (x, y, z)
        for (int comp = 0; comp < 3; ++comp) {
            std::vector<std::vector<Float>> A(n, std::vector<Float>(2));
            std::vector<Float> b(n);

            for (size_t i = 0; i < n; ++i) {
                const Float t = params[i];
                const Float t2 = t * t;
                const Float t3 = t2 * t;
                const Float mt = 1.0 - t;
                const Float mt2 = mt * mt;
                const Float mt3 = mt2 * mt;

                // Coefficients for w1 and w2 in cubic bezier formula
                const Float coeffW1 = Float(3.0) * mt2 * t;
                const Float coeffW2 = Float(3.0) * mt * t2;

                A[i][0] = coeffW1;
                A[i][1] = coeffW2;

                // Target: we want B(t) = vertices[i], so target = vertices[i] - (fixed terms); fixed terms are contributions from w0 and w3
                const Float fixedContribution = w0[comp] * mt3 + w3[comp] * t3;
                b[i] = vertices[i][comp] - fixedContribution;
            }

            // Solve normal equations: (A^T * A) * x = A^T * b
            std::vector<std::vector<Float>> ATA(2, std::vector<Float>(2, Float(0.0)));
            std::vector<Float> ATb(2, Float(0.0));

            // Compute A^T * A
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) {
                    for (size_t k = 0; k < n; ++k) {
                        ATA[i][j] += A[k][i] * A[k][j];
                    }
                }
            }

            // Compute A^T * b
            for (int i = 0; i < 2; ++i) {
                for (size_t k = 0; k < n; ++k) {
                    ATb[i] += A[k][i] * b[k];
                }
            }

            // Solve 2x2 system
            const Float det = ATA[0][0] * ATA[1][1] - ATA[0][1] * ATA[1][0];

            if (abs(det) < Float(1e-10)) { // Singular matrix, use fallback
                const Float tangentComponent = (w3[comp] - w0[comp]) / Float(3.0);
                w1[comp] = w0[comp] + tangentComponent;
                w2[comp] = w3[comp] - tangentComponent;
            } else { // Solve for handle coordinates
                w1[comp] = (ATb[0] * ATA[1][1] - ATb[1] * ATA[0][1]) / det;
                w2[comp] = (ATA[0][0] * ATb[1] - ATA[1][0] * ATb[0]) / det;
            }
        }
    }

    /** @brief Evaluates point on bezier curve at parameter t.
     * @param bezierCPs Where the bezier curve is saved as its individual control points (potentially together with other curves).
     * @param t How far along the curve we want to be (between 0 and 1).
     * @param idx At which index in the CP list we need to look (where the first CP is).
     */
    inline Point evaluateBezier(const std::vector<Point> &bezierCPs, const Float t, const int32_t idx = 0) {
        const Float omt = Float(1.0) - t;
        const Float omt2 = omt * omt;
        const Float omt3 = omt2 * omt;
        const Float t2 = t * t;
        const Float t3 = t2 * t;

        return bezierCPs[idx] * omt3 +
            bezierCPs[idx + 1] * (3 * omt2 * t) +
            bezierCPs[idx + 2] * (3 * omt * t2) +
            bezierCPs[idx + 3] * t3;
    }

    /** @brief Use a simple sampling strategy to estimate the length of a given bezier curve. 
     * @param bezierCPs Where the bezier curve is saved as its individual control points (potentially together with other curves).
     * @param idx At which index in the CP list we need to look (where the first CP is).
     * @param numSamples The number of samples we want to take; the bigger the more accurate the estimate will be. Should never be below 2.
     */
    inline Float estimateBezierLength(const std::vector<Point> &bezierCPs,
                                      const int32_t idx = 0, const int32_t numSamples = 20) {
        Float length = Float(0.0);
        Point prevPoint =  evaluateBezier(bezierCPs, Float(0.0), idx);

        for (int32_t i = 1; i <= numSamples; i++) {
            const Float t = Float(i) / numSamples;
            const Point currentPoint = evaluateBezier(bezierCPs, t, idx);
            length += (currentPoint - prevPoint).length();
            prevPoint = currentPoint;
        }

        return length;
    }

    /** @brief Evaluates derivative (tangent) of a bezier curve at parameter t.
     * The result is NOT normalized.
     * @param bezierCPs Where the bezier curve is saved as its individual control points (potentially together with other curves).
     * @param t How far along the curve we want to be (between 0 and 1).
     * @param idx At which index in the CP list we need to look (where the first CP is).
     */
    inline Vector evaluateBezierDerivative(const std::vector<Point> &bezierCPs, const Float t,
                                           const int32_t idx = 0) {
        const Float omt = Float(1.0) - t;
        const Float omt2 = omt * omt;
        const Float t2 = t * t;

        Point w1 = bezierCPs[idx + 1];
        Point w2 = bezierCPs[idx + 2];

        return (w1 - bezierCPs[idx]) * (3 * omt2) +
            (w2 - w1) * (6 * omt * t) +
            (bezierCPs[idx + 3] - w2) * (3 * t2); 
    }

    /** @brief Calculates the normalized tangent vector of a bezier at parameter t.
     * The result is normalized.
     * @param bezierCPs Where the bezier curve is saved as its individual control points (potentially together with other curves).
     * @param t How far along the curve we want to be (between 0 and 1).
     * @param idx At which index in the CP list we need to look (where the first CP is).
     */
    inline Vector getBezierTangent(const std::vector<Point> &bezierCPs, const Float t,
                            const int32_t idx = 0) {
        Vector tangent = evaluateBezierDerivative(bezierCPs, t, idx);
        // Float length = tangent.length();

        return tangent.normalized(); // This should be sufficient tbh

        // if (length > Float(0.0001)) return tangent * (Float(1.0) / length);

        // Handle corner cases, e.g. at inflection points or when control points overlap
        // if (t > Epsilon) {
        //     Point p1 = evaluateBezier(bezierCPs, t, idx);
        //     Point p2 = evaluateBezier(bezierCPs, t - Epsilon, idx);
        //     Vector direction = p1 - p2;
        //     Float dirLength = direction.length();
        //     if (dirLength > Float(0.0001)) return direction * (Float(1.0) / dirLength);
        // }

        // if (t < Float(1.0) - Epsilon) {
        //     Point p1 = evaluateBezier(bezierCPs, t + Epsilon, idx);
        //     Point p2 = evaluateBezier(bezierCPs, t, idx);
        //     Vector direction = p1 - p2;
        //     Float dirLength = direction.length();
        //     if (dirLength > Float(0.0001)) return direction * (Float(1.0) / dirLength);
        // }

        // return Vector(Float(1.0), Float(0.0), Float(0.0));
    }

    /**
     * @brief Closed-form ∫₀¹ ‖A(t)−B(t)‖² dt for two cubic Bézier curves whose control points are already in the same canonical frame.
     * Ideally use canonicalize() first to achieve this.
     * cpsA / cpsB must each point to 4 consecutive Points.
     */
    inline Float bezierL2Sq(const Point *cpsA, const Point *cpsB) {
        // Differences of control points
        Vector d[4];
        for (int i = 0; i < 4; ++i)
            d[i] = Vector(cpsA[i] - cpsB[i]);

        // Dot-product helpers
        auto dp = [&](int i, int j) -> Float { return d[i].dot(d[j]); };

        Float sum =
            Float(6) * dp(0,0) +
            Float(6) * dp(0,1) +
            Float(3) * dp(0,2) +
            Float(2) * dp(0,3) +
            Float(6) * dp(1,1) +
            Float(6) * dp(1,2) +
            Float(3) * dp(1,3) +
            Float(6) * dp(2,2) +
            Float(6) * dp(2,3) +
            Float(6) * dp(3,3);

        return sum / Float(35);
    }

    // MARK: - Comparisons

    /**
     * Rotation- and translation-invariant L² distance between two single-segment cubic Bézier curves.
     * @param hairCPs All control points of the hairs.
     * @param firstIdx The index of the first hair we want to compare.
     * @param secondIdx The index of the second hair we want to compare.
     * @param numCPsPerHair How many control points each hair has.
     * @param numSegments How many segments each hair has.
     */
    inline Float calculateCurveDistance(const std::vector<Point> &hairCPs, // TODO It would be nice if the canonicalization would only be done once before calling this function
                                        const int32_t firstIdx, const int32_t secondIdx,
                                       const int32_t numCPsPerHair) {
        const Point *cpsA = hairCPs.data() + firstIdx * numCPsPerHair;
        const Point *cpsB = hairCPs.data() + secondIdx * numCPsPerHair;

        std::array<Point, 4> canonA = canonicalize(cpsA);
        std::array<Point, 4> canonB = canonicalize(cpsB);

        // // Version that samples points along curve and compares
        // Float l2sq = bezierL2Sq(canonA.data(), canonB.data());

        // // Guard against tiny negative values from floating-point rounding
        // return std::sqrt(std::max(l2sq, Float(0)));

        // Version that only compares last three control points (p0 is always equal)
        Float d = Float(0.0f);
        for (int32_t i = 1; i <= 3; ++i) {
            Vector diff = Vector(canonA[i] - canonB[i]);
            d += diff.lengthSquared();
        }

        return std::sqrt(d);
    }

    inline std::vector<size_t> findRepresentativeCurves(const std::vector<Point> &hairCPs,
                                                       const int32_t numSpines,
                                                       const int32_t numCPsPerHair,
                                                       const int32_t k,
                                                       const int32_t maxIterations = 100) {

        // Randomly select k initial medoids
        std::vector<int32_t> medoids;
        std::vector<int32_t> candidates(numSpines);
        std::iota(candidates.begin(), candidates.end(), 0); // Fill vector with 0 to numSpines - 1
        for (int32_t i = 0; i < k; ++i) {
            medoids.push_back(candidates[i]);
        }

        // Assign cluster for each curve
        std::vector<int32_t> assignments(numSpines, -1);
        bool changed = true;
        int32_t iteration = 0;

        while (changed && iteration < maxIterations) {
            changed = false;
            iteration++;

            // Assign each curve to nearest medoid
            for (int32_t curveIdx : candidates) {
                Float minDist = Infinity;
                int32_t bestMedoid = -1;

                for (int32_t medoidIdx = 0; medoidIdx < k; ++medoidIdx) {
                    Float dist = calculateCurveDistance(hairCPs, curveIdx, medoids[medoidIdx], numCPsPerHair);

                    if (dist < minDist) {
                        minDist = dist;
                        bestMedoid = medoidIdx;
                    }
                }

                if (assignments[curveIdx] != bestMedoid) {
                    assignments[curveIdx] = bestMedoid;
                    changed = true;
                }
            }
        }

        // Find new medoid for each cluster (the curve that minimizes sum of distances within cluster)
        for (int32_t clusterIdx = 0; clusterIdx < k; ++clusterIdx) {
            // Get all curves in this cluster
            std::vector<int32_t> clusterMembers;
            for (int32_t curveIdx : candidates) {
                if (assignments[curveIdx] == clusterIdx) clusterMembers.push_back(curveIdx);
            }

            if (clusterMembers.empty()) continue;

            // Find the member with minimum total distance to all other members
            Float minTotalDist = Infinity;
            int32_t bestMedoid = medoids[clusterIdx];

            for (int32_t candidateIdx : clusterMembers) {
                Float totalDist = Float(0.0);
                for (int32_t otherIdx : clusterMembers) {
                    if (candidateIdx != otherIdx) {
                        totalDist += calculateCurveDistance(hairCPs, candidateIdx, otherIdx, numCPsPerHair);
                    }
                }

                if (totalDist < minTotalDist) {
                    minTotalDist = totalDist;
                    bestMedoid = candidateIdx;
                }
            }

            if (medoids[clusterIdx] != bestMedoid) {
                medoids[clusterIdx] = bestMedoid;
                changed = true;
            }
        }

        return std::vector<size_t>(medoids.begin(), medoids.end());
    }

    // MARK: - PCA & SVD

    /**
     * @brief Class that uses the Eigen library to perform PCA.
     * It is used by the Feathers class to find a good normal to the rectangle
     * that will be used as the low-detail version of a feather.
     */
    class PCA {
    private:
        Eigen::MatrixXd m_data;
        Eigen::Vector3d m_mean;
        Eigen::Matrix3d m_covariance;
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> m_eigenSolver;

    public:
        void fit(const Eigen::MatrixXd& points) { // Rows of matrix are the points
            m_data = points;
            int num_points = points.rows();
            
            // Calculate mean
            m_mean = points.colwise().mean();
            
            // Center the data
            Eigen::MatrixXd centered = points.rowwise() - m_mean.transpose();
            
            // Calculate covariance matrix
            m_covariance = (centered.transpose() * centered) / (num_points - 1);
            
            // Compute eigenvalues and eigenvectors
            m_eigenSolver.compute(m_covariance);
        }
        
        Eigen::Vector3d getMean() const { return m_mean; }
        
        Eigen::Vector3d getNormal() const {
            // The normal to the plane is the eigenvector with smallest eigenvalue
            int min_idx;
            m_eigenSolver.eigenvalues().minCoeff(&min_idx);
            return m_eigenSolver.eigenvectors().col(min_idx);
        }
        
        Eigen::Vector3d getPrincipalDirection() const {
            // The principal direction is the eigenvector with largest eigenvalue
            int max_idx;
            m_eigenSolver.eigenvalues().maxCoeff(&max_idx);
            return m_eigenSolver.eigenvectors().col(max_idx);
        }

        std::pair<Vector, Vector> getTwoPrincipalDirections() const {
            Eigen::Vector3d eigenvals = m_eigenSolver.eigenvalues();

            // Find indices sorted by eigenvalue (descending)
            std::vector<int> indices = {0, 1, 2};
            std::sort(indices.begin(), indices.end(),
                      [&eigenvals](int a, int b) { return eigenvals(a) > eigenvals(b); });

            Eigen::Matrix3d sortedEigenvecs;
            for (int i = 0; i < 3; ++i) {
                sortedEigenvecs.col(i) = m_eigenSolver.eigenvectors().col(indices[i]);
            }

            Vector first(sortedEigenvecs(0,0), sortedEigenvecs(1,0), sortedEigenvecs(2,0)); // First column
            Vector second(sortedEigenvecs(0,1), sortedEigenvecs(1,1), sortedEigenvecs(2,1)); // Second column
            return {first, second};
        }
        
        Eigen::Vector3d getEigenvalues() const {
            return m_eigenSolver.eigenvalues();
        }
        
        Eigen::Matrix3d getEigenvectors() const {
            return m_eigenSolver.eigenvectors();
        }
    };

    /**
     * @brief Class that uses the Eigen library to perform SVD.
     * It is used by the Feathers class to find a good normal to the rectangle
     * that will be used as the low-detail version of a feather.
     */
    class SVD {
    private:
        Eigen::MatrixXd m_data;
        Eigen::Vector3d m_mean;
        Eigen::JacobiSVD<Eigen::MatrixXd> m_svd;
        Eigen::Vector3d m_singularValues;
        Eigen::Matrix3d m_principalComponents;

    public:
        void fit(const Eigen::MatrixXd &points) {
            m_data = points;

            // Calculate mean
            m_mean = points.colwise().mean();

            // Center the data
            Eigen::MatrixXd centered = points.rowwise() - m_mean.transpose();

            // Perform SVD on centered data
            m_svd.compute(centered, Eigen::ComputeThinU | Eigen::ComputeFullV); // ComputeFullU

            // The principal components are the columns of V (right singular vectors)
            m_principalComponents = m_svd.matrixV();

            // The singular values relate to eigenvalues by: eigenvalue = (singularValue)^2 / (n-1)
            m_singularValues = m_svd.singularValues();
        }
        
        Eigen::Vector3d getMean() const { return m_mean; }
        
        Eigen::Vector3d getNormal() const {
            return m_principalComponents.col(2);
        }
        
        Eigen::Vector3d getPrincipalDirection() const {
            return m_principalComponents.col(0);
        }

        std::pair<Vector, Vector> getTwoPrincipalDirections() const {
            // SVD already sorts by singular value in descending order
            Eigen::Vector3d first = m_principalComponents.col(0);   // Largest singular value
            Eigen::Vector3d second = m_principalComponents.col(1);  // Second largest singular value
            
            Vector first_vec(first(0), first(1), first(2));
            Vector second_vec(second(0), second(1), second(2));
            return {first_vec, second_vec};
        }
        
        Eigen::Vector3d getEigenvalues() const {
            int num_points = m_data.rows();
            Eigen::Vector3d eigenvals(3);
            
            // Convert singular values to eigenvalues
            for (int i = 0; i < 3; ++i) {
                if (i < m_singularValues.size()) {
                    eigenvals(i) = (m_singularValues(i) * m_singularValues(i)) / (num_points - 1);
                } else {
                    eigenvals(i) = 0.0;
                }
            }
            
            return eigenvals;
        }
        
        Eigen::Matrix3d getEigenvectors() const {
            return m_principalComponents;
        }
    };

    /// @brief Converts std::vector containing lightwave::Vector to Eigen::MatrixXd.
    Eigen::MatrixXd pointsToMatrix(const std::vector<Point> &points) {
        Eigen::MatrixXd matrix(points.size(), 3);
        for (size_t i = 0; i < points.size(); ++i) {
            matrix(i, 0) = points[i][0];
            matrix(i, 1) = points[i][1];
            matrix(i, 2) = points[i][2];
        }
        return matrix;
    }

    /// @brief Converts Eigen::MatrixXd to std::vector containing lightwave::Vector.
    std::vector<Vector> matrixToVectors(const Eigen::MatrixXd &matrix) {
        std::vector<Vector> vectors;
        for (int i = 0; i < matrix.rows(); ++i) {
            Vector v(matrix(i, 0), matrix(i, 1), matrix(i, 2));
            vectors.push_back(v);
        }
        return vectors;
    }

    /// @brief Converts Eigen::Vector3d to lightwave::Vector.
    Vector vector3dToVector(const Eigen::Vector3d &v) {
        return Vector(v(0), v(1), v(2));
    }

}