#include <lightwave.hpp>

namespace lightwave {

/**
 * @brief A perspective camera with a given field of view angle and transform.
 *
 * In local coordinates (before applying m_transform), the camera looks in
 * positive z direction [0,0,1]. Pixels on the left side of the image ( @code
 * normalized.x < 0 @endcode ) are directed in negative x direction ( @code
 * ray.direction.x < 0 ), and pixels at the bottom of the image ( @code
 * normalized.y < 0 @endcode ) are directed in negative y direction ( @code
 * ray.direction.y < 0 ).
 */
class Perspective : public Camera {
    Vector2 m_span;
    Float m_pixelFov; // FOV angle per pixel (in radians)
    Vector2 m_pixelSize;

public:
    Perspective(const Properties &properties) : Camera(properties) {
        const Float fov           = properties.get<float>("fov");
        const std::string fovAxis = properties.get<std::string>("fovAxis");

        const Float fullFov = fov * Pi / 180;
        const Float fovTan = tan(fov * Pi / 360);
        const Float aspect = m_resolution.x() / Float(m_resolution.y());

        if (fovAxis == "x") {
            m_span = { fovTan, fovTan / aspect };
            m_pixelFov = fullFov / static_cast<Float>(m_resolution.x());
        } else {
            m_span = { fovTan * aspect, fovTan };
            m_pixelFov = fullFov / static_cast<Float>(m_resolution.y());
        }

        m_pixelSize = {
            Float(2) * m_span.x() / m_resolution.x(),
            Float(2) * m_span.y() / m_resolution.y()
        };
        // hints:
        // * precompute any expensive operations here (most importantly
        // trigonometric functions)
        // * use m_resolution to find the aspect ratio of the image
    }

    CameraSample sample(const Point2 &normalized, Sampler &rng) const override {
        Point origin{ 0, 0, 0 };
        // logger(EWarn, "m_span: %s, normalized: %s, Vector2(normalized): %s", m_span, normalized, Vector2(normalized));
        Vector direction{ m_span * Vector2(normalized), 1 };
        return {
            .ray    = m_transform->apply(Ray(origin, direction)).normalized(),
            .weight = Color::white(),
        };
        // hints:
        // * use m_transform to transform the local camera coordinate system
        // into the world coordinate system
    }

    /// @brief Computes the footprint area that a pixel projected over given distance would have.
    Float computePixelFootprint(Float distance) const override {
        // Float footprintSide = Float(2) * distance * tan(m_pixelFov / Float(2));
        // return footprintSide * footprintSide;
        Vector2 footprint = distance * m_pixelSize;
        return footprint.x() * footprint.y();
    }

    std::string toString() const override {
        return tfm::format("Perspective[\n"
                           "  width = %d,\n"
                           "  height = %d,\n"
                           "  transform = %s,\n"
                           "]",
                           m_resolution.x(), m_resolution.y(),
                           indent(m_transform));
    }
};

} // namespace lightwave

REGISTER_CAMERA(Perspective, "perspective")
