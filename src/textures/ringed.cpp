#include <lightwave.hpp>

namespace lightwave {

/// @brief This texture is only meant to be used with Curves.
/// It expects the uv coordinates to have the following pattern:
/// (hairT, hairIdx), where hairT is how far along the curve it is,
/// and hairIdx gives the index of this curve in the curve collection. 
class Ringed : public Texture {
    Color m_color0;
    Color m_color1;

    float hash(int hairIdx, int seed) const {
        unsigned int hash = hairIdx * 747796405u + seed * 2654435761u;
        hash ^= hash >> 13;
        hash *= 2654435761u;
        hash ^= hash >> 16;
        // Convert to float in [0, 1] range
        return (hash & 0x7FFFFF) / float(0x7FFFFF);
    }

public:
    Ringed(const Properties &properties) {
        m_color0 = properties.get<Color>("color0", Color(0));
        m_color1 = properties.get<Color>("color1", Color(1));
    }

    Color evaluate(const Point2 &uv) const override {
        float t = uv.x();
        int hairIdx = int(uv.y());
        
        const int maxRings = 15;
        const float minWidth = 0.005f;
        const float maxWidth = 0.015f;
        const float transitionFactor = 0.8f;
        
        // Check if we're in any ring or its gradient transition
        for (int ringId = 0; ringId < maxRings; ringId++) {
            float randVal = hash(hairIdx, ringId * 3);

            // Distribute rings along the curve with bias toward t=1
            float ringPower = 0.3f + 0.7f * ringId / float(maxRings);
            float ringPos = std::pow(randVal, ringPower);

            float ringWidth = minWidth + (maxWidth - minWidth) * hash(hairIdx, ringId * 3 + 1) * (0.5f + 0.5f * t);

            // If we're inside the ring's bounds (including transition area)
            float distToRingCenter = abs(t - ringPos);
            float halfWidth = ringWidth * 0.5f;
            float transitionWidth = halfWidth * transitionFactor;
            float innerRadius = halfWidth - transitionWidth;
            if (distToRingCenter < halfWidth) {
                if (distToRingCenter <= innerRadius) // If we're inside this ring's bounds, return the ring color
                    return m_color1;
                else { // We're in transition area
                    float blendFactor = (distToRingCenter - innerRadius) / transitionWidth;
                    blendFactor = blendFactor * blendFactor * (3.0f - 2.0f * blendFactor); // Use smooth interpolation (s-curve)
                    return m_color1 * (1.0f - blendFactor) + m_color0 * blendFactor;
                }
            }
        }
        
        // If not in any ring, return the base color
        return m_color0;
    }

    std::string toString() const override {
        return tfm::format("Ringed[\n"
                           "  color0 = %s,\n"
                           "  color1 = %s\n"
                           "]",
                        indent(m_color0),
                        indent(m_color1));
    }
};

} // namespace lightwave

REGISTER_TEXTURE(Ringed, "ringed")
