#include <lightwave.hpp>

#include "feathermath.hpp"
#include "../core/plyparser.hpp"
#include "accel.hpp"
#include "group.hpp"
#include "curves.hpp"
#include "rectangle.hpp"
#include "../samplers/independent.hpp"
#include "../textures/imagetexture.hpp"
#include "../bsdfs/principled.hpp"

#include <random>
#include <unordered_map>
#include <chrono>

// #include <fstream>
// #include <iostream>

namespace lightwave {

/**
 * @brief A shape consisting of many multiple feathers, by grouping curves.
 * Some code has been taken from group, as this class fundamentally
 * behaves like one with some extras.
 */
class Feathers : public AccelerationStructure {
    /// @brief The file the curve joints for the feather were loaded from,
    /// for logging and debugging purposes.
    std::filesystem::path m_originalPath;

    /// @brief How many feathers are actually generated instead of just instanced from existing ones.
    size_t m_numOriginalFeathers;
    /// @brief How many control points a curve has (meaning any feather component).
    static constexpr int32_t m_numCPs = 4;

    /// @brief Contains different feather component types. The components should all be saved in
    /// curve collections (see curves.cpp). This is the high-detail version.
    std::vector<ref<Instance>> m_childrenHigh;
    /// @brief Contains feather meshes. This is the low-detail version.
    std::vector<ref<Instance>> m_childrenLow;

    /// @brief Remember indices of original feathers here for later reference.
    std::vector<size_t> m_selectedIndices;

    // Feather Parameters
    static constexpr int32_t m_curveSplits     = 16;
    // static constexpr Float m_rootRadiusSpine   = Float(0.005);
    // static constexpr Float m_tipRadiusSpine    = Float(0.0001); // /50
    // static constexpr Float m_rootRadiusBarb    = Float(0.0005); // / 10
    // static constexpr Float m_tipRadiusBarb     = Float(0.0001); // / 50
    // static constexpr Float m_rootRadiusBarbule = Float(0.0001); // / 50
    // static constexpr Float m_tipRadiusBarbule  = Float(0.00001); // / 500
    const std::string m_radiusFunction = "sigmoid";
    Float m_rootRadiusSpine, m_tipRadiusSpine, m_rootRadiusBarb, m_tipRadiusBarb, m_rootRadiusBarbule, m_tipRadiusBarbule;

    Float m_asymmetry                               = Float(0); // Controls left-right asymmetry, values should be in range of [-1.0, 1.0] (Was 0.5 for a while)
    static constexpr Float m_groupProbability       = Float(0.05); // Probability of starting a new group
    static constexpr Float m_groupDirectionVariance = Float(0.3); // How much group direction can vary
    static constexpr bool m_useConvergence          = false; // Whether grouped barbs converge or not
    static constexpr Float m_baseAngleBlendBarbule  = Float(0.5); // 0.7
    static constexpr Float m_lateTipThreshold       = Float(0.9); // Only increase angle after 90% of barb length

    // Shape type modifications
    Float m_barbAngleBlend; // How strongly barbs are angled forward. Default: 0.4
    Float m_barbToSpineLengthRatio; // How long barbs are compared to the spine. Default: 0.4
    bool m_isTailFeather; // true: tail feather, false: wing feather

    // Number in [0, 1] that determines how far along the curve we take the derivative to get the new feather direction
    Float m_derivativePosition;

    // Randomness
    std::random_device m_rd;
    std::mt19937 m_gen;
    Float m_barbuleDirVariation;

protected:
    /// @brief Saves parameters from creating geometry of low detail feathers for their later texture creation.
    struct ParamsForTextureBaking {
        Vector u, v;
        Float lenU, lenV;
        Point rectOrigin;
        Vector rectNormal;
        Frame localFrame;
        int32_t pixelCount;
        
        ParamsForTextureBaking(Vector u, Vector v, Float lenU, Float lenV,
                               Point rectOrigin, Vector rectNormal, 
                               Frame frame, int32_t pixelCount)
            : u(u), v(v), lenU(lenU), lenV(lenV),
              rectOrigin(rectOrigin), rectNormal(rectNormal),
              localFrame(frame), pixelCount(pixelCount) {}
    };

    // Information that will be used for texture baking for low detail feathers
    ref<Bsdf> m_bsdf;
    std::unordered_map<int32_t, ParamsForTextureBaking> m_infosFromGeometry;

