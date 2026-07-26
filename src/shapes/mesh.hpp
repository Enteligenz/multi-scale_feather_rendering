#include <lightwave.hpp>

#include "../core/plyparser.hpp"
#include "accel.hpp"

namespace lightwave {

/**
 * @brief A shape consisting of many (potentially millions) of triangles, which
 * share an index and vertex buffer. Since individual triangles are rarely
 * needed (and would pose an excessive amount of overhead), collections of
 * triangles are combined in a single shape.
 */
class TriangleMesh : public AccelerationStructure {
    /**
     * @brief The index buffer of the triangles.
     * The n-th element corresponds to the n-th triangle, and each component of
     * the element corresponds to one vertex index (into @c m_vertices ) of the
     * triangle. This list will always contain as many elements as there are
     * triangles.
     */
    std::vector<Vector3i> m_triangles;
    /**
     * @brief The vertex buffer of the triangles, indexed by m_triangles.
     * Note that multiple triangles can share vertices, hence there can also be
     * fewer than @code 3 * numTriangles @endcode vertices.
     */
    std::vector<Vertex> m_vertices;
    /// @brief The file this mesh was loaded from, for logging and debugging
    /// purposes.
    std::filesystem::path m_originalPath;
    /// @brief Whether to interpolate the normals from m_vertices, or report the
    /// geometric normal instead.
    bool m_smoothNormals;

protected:
    std::pair<int, int> numberOfPrimitives() const override { return { int(m_triangles.size()), 0 }; }

    bool intersect(int primitiveIndex, const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng, bool useHighLOD=true) const override {
        const auto &triangle = m_triangles[primitiveIndex];
        const auto &v0       = m_vertices[triangle[0]];
        const auto &v1       = m_vertices[triangle[1]];
        const auto &v2       = m_vertices[triangle[2]];

        const auto edge1 = v1.position - v0.position;
        const auto edge2 = v2.position - v0.position;

        const auto pvec = ray.direction.cross(edge2);
        const auto det  = edge1.dot(pvec);
        if (det > Float(-1e-8) && det < Float(1e-8))
            return false;
        const auto invDet = 1 / det;

        const auto tvec = ray.origin - v0.position;
        const Float u   = tvec.dot(pvec) * invDet;
        if (u < 0 || u > 1)
            return false;

        const auto qvec = tvec.cross(edge1);
        const Float v   = ray.direction.dot(qvec) * invDet;
        if (v < 0 || u + v > 1)
            return false;

        const Float t = edge2.dot(qvec) * invDet;
        if (t < Epsilon || t >= its.t)
            return false;

        const auto vInterpolated = Vertex::interpolate({ u, v }, v0, v1, v2);
        if (!its.testAlpha(vInterpolated.uv, rng))
            return false;

        const auto deltaUV1 = v1.uv - v0.uv;
        const auto deltaUV2 = v2.uv - v0.uv;

        const Float r =
            deltaUV1.x() * deltaUV2.y() - deltaUV1.y() * deltaUV2.x();
        const auto tangent =
            abs(r) < 1e-6 ? edge1
                          : (edge1 * deltaUV2.y() - edge2 * deltaUV1.y()) / r;

        its.t              = t;
        its.position       = vInterpolated.position;
        its.uv             = vInterpolated.uv;
        its.geometryNormal = edge1.cross(edge2).normalized();
        its.shadingNormal  = m_smoothNormals ? vInterpolated.normal.normalized() : its.geometryNormal;
        its.tangent        = tangent;
        its.pdf            = 0;

        assert_finite(its.t, {
            logger(EError, "triangle: %s %s %s", v0.position, v1.position,
                   v2.position);
            logger(EError, "indices: %d %d %d", triangle[0], triangle[1],
                   triangle[2]);
            logger(EError, "count: %d", m_vertices.size());
            logger(EError, "offending shape: %s", this);
        });
        return true;
        // hints:
        // * use m_triangles[primitiveIndex] to get the vertex indices of the
        // triangle that should be intersected
        // * if m_smoothNormals is true, interpolate the vertex normals from
        // m_vertices
        //   * make sure that your shading frame stays orthonormal!
        // * if m_smoothNormals is false, use the geometrical normal (can be
        // computed from the vertex positions)
    }

    Bounds getBoundingBox(int primitiveIndex, bool forHighLOD=true) const override {
        const auto &triangle = m_triangles[primitiveIndex];
        const auto &v0       = m_vertices[triangle[0]];
        const auto &v1       = m_vertices[triangle[1]];
        const auto &v2       = m_vertices[triangle[2]];

        Bounds result;
        result.extend(v0.position);
        result.extend(v1.position);
        result.extend(v2.position);
        return result;
    }

