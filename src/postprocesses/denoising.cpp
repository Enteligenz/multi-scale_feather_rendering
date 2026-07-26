#include <lightwave.hpp>

#ifdef LW_WITH_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace lightwave {

class Denoising : public Postprocess {
    ref<Image> m_normals;
    ref<Image> m_albedo;

#ifdef LW_WITH_OIDN
    oidn::DeviceRef m_oidnDevice;
#endif

public:
    Denoising(const Properties &properties) : Postprocess(properties) {
        m_normals = properties.getOptional<Image>("normals");
        m_albedo  = properties.getOptional<Image>("albedo");

#ifdef LW_WITH_OIDN
        m_oidnDevice = oidn::newDevice();
        m_oidnDevice.commit();
#endif
    }

    void execute() override {
#ifdef LW_WITH_OIDN
        m_output->initialize(m_input->resolution());

        const auto size           = m_input->resolution();
        const int bytePixelStride = m_input->getBytesPerPixel();
        const int byteRowStride   = size.x() * bytePixelStride;

        auto filter = m_oidnDevice.newFilter("RT");
        filter.setImage("color", m_input->data(), oidn::Format::Float3,
                        size.x(), size.y(), 0, bytePixelStride, byteRowStride);
        filter.setImage("output", m_output->data(), oidn::Format::Float3,
                        size.x(), size.y(), 0, bytePixelStride, byteRowStride);
        if (m_albedo) {
            filter.setImage("albedo", m_albedo->data(), oidn::Format::Float3,
                            size.x(), size.y(), 0, bytePixelStride,
                            byteRowStride);
        }
        if (m_normals) {
            filter.setImage("normal", m_normals->data(), oidn::Format::Float3,
                            size.x(), size.y(), 0, bytePixelStride,
                            byteRowStride);
        }
        filter.set("hdr", true);
        filter.commit();
        filter.execute();

        const char *error;
        if (m_oidnDevice.getError(error) != oidn::Error::None) {
            std::cerr << "OpenImageDenoise error: " << error << std::endl;
        } else {
            std::cout << "OpenImageDenoise finished successfully" << std::endl;
        }
#else
        m_output->copy(*m_input);
#endif

        m_output->save();
    }

    std::string toString() const override {
        return tfm::format("Denoising[\n"
                           "  input = %s,\n"
                           "  output = %s,\n"
#ifndef LW_WITH_OIDN
                           "  no oidn\n"
#endif
                           "]",
                           indent(m_input), indent(m_output));
    }
};

} // namespace lightwave

REGISTER_POSTPROCESS(Denoising, "denoising");