    /// @brief Which algorithm will be used for finding a plane for the low detail version of a feather.
    /// Options are: Standard, PCA, SVD.
    PlaneFindingMode m_planeFindingMode;

protected:
    /** @brief Generates feathers based on a hair mesh from blender, both a high- and low-detail version,
     * and saves them into m_childrenHigh and m_childrenLow, respectively.
     * Only generates a few feathers, instancing them to cover the target object for cheap.
     */
    void generateFeathers() {
        int32_t numCPsPerHair; // How many control points each curve (= hair) has; Note that this is probably different from m_numCPs
        int32_t numSegments; // How many segments each hair has
        std::vector<Point> hairCPs;
        std::vector<Vector> hairNormals;
        readCurvePLY(m_originalPath, hairCPs, hairNormals, numCPsPerHair, numSegments);
        const size_t numSpines = hairCPs.size() / numCPsPerHair;
        logger(EInfo, "Generating feathers from hair with %llu segments, %llu control points per hair and %llu hairs in total.",
                numSegments, numCPsPerHair, numSpines);

        // Make sure we are working with valid feather counts
        if (numSpines == 0) logger(EError, "The are no hairs to generate feathers from!");
        if (m_numOriginalFeathers > numSpines) {
            logger(EWarn, "There are not enough spines to support %i original feathers. Original feather count has thus been set to %i.",
                m_numOriginalFeathers, numSpines);
            m_numOriginalFeathers = numSpines;
        }

        // Select which hairs we want to properly convert to feathers (which to generate)
        m_infosFromGeometry.reserve(m_numOriginalFeathers);
        m_selectedIndices = findRepresentativeCurves(hairCPs, numSpines, numCPsPerHair, m_numOriginalFeathers);

        // Prepare some useful numbers for the instancing later
        std::vector<Point> originalStartingPoints(numSpines); // Contains points at which original feathers start
        std::vector<Vector> originalDirections(numSpines); // Contains vectors that span first and last control points of hairs (not normalized!)

        // DEBUGGING CODE
        std::ostringstream result;
        for (size_t i = 0; i < m_selectedIndices.size(); ++i) {
            if (i > 0) result << ", ";
            result << m_selectedIndices[i];
        }
        logger(EDebug, "selected hair indices that feathers will be generated for: %s", result.str());

        // Initialize the two lists of children with specific sizes
        m_childrenHigh.resize(numSpines);
        m_childrenLow.resize(numSpines);

        // Generate feathers
        auto tStart = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> thread_list;
        for (int32_t i : m_selectedIndices) { // Note: i does (usually) not go 0, 1, 2, ... but picks some numbers with holes in-between!
            int32_t seed = std::uniform_int_distribution<int32_t>(INT32_MIN, INT32_MAX)(m_gen);
            thread_list.emplace_back(&Feathers::generateSingleFeather, this, seed, i, std::ref(hairCPs), numSegments, 
                numCPsPerHair, std::ref(hairNormals), std::ref(originalStartingPoints), std::ref(originalDirections));
        }

        for (size_t i = 0; i < thread_list.size(); i++) {
            thread_list[i].join();
        }
        auto tEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = tEnd - tStart;  // Measure generation time
        logger(EWarn, "Feather generation took a total of %f ms", elapsed.count()); // TODO check if format chosen is correct in string!

        // 6. Instance other feathers
        std::vector<size_t> featherCounts(numSpines, 1); // For assignment statistics
        if (m_numOriginalFeathers <= 0) logger(EError, "The number of generated feathers is set to 0!");
        // Note: Feather n is saved in a group at m_childrenHigh[n] and its low detail version in m_childrenLow[n]
        for (size_t i = 0; i < numSpines; i++) {
            // Skip feather if it is one of the generated ones
            if (std::find(m_selectedIndices.begin(), m_selectedIndices.end(), i) != m_selectedIndices.end()) continue;

            // Pick which feather to use as base (based on how well it fits the current one)
            std::vector<Float> weights;
            for (size_t j = 0; j < m_numOriginalFeathers; ++j) {
                const Float curveDistance = calculateCurveDistance(hairCPs, i, m_selectedIndices[j], numCPsPerHair);
                Float weight = std::exp(-curveDistance * Float(2.0)); // To emphasize closer matches
                weights.push_back(weight);
            }
            int32_t realFeatherIndex = weightedRandomSelection(weights); // The nth generated feather has index n
            int32_t originalFeatherIdx = m_selectedIndices[realFeatherIndex]; // The index of the original feather in m_childrenHigh
            featherCounts[originalFeatherIdx] += 1;

            // Compute necessary transforms
            ref<Transform> newTransform = std::make_shared<Transform>();

            // Scale (small, random modifications)
            Point originalw0 = originalStartingPoints[originalFeatherIdx];
            Vector translationToOrigin = Vector(-originalw0[0], -originalw0[1], -originalw0[2]);
            newTransform->translate(translationToOrigin); // This is necessary due to the way scaling behaves when not in the origin
            Float scaleModifier = std::uniform_real_distribution<Float>(Float(0.9), Float(1.1))(m_gen);
            newTransform->scale(Vector(scaleModifier));

            // Rotation (using quaternions)
            Vector originalDir = originalDirections[originalFeatherIdx].normalized();
            const Point start = hairCPs[i * numCPsPerHair];
            Vector newDir = (evaluateBezierDerivative(hairCPs, Float(m_derivativePosition), i * numCPsPerHair)).normalized();
            // Vector newDir = (evaluateBezierDerivative(hairCPs, Float(0), i * numCPsPerHair)); // This produces some wild feathers :D
            Vector originalNormal = hairNormals[originalFeatherIdx].normalized();
            Vector newNormal = hairNormals[i].normalized();
            Matrix4x4 rotationMatrix = computeDoubleRotation(originalDir, newDir, originalNormal, newNormal);
            newTransform->matrix(rotationMatrix);

            // Translation
            Vector translation = Vector(start);
            newTransform->translate(translation);

            // Create instance that points at correct original instance and save it into list of children (both high and low detail)
            ref<Instance> featherInstance = std::make_shared<Instance>(m_childrenHigh[originalFeatherIdx], newTransform);
            m_childrenHigh[i] = featherInstance;

            if (m_useLOD) {
                ref<Instance> roughInstance = std::make_shared<Instance>(m_childrenLow[originalFeatherIdx], newTransform);
                m_childrenLow[i] = roughInstance;
            } else {
                m_childrenLow[i] = featherInstance;
            }
            // // // // m_childrenHigh[i] = roughInstance; // DEBUG, remove this later in favor of block above

            // DEBUG Use this instead of everything above (except for skip-statement) inside of loop to only show primary feathers and rest as curves!
            // std::vector<Point> curveCPs;
            // ref<Transform> newTransform = std::make_shared<Transform>();
            // curveCPs.push_back(hairCPs[i * numCPsPerHair]);
            // curveCPs.push_back(hairCPs[i * numCPsPerHair + 1]);
            // curveCPs.push_back(hairCPs[i * numCPsPerHair + 2]);
            // curveCPs.push_back(hairCPs[i * numCPsPerHair + 3]);
            // ref<Curves> spineCurvesRef = std::make_shared<Curves>(curveCPs, m_originalPath, m_curveSplits, Float(0.01), Float(0.0001), m_radiusFunction, m_numCPs);
            // ref<Instance> curveInstance = std::make_shared<Instance>(spineCurvesRef, newTransform);
            // m_childrenHigh[i] = curveInstance;
            // m_childrenLow[i] = curveInstance;
        }

        // Print out feather assignment statistics
        for (size_t idx = 0; idx < m_selectedIndices.size(); idx++) {
            logger(EDebug, "Feather with original index %i appears %i times.", m_selectedIndices[idx], featherCounts[m_selectedIndices[idx]]);
        }
    }