    Point getCentroid(int primitiveIndex) const override {
        const auto &triangle = m_triangles[primitiveIndex];
        const auto &v0       = m_vertices[triangle[0]];
        const auto &v1       = m_vertices[triangle[1]];
        const auto &v2       = m_vertices[triangle[2]];
        return (Vector(v0.position) + Vector(v1.position) +
                Vector(v2.position)) /
               3;
    }

public:
    TriangleMesh(const Properties &properties) {
        m_originalPath  = properties.get<std::filesystem::path>("filename");
        m_smoothNormals = properties.get<bool>("smooth", true);
        readPLY(m_originalPath, m_triangles, m_vertices);
        logger(EInfo, "loaded ply with %d triangles, %d vertices",
               m_triangles.size(), m_vertices.size());
        buildAccelerationStructure();
    }

    /** @brief Alternative constructor, specifically for use in the Feathers class.
     *  It constructs a low-detail version of a given feather.
     *  @param barbCPs Control points for all barbs of target feather.
     *  @param spineTip Control point that sits at the tip of the spine.
     *  @param x How many barbs should be merged into one triangle.
     *  @param filePath Path to the file that contains the data the feather is based on.
     */
    TriangleMesh(const std::vector<Point> &barbCPs, const Point &spineTip,
        const size_t x, const std::filesystem::path &filePath) : m_originalPath(filePath) {
        m_smoothNormals = false;
        Vector dummyNormal = Vector(Float(1.0));
        Vector2 dummyUV = Vector2(Float(0.0));
        const size_t n = 2 * x; // *2 because each barb is split into 2 vertices: beginning and end.

        // Save all vertices into m_vertices
        for (size_t i = 0; i < barbCPs.size(); i+= 4) {
            Vertex vertexFront;
            vertexFront.position = { barbCPs[i] };
            vertexFront.normal = dummyNormal;
            vertexFront.uv = dummyUV;
            m_vertices.push_back(vertexFront);

            Vertex vertexEnd;
            vertexEnd.position = { barbCPs[i+3] };
            vertexEnd.normal = dummyNormal;
            vertexEnd.uv = dummyUV;
            m_vertices.push_back(vertexEnd);
        }

        // Do triangle creation for one side of the feather
        size_t numVertices = m_vertices.size();
        for (size_t i = 0; i < numVertices / 2 - 2; i+=n) {
            if (i + n > numVertices / 2 - 2) {
                int32_t nClipped = (numVertices / 2 - 2) - i;
                m_triangles.emplace_back(i, i+1, i+nClipped); // Close triangle
                m_triangles.emplace_back(i+nClipped, i+1, i+nClipped+1); // Far triangle
            } else {
                m_triangles.emplace_back(i, i+1, i+n);
                m_triangles.emplace_back(i+n, i+1, i+n+1);
            }
        }

        // Do triangle creation for the other side
        for (size_t i = numVertices / 2; i < numVertices; i+=n) {
            if (i + n > numVertices - 2) {
                int32_t nClipped = (numVertices - 2) - i;
                m_triangles.emplace_back(i, i+nClipped, i+1);
                m_triangles.emplace_back(i+nClipped, i+nClipped+1, i+1);
            } else {
                m_triangles.emplace_back(i, i+n, i+1);
                m_triangles.emplace_back(i+n, i+n+1, i+1);
            }
        }

        size_t startLeft = numVertices / 2 - 2;
        size_t startRight = numVertices - 2;

        // Save spine tip into m_vertices (note that we deliberately do not update numVertices here!)
        Vertex finalVertex;
        finalVertex.position = { spineTip };
        finalVertex.normal = dummyNormal;
        finalVertex.uv = dummyUV;
        m_vertices.push_back(finalVertex);

        // Add triangles at the tip
        m_triangles.emplace_back(startLeft, startLeft+1, m_vertices.size()-1);
        m_triangles.emplace_back(startRight, m_vertices.size()-1, startRight+1);

        logger(EInfo, "created simplified feather mesh with %d triangles, %d vertices",
               m_triangles.size(), m_vertices.size());
        buildAccelerationStructure();
    }

    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override {
        PROFILE("Triangle mesh")
        return AccelerationStructure::intersect(ray, its, p, rng);
    }

    AreaSample sampleArea(Sampler &rng) const override{
        // only implement this if you need triangle mesh area light sampling for
        // your rendering competition
        NOT_IMPLEMENTED
    }

    std::string toString() const override {
        return tfm::format("Mesh[\n"
                           "  vertices = %d,\n"
                           "  triangles = %d,\n"
                           "  filename = \"%s\"\n"
                           "]",
                           m_vertices.size(), m_triangles.size(),
                           m_originalPath.generic_string());
    }
};

} // namespace lightwave