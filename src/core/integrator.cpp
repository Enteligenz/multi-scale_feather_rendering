#include <lightwave/camera.hpp>
#include <lightwave/integrator.hpp>
#include <lightwave/parallel.hpp>

#include <algorithm>
#include <chrono>

#include <lightwave/iterators.hpp>
#include <lightwave/streaming.hpp>

namespace lightwave {

void SamplingIntegrator::execute() { // Version without time heatmap
    if (!m_image) {
        lightwave_throw(
            "<integrator /> needs an <image /> child to render into!");
    }

    const Vector2i resolution = m_scene->camera()->resolution();
    m_image->initialize(resolution);

    const Float norm = Float(1.0) / m_sampler->samplesPerPixel();

    Streaming stream{ *m_image };
    ProgressReporter progress{ resolution.product() };
    for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block) {
        auto sampler = m_sampler->clone();
        for (auto pixel : block) {
            Color sum;
            for (int sample = 0; sample < m_sampler->samplesPerPixel();
                 sample++) {
                sampler->seed(pixel, sample);
                auto cameraSample = m_scene->camera()->sample(pixel, *sampler);
                sum += cameraSample.weight * Li(cameraSample.ray, *sampler);
            }
            m_image->get(pixel) = norm * sum;
        }

        progress += block.diagonal().product();
        stream.updateBlock(block);
    });
    progress.finish();

    m_image->save();
}

// void SamplingIntegrator::execute() { // Version with time heatmap
//     if (!m_image) {
//         lightwave_throw(
//             "<integrator /> needs an <image /> child to render into!");
//     }

//     const Vector2i resolution = m_scene->camera()->resolution();
//     m_image->initialize(resolution);

//     const Float norm = Float(1.0) / m_sampler->samplesPerPixel();

//     Streaming stream{ *m_image };
//     ProgressReporter progress{ resolution.product() };
//     for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block) {
//         auto sampler = m_sampler->clone();
//         for (auto pixel : block) {
//             Color sum;
//             for (int sample = 0; sample < m_sampler->samplesPerPixel();
//                  sample++) {
//                 sampler->seed(pixel, sample);
//                 auto cameraSample = m_scene->camera()->sample(pixel, *sampler);
//                 sum += cameraSample.weight * Li(cameraSample.ray, *sampler);
//             }
//             m_image->get(pixel) = norm * sum;
//         }

//         progress += block.diagonal().product();
//         stream.updateBlock(block);
//     });
//     progress.finish();

//     m_image->save();
// }

void SamplingIntegrator::execute() { // Version with time heatmap
    if (!m_image) {
        lightwave_throw(
            "<integrator /> needs an <image /> child to render into!");
    }

    const Vector2i resolution = m_scene->camera()->resolution();
    m_image->initialize(resolution);

    // --- Time heatmap ---
    Image timeMap;
    timeMap.initialize(resolution);
    timeMap.setBasePath(m_image->getBasePath());
    timeMap.setId(m_image->getId() + "_timeMap");
    // ---

    const Float norm = Float(1.0) / m_sampler->samplesPerPixel();

    Streaming stream{ *m_image };
    ProgressReporter progress{ resolution.product() };
    for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block) {
        auto sampler = m_sampler->clone();
        for (auto pixel : block) {
            Color sum;
            auto start = std::chrono::steady_clock::now(); // Time heatmap
            for (int sample = 0; sample < m_sampler->samplesPerPixel();
                 sample++) {
                sampler->seed(pixel, sample);
                auto cameraSample = m_scene->camera()->sample(pixel, *sampler);
                sum += cameraSample.weight * Li(cameraSample.ray, *sampler);
            }
            auto end = std::chrono::steady_clock::now(); // Time heatmap
            m_image->get(pixel) = norm * sum;
            float microseconds = std::chrono::duration<float, std::micro>(end - start).count(); // Time heatmap
            // timeMap.get(pixel) = Color(microseconds);
            timeMap.get(pixel) = Color(std::log1p(microseconds)); // Time heatmap
        }

        progress += block.diagonal().product();
        stream.updateBlock(block);
    });
    progress.finish();

    m_image->save();
    timeMap.save(); // Time heatmap
}

} // namespace lightwave