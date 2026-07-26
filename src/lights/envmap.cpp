#include <lightwave.hpp>

#include <vector>

namespace lightwave {

class EnvironmentMap final : public BackgroundLight {
    /// @brief The texture to use as background
    ref<Texture> m_texture;
    /// @brief An optional transform from local-to-world space
    ref<Transform> m_transform;

    class Sampling {
        struct Node {
            Float pLeft;
            Float pTopLeft;
            Float pTopRight;
            Float pSum;
        };

        int m_resolution = 0;
        std::vector<Node> m_tree;
        std::vector<Float> m_pdfs;

        // https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
        uint32_t compact1By1(uint32_t x) const {
            x &= 0x55555555; // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
            x = (x ^ (x >> 1)) &
                0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
            x = (x ^ (x >> 2)) &
                0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
            x = (x ^ (x >> 4)) &
                0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
            x = (x ^ (x >> 8)) &
                0x0000ffff; // x = ---- ---- ---- ---- fedc ba98 7654 3210
            return x;
        }

        Point2 primaryToUvSpace(const Point2 &primary) const {
            return { primary.x(), safe_acos(2 * primary.y() - 1) * InvPi };
        }

        Point2 uvToPrimarySpace(const Point2 &uv) const {
            return { uv.x(), cos(uv.y() * Pi) / 2 + Float(0.5) };
        }

    public:
        void build(int log2resolution, const ref<Texture> &texture) {
            // setup buffers
            m_resolution = 1 << log2resolution;
            m_pdfs.resize(m_resolution * m_resolution);

            // estimate all pdfs
            Float pdfAcc    = 0;
            constexpr int Ns = 8;
            for (int y = 0; y < m_resolution; y++) {
                for (int x = 0; x < m_resolution; x++) {
                    Float &pdf = m_pdfs[x + y * m_resolution];

                    pdf = 0;
                    for (int sy = 0; sy < Ns; sy++) {
                        for (int sx = 0; sx < Ns; sx++) {
                            Point2 primary{
                                (x * Ns + sx + Float(0.5)) / (Ns * m_resolution),
                                (y * Ns + sy + Float(0.5)) / (Ns * m_resolution)
                            };
                            Point2 uv = primaryToUvSpace(primary);
                            pdf += sqr(texture->evaluate(uv).mean());
                        }
                    }
                    pdf = sqrt(pdf);
                    pdfAcc += Float(pdf);
                }
            }

            // normalize everything
            Float norm = (m_resolution * m_resolution) / pdfAcc;
            for (auto &pdf : m_pdfs)
                pdf *= norm;

            // build tree
            m_tree.resize(((1 << (2 * log2resolution)) - 1) / 3);
            for (int level = log2resolution - 1; level >= 0; level--) {
                const int levelOffset = ((1 << (2 * level)) - 1) / 3;
                int nodesOnLevel      = 1 << (2 * level);
                for (int index = 0; index < nodesOnLevel; index++) {
                    const int nodeIndex = levelOffset + index;
                    Node &node          = m_tree[nodeIndex];

                    Float childWeights[4];
                    if (level == log2resolution - 1) {
                        // lookup in m_pdf, need to decode morton code first
                        const int x = 2 * compact1By1(index >> 0);
                        const int y = 2 * compact1By1(index >> 1);
                        for (int child = 0; child < 4; child++) {
                            childWeights[child] =
                                m_pdfs[x + (child & 1) +
                                       (y + (child >> 1)) * m_resolution];
                        }
                    } else {
                        // lookup children
                        for (int child = 0; child < 4; child++) {
                            childWeights[child] =
                                m_tree[4 * nodeIndex + child + 1].pSum;
                        }
                    }

                    node.pSum = childWeights[0] + childWeights[1] +
                                childWeights[2] + childWeights[3];
                    node.pLeft =
                        (childWeights[0] + childWeights[2]) / node.pSum;
                    node.pTopLeft =
                        childWeights[0] / (childWeights[0] + childWeights[2]);
                    node.pTopRight =
                        childWeights[1] / (childWeights[1] + childWeights[3]);
                }
            }
        }

