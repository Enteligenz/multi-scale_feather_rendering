#pragma once

#include <lightwave/core.hpp>
#include <lightwave/math.hpp>
#include <lightwave/shape.hpp>

#include <numeric>

namespace lightwave {

/**
 * @brief Parent class for shapes that combine many individual shapes (e.g.,
 * triangle meshes), and hence benefit from building an acceleration structure
 * over their children.
 *
 * To use this class, you will need to implement the following methods:
 * - numberOfPrimitives()           -- report the number of individual children
 * that the shape has
 * - intersect(primitiveIndex, ...) -- intersect a single child (identified by
 * the given index) for the given ray
 * - getBoundingBox(primitiveIndex) -- return the bounding box of a single child
 * (used for building the BVH)
 * - getCentroid(primitiveIndex)    -- return the centroid of a single child
 * (used for building the BVH)
 *
 * @example For a simple example of how to use this class, look at @ref
 * shapes/group.cpp
 * @see Group
 * @see TriangleMesh
 */
class AccelerationStructure : public Shape {
    /// @brief The datatype used to index BVH nodes and the primitive index
    /// remapping.
    typedef int32_t NodeIndex;

    /// @brief A node in our binary BVH tree.
    struct Node {
        /// @brief The axis aligned bounding box of this node.
        Bounds aabb;
        /**
         * @brief Either the index of the left child node in m_nodes (for
         * internal nodes), or the first primitive in m_primitiveIndices (for
         * leaf nodes).
         * @note For efficiency, we store the BVH nodes so that the right child
         * always directly follows the left child, i.e., the index of the right
         * child is always @code leftFirst + 1 @endcode .
         * @note For efficiency, we store primitives so that children of a leaf
         * node are always contigous in m_primitiveIndices.
         */
        NodeIndex leftFirst;
        /// @brief The number of primitives in a leaf node, or 0 to indicate
        /// that this node is not a leaf node.
        NodeIndex primitiveCount;

        /// @brief Whether this BVH node is a leaf node.
        bool isLeaf() const { return primitiveCount != 0; }

        /// @brief For internal nodes: The index of the left child node in
        /// m_nodes.
        NodeIndex leftChildIndex() const { return leftFirst; }
        /// @brief For internal nodes: The index of the right child node in
        /// m_nodes.
        NodeIndex rightChildIndex() const { return leftFirst + 1; }

        /// @brief For leaf nodes: The first index in m_primitiveIndices.
        NodeIndex firstPrimitiveIndex() const { return leftFirst; }
        /// @brief For leaf nodes: The last index in m_primitiveIndices (still
        /// included).
        NodeIndex lastPrimitiveIndex() const {
            return leftFirst + primitiveCount - 1;
        }
    };

    /// @brief A list of all BVH nodes.
    std::vector<Node> m_nodes;
    /// @brief A list of all low-detail BVH nodes.
    std::vector<Node> m_nodesLow;
    /**
     * @brief Mapping from internal @c NodeIndex to @c primitiveIndex as used by
     * all interface methods. For efficient storage, we assume that children of
     * BVH leaf nodes have contiguous indices, which would require re-ordering
     * the primitives. For simplicity, we instead perform this re-ordering on a
     * list of indices (which starts of as @code 0, 1, 2, ..., primitiveCount -
     * 1 @endcode ), which allows us to translate from re-ordered (contiguous)
     * indices to the indices the user of this class expects.
     */
    std::vector<int> m_primitiveIndices;
    /// @brief Low-detail version of @c m_primitiveIndices
    std::vector<int> m_primitiveIndicesLow;

    /// @brief Returns the root BVH node.
    const Node &rootNode(bool forHighLOD=true) const {
        // by convention, this is always the first element of m_nodes
        if (forHighLOD) return m_nodes.front();
        else return m_nodesLow.front();
    }