    /** @brief Generates a single feather (its spine, barbs and barbules)
     *  as well as a low-detail version of it and saves them into the respective children lists.
     *  It does not prepare the texture etc, this is done elsewhere (see bakeFeatherTextures()). 
     *  @param seed Seed for random number generation.
     *  @param featherIdx Index that points to which hair is being used as a base for this feather.
     *  @param hairCPs All control points of the hairs.
     *  @param numSegments How many segments each hair has.
     *  @param numCPsPerHair How many control points each hair has.
     *  @param hairNormals The normals of each hair.
     *  @param originalStartingPoints Root of each hair. 
     *  @param originalDirections Initial direction each of the hairs are pointing at.
    **/
    void generateSingleFeather(const int32_t seed, const int32_t featherIdx, const std::vector<Point> &hairCPs,
            const int32_t numSegments, const int32_t numCPsPerHair,
            const std::vector<Vector> &hairNormals,
            std::vector<Point> &originalStartingPoints, std::vector<Vector> &originalDirections) {
        std::mt19937 gen(seed);
        std::uniform_real_distribution<Float> dis(Float(0.0), Float(1.0));

        // Save control points for different feather component type separately
        std::vector<Point> spineCPs, barbCPs, barbuleCPs;
        int32_t numCurrentBarbs = 0;

        // Save end point of each barbule for PCA
        std::vector<Point> PCAData;
        // Save end point of each end barbule for finding minimum bounding rectangle
        std::vector<Point> featherOutlineData;

        // Collect some statistics
        int32_t numBarbs = 0;
        int32_t numBarbules = 0;

        // 1. Create the main spine
        size_t startingIdx = featherIdx * numCPsPerHair;
        spineCPs.insert(spineCPs.end(), {hairCPs[startingIdx], hairCPs[startingIdx+1], hairCPs[startingIdx+2], hairCPs[startingIdx+3]});

        // Some useful stuff for later instancing
        // originalDirections[featherIdx] = (evaluateBezierDerivative(spineCPs, Float(m_derivativePosition), 0)).normalized(); // Technically this might be more correct here
        originalDirections[featherIdx] = spineCPs[1] - spineCPs[0];
        originalStartingPoints[featherIdx] = spineCPs[0];
        int32_t barbuleCount = 0; // Number of barbules per feather side

        // 2. Create barbs along the spine
        const Float totalSpineLength = estimateBezierLength(spineCPs);
        logger(EWarn, "length bezier: %f", totalSpineLength);
        const Float barbLength = m_barbToSpineLengthRatio * totalSpineLength;

        m_barbuleDirVariation = Float(0.05) * totalSpineLength;

        // Create barbs on both sides
        for (int32_t side : {-1, 1}) { // If looking in direction base->tip, -1 is left side, 1 is right side
            Float realPosition = Float(0.0);
            Float u = Float(0.0); // Current t

            // Initialize variables for transport frame
            Vector prevTangent = getBezierTangent(spineCPs, u).normalized();
            Vector initialUp = hairNormals[featherIdx];
            initialUp = (initialUp - initialUp.dot(prevTangent) * prevTangent).normalized(); // Ensure initialUp is perpendicular to initial tangent
            Vector transportedUp = initialUp;

            // Unzip parameters
            std::uniform_real_distribution<Float> unzipDist(0.0, 1.0);
            std::uniform_int_distribution<int> unzipLengthDist(3, 10);
            Float unzipThreshold = Float(0.05); // 5% chance of unzipping
            int32_t remainingUnzipBarbs = 0;
            Float currentUnzipAmount = Float(0.0);
            int32_t unzipSide = 1; // Direction of deviation

            while (u < Float(1.0)) {
                numBarbs++;
                // Get position and tangent at point u, also perpendicular direction to the spine (uses surface normal)
                const Point barbPos = evaluateBezier(spineCPs, u);
                Vector currentTangent = getBezierTangent(spineCPs, u); // This is essentially the spine direction
                transportedUp = parallelTransport(transportedUp, prevTangent, currentTangent);
                Vector perpDir = currentTangent.cross(transportedUp).normalized();

                // For wing feathers, swap up and perp directions
                Vector up = transportedUp;
                if (!m_isTailFeather) {
                    up = -perpDir; // Has to be flipped for the test scene I have, but might not need to be for others. Really depends where you have your camera :)
                    perpDir = transportedUp;
                }

                // Barb length formula
                const Float baseProfile = std::pow(std::sin(u * Pi), Float(0.7));
                const Float sigmoidTip = Float(1.0) / (Float(1.0) + std::exp((u - Float(0.85)) * Float(15.0))); // Sharp drop-off after u=0.85
                Float lengthProfile = baseProfile * (Float(0.7) + Float(0.3) * sigmoidTip); // baseProfile * (Float(0.7) + Float(0.3) * sigmoidTip);

                // Ensure minimum length (prevent zero-length barbs), which is smaller at tip
                const Float minLengthFactor = Float(0.05) * (Float(1.0) - Float(0.8) * (std::pow(u, Float(2.0)))); // Gets smaller toward tip but never zero
                lengthProfile = max(lengthProfile, minLengthFactor);

                // Teardrop shape - reduce asymmetry towards the tip
                const Float asymmetryDirection = m_asymmetry >= 0 ? Float(1.0) : Float(-1.0);
                const Float positionBasedAsymmetry = abs(m_asymmetry) * (Float(1.0) - std::pow(u, Float(1.5)));
                const Float sideMultiplier = Float(1.0) + (positionBasedAsymmetry * asymmetryDirection * side * Float(0.6));
                const Float naturalProfile = lengthProfile * sideMultiplier;
                const Float tipReduction = std::pow(u, Float(2.0)) * Float(0.2);
                const Float currentBarbLength = barbLength * naturalProfile * (Float(1.0) - tipReduction);

                // Alternative ellipsoid approach, does not use asymmetry as well though (alternative to teardrop shape)
                // const Float a = barbLength * lengthProfile * (Float(1.0) + m_asymmetry); // Semi-major axis
                // const Float b = barbLength * lengthProfile * (Float(1.0) - m_asymmetry * Float(0.5)); // Semi-minor axis
                // const Float centerOffset = m_asymmetry * barbLength * lengthProfile * Float(0.4);
                // // Distance from offset center to edge of ellipse in perpendicular direction
                // const Float x_offset = side * centerOffset;
                // const Float ellipse_radius = sqrt((a*a * b*b) / (b*b + (a*a - b*b) * std::pow(side, Float(2.0))));
                // const Float currentBarbLength = ellipse_radius - abs(x_offset);

                // Barb angle formula
                perpDir = (perpDir * side).normalized();
                const Float baseAngleProgression = Float(0.05) 
                                                 + Float(0.15) 
                                                 * (std::pow(u, Float(2.0))); // Starts more perpendicular, gradually angles forward
                const Float tipAngleFactor = Float(1.0) / (Float(1.0) 
                                           + std::exp(-(u - Float(0.9)) * Float(15.0))); // Sigmoid centered at u=0.85
                const Float tipAngleAdjustment = Float(0.2) * tipAngleFactor; // Maximum 0.5 additional forward angle

                // Combine factors from shape type, how far along the spine we are and tip adjustments
                Float angleBlend = m_barbAngleBlend + baseAngleProgression + tipAngleAdjustment;
                angleBlend = min(Float(0.8), angleBlend); // Cap at some angle

                // Blend perpendicular with forward (spine) direction
                Vector barbDir = (perpDir * (1 - angleBlend) + currentTangent * angleBlend).normalized();

                // Handle unzipping regions
                if (remainingUnzipBarbs <= 0) {
                    // Check if we should start a new unzip region
                    if (unzipDist(gen) < unzipThreshold) {
                        remainingUnzipBarbs = unzipLengthDist(gen);
                        // Randomly choose direction of deviation (perpendicular to spine)
                        unzipSide = (unzipDist(gen) < 0.5) ? -1 : 1;
                        // Vary strength of unzipping
                        std::uniform_real_distribution<Float> strengthDist(0.0075, 0.075);
                        currentUnzipAmount = strengthDist(gen);
                    } else {
                        currentUnzipAmount  = Float(0.0);
                    }
                } else {
                    remainingUnzipBarbs--;
                }

                // Create the barb curve
                createBarb(barbCPs, barbPos, barbDir, currentTangent, currentBarbLength, perpDir, currentUnzipAmount, unzipSide);

                // Move to next position along spine; barbs less dense towards tip
                Float positionProgression = Float(2) * m_rootRadiusBarb * (Float(10) + pow(u, Float(1.5)));
                realPosition += positionProgression * std::uniform_real_distribution<Float>(0.9f, 1.1f)(gen); // Vary barb positioning a bit so they do not always align
                u = realPosition / totalSpineLength;
                u = min(u, Float(1.0));

                // 3. Create barbules along each barb
                const Float barbuleLength = Float(0.025) * barbLength; // 0.035 also kinda works, it's just a bit denser
                for (int32_t barbuleSide : {-1, 1}) { // If looking in direction base->tip, -1 is left side, 1 is right side
                    Float realPositionBarbule = Float(0.0);
                    Float t = Float(0.0);
                    while (t < Float(1.0)) {
                        numBarbules++;
                        barbuleCount++;
                        int32_t currBarbIdx = numCurrentBarbs * m_numCPs;
                        // Get position and tangent at point t, also perpendicular direction to the barb
                        const Point barbulePos = evaluateBezier(barbCPs, t, currBarbIdx);
                        perpDir = barbDir.cross(up) * barbuleSide; // Reuse up and barbDir from earlier // copyTransportedUp for wing feathers

                        // Barb angle formula
                        Float additionalAngle = Float(0.0);
                        if (t > m_lateTipThreshold) {
                            const Float tNormalized = (t - m_lateTipThreshold) / (Float(1.0) - m_lateTipThreshold);
                            additionalAngle = Float(0.2)
                                            * (Float(1.0) / (Float(1.0)
                                            + std::exp(Float(-15.0) // -12.0 (narrower angle)
                                            * (tNormalized - Float(0.6))))); // -0.5 (narrower angle)
                        }

                        const Float angleBlendBarbule = min(Float(0.9), m_baseAngleBlendBarbule + additionalAngle);
                        Vector barbuleDir = (perpDir * (Float(1.0) - angleBlendBarbule) + barbDir * angleBlendBarbule).normalized();
                        
                        // Current barbule length; shorter near tip
                        // Linear
                        // Float currentBarbuleLength = barbuleLength * (Float(1.0) - t * Float(0.3));
                        // Sigmoid
                        const Float midPoint = Float(0.85); // Where the transition happens (0.85 = 85% along the barbule)
                        const Float sharpness = Float(8.0); // Higher value = sharper transition
                        const Float tSigmoid = Float(1.0) / (Float(1.0) + std::exp(sharpness * (midPoint - t)));
                        Float currBarbuleLength = barbuleLength + tSigmoid * (-barbuleLength / Float(2.0)); // Towards the end, barbule length halves

                        // Randomize a little
                        const Vector variation = Vector(
                            std::uniform_real_distribution<Float>(-m_barbuleDirVariation, m_barbuleDirVariation)(gen),
                            std::uniform_real_distribution<Float>(-m_barbuleDirVariation, m_barbuleDirVariation)(gen),
                            std::uniform_real_distribution<Float>(-m_barbuleDirVariation, m_barbuleDirVariation)(gen)
                        );
                        barbuleDir += variation;
                        barbuleDir = barbuleDir.normalized();
                        currBarbuleLength = currBarbuleLength * std::uniform_real_distribution<Float>(Float(0.8), Float(1.2))(gen);

                        // Create the barbule curve
                        createBarbule(barbuleCPs, barbulePos, barbuleDir, barbDir, currBarbuleLength, PCAData);

                        // Move to next position along barb
                        realPositionBarbule += Float(2) * m_rootRadiusBarbule * (Float(1) + pow(t, Float(1.5)));
                        t = realPositionBarbule / currentBarbLength;
                        t = min(t, Float(1.0));
                    }
                    featherOutlineData.push_back(barbuleCPs.back());
                }
                numCurrentBarbs++;
                prevTangent = currentTangent.normalized();
            }
        }
        // 4. Save original feather as curves object (all components of the same type of one feather get saved together)
        ref<Curves> spineCurvesRef = std::make_shared<Curves>(spineCPs, m_originalPath, m_curveSplits, m_rootRadiusSpine, m_tipRadiusSpine, m_radiusFunction, m_numCPs);
        ref<Curves> barbCurvesRef = std::make_shared<Curves>(barbCPs, m_originalPath, m_curveSplits, m_rootRadiusBarb, m_tipRadiusBarb, m_radiusFunction, m_numCPs);
        ref<Curves> barbuleCurvesRef = std::make_shared<Curves>(barbuleCPs, m_originalPath, m_curveSplits, m_rootRadiusBarbule, m_tipRadiusBarbule, m_radiusFunction, m_numCPs);

        // 5. Save instances containing originals with only basic transform (that hair had) in children
        ref<Group> groupedFeather = std::make_shared<Group>(spineCurvesRef, barbCurvesRef, barbuleCurvesRef);
        m_childrenHigh[featherIdx] = std::make_shared<Instance>(groupedFeather, nullptr);

        logger(EWarn, "Generated a feather with %i barbs and %i barbules", numBarbs, numBarbules);
        logger(EWarn, "Size of a curves object: %i", sizeof(Curves));

        // If we only want high detail, return before we start creating low detail versions
        if (!m_useLOD) {
            m_childrenLow[featherIdx] = std::make_shared<Instance>(groupedFeather, nullptr);
            return;
        }

        // Output PCA data in .csv format
        // std::ofstream file("./data_visualization/pca_data.csv");
        // if (!file.is_open()) {
        //     logger(EError, "Failed to open file.");
        // } else {
        //     // Header
        //     file << "x,y,z\n";
        //     // Data
        //     for (const auto& point : PCAData) {
        //         file << point.x() << "," << point.y() << "," << point.z() << "\n";
        //     }
        //     file.close();
        //     logger(EWarn, "Exported %i points.", PCAData.size());
        // }
        // logger(EWarn, "Don't forget to kill the process!");
        // exit(0);

        // 6. Create mesh with simplified feather
        // Create infinite plane onto which we want to project feather
        Vector u, v;
        if (m_planeFindingMode == PlaneFindingMode::PCA) {
            PCA pca;
            pca.fit(pointsToMatrix(PCAData));
            std::pair<Vector, Vector> resUV = pca.getTwoPrincipalDirections();
            u = resUV.first;
            v = resUV.second;
        } else if (m_planeFindingMode == PlaneFindingMode::SVD) {
            SVD svd;
            svd.fit(pointsToMatrix(PCAData));
            std::pair<Vector, Vector> resUV = svd.getTwoPrincipalDirections();
            u = resUV.first;
            v = resUV.second;
        } else { // if PlaneFindingMode::Standard
            const Vector prevTangent = getBezierTangent(spineCPs, Float(0.0)).normalized();
            Vector initialUp = hairNormals[featherIdx];
            initialUp = (initialUp - initialUp.dot(prevTangent) * prevTangent).normalized();
            const Vector spineVector = spineCPs[spineCPs.size()-1] - spineCPs[0];
            const Vector midpointUp = parallelTransport(initialUp, prevTangent, spineVector);
            const Vector perpVector = spineVector.cross(midpointUp); // Second spanning vector

            u = spineVector.normalized();
            v = perpVector.normalized();
        }

        const Point planeOrigin = evaluateBezier(spineCPs, Float(0.0));

        // Find the bounding rectangle of the feather
        Float minU = Float(0.0);
        Float maxU = (spineCPs[spineCPs.size()-1] - planeOrigin).dot(u); // Most of the time the spine end point will be the farthest along u
        Float minV = std::numeric_limits<Float>::max();
        Float maxV = std::numeric_limits<Float>::lowest();

        for (size_t i = 0; i < featherOutlineData.size(); i++) {
            Vector toPoint = featherOutlineData[i] - planeOrigin;

            // Project onto the two plane axes
            Float uCoord = toPoint.dot(u);
            Float vCoord = toPoint.dot(v);

            minU = min(minU, uCoord);
            maxU = max(maxU, uCoord);
            minV = min(minV, vCoord);
            maxV = max(maxV, vCoord);
        }
        maxV = max(abs(minV), abs(maxV));
        minV = -maxV;

        // Create rectangle and necessary transforms for it to fit the feather
        const Float width = (maxU - minU);
        const Float height = (maxV - minV);
        ref<Rectangle> simplifiedFeatherRect = std::make_shared<Rectangle>(width, height); // width, height
        ref<Transform> transformRect = std::make_shared<Transform>();

        transformRect->translate(Vector(Float(width/2), Float(0.0), Float(0.0)));

        // Rotation
        transformRect->rotateToPlane(u, v);
        // Translate
        transformRect->translate(Vector(planeOrigin));

        // 7. Prepare information needed for later texture baking
        const Vector planeNormal = (u.cross(v)).normalized();
        const Point shiftedPlaneOrigin = planeOrigin + minV * v;
        logger(EWarn, "barbuleCount: %i", barbuleCount);
        int32_t pixelCount = barbuleCount * 10; // * 1000 seems to be the limit with my usual barbule counts
        Frame f = Frame(u.normalized(), v.normalized(), planeNormal);

        m_infosFromGeometry.insert({featherIdx, ParamsForTextureBaking(
            u, v, width, height, shiftedPlaneOrigin, planeNormal, f, pixelCount
        )});

        // logger(EDebug, "idx=%i || u: %s, v: %s, normal: %s | testU: %s uPCA: %s, vPCA: %s", featherIdx, u, v, planeNormal, testU, uPCA, vPCA);

        // 8. Save low detail feather into children
        ref<Instance> castedSimplifiedFeather = std::make_shared<Instance>(simplifiedFeatherRect, transformRect);
        m_childrenLow[featherIdx] = castedSimplifiedFeather;
    }

