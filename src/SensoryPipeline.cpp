#include "metabrain/SensoryPipeline.hpp"

#include <algorithm>
#include <cmath>

namespace metabrain {

std::vector<float> SensoryPipeline::EncodeText(std::string_view text, size_t num_channels) {
    std::vector<float> pulses(num_channels, 0.0f);
    if (num_channels == 0 || text.empty()) {
        return pulses;
    }

    uint64_t str_hash = 14695981039346656037ULL;
    for (unsigned char c : text) {
        str_hash ^= static_cast<uint64_t>(c);
        str_hash *= 1099511628211ULL;
    }

    for (size_t i = 0; i < text.size(); ++i) {
        const uint64_t c0 = static_cast<unsigned char>(text[i]);
        uint64_t h = str_hash ^ ((c0 + i * 31ULL) * 11400714819323198485ULL);
        for (size_t ch = 0; ch < num_channels; ++ch) {
            uint64_t mix = h ^ (static_cast<uint64_t>(ch + 1) * 0x9E3779B97F4A7C15ULL);
            mix ^= (mix >> 30);
            mix *= 0xBF58476D1CE4E5B9ULL;
            mix ^= (mix >> 27);
            const float sign = ((mix >> 11) & 1ULL) ? 1.0f : -1.12f;
            const float mag = 0.30f + 0.70f * (static_cast<float>((mix >> 32) & 0xFFULL) / 255.0f);
            pulses[ch] += sign * mag;
        }
    }

    float max_pos = 0.0f;
    for (float& p : pulses) {
        p = std::max(0.0f, p);
        if (p > max_pos) max_pos = p;
    }
    if (max_pos > 1e-5f) {
        for (float& p : pulses) {
            p = std::clamp(p / max_pos, 0.0f, 1.0f);
        }
    } else {
        pulses[str_hash % num_channels] = 1.0f;
    }
    return pulses;
}

std::vector<float> SensoryPipeline::EncodeImageGrid(const std::vector<float>& pixels,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    size_t num_channels) {
    std::vector<float> out(num_channels, 0.0f);
    if (num_channels == 0 || pixels.empty() || width == 0 || height == 0) {
        return out;
    }

    auto at = [&](uint32_t x, uint32_t y) -> float {
        const size_t idx = static_cast<size_t>(y) * width + x;
        return (idx < pixels.size()) ? std::clamp(pixels[idx], 0.0f, 1.0f) : 0.0f;
    };

    // Extract 8 V1-inspired primary visual features:
    // [0..3]: 4 Quadrant Luminance Receptive Fields
    // [4]: Horizontal Edge Energy |dI/dy|
    // [5]: Vertical Edge Energy   |dI/dx|
    // [6]: Diagonal Edge Energy   |dI/dxy|
    // [7]: Center-Surround Foveal Contrast
    float features[8] = {0.0f};
    float quad_counts[4] = {0.0f};
    float edge_count = 0.0f;
    float center_sum = 0.0f, center_cnt = 0.0f;
    float surround_sum = 0.0f, surround_cnt = 0.0f;

    const float cx = (static_cast<float>(width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(height) - 1.0f) * 0.5f;
    const float r_fovea = std::max(1.5f, std::min(cx, cy) * 0.52f);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const float val = at(x, y);
            const size_t q = (y >= height / 2 ? 2u : 0u) + (x >= width / 2 ? 1u : 0u);
            features[q] += val;
            quad_counts[q] += 1.0f;

            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (std::sqrt(dx * dx + dy * dy) <= r_fovea) {
                center_sum += val;
                center_cnt += 1.0f;
            } else {
                surround_sum += val;
                surround_cnt += 1.0f;
            }

            if (x + 1 < width && y + 1 < height) {
                const float gx = std::abs(at(x + 1, y) - val);
                const float gy = std::abs(at(x, y + 1) - val);
                const float gd = std::abs(at(x + 1, y + 1) - at(x + 1, y));
                features[4] += gy;
                features[5] += gx;
                features[6] += gd;
                edge_count += 1.0f;
            }
        }
    }

    for (int q = 0; q < 4; ++q) {
        if (quad_counts[q] > 0.0f) {
            features[q] = std::min(1.0f, (features[q] / quad_counts[q]) * 2.4f);
        }
    }
    if (edge_count > 0.0f) {
        features[4] = std::min(1.0f, (features[4] / edge_count) * 4.5f);
        features[5] = std::min(1.0f, (features[5] / edge_count) * 4.5f);
        features[6] = std::min(1.0f, (features[6] / edge_count) * 4.5f);
    }
    const float c_mean = (center_cnt > 0.0f) ? (center_sum / center_cnt) : 0.0f;
    const float s_mean = (surround_cnt > 0.0f) ? (surround_sum / surround_cnt) : 0.0f;
    features[7] = std::clamp((c_mean - s_mean * 0.5f) * 2.2f, 0.0f, 1.0f);

    for (size_t ch = 0; ch < num_channels; ++ch) {
        if (ch < 8) {
            out[ch] = std::clamp(features[ch], 0.0f, 1.0f);
        } else {
            // Spatial bin projection for extra channels beyond 8
            const size_t start = ((ch - 8) * pixels.size()) / std::max<size_t>(1, num_channels - 8);
            const size_t end = std::min(pixels.size(), start + std::max<size_t>(1, pixels.size() / num_channels));
            float acc = 0.0f;
            for (size_t k = start; k < end; ++k) acc += std::clamp(pixels[k], 0.0f, 1.0f);
            out[ch] = (end > start) ? std::min(1.0f, (acc / static_cast<float>(end - start)) * 2.0f) : 0.0f;
        }
    }
    return out;
}

std::vector<float> SensoryPipeline::NormalizeManualPulses(const std::vector<float>& raw,
                                                          size_t num_channels) {
    std::vector<float> out(num_channels, 0.0f);
    const size_t limit = std::min(raw.size(), num_channels);
    for (size_t i = 0; i < limit; ++i) {
        out[i] = std::clamp(raw[i], 0.0f, 1.0f);
    }
    return out;
}

} // namespace metabrain