    /**
     * @brief Intersects a BVH node, recursing into children (for internal
     * nodes), or intersecting all primitives (for leaf nodes).
     */
    bool intersectNode(const Node &node, const Ray &ray, Intersection &its,
                       Payload &p, Sampler &rng,
                       const std::vector<int> &primitiveIndices, const std::vector<Node> &nodes,
                       const bool useHighLOD) const {
        // update the statistic tracking how many BVH nodes have been tested for
        // intersection
        its.stats.bvhCounter++;

        bool wasIntersected = false;
        if (node.isLeaf()) {
            for (NodeIndex i = 0; i < node.primitiveCount; i++) {
                // update the statistic tracking how many children have been
                // tested for intersection
                its.stats.primCounter++;
                // test the child for intersection
                wasIntersected |= intersect(
                    primitiveIndices[node.leftFirst + i], ray, its, p, rng, useHighLOD);
            }
        } else { // internal node
            // test which bounding box is intersected first by the ray.
            // this allows us to traverse the children in the order they are
            // intersected in, which can help prune a lot of unnecessary
            // intersection tests.
            const auto leftT =
                intersectAABB(nodes[node.leftChildIndex()].aabb, ray);
            const auto rightT =
                intersectAABB(nodes[node.rightChildIndex()].aabb, ray);
            if (leftT < rightT) { // left child is hit first; test left child
                                  // first, then right child
                if (leftT < its.t)
                    wasIntersected |= intersectNode(
                        nodes[node.leftChildIndex()], ray, its, p, rng, primitiveIndices, nodes, useHighLOD);
                if (rightT < its.t)
                    wasIntersected |= intersectNode(
                        nodes[node.rightChildIndex()], ray, its, p, rng, primitiveIndices, nodes, useHighLOD);
            } else { // right child is hit first; test right child first, then
                     // left child
                if (rightT < its.t)
                    wasIntersected |= intersectNode(
                        nodes[node.rightChildIndex()], ray, its, p, rng, primitiveIndices, nodes, useHighLOD);
                if (leftT < its.t)
                    wasIntersected |= intersectNode(
                        nodes[node.leftChildIndex()], ray, its, p, rng, primitiveIndices, nodes, useHighLOD);
            }
        }
        return wasIntersected;
    }

    /// @brief Performs a slab test to intersect a bounding box with a ray,
    /// returning Infinity in case the ray misses.
    Float intersectAABB(const Bounds &bounds, const Ray &ray) const {
        const auto t1 = (bounds.min() - ray.origin) / ray.direction;
        // intersect all axes at once with the maximum slabs of the bounding box
        const auto t2 = (bounds.max() - ray.origin) / ray.direction;

        // the elementwiseMin picks the near slab for each axis, of which we
        // then take the maximum
        const auto tNear = elementwiseMin(t1, t2).maxComponent();
        // the elementwiseMax picks the far slab for each axis, of which we then
        // take the minimum
        const auto tFar = elementwiseMax(t1, t2).minComponent();

        if (tFar < tNear)
            return Infinity; // the ray does not intersect the bounding box
        if (tFar < Epsilon)
            return Infinity; // the bounding box lies behind the ray origin

        return tNear; // return the first intersection with the bounding box
                      // (may also be negative!)
    }

    /// @brief Computes the axis aligned bounding box for a leaf BVH node
    void computeAABB(Node &node, std::vector<int> &primitiveIndices, bool forHighLOD=true) {
        node.aabb = Bounds::empty();
        for (NodeIndex i = 0; i < node.primitiveCount; i++) {
            const Bounds childAABB =
                getBoundingBox(primitiveIndices[node.leftFirst + i], forHighLOD);
            node.aabb.extend(childAABB);
        }
    }

    /// @brief Computes the surface area of a bounding box.
    Float surfaceArea(const Bounds &bounds) const {
        const auto size = bounds.diagonal();
        return 2 * (size.x() * size.y() + size.x() * size.z() +
                    size.y() * size.z());
    }

