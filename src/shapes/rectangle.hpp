#include <lightwave.hpp>

namespace lightwave {

/// @brief A rectangle in the xy-plane, spanning from [-1,-1,0] to [+1,+1,0].
class Rectangle : public Shape {

    Float u = Float(2.0);
    Float v = Float(2.0);

    /**
     * @brief Constructs a surface event for a given position, used by @ref
     * intersect to populate the @ref Intersection and by @ref sampleArea to
     * populate the @ref AreaSample .
     * @param surf The surface event to populate with texture coordinates,
     * shading frame and area pdf
     * @param position The hitpoint (i.e., point in [-1,-1,0] to [+1,+1,0]),
     * found via intersection or area sampling
     */
    inline void populate(SurfaceEvent &surf, const Point &position) const {
        surf.position = position;

        // map the position from [-u/2,-v/2,0]..[+u/2,+v/2,0] to [0,0]..[1,1] by
        // discarding the z component and rescaling
        surf.uv.x() = (position.x() / u) + Float(0.5);
        surf.uv.y() = (position.y() / v) + Float(0.5);

        // the tangent always points in positive x direction
        surf.tangent = Vector(1, 0, 0);
        // and accordingly, the normal always points in the positive z direction
        surf.shadingNormal = Vector(0, 0, 1);
        surf.geometryNormal = Vector(0, 0, 1);

        // since we sample the area uniformly, the pdf is given by 1/surfaceArea
        surf.pdf = Float(1.0) / (u * v);
    }

public:
    Rectangle(const Properties &properties) {}

    // Constructor for use in feathers.cpp
    Rectangle(Float u, Float v) : u(u), v(v) {}

    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override {
        PROFILE("Rectangle")
        
        // if the ray travels in the xy-plane, we report no intersection
        // (we ignore the edge case - pun intended - that the ray might have
        // infinite intersections with the rectangle)
        if (ray.direction.z() == 0)
            return false;

        // ray.origin.z + t * ray.direction.z = 0
        // <=> t = -ray.origin.z / ray.direction.z
        const Float t = -ray.origin.z() / ray.direction.z();

        // note that we never report an intersection closer than Epsilon (to
        // avoid self-intersections)! we also do not update the intersection if
        // a closer intersection already exists (i.e., its.t is lower than our
        // own t)
        if (t < Epsilon || t > its.t)
            return false;

        // compute the hitpoint
        const Point position = ray(t);
        // we have intersected an infinite plane at z=0; now dismiss anything
        // outside of the [-u/2,-v/2,0]..[+u/2,+v/2,0] domain.
        const Float halfU = u / 2;
        const Float halfV = v / 2;
        if (abs(position.x()) > halfU || abs(position.y()) > halfV)
            return false;

        // If we hit a transparent area, we report it as no intersection
        Point2 testUV = Point2((position.x() + halfU) / u, (position.y() + halfV) / v);
        if (!its.testAlpha(testUV, rng))
            return false;

        // we have determined there was an intersection! we are now free to
        // change the intersection object and return true.
        its.t = t;
        populate(its,
                 position); // compute the shading frame, texture coordinates
                            // and area pdf (same as sampleArea)
        return true;
    }

    Bounds getBoundingBox() const override {
        const Float halfU = u / 2;
        const Float halfV = v / 2;
        return Bounds(Point{ -halfU, -halfV, 0 }, Point{ +halfU, +halfV, 0 });
    }

    Point getCentroid() const override { return Point(0); }

    AreaSample sampleArea(Sampler &rng) const override {
        Point2 rnd = rng.next2D(); // sample a random point in [0,0]..[1,1]
        Point position{
            u * (rnd.x() - Float(0.5)), // I hope this is correct...
            v * (rnd.y() - Float(0.5)),
            Float(0.0)
        }; // stretch the random point to [-u/2,-v/2]..[+u/2,+v/2] and set z=0

        AreaSample sample;
        populate(sample,
                 position); // compute the shading frame, texture coordinates
                            // and area pdf (same as intersection)
        return sample;
    }

    std::string toString() const override {
        // return "Rectangle[]"; 
        return tfm::format("Rectangle[width=%f, height=%f]", u, v);
    }
};

} // namespace lightwave