    /**
     * @brief Creates a texture that looks like the original feathers that we can put onto the simplified feathers.
     * Creates an alpha mask for transparency, a texture and a normal map for each.
     * @param idx Index at which the feather that we want to bake lies at in m_children.
     * @param numSamples How many samples we take per "pixel".
     * @param outNormal Reference to normal map texture will be saved here.
     * @param outAlpha Reference to alpha map texture will be saved here.
     */
    void bakeFeatherTexture(const int32_t idx, const int32_t numSamples,
                            ref<Texture> &outNormal, ref<Texture> &outAlpha) {
        // Skip if we do not need low detail
        if (!m_useLOD) {
            outAlpha = std::make_shared<ConstantTexture>(Color());
            outNormal = std::make_shared<ConstantTexture>(Color());
            return;
        }
        
        const Float lenU = m_infosFromGeometry.at(idx).lenU;
        const Float lenV = m_infosFromGeometry.at(idx).lenV;

        // Compute resolution
        const Float ratio = lenU / lenV; // The longer side should have more pixels
        const int baseSide = static_cast<int>(sqrt(m_infosFromGeometry.at(idx).pixelCount / ratio));
        const Vector2i resolution = Vector2i(static_cast<int>(ratio * baseSide), baseSide);
        logger(EDebug, "Creating texture for simplified feather with resolution: %s", resolution);
        const Vector2 stepSize = Vector2(lenU / resolution[0], lenV / resolution[1]); // Distance between each ray origin (pre-jitter)

        Image alphaImg = Image(resolution);
        Image normalImg = Image(resolution);

        // Shoot a ray for each pixel
        const float normalization = (1.0f / static_cast<float>(numSamples));
        // --- CUT HERE FOR DEBUG -------------------------------------------------------------------
        // TODO can't I just use: #pragma omp parallel for
        for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block) {
            for (auto pixel : block) { // pixel: e.g. Point[6, 9] (from 0,0 to 9,9)
                Independent rng = Independent();
                Color sumAlpha;
                Vector sumNormal;
                Point2i flippedPixel(pixel[0], resolution[1] - 1 - pixel[1]); // Otherwise v-side would be flipped

                for (int32_t sample = 0; sample < numSamples; sample++) {
                    Payload p;

                    // Add a small random offset to the ray origin
                    auto randomOffset = rng.next2D();
                    const Vector uOffset = (pixel[0] * stepSize[0]
                                        + randomOffset[0] * stepSize[0])
                                        * m_infosFromGeometry.at(idx).u;
                    const Vector vOffset = (pixel[1] * stepSize[1]
                                        + randomOffset[1] * stepSize[1])
                                        * m_infosFromGeometry.at(idx).v;
                    // const Vector uOffset = (pixel[0] + randomOffset[0]) * stepSize[0] * m_infosFromGeometry.at(idx).u;
                    // const Vector vOffset = (pixel[1] + randomOffset[1]) * stepSize[1] * m_infosFromGeometry.at(idx).v;
                    Point rayOrigin = m_infosFromGeometry.at(idx).rectOrigin
                                    - m_infosFromGeometry.at(idx).rectNormal
                                    + uOffset + vOffset;

                    Ray ray = Ray(rayOrigin, m_infosFromGeometry.at(idx).rectNormal);
                    Intersection its(-ray.direction);

                    bool wasIntersected = m_childrenHigh[idx]->intersect(ray, its, p, rng);
                    sumAlpha += normalization * (wasIntersected ? Color(1.0f) : Color(0.0f));
                    
                    if (wasIntersected) {
                        Vector normal = m_infosFromGeometry.at(idx).localFrame.toLocal(its.shadingNormal);
                        sumNormal += normal;
                    }
                }

                sumNormal = sumNormal.length() == 0 ? sumNormal : sumNormal.normalized();
                
                alphaImg.get(flippedPixel)  = sumAlpha;
                normalImg.get(flippedPixel) = sumNormal.isZero() ? // Save some other normalized vector in case there is no normal here.
                                              Color(1.0f, 0.5f, 0.5f) : 
                                              Color((float(sumNormal[0]) + 1.0f) / 2.0f,
                                                    (float(sumNormal[1]) + 1.0f) / 2.0f,
                                                    (float(sumNormal[2]) + 1.0f) / 2.0f);
            }
        });

