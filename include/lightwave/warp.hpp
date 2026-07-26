/**
 * @file warp.hpp
 * @brief Contains functions that map one domain to another.
 */

#pragma once

#include <lightwave/math.hpp>

namespace lightwave {

/**
 * @brief Warps a given point from the unit square ([0,0] to [1,1]) to a unit
 * circle (centered around [0,0] with radius 1), with uniform density given by
 * @code 1 / Pi @endcode .
 * @see Based on
 * http://psgraphics.blogspot.ch/2011/01/improved-code-for-concentric-map.html
 */
inline Point2 squareToUniformDiskConcentric(const Point2 &sample) {
    Float r1 = 2 * sample.x() - 1;
    Float r2 = 2 * sample.y() - 1;

    Float phi, r;
    if (r1 == 0 && r2 == 0) {
        r   = 0;
        phi = 0;
    } else if (r1 * r1 > r2 * r2) {
        r   = r1;
        phi = Pi4 * (r2 / r1);
    } else {
        r   = r2;
        phi = Pi2 - Pi4 * (r1 / r2);
    }

    Float cosPhi = std::cos(phi);
    Float sinPhi = std::sin(phi);
    return { r * cosPhi, r * sinPhi };
}

/**
 * @brief Warps a given point from the unit square ([0,0] to [1,1]) to a unit
 * sphere (centered around [0,0,0] with radius 1), with uniform density given by
 * @code 1 / (4 * Pi) @endcode .
 */
inline Vector squareToUniformSphere(const Point2 &sample) {
    Float z      = 1 - 2 * sample.y();
    Float r      = safe_sqrt(1 - z * z);
    Float phi    = 2 * Pi * sample.x();
    Float cosPhi = std::cos(phi);
    Float sinPhi = std::sin(phi);
    return { r * cosPhi, r * sinPhi, z };
}

/**
 * @brief Warps a given point from the unit square ([0,0] to [1,1]) to a unit
 * hemisphere (centered around [0,0,0] with radius 1, pointing in z direction),
 * with respect to solid angle.
 */
inline Vector squareToUniformHemisphere(const Point2 &sample) {
    Point2 p = squareToUniformDiskConcentric(sample);
    Float z  = Float(1.0) - p.x() * p.x() - p.y() * p.y();
    Float s  = sqrt(z + Float(1.0));
    return { s * p.x(), s * p.y(), z };
}

/// @brief Returns the density of the @ref squareToUniformHemisphere warping.
inline Float uniformHemispherePdf() { return Inv2Pi; }

/**
 * @brief Warps a given point from the unit square ([0,0] to [1,1]) to a unit
 * hemisphere (centered around [0,0,0] with radius 1, pointing in z direction),
 * with density given by @code cos( angle( result, [0,0,1] ) ) @endcode .
 */
inline Vector squareToCosineHemisphere(const Point2 &sample) {
    Point2 p = squareToUniformDiskConcentric(sample);
    Float z  = safe_sqrt(Float(1.0) - p.x() * p.x() - p.y() * p.y());
    return { p.x(), p.y(), z };
}

/// @brief Returns the density of the @ref squareToCosineHemisphere warping.
inline Float cosineHemispherePdf(const Vector &vector) {
    return InvPi * std::max(vector.z(), Float(0));
}

} // namespace lightwave
