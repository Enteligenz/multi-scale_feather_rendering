/**
 * @brief Functions for dealing with Fresnel computations.
 * @file fresnel.hpp
 */

#pragma once

#include <lightwave/color.hpp>
#include <lightwave/math.hpp>

namespace lightwave {

inline Float schlickWeight(Float cosTheta) {
    Float m = saturate(1 - cosTheta);
    return (m * m) * (m * m) * m;
}

/**
 * The Schlick approximation of the Fresnel term.
 * @note See "An Inexpensive BRDF Model for Physically-based Rendering" [Schlick
 * 1994].
 */
template <typename T> inline T schlick(T F0, Float cosTheta) {
    return F0 + (1 - F0) * schlickWeight(cosTheta);
}

/**
 * Unpolarized Fresnel term for dielectric materials.
 * @param cosThetaI Cosine of the incident direction with respect to the surface
 * normal.
 * @param eta The relative IOR (n2 / n1).
 */
inline Float fresnelDielectric(Float cosThetaI, Float eta) {
    const Float invEta = 1 / eta;
    Float cosThetaTSqr = 1 - sqr(invEta) * (1 - sqr(cosThetaI));
    if (cosThetaTSqr <= Float(0.0)) {
        /// total internal reflection
        return 1;
    }

    cosThetaI       = abs(cosThetaI);
    Float cosThetaT = sqrt(cosThetaTSqr);

    Float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    Float Rp = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);

    /// Average the power of both polarizations
    return Float(0.5) * (Rs * Rs + Rp * Rp);
}

/**
 * Fresnel equations for dielectric/dielectric interfaces.
 * @param R Output.
 * @param phi Output.
 */
inline void fresnelDielectric(Float ct1, Float n1, Float n2, Vector2 &R, Vector2 &phi) {
    Float st1 = (Float(1.0f) - ct1 * ct1); // Sinus theta1 "squared"
    Float nr = n1 / n2;

    if (sqr(nr) * st1 > Float(1.0f)) { // Total reflection
        R = Vector2(Float(1.0f));
        phi = Float(2.0f) * Vector2(atan(-sqr(nr) * sqrt(st1 - Float(1.0f) / sqr(nr)) / ct1),
                                    atan(-sqrt(st1 - Float(1.0f) / sqr(nr)) / ct1));
    } else { // Transmission & Reflection
        Float ct2 = sqrt(Float(1.0f) - sqr(nr) * st1);
        Vector2 r = Vector2((n2 * ct1 - n1 * ct2) / (n2 * ct1 + n1 * ct2),
                            (n1 * ct1 - n2 * ct2) / (n1 * ct1 + n2 * ct2));
        phi.x() = (r.x() < Float(0.0f)) ? Pi : Float(0.0f);
        phi.y() = (r.y() < Float(0.0f)) ? Pi : Float(0.0f);
        R = sqr(r);
    }
}

/**
 * Fresnel equations for dielectric/conductor interfaces.
 * @param R Output.
 * @param phi Output.
 */
inline void fresnelConductor(Float ct1, Float n1, Float n2, Float k, Vector2 &R, Vector2 &phi) {
    if (k == 0) { // Use dielectric formula to avoid numerical issues
        fresnelDielectric(ct1, n1, n2, R, phi);
        return;
    }

    Float A = sqr(n2) * (Float(1.0f) - sqr(k)) - sqr(n1) * (Float(1.0f) - sqr(ct1));
    Float B = sqrt(sqr(A) + sqr(Float(2.0f) * sqr(n2) * k));
    float U = sqrt((A + B) / Float(2.0f));
    float V = sqrt((B - A) / Float(2.0f));

    R.y() = (sqr(n1 * ct1 - U) + sqr(V)) / (sqr(n1 * ct1 + U) + sqr(V));
    phi.y() = atan2(Float(2.0f) * n1 * V * ct1, sqr(U) + sqr(V) - sqr(n1 * ct1)) + Pi;

    R.x() = (sqr(sqr(n2) * (Float(1.0f) - sqr(k)) * ct1 - n1 * U) + sqr(Float(2.0f) * sqr(n2) * k * ct1 - n1 * V)) 
			/ ( sqr(sqr(n2) * (Float(1.0f) - sqr(k)) * ct1 + n1 * U) + sqr(Float(2.0f) * sqr(n2) * k * ct1 + n1 * V));
	phi.x() = atan2(Float(2.0f) * n1 * sqr(n2) * ct1 * (Float(2.0f) * k * U - (Float(1.0f) - sqr(k)) * V),
                   sqr(sqr(n2) * (Float(1.0f) + sqr(k)) * ct1) - sqr(n1) * (sqr(U) + sqr(V)));
}

} // namespace lightwave