        Float pdf(Point2 uv) const {
            if (m_resolution == 0)
                return 1;

            Point2 primary = uvToPrimarySpace(uv);
            auto x =
                clamp(int(primary.x() * m_resolution), 0, m_resolution - 1);
            auto y =
                clamp(int(primary.y() * m_resolution), 0, m_resolution - 1);
            return m_pdfs[x + y * m_resolution];
        }

        Point2 sample(Point2 rnd) const {
            if (m_resolution == 0)
                return rnd;

            auto result = Point2(0);

            int index   = 0;
            Float scale = 1;
            while (index < int(m_tree.size())) {
                scale *= Float(0.5);

                const Node &node = m_tree[index];
                index            = 4 * index + 1;

                Float pTop;
                if (rnd.x() < node.pLeft) {
                    rnd.x() = rnd.x() / node.pLeft;
                    pTop    = node.pTopLeft;
                } else {
                    rnd.x() = (rnd.x() - node.pLeft) / (1 - node.pLeft);
                    result.x() += scale;
                    index += 1;
                    pTop = node.pTopRight;
                }

                if (rnd.y() < pTop) {
                    rnd.y() = rnd.y() / pTop;
                } else {
                    rnd.y() = (rnd.y() - pTop) / (1 - pTop);
                    result.y() += scale;
                    index += 2;
                }
            }

            result += scale * Vector2(rnd);
            return primaryToUvSpace(result);
        }
    } m_sampling;

public:
    EnvironmentMap(const Properties &properties) : BackgroundLight(properties) {
        m_texture   = properties.getChild<Texture>();
        m_transform = properties.getOptionalChild<Transform>();
        if (m_samplingWeight > 0) {
            m_sampling.build(10, m_texture);
        }
    }

    EmissionEval evaluate(const Vector &direction) const override {
        Vector d = direction;
        if (m_transform)
            d = m_transform->inverse(d).normalized();
        const Point2 warped((std::atan2(-d.z(), d.x()) + Pi) * Inv2Pi,
                            safe_acos(d.y()) * InvPi);

        // x=-1,z= 0 -> tex_x= 0.00
        // x= 0,z=+1 -> tex_x= 0.25
        // x= 1,z= 0 -> tex_x= 0.50
        // x= 0,z=-1 -> tex_x= 0.75

        // y=-1 -> tex_y= 1.0
        // y= 0 -> tex_y= 0.5
        // y=+1 -> tex_y= 0.0

        assert_finite(m_texture->evaluate(warped),
                      { logger(EWarn, "warped: %s from %s", warped, d); });
        // hints:
        // * if (m_transform) { transform direction vector from world to local
        // coordinates }
        // * find the corresponding pixel coordinate for the given local
        // direction
        return {
            .value = m_texture->evaluate(warped),
            .pdf = m_sampling.pdf(warped) * Inv4Pi,
        };
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        const Point2 warped  = m_sampling.sample(rng.next2D());
        const Float cosTheta = cos(warped.y() * Pi);
        const Float sinTheta = safe_sqrt(1 - sqr(cosTheta));

        Vector direction = {
            sinTheta * -cos(2 * Pi * warped.x()),
            cosTheta,
            sinTheta * sin(2 * Pi * warped.x()),
        };
        if (m_transform)
            direction = m_transform->apply(direction).normalized();

        const auto E    = m_texture->evaluate(warped);
        const Float pdf = m_sampling.pdf(warped) * Inv4Pi;
        // implement better importance sampling here, if you ever need it
        // (useful for environment maps with bright tiny light sources, like the
        // sun for example)

        return {
            .wi = direction,
            .weight = E / pdf,
            .pdf = pdf,
            .distance = Infinity,
        };
    }

    std::string toString() const override {
        return tfm::format(
            "EnvironmentMap[\n"
            "  texture = %s,\n"
            "  transform = %s\n"
            "]",
            indent(m_texture),
            indent(m_transform));
    }
};

} // namespace lightwave

REGISTER_LIGHT(EnvironmentMap, "envmap")
