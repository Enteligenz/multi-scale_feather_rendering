#pragma once

#include <lightwave.hpp>

namespace lightwave {

class ImageTexture : public Texture {
    enum class BorderMode {
        Clamp,
        Repeat,
    };

    enum class FilterMode {
        Nearest,
        Bilinear,
    };

    ref<Image> m_image;
    Float m_exposure;
    BorderMode m_border;
    FilterMode m_filter;

    static inline int clampBorder(int x, int w) {
        return std::clamp(x, 0, w - 1);
    }

    static inline int repeatBorder(int x, int w) {
        const int t = x % w;
        return t < 0 ? t + w : t;
    }

    inline Point2i handleBorder(const Point2i &uv) const {
        const auto res = m_image->resolution();
        switch (m_border) {
        default:
        case BorderMode::Clamp:
            return Point2i(clampBorder(uv.x(), res.x()),
                           clampBorder(uv.y(), res.y()));
        case BorderMode::Repeat:
            return Point2i(repeatBorder(uv.x(), res.x()),
                           repeatBorder(uv.y(), res.y()));
        }
    }

    struct ImageTexelPoint {
        Point2i i;
        Point2 f;
    };

    inline ImageTexelPoint mapUV(const Point2 &uv,
                                 const Vector2 &offset = Vector2(Float(0.5))) const {
        const auto res = Vector2i(m_image->resolution());
        const Vector2 k =
            Vector2(uv.x(), 1 - uv.y()) * res.cast<Float>() - offset;
        const Point2i i =
            Point2i((int) std::floor(k.x()), (int) std::floor(k.y()));
        const Point2 f = k - i.cast<Float>();
        return ImageTexelPoint{ i, f };
    }

    inline Color nearestFilter(const Point2 &uv) const {
        const auto texel =
            mapUV(uv, Vector2(0)); // Nearest filter uses no offset
                                   // (Cycles/OpenImageIO convention)
        const auto p = handleBorder(texel.i);
        return (*m_image)(p);
    }

    inline Color bilinearFilter(const Point2 &uv) const {
        const auto texel = mapUV(uv);
        const auto p0    = handleBorder(texel.i);
        const auto p1    = handleBorder(Vector2i(texel.i) + Vector2i(1));

        const auto v00 = (*m_image)(p0);
        const auto v01 = (*m_image)(Point2i(p0.x(), p1.y()));
        const auto v10 = (*m_image)(Point2i(p1.x(), p0.y()));
        const auto v11 = (*m_image)(p1);

        return lerp(lerp(v00, v10, texel.f.x()),
                    lerp(v01, v11, texel.f.x()),
                    texel.f.y());
    }

public:
    ImageTexture(const Properties &properties) {
        if (properties.has("filename")) {
            m_image = std::make_shared<Image>(properties);
        } else {
            m_image = properties.getChild<Image>();
        }
        m_exposure = properties.get<float>("exposure", 1);

        // clang-format off
        m_border = properties.getEnum<BorderMode>("border", BorderMode::Repeat, {
            { "clamp", BorderMode::Clamp },
            { "repeat", BorderMode::Repeat },
        });

        m_filter = properties.getEnum<FilterMode>("filter", FilterMode::Bilinear, {
            { "nearest", FilterMode::Nearest },
            { "bilinear", FilterMode::Bilinear },
        });
        // clang-format on
    }

    ImageTexture(const ref<Image> &img) {
        m_image = img;
        m_exposure = 1.0f;
        m_border = BorderMode::Clamp;
        m_filter = FilterMode::Bilinear;
    }

    Color evaluate(const Point2 &uv) const override {
        switch (m_filter) {
        default:
        case FilterMode::Bilinear:
            return m_exposure * bilinearFilter(uv);
        case FilterMode::Nearest:
            return m_exposure * nearestFilter(uv);
        }
    }

    std::string toString() const override {
        return tfm::format(
            "ImageTexture[\n"
            "  image = %s,\n"
            "  exposure = %f,\n"
            "]",
            indent(m_image),
            m_exposure);
    }
};

} // namespace lightwave