    /**
     * For a given node, computes split axis and split position that minimize the surface area heuristic.
     * @param node The BVH node to compute the split for.
     * @param out bestSplitAxis The optimal split axis, or -1 if no useful split exists
     * @param out bestSplitPosition The optimal split position, undefined if no useful split exists
     * @param primitiveIndices The index list we want to use (should be m_primitiveIndices or m_primitiveIndicesLow).
     * @param forHighLOD Whether this is for a high- or low-detail node.
     */
    void binning(const Node &node, int &bestSplitAxis, Float &bestSplitPosition,
        std::vector<int> &primitiveIndices, bool forHighLOD=true) {
        static constexpr size_t BinCount = 16;
        struct Bin {
            Bounds aabb;
            NodeIndex primCount{ 0 };
            Float rightCost{ Float(0.) };
        };
        struct Split {
            Float cost     = Infinity;
            int axis       = -1;
            Float position = 0;
        };
        
        Split bestSplit;

        // compute SAH cost of doing no split and use as baseline
        const Float traversalCost = Float(1.);
        bestSplit.cost =
            (node.primitiveCount - traversalCost) * surfaceArea(node.aabb);

        const NodeIndex firstPrim = node.firstPrimitiveIndex();
        const NodeIndex lastPrim  = node.lastPrimitiveIndex();

        // for (int test = 0; test < 1; test++) {
        // const int splitAxis = node.aabb.diagonal().maxComponentIndex();
        for (int splitAxis = 0; splitAxis < 3; splitAxis++) {
            Split split;
            split.axis = splitAxis;

            // compute range of centroid
            Float centroidMin = +Infinity;
            Float centroidMax = -Infinity;
            for (NodeIndex i = firstPrim; i <= lastPrim; i++) {
                const Float centroid =
                    getCentroid(primitiveIndices[i])[splitAxis];
                centroidMin = std::min(centroidMin, centroid);
                centroidMax = std::max(centroidMax, centroid);
            }

            if (centroidMin == centroidMax) {
                continue;
            }

            const Float binSize        = (centroidMax - centroidMin) / BinCount;
            const Float inverseBinSize = BinCount / (centroidMax - centroidMin);

            // compute bins
            std::array<Bin, BinCount> bins;
            for (NodeIndex i = firstPrim; i <= lastPrim; i++) {
                const Float centroid =
                    getCentroid(primitiveIndices[i])[split.axis];
                int binId = int((centroid - centroidMin) * inverseBinSize);
                binId     = std::min(int(BinCount - 1), std::max(binId, 0));

                auto &bin = bins[binId];
                bin.aabb.extend(getBoundingBox(primitiveIndices[i], forHighLOD));
                bin.primCount++;
            }

            // Sweep bins to compute SAH
            Bounds sweepBBox;
            NodeIndex sweepCount = 0;
            for (int i = BinCount - 1; i >= 0; --i) {
                sweepCount += bins[i].primCount;
                sweepBBox.extend(bins[i].aabb);
                bins[i].rightCost = surfaceArea(sweepBBox) * sweepCount;
            }

            sweepBBox  = Bounds{};
            sweepCount = 0;
            for (size_t i = 0; i < BinCount - 1; ++i) {
                sweepCount += bins[i].primCount;
                sweepBBox.extend(bins[i].aabb);
                const Float leftCost  = surfaceArea(sweepBBox) * sweepCount;
                const Float totalCost = leftCost + bins[i + 1].rightCost;
                if (totalCost < split.cost) {
                    split.cost     = totalCost;
                    split.position = centroidMin + (int(i) + 1) * binSize;
                }
            }

            if (split.cost < bestSplit.cost) {
                bestSplit = split;
            }
        }

        bestSplitAxis     = bestSplit.axis;
        bestSplitPosition = bestSplit.position;
    }

    /// @brief Attempts to subdivide a given BVH node.
    void subdivide(Node &parent, std::vector<int> &primitiveIndices, std::vector<Node> &nodes, bool forHighLOD) {
        // only subdivide if enough children are available.
        if (parent.primitiveCount <= 2) {
            return;
        }

        static constexpr bool UseSAH = true;
        // set to true when implementing binning

        int splitAxis = -1;
        Float splitPosition;
        if (UseSAH) {
            // pick split axis and position using binned SAH
            binning(parent, splitAxis, splitPosition, primitiveIndices, forHighLOD);
        } else {
            // split in the middle of the longest axis
            splitAxis     = parent.aabb.diagonal().maxComponentIndex();
            splitPosition = parent.aabb.center()[splitAxis];
        }

        if (splitAxis == -1) {
            // a split axis of -1 indicates that no useful split exists
            return;
        }

        // the point at which to split (note that primitives must be re-ordered
        // so that all children of the left node will have a smaller index than
        // firstRightIndex, and nodes on the right will have an index larger or
        // equal to firstRightIndex)
        NodeIndex firstRightIndex = parent.firstPrimitiveIndex();
        NodeIndex lastLeftIndex   = parent.lastPrimitiveIndex();

        // partition algorithm (you might remember this from quicksort)
        while (firstRightIndex <= lastLeftIndex) {
            if (getCentroid(primitiveIndices[firstRightIndex])[splitAxis] <
                splitPosition) {
                firstRightIndex++;
            } else {
                std::swap(primitiveIndices[firstRightIndex],
                          primitiveIndices[lastLeftIndex--]);
            }
        }

        const NodeIndex firstLeftIndex = parent.firstPrimitiveIndex();
        const NodeIndex leftCount      = firstRightIndex - firstLeftIndex;
        const NodeIndex rightCount     = parent.primitiveCount - leftCount;

        if (leftCount == 0 || rightCount == 0) {
            // if either child gets no primitives, we abort subdividing
            return;
        }

        // the two children will always be contiguous in our m_nodes list
        const NodeIndex leftChildIndex  = (NodeIndex) (nodes.size() + 0);
        const NodeIndex rightChildIndex = (NodeIndex) (nodes.size() + 1);
        parent.primitiveCount = 0; // mark the parent node as internal node
        parent.leftFirst      = leftChildIndex;

        nodes.emplace_back();
        nodes[leftChildIndex].leftFirst      = firstLeftIndex;
        nodes[leftChildIndex].primitiveCount = leftCount;

        nodes.emplace_back();
        nodes[rightChildIndex].leftFirst      = firstRightIndex;
        nodes[rightChildIndex].primitiveCount = rightCount;

        // first, process the left child node (and all of its children)
        computeAABB(nodes[leftChildIndex], primitiveIndices, forHighLOD);
        subdivide(nodes[leftChildIndex], primitiveIndices, nodes, forHighLOD);
        // then, process the right child node (and all of its children)
        computeAABB(nodes[rightChildIndex], primitiveIndices, forHighLOD);
        subdivide(nodes[rightChildIndex], primitiveIndices, nodes, forHighLOD);
    }

protected:
    /// @brief Flag that indicates whether LOD should be used or not. Default is false.
    /// Classes that want to use LOD should set this as true in their constructor.
    bool m_useLOD = false;
    /// @brief The threshold that is used for switching between the levels of detail.
    /// Defaults need to be set in the properties.get<>() call for every class that uses this feature.
    Float m_thresholdLOD = Float(0.0f);

