#pragma once

#include <lightwave/core.hpp>
#include <lightwave/hash.hpp>
#include <lightwave/math.hpp>
#include <numeric>

namespace lightwave {

/// Returns the integer that is greater or equal to the logarithm base 2 of the
/// argument.
template <typename T> inline T closest_log2(T i) {
    T p = 1, q = 0;
    while (i > p)
        p <<= 1, q++;
    return q;
}

class HashGrid {
public:
    HashGrid() {}

    template <typename PositionFn>
    void build(PositionFn positions, size_t num_photons, Float radius) {
        radius_sqr = radius * radius;
        inv_size   = Float(0.5) / radius;

        // Compute the global bounding box encompassing all the photons
        bbox = Bounds();
        // #pragma omp parallel for reduction(bbox_extend: bbox)
        for (size_t i = 0; i < num_photons; ++i)
            bbox.extend(positions(i));

        // Slightly enlarge the bounding box of the photons to avoid numerical
        // problems
        auto extents = bbox.diagonal();
        bbox.max() += extents * Float(0.001);
        bbox.min() -= extents * Float(0.001);

        photons.resize(num_photons);
        cell_counts.resize(size_t(1) << (closest_log2(num_photons) + 1));
        std::fill(cell_counts.begin(), cell_counts.end(), 0);

        // Count the number of photons per cell
        // #pragma omp parallel for
        for (size_t i = 0; i < num_photons; i++) {
            auto h = hash_photon(positions(i));
            // #pragma omp atomic
            cell_counts[h]++;
        }

        // Compute the insertion position for each cell
        std::partial_sum(cell_counts.begin(), cell_counts.end(),
                         cell_counts.begin());
        assert(cell_counts.back() == photons.size());

        // Put the photons in their respective cells
        // #pragma omp parallel for
        for (size_t i = 0; i < num_photons; i++) {
            auto h = hash_photon(positions(i));
            int old_count;
            // #pragma omp atomic capture
            old_count          = --cell_counts[h];
            photons[old_count] = uint32_t(i);
        }
    }

    template <typename PositionFn, typename InsertFn>
    void query(const Point &pos, PositionFn positions, InsertFn insert) const {
        if (!bbox.includes(pos))
            return;

        auto p  = (pos - bbox.min()) * inv_size;
        int px1 = int(p.x());
        int py1 = int(p.y());
        int pz1 = int(p.z());
        int px2 = px1 + (p.x() - Float(px1) > Float(0.5) ? 1 : -1);
        int py2 = py1 + (p.y() - Float(py1) > Float(0.5) ? 1 : -1);
        int pz2 = pz1 + (p.z() - Float(pz1) > Float(0.5) ? 1 : -1);

        for (int i = 0; i < 8; i++) {
            auto range = cell_range(i & 1 ? px2 : px1, i & 2 ? py2 : py1,
                                    i & 4 ? pz2 : pz1);

            for (auto j = range.first; j < range.second; j++) {
                int photon_id   = photons[j];
                auto photon_pos = positions(photon_id);
                auto d          = (pos - photon_pos).lengthSquared();
                if (d < radius_sqr)
                    insert(photon_id, d);
            }
        }
    }

private:
    std::pair<size_t, size_t> cell_range(uint32_t x, uint32_t y,
                                         uint32_t z) const {
        auto h = hash_cell(x, y, z);
        return std::make_pair(cell_counts[h], h == cell_counts.size() - 1
                                                  ? photons.size()
                                                  : cell_counts[h + 1]);
    }

    /// Returns the initializer for Bernstein's hash function
    inline uint32_t bernstein_init() const { return 5381; }

    /// Hashes 4 bytes using Bernstein's hash
    inline uint32_t bernstein_hash(uint32_t h, uint32_t d) const {
        h = (h * 33) ^ (d & 0xFF);
        h = (h * 33) ^ ((d >> 8) & 0xFF);
        h = (h * 33) ^ ((d >> 16) & 0xFF);
        h = (h * 33) ^ ((d >> 24) & 0xFF);
        return h;
    }

    uint32_t hash_cell(uint32_t x, uint32_t y, uint32_t z) const {
        uint32_t h = bernstein_hash(bernstein_init(), x);
        h          = bernstein_hash(h, y);
        h          = bernstein_hash(h, z);
        return h & (cell_counts.size() - 1);
    }

    uint32_t hash_photon(const Point &pos) const {
        auto p = ((pos - bbox.min()) * inv_size).cast<uint32_t>();
        return hash_cell(p.x(), p.y(), p.z());
    }

    std::vector<uint32_t> photons;
    std::vector<uint32_t> cell_counts;
    Bounds bbox;
    Float inv_size;
    Float radius_sqr;
};

} // namespace lightwave