        // --- DEBUGGING WITHOUT PARALLELIZATION ----------------------------------------------
        // for (int x = 0; x < resolution.x(); x++) {
        //     for (int y = 0; y < resolution.y(); y++) {
        //         Independent rng = Independent();
        //         Color sumAlpha;
        //         Vector sumNormal;
        //         Point2i flippedPixel(x, resolution[1] - 1 - y); // Otherwise v-side would be flipped
        //         logger(EError, "2 pixel %i %i", x, y);

        //         for (int32_t sample = 0; sample < numSamples; sample++) {
        //             // Add a small random offset to the ray origin
        //             auto randomOffset = rng.next2D();
        //             const Vector uOffset = (x * stepSize[0]
        //                                 + randomOffset[0] * stepSize[0])
        //                                 * m_infosFromGeometry.at(idx).u;
        //             const Vector vOffset = (y * stepSize[1]
        //                                 + randomOffset[1] * stepSize[1])
        //                                 * m_infosFromGeometry.at(idx).v;
        //             Point rayOrigin = m_infosFromGeometry.at(idx).rectOrigin
        //                             - m_infosFromGeometry.at(idx).rectNormal
        //                             + uOffset + vOffset;
        //             logger(EError, "2-1 sample %i pixel %i %i", sample, x, y);

