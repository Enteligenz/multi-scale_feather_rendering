#include <lightwave.hpp>

namespace lightwave {

class Mandelbulb : public Shape {
    static constexpr Float Power      = 3;
    static constexpr Float SdfEpsilon = 1e-4;
    static constexpr int MaxSteps     = 200;

    Float smoothmin(Float a, Float b, Float strength) const {
        return -log(exp(-a * strength) + exp(-b * strength)) / strength;
    }

    Float sdf(const Point &pos, int &steps) const {
        // Float a = (pos - Point(Float(-0.5),0,0)).length() - 1;
        // Float b = (pos - Point(Float(+0.5),0,0)).length() - 1;
        // Float k = 20;
        // return smoothmin(a, b, 10);
        // return min(a, b);

        Vector z{ pos };
        Float dr = 1;
        Float r  = 0;
        for (steps = 0; steps < 20; steps++) {
            r = z.length();
            if (r > 4)
                break;

            // convert to polar coordinates
            Float theta = acos(z.z() / r);
            Float phi   = atan2(z.y(), z.x());
            dr          = pow(r, Power - 1) * Power * dr + 1;

            // scale and rotate the point
            Float zr = pow(r, Power);
            theta    = theta * Power;
            phi      = phi * Power;

            // convert back to cartesian coordinates
            z = zr * Vector(sin(theta) * cos(phi), sin(theta) * sin(phi),
                            cos(theta)) +
                Vector(pos);
        }

        return Float(0.5) * std::log(r) * r / dr;
    }

    Vector normal(const Point &pos) const {
        int steps;
        return (Vector(sdf(pos, steps)) -
                Vector(sdf(pos + Vector(SdfEpsilon, 0, 0), steps),
                       sdf(pos + Vector(0, SdfEpsilon, 0), steps),
                       sdf(pos + Vector(0, 0, SdfEpsilon), steps)))
            .normalized();
    }

public:
    Mandelbulb(const Properties &properties) {}

    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override {
        int steps;

        Float t = Epsilon;
        for (int i = 0; i < MaxSteps; i++) {
            Float dist = sdf(ray(t), steps);
            if (dist >= its.t || dist > 1e+3)
                break;
            if (dist < SdfEpsilon) {
                its.t = t;

                its.position = ray(t);
                its.uv.x()   = its.position.x();
                its.uv.y()   = its.position.y();
                its.shadingNormal = normal(its.position);
                its.geometryNormal = normal(its.position);
                its.tangent = Vector(1, 0, 0);

                return true;
            }
            t += dist;
        }

        return false;
    }

    Bounds getBoundingBox() const override {
        return Bounds(Point(-10), Point(+10));
    }

    Point getCentroid() const override { return Point(0); }

    std::string toString() const override { return "Mandelbulb[]"; }
};

} // namespace lightwave

REGISTER_SHAPE(Mandelbulb, "mandelbulb")