    // LOD switching weights for Philipp Ziegler's formula
    Float m_a = Float(0.1);
    Float m_b = Float(0.05); // 0.0025 maybe better because of squared distance
    Float m_c = Float(0.2);

    /// @brief Returns the number of children (individual shapes) that are part
    /// of this acceleration structure.
    /// The first number are the usual primitives, the second the low detail
    /// ones (if they exist, otherwise it's 0).
    virtual std::pair<int, int> numberOfPrimitives() const = 0;
    /// @brief Intersect a single child (identified by the index) with the given
    /// ray.
    virtual bool intersect(int primitiveIndex, const Ray &ray, Intersection &its, 
                           Payload &p, Sampler &rng, bool useHighLOD=true) const = 0;
    /// @brief Returns the axis aligned bounding box of the given child.
    virtual Bounds getBoundingBox(int primitiveIndex, bool forHighLOD=true) const = 0;
    /// @brief Returns the centroid of the given child.
    virtual Point getCentroid(int primitiveIndex) const = 0;

    /// @brief Builds the acceleration structure.
    void buildAccelerationStructure() {
        Timer buildTimer;

        // fill primitive indices with 0 to primitiveCount - 1
        auto [numHigh, numLow] = numberOfPrimitives();
        m_primitiveIndices.resize(numHigh);
        std::iota(m_primitiveIndices.begin(), m_primitiveIndices.end(), 0);
        m_primitiveIndicesLow.resize(numLow);
        std::iota(m_primitiveIndicesLow.begin(), m_primitiveIndicesLow.end(), 0);

        // create root node
        auto &root          = m_nodes.emplace_back();
        root.leftFirst      = 0;
        root.primitiveCount = numHigh;
        computeAABB(root, m_primitiveIndices, true);
        subdivide(root, m_primitiveIndices, m_nodes, true);

        // create root node for low detail version if we are using LOD
        if (numLow > 0) {
            auto &rootLow          = m_nodesLow.emplace_back();
            rootLow.leftFirst      = 0;
            rootLow.primitiveCount = numLow;
            computeAABB(rootLow, m_primitiveIndicesLow, false);
            subdivide(rootLow, m_primitiveIndicesLow, m_nodesLow, false);
        }

        if (numLow > 0) {
            logger(EInfo, "built BVH with %ld nodes for %ld primitives (high detail) as well as %ld nodes for %ld primitives (low detail) in %.1f ms",
               m_nodes.size(), numHigh,
               m_nodesLow.size(), numLow,
               buildTimer.getElapsedTime() * 1000);
        } else {
            logger(EInfo, "built BVH with %ld nodes for %ld primitives in %.1f ms",
               m_nodes.size(), numHigh,
               buildTimer.getElapsedTime() * 1000);
        }
    }

public:
    bool intersect(const Ray &ray, Intersection &its,
                   Payload &p, Sampler &rng) const override;

    // Note: for simplicity, this is the same, no matter the level of detail.
    Bounds getBoundingBox() const override { return rootNode(true).aabb; }

    // Note: for simplicity, this is the same, no matter the level of detail.
    Point getCentroid() const override { return rootNode(true).aabb.center(); }

    void passInfoToFeathers(ref<Bsdf> bsdf) override {};
};

} // namespace lightwave
