#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace metabrain {

/**
 * Multi-modal Sensory Transduction Pipeline.
 * Converts Text, 2D Retina Image Grids, and Manual Pulse Vectors into
 * normalized [0.0 .. 1.0] float arrays for Root Sensory Neurons.
 */
class SensoryPipeline {
public:
    [[nodiscard]] static std::vector<float> EncodeText(std::string_view text,
                                                       size_t num_channels);

    /**
     * Extracts multi-scale spatial luminance, center-surround contrast, and
     * horizontal/vertical/diagonal edge energies from a 2D grayscale image grid.
     */
    [[nodiscard]] static std::vector<float> EncodeImageGrid(const std::vector<float>& pixels,
                                                            uint32_t width,
                                                            uint32_t height,
                                                            size_t num_channels);

    [[nodiscard]] static std::vector<float> NormalizeManualPulses(const std::vector<float>& raw,
                                                                  size_t num_channels);
};

} // namespace metabrain
