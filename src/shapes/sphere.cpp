#include <lightwave.hpp>

namespace lightwave {

inline bool solveQuadratic(const Float &b, const Float &c, Float &x0, Float &x1) {
    // assumes that a=1
    Float discr = b * b - 4 * c;
    if (discr < 0) return false;
    else if (discr == 0) x0 = x1 = - Float(0.5) * b;
    else {
        Float q = (b > 0) ?
            Float(-0.5) * (b + sqrt(discr)) :
            Float(-0.5) * (b - sqrt(discr));
        x0 = q;
        x1 = c / q;
    }
    if (x0 > x1) std::swap(x0, x1);
    
    return true;
}

class Sphere : public Shape {
    void populate(SurfaceEvent &surf, const Point &position) const {
        surf.position = Point(Vector(position).normalized());
        surf.uv       = Vector2(
            (std::atan2(-surf.position.z(), surf.position.x()) + Pi) * Inv2Pi,
            safe_acos(surf.position.y()) * InvPi);

        surf.geometryNormal = Vector(surf.position);
        surf.shadingNormal  = surf.geometryNormal;
        surf.tangent        = Vector(1, 0, 0);
        surf.pdf            = Inv4Pi;
    }

public:
    Sphere(const Properties &properties) {}

    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override {
        PROFILE("Sphere")
        
        const Float b = 2 * ray.direction.dot(Vector(ray.origin));
        const Float c = Vector(ray.origin).lengthSquared() - 1;

        Float t, tFar;
        if (!solveQuadratic(b, c, t, tFar)) return false;
        if (t < Epsilon) t = tFar;
        if (t < Epsilon || t >= its.t) return false;

        its.t = t;
        populate(its, ray(its.t));
        return true;
    }

    Bounds getBoundingBox() const override {
        return Bounds(Point(-1), Point(+1));
    }

    Point getCentroid() const override { return Point(0); }

    AreaSample sampleArea(Sampler &rng) const override {
        AreaSample sample;
        populate(sample, squareToUniformSphere(rng.next2D()));
        return sample;
    }

    std::string toString() const override { return "Sphere[]"; }
};

} // namespace lightwave

REGISTER_SHAPE(Sphere, "sphere")