        //             Ray ray = Ray(rayOrigin, m_infosFromGeometry.at(idx).rectNormal);
        //             Intersection its(-ray.direction);
        //             logger(EError, "2-2 sample %i pixel %i %i", sample, x, y);

        //             bool wasIntersected = m_childrenHigh[idx]->intersect(ray, its, p, rng);
        //             logger(EError, "2-2-2 sample %i pixel %i %i", sample, x, y);
        //             sumAlpha += normalization * (wasIntersected ? Color(1.0f) : Color(0.0f));
        //             logger(EError, "2-3 sample %i pixel %i %i", sample, x, y);
                    
        //             Vector normal = its.shadingNormal;
        //             sumNormal += normalization * normal;
        //             logger(EError, "2-4 sample %i pixel %i %i", sample, x, y);
        //         }
        //         alphaImg.get(flippedPixel) = sumAlpha;
        //         normalImg.get(flippedPixel) = Color(sumNormal[0] + 1.0f / 2.0f,
        //                                             sumNormal[1] + 1.0f / 2.0f,
        //                                             sumNormal[2] + 1.0f / 2.0f);
        //         logger(EError, "3 pixel %i %i", x, y);
        //     }
        // }
        // logger(EError, "4");
        // ------------------------------------------------------------------------------------

        // Debug outputs
        alphaImg.saveAt("alpha_test_objID=" + id() + "_idx=" + std::to_string(idx) + ".exr");
        normalImg.saveAt("normal_tex_test_objID=" + id() + "_idx=" + std::to_string(idx) + ".exr");

