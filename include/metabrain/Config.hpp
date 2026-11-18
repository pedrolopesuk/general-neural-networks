#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace metabrain {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& rhs) const noexcept {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    constexpr Vec3 operator-(const Vec3& rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    constexpr Vec3 operator*(float s) const noexcept {
        return {x * s, y * s, z * s};
    }
    constexpr Vec3& operator+=(const Vec3& rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& rhs) noexcept {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    constexpr Vec3& operator*=(float s) noexcept {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }

    [[nodiscard]] float LengthSq() const noexcept {
        return x * x + y * y + z * z;
    }
    [[nodiscard]] float Length() const noexcept {
        return std::sqrt(LengthSq());
    }
    [[nodiscard]] Vec3 Normalized(float eps = 1e-6f) const noexcept {
        const float len = Length();
        if (len < eps) {
            return {0.0f, 1.0f, 0.0f};
        }
        const float inv = 1.0f / len;
        return {x * inv, y * inv, z * inv};
    }
};

/**
 * Global Hyperparameters governing neural dynamics, Hebbian plasticity,
 * backpropagation credit assignment, and recursive neurogenesis.
 */
struct BrainConfig {
    // Plasticity & Learning Hyperparameters
    float learning_rate = 0.015f;           // Base cortical Hebbian plasticity rate (eta)
    float backprop_learning_rate = 0.28f;   // Supervised / Reward-Punish gradient step
    float eligibility_decay = 0.84f;        // Temporal trace decay (gamma_e)
    float oja_stabilization = 0.45f;        // Multiplicative weight normalization factor
    float weight_clip = 3.5f;               // Hard bound on synaptic weights [-clip, +clip]

    // Membrane & Firing Hyperparameters
    float base_threshold = 0.36f;           // Baseline firing threshold (theta_0)
    float base_gain = 5.2f;                 // Activation steepness
    float membrane_leak = 0.45f;            // Leaky integration retention [0..1)
    float composite_child_mix = 0.42f;      // Blend weight of internal child sub-network

    // Neuromodulatory Sensitivity Coupling
    float fear_threshold_shift = 0.30f;     // How strongly Fear lowers firing threshold
    float fatigue_threshold_shift = 0.36f;  // How strongly Fatigue raises firing threshold
    float dopamine_lr_boost = 2.6f;         // Multiplier on learning rate during Dopamine surge
    float curiosity_exploration = 0.05f;    // Stochastic exploration amplitude

    // Structural Recursive Growth (Curiosity-driven Mitosis)
    float neurogenesis_surprise_threshold = 0.20f;
    uint32_t max_recursion_depth = 3;
    uint32_t max_children_per_neuron = 6;
    uint64_t mature_memory_ticks = 120;     // Age threshold for purple vs green Age Heatmap
};

/**
 * Global "Endocrine System" neurochemical wash that modulates every neuron
 * and synapse across all recursive depths in real time.
 */
struct EndocrineState {
    // Active neurochemical concentrations
    float fear = 0.12f;        // [0.0 .. 1.0] Lowers threshold -> hyper-reactive & anxious
    float dopamine = 0.25f;    // [-0.5 .. 2.0] Boosts learning rate -> locks in active pathways
    float fatigue = 0.08f;     // [0.0 .. 1.0] Raises threshold -> sluggish & dampened
    float curiosity = 0.50f;   // [0.0 .. 1.0] Drives exploration & recursive AddChild() growth

    // Tonic homeostatic baselines
    float tonic_fear = 0.10f;
    float tonic_dopamine = 0.20f;
    float tonic_fatigue = 0.08f;
    float tonic_curiosity = 0.45f;

    void Clamp() noexcept {
        fear = std::clamp(fear, 0.0f, 1.0f);
        dopamine = std::clamp(dopamine, -0.5f, 2.0f);
        fatigue = std::clamp(fatigue, 0.0f, 1.0f);
        curiosity = std::clamp(curiosity, 0.0f, 1.0f);
    }

    /**
     * Smoothly relaxes phasic hormone surges toward tonic resting levels
     * while accumulating slight metabolic fatigue from high network activity.
     */
    void StepHomeostasis(float mean_network_activity, float decay_rate = 0.04f) noexcept {
        fear += (tonic_fear - fear) * decay_rate;
        dopamine += (tonic_dopamine - dopamine) * (decay_rate * 1.35f);
        curiosity += (tonic_curiosity - curiosity) * (decay_rate * 0.5f);

        // High sustained firing gradually builds fatigue; quiet rest clears it
        const float metabolic_load = 0.008f * mean_network_activity;
        fatigue += (tonic_fatigue - fatigue) * (decay_rate * 0.6f) + metabolic_load;
        Clamp();
    }

    /**
     * Effective firing threshold after Fear (lowers) and Fatigue (raises) wash.
     */
    [[nodiscard]] float EffectiveThreshold(const BrainConfig& cfg) const noexcept {
        const float shifted = cfg.base_threshold
                            - (cfg.fear_threshold_shift * fear)
                            + (cfg.fatigue_threshold_shift * fatigue);
        return std::clamp(shifted, 0.03f, 0.92f);
    }

    /**
     * Effective activation slope: Fear sharpens reactivity; Fatigue dulls gain.
     */
    [[nodiscard]] float EffectiveGain(const BrainConfig& cfg) const noexcept {
        const float gain = cfg.base_gain * (1.0f + 0.65f * fear - 0.35f * fatigue);
        return std::max(1.2f, gain);
    }

    /**
     * Effective plasticity rate: Dopamine strongly amplifies consolidation;
     * Fatigue suppresses plasticity.
     */
    [[nodiscard]] float EffectiveLearningRate(const BrainConfig& cfg) const noexcept {
        const float da_factor = std::max(0.05f, 1.0f + cfg.dopamine_lr_boost * dopamine);
        const float fatigue_drag = std::max(0.15f, 1.0f - 0.60f * fatigue);
        return cfg.learning_rate * da_factor * fatigue_drag;
    }

    /**
     * Amplitude damping caused by Fatigue/Sadness.
     */
    [[nodiscard]] float AmplitudeScale() const noexcept {
        return std::clamp(1.0f - 0.42f * fatigue, 0.25f, 1.0f);
    }
};

} // namespace metabrain
