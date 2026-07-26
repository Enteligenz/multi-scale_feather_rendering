#include <lightwave/math.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace lightwave {

void readPLY(const std::filesystem::path &path, std::vector<Vector3i> &indices,
             std::vector<Vertex> &vertices);

void readCurvePLY(const std::filesystem::path &path,
    std::vector<Point> &controlPointData,
    std::vector<Vector> &normalData,
    int32_t &numControlPointsPerCurve,
    int32_t &numSegments);

}