        // Create references
        ref<Image> alphaImgRef = std::make_shared<Image>(alphaImg);
        ref<Image> normalImgRef = std::make_shared<Image>(normalImg);
        outAlpha = std::make_shared<ImageTexture>(alphaImgRef); // ref<Texture> alphaTexRef
        outNormal = std::make_shared<ImageTexture>(normalImgRef); // ref<Texture> normalTexRef
    }

    /** @brief Creates a single barb (secondary feather) as a cubic Bézier curve.
     * @param barbCPs Output: The vector into which we append our finished barb at the end.
     * @param barbPos Point at which the barb will start.
     * @param barbDir Direction to which the barb will point at its starting point.
     * @param spineDir Direction in which the spine tangent is going at barb starting point.
     * @param barbLength Length the barb should have.
     * @param perpDir Perpendicular direction to spine.
     * @param unzipAmount How strong the unzipping effect is (scales with barb length).
     * @param unzipSide Which side of the unzipping we are currently on.
     * @param curveStrength Modifies how strongly the barb curves.
     */
    void createBarb(std::vector<Point> &barbCPs, const Point &barbPos, const Vector &barbDir,
                    const Vector &spineDir, const Float barbLength, const Vector &perpDir,
                    const Float unzipAmount = Float(0.0), const int unzipSide = 0,
                    const Float curveStrength = Float(0.4)) {
        const Point w0 = barbPos;
        Vector endDirection = barbDir * Float(0.5) + spineDir * (curveStrength * Float(0.5));
        endDirection = endDirection.normalized();

        // Add lateral deviation based on unzip amount
        Vector lateralOffset = perpDir * (unzipAmount * unzipSide);
        const Point w3 = w0 + endDirection * barbLength + lateralOffset * barbLength;

        // w1 stays closer to original direction for straighter start
        const Float w1Length = barbLength * Float(0.6);
        const Point w1 = w0 + barbDir * w1Length;// + lateralOffset * (barbLength * Float(0.2));
        
        // w2 pulls towards the curved end with increased influence for a sharper curve
        const Float w2Length = barbLength * Float(0.15);
        const Vector curveDirection = (barbDir * Float(0.1) + spineDir * (curveStrength * Float(0.9))).normalized();
        const Point w2 = w3 - curveDirection * w2Length;// + lateralOffset * (barbLength * Float(0.1));

        barbCPs.insert(barbCPs.end(), {w0, w1, w2, w3});
    }

    /** @brief Create a single barbule (tertiary feather) as a bezier curve.
     * @param barbuleCPs Output: The vector into which we append our finished barbule at the end.
     * @param barbulePos Point at which the barbule will start.
     * @param barbuleDir Direction to which the barbule will point at its starting point (should be normalized).
     * @param barbDir Direction in which the barb tangent is going at barbule starting point.
     * @param barbuleLength Length the barbule should have.
     * @param PCAData Output: The vector in which w3 of every barbule is saved for using it in PCA later.
     */
    void createBarbule(std::vector<Point> &barbuleCPs, const Point &barbulePos, const Vector &barbuleDir,
                    const Vector &barbDir, const Float barbuleLength, std::vector<Point> &PCAData) {
        // # add small random variation to direction
        // direction = direction.normalize()
        // random_deflection = Vec3(
        //     random.uniform(-waviness, waviness),
        //     random.uniform(-waviness, waviness),
        //     random.uniform(-waviness, waviness)
        // )
        // direction = (direction + random_deflection).normalize()
        
        const Point w0 = barbulePos;
        const Point w3 = w0 + barbuleDir * barbuleLength;

        // Control points along the direction
        const Float handleLength = barbuleLength / Float(3.0);
        const Point w1 = w0 + barbuleDir * handleLength;
        const Point w2 = w3 - barbuleDir * handleLength;

        barbuleCPs.insert(barbuleCPs.end(), {w0, w1, w2, w3});
        PCAData.push_back(w0);
    }

    /// @brief Randomly selects an index based on given weights.
    /// The index is in the range of [0, weights.size()].
    int32_t weightedRandomSelection(const std::vector<Float> &weights) {
        Float totalWeight = std::accumulate(weights.begin(), weights.end(), Float(0.0));
        if (totalWeight <= 0) { // Fallback in case all weights are 0
            std::uniform_int_distribution<int32_t> dist(0, weights.size() - 1);
            return dist(m_gen);
        }

        std::uniform_real_distribution<Float> dist(Float(0.0), totalWeight);
        Float randomValue = dist(m_gen);

        Float cumulativeWeight = Float(0.0);
        for (size_t i = 0; i < weights.size(); ++i) {
            cumulativeWeight += weights[i];
            if (randomValue <= cumulativeWeight) {
                return i;
            }
        }

        return weights.size() - 1; // Fallback

        // Float highestVal = weights[0];
        // int32_t bestIdx = 0;

        // for (int32_t i = 0; i < weights.size(); ++i) {
        //     if (weights[i] > highestVal) {
        //         highestVal = weights[i];
        //         bestIdx = i;
        //     }
        // }

        // return bestIdx;
    }

protected: // TODO change all these to reflect split between LODs!
           // meaning split their output into two versions, and add a parameter for switching
    std::pair<int, int> numberOfPrimitives() const override { 
        // TODO If I ever want to offer m_useLOD as a flag, I will need to change this again :)
        // It needs to be like this rn because m_useLOD will be false during buildAccelerationStructure, but numberOfPrimitives is needed in there.
        // if (m_useLOD) return { int(m_childrenHigh.size()), int(m_childrenLow.size()) };
        // else return { int(m_childrenHigh.size()), 0 };
        return { int(m_childrenHigh.size()), int(m_childrenLow.size()) };
    }

    Bounds getBoundingBox(int primitiveIndex, bool forHighLOD) const override {
        if (forHighLOD) return m_childrenHigh[primitiveIndex]->getBoundingBox();
        else return m_childrenLow[primitiveIndex]->getBoundingBox();
    }

    Point getCentroid(int primitiveIndex) const override {
        return m_childrenHigh[primitiveIndex]->getCentroid();
    }

    bool intersect(int primitiveIndex, const Ray &ray, Intersection &its, Payload &p,
                   Sampler &rng, bool useHighLOD=true) const override {
        if (useHighLOD || !m_useLOD) {
            bool wasIntersected = m_childrenHigh[primitiveIndex]->intersect(ray, its, p, rng);
            if (wasIntersected) {
                p.currentObjID = std::hash<std::string>{}(id());
                p.currentFeatherID = primitiveIndex;
                p.currentComingFromHD = true;
            }
            return wasIntersected;
        }
        else {
            // If this feather is the same feather as what the ray last hit, we do an early exit,
            // to prevent self-intersection between different levels of detail
            if (p.isComingFromHD && p.lastObjID == std::hash<std::string>{}(id()) && p.lastFeatherID == primitiveIndex)
                return false;

            bool wasIntersected = m_childrenLow[primitiveIndex]->intersect(ray, its, p, rng);
            if (wasIntersected) {
                p.currentObjID = std::hash<std::string>{}(id());
                p.currentFeatherID = primitiveIndex;
                p.currentComingFromHD = false;
            }
            return wasIntersected;
        }
    }

public:
    Feathers(const Properties &properties) {
        m_gen = std::mt19937(42); // m_rd()

        m_originalPath           = properties.get<std::filesystem::path>("filename");
        m_useLOD                 = properties.get<bool>("lod", "true");
        m_thresholdLOD           = Float(properties.get<float>("thresholdLOD"));
        m_numOriginalFeathers    = properties.get<int>("numOriginalFeathers", 2);
        m_rootRadiusSpine        = Float(properties.get<float>("rootRadiusSpine", 0.005f));
        m_asymmetry              = Float(properties.get<float>("asymmetry", 0)); // Negative values make left side shorter, positives right side
        m_planeFindingMode       = properties.get<PlaneFindingMode>("planeFindingMode", PlaneFindingMode::PCA);
        m_barbAngleBlend         = Float(properties.get<float>("barbAngle", 0.4f));
        m_barbToSpineLengthRatio = Float(properties.get<float>("barbLengthRatio", 0.3f));
        m_derivativePosition     = Float(properties.get<float>("directionDerivativePos", Float(0)));
        m_isTailFeather          = properties.get<bool>("isTailFeather", "true");

        m_tipRadiusSpine = m_rootRadiusSpine / Float(50);
        m_rootRadiusBarb = m_rootRadiusSpine / Float(10);
        m_tipRadiusBarb = m_rootRadiusBarb / Float(10);
        m_rootRadiusBarbule = m_rootRadiusSpine / Float(50);
        m_tipRadiusBarbule = m_rootRadiusBarbule / Float(10);

        m_a = Float(properties.get<float>("weightA", 0.1f));
        m_b = Float(properties.get<float>("weightB", 0.0025f)); // 0.05^2 because of squared distance
        m_c = Float(properties.get<float>("weightC", 0.2f));

        generateFeathers();

        buildAccelerationStructure();
    }

    void markAsVisible() override {
        for (auto &child : m_childrenHigh)
            child->markAsVisible();
        for (auto &child : m_childrenLow)
            child->markAsVisible();
    }

    void passInfoToFeathers(ref<Bsdf> originalBsdf) override {
        m_bsdf = originalBsdf;

        for (size_t idx : m_selectedIndices) {
            ref<Texture> normalTex, alphaTex;
            ref<Bsdf> simpleBsdf;

            if (m_useLOD) bakeFeatherTexture(idx, 10, normalTex, alphaTex);

            // Assign textures and bsdf to instances
            m_childrenLow[idx]->setLateParameters(m_bsdf, normalTex, alphaTex);
        }
        m_infosFromGeometry.clear();
    }

    std::string toString() const override {
        std::stringstream oss;
        // oss << "Feather group - high detail[" << std::endl;
        // for (auto &entity : m_childrenHigh) {
        //     oss << "  " << indent(entity) << "," << std::endl;
        // }
        // oss << "]," << std::endl << "low detail[";
        // for (auto &entity : m_childrenLow) {
        //     oss << "  " << indent(entity) << "," << std::endl;
        // }
        // oss << "]";
        oss << "Feather group" << std::endl;
        return oss.str();
    }
};

} // namespace lightwave

REGISTER_SHAPE(Feathers, "feathers")
