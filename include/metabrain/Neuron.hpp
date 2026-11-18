#pragma once

#include "metabrain/Config.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace metabrain {

class Neuron;

enum class NeuronRole : uint8_t {
    RootSensory = 0,       // Macro input receptors receiving float[0..1] pulses
    CorticalComposite = 1, // Associative cortical units capable of hosting child sub-networks
    MicroInterneuron = 2,  // Nested recursive child neurons inside a parent Composite Neuron
    TerminalReadout = 3    // Output decision neurons read by firing brightness
};

[[nodiscard]] inline const char* RoleToString(NeuronRole role) noexcept {
    switch (role) {
        case NeuronRole::RootSensory:       return "RootSensory";
        case NeuronRole::CorticalComposite: return "CorticalComposite";
        case NeuronRole::MicroInterneuron:  return "MicroInterneuron";
        case NeuronRole::TerminalReadout:   return "TerminalReadout";
    }
    return "CorticalComposite";
}

[[nodiscard]] inline NeuronRole RoleFromString(const std::string& str) noexcept {
    if (str == "RootSensory") return NeuronRole::RootSensory;
    if (str == "MicroInterneuron") return NeuronRole::MicroInterneuron;
    if (str == "TerminalReadout") return NeuronRole::TerminalReadout;
    return NeuronRole::CorticalComposite;
}

/**
 * Directed weighted synaptic connection into a target Neuron.
 * Uses std::weak_ptr<Neuron> for the presynaptic source so recurrent loops
 * and cross-hierarchy links NEVER cause std::shared_ptr reference cycles.
 */
struct Synapse {
    std::weak_ptr<Neuron> source;
    uint64_t source_id = 0;
    float weight = 0.1f;
    float eligibility_trace = 0.0f;
    float last_delta_weight = 0.0f;
    uint64_t age_ticks = 0;
};

/**
 * Recursive Composite Neuron (Holon).
 *
 * Implements the Composite Design Pattern:
 *  - A leaf Neuron integrates weighted synaptic inputs + bias through an
 *    endocrine-modulated activation function.
 *  - A composite Neuron owns a recursive sub-network of Child Neurons
 *    (std::vector<std::shared_ptr<Neuron>>), routing somatic excitation into
 *    its micro-circuit and pooling their emergent response.
 */
class Neuron : public std::enable_shared_from_this<Neuron> {
public:
    explicit Neuron(uint64_t id,
                    float bias = 0.0f,
                    NeuronRole role = NeuronRole::CorticalComposite,
                    std::string label = "");

    ~Neuron() = default;

    // Factory helper ensuring shared_from_this() is always valid
    [[nodiscard]] static std::shared_ptr<Neuron> Create(
        uint64_t id,
        float bias = 0.0f,
        NeuronRole role = NeuronRole::CorticalComposite,
        std::string label = "");

    // -------------------------------------------------------------------------
    // Composite Pattern Operations (Dynamic Sub-Network Instantiation)
    // -------------------------------------------------------------------------

    /**
     * Attaches an existing Neuron as a nested child of this Composite Neuron.
     */
    std::shared_ptr<Neuron> AddChild(std::shared_ptr<Neuron> child);

    /**
     * Dynamically instantiates and attaches a new child Neuron inside this Neuron.
     */
    std::shared_ptr<Neuron> AddChild(uint64_t child_id,
                                     float child_bias = 0.0f,
                                     std::string label = "");

    /**
     * Instantiates an interconnected internal micro-circuit (lateral + recurrent
     * child neurons) inside this Neuron, turning it into a rich Composite Holon.
     */
    std::vector<std::shared_ptr<Neuron>> SpawnMicroCircuit(
        uint32_t num_children,
        uint64_t& next_id_counter,
        uint32_t rng_seed = 1337);

    // -------------------------------------------------------------------------
    // Synaptic Wiring
    // -------------------------------------------------------------------------

    /**
     * Connects `presynaptic` -> `this` with initial `weight`.
     * Updates weight if connection already exists.
     */
    void ConnectFrom(const std::shared_ptr<Neuron>& presynaptic, float weight);

    /**
     * Connects `this` -> `postsynaptic` with initial `weight`.
     */
    void ConnectTo(const std::shared_ptr<Neuron>& postsynaptic, float weight);

    // -------------------------------------------------------------------------
    // Core Neural Computation & Plasticity
    // -------------------------------------------------------------------------

    /**
     * Weighted integration of inputs, recursive micro-circuit evaluation across
     * children_, and endocrine-modulated non-linear activation.
     */
    float ProcessSignal(const BrainConfig& cfg,
                        const EndocrineState& endocrine,
                        float exploration_noise = 0.0f);

    /**
     * Three-Factor Oja-Stabilized Hebbian Learning ("neurons that fire together,
     * wire together"), gated by Synaptic Eligibility Traces and global Dopamine.
     * Recursively invokes Learn() on all child sub-neurons.
     */
    void Learn(const BrainConfig& cfg,
               const EndocrineState& endocrine,
               float reward_modulation = 1.0f);

    /**
     * Latches previous-tick activation prior to synchronous wave evaluation.
     */
    void LatchActivation() noexcept;

    /**
     * Injects external sensory current (e.g. float [0..1] pulse into Root Neurons).
     */
    void InjectExternalInput(float current) noexcept { external_input_ += current; }
    void SetExternalInput(float current) noexcept { external_input_ = current; }
    void ClearExternalInput() noexcept { external_input_ = 0.0f; }

    // -------------------------------------------------------------------------
    // Accessors & Mutators (Used by Engine, CSR Compiler, Layout, & Serializer)
    // -------------------------------------------------------------------------

    [[nodiscard]] uint64_t GetId() const noexcept { return id_; }
    [[nodiscard]] const std::string& GetLabel() const noexcept { return label_; }
    void SetLabel(std::string lbl) { label_ = std::move(lbl); }

    [[nodiscard]] NeuronRole GetRole() const noexcept { return role_; }
    void SetRole(NeuronRole role) noexcept { role_ = role; }

    [[nodiscard]] uint32_t GetDepth() const noexcept { return depth_; }
    void SetDepth(uint32_t depth) noexcept;

    [[nodiscard]] float GetBias() const noexcept { return bias_; }
    void SetBias(float bias) noexcept { bias_ = bias; }

    [[nodiscard]] float GetActivation() const noexcept { return activation_; }
    void SetActivation(float act) noexcept { activation_ = act; }

    [[nodiscard]] float GetPrevActivation() const noexcept { return prev_activation_; }
    void SetPrevActivation(float act) noexcept { prev_activation_ = act; }

    [[nodiscard]] float GetMembranePotential() const noexcept { return membrane_potential_; }
    void SetMembranePotential(float v) noexcept { membrane_potential_ = v; }

    [[nodiscard]] float GetExternalInput() const noexcept { return external_input_; }

    [[nodiscard]] float GetErrorGradient() const noexcept { return error_gradient_; }
    void SetErrorGradient(float grad) noexcept { error_gradient_ = grad; }
    void AccumulateErrorGradient(float grad) noexcept { error_gradient_ += grad; }

    [[nodiscard]] float GetSurpriseEma() const noexcept { return surprise_ema_; }
    void SetSurpriseEma(float s) noexcept { surprise_ema_ = s; }

    [[nodiscard]] uint64_t GetAgeTicks() const noexcept { return age_ticks_; }
    void SetAgeTicks(uint64_t age) noexcept { age_ticks_ = age; }
    void IncrementAge() noexcept { ++age_ticks_; }

    [[nodiscard]] const Vec3& GetPosition() const noexcept { return position_; }
    void SetPosition(const Vec3& pos) noexcept { position_ = pos; }

    [[nodiscard]] const Vec3& GetVelocity() const noexcept { return velocity_; }
    void SetVelocity(const Vec3& vel) noexcept { velocity_ = vel; }

    /**
     * Prunes dead/atrophied synapses older than `min_age_ticks` whose |weight| < `min_abs_weight`.
     */
    size_t PruneWeakSynapses(float min_abs_weight = 0.015f, uint64_t min_age_ticks = 80);

    [[nodiscard]] bool IsComposite() const noexcept { return !children_.empty(); }
    [[nodiscard]] const std::vector<std::shared_ptr<Neuron>>& GetChildren() const noexcept {
        return children_;
    }

    [[nodiscard]] std::shared_ptr<Neuron> GetParent() const noexcept {
        return parent_.lock();
    }

    [[nodiscard]] const std::vector<Synapse>& GetSynapses() const noexcept {
        return incoming_synapses_;
    }
    [[nodiscard]] std::vector<Synapse>& GetSynapsesMutable() noexcept {
        return incoming_synapses_;
    }

    /**
     * Convenience snapshot returning raw connection weights (matches Blueprint spec).
     */
    [[nodiscard]] std::vector<float> GetConnectionWeights() const;

    /**
     * Normalized [0..1] maturity ratio for Unreal Engine 5 / WebGL Age Heatmap Lens
     * (0.0 = Vibrant Emerald Nascent Node -> 1.0 = Deep Purple Consolidated Memory).
     */
    [[nodiscard]] float GetMaturityRatio(uint64_t mature_ticks = 120) const noexcept {
        if (mature_ticks == 0) return 1.0f;
        return std::min(1.0f, static_cast<float>(age_ticks_) / static_cast<float>(mature_ticks));
    }

private:
    uint64_t id_ = 0;
    std::string label_;
    NeuronRole role_ = NeuronRole::CorticalComposite;
    uint32_t depth_ = 0;

    float bias_ = 0.0f;
    float membrane_potential_ = 0.0f;
    float activation_ = 0.0f;
    float prev_activation_ = 0.0f;
    float external_input_ = 0.0f;
    float error_gradient_ = 0.0f;
    float surprise_ema_ = 0.0f;

    uint64_t age_ticks_ = 0;
    Vec3 position_{0.0f, 0.0f, 0.0f};
    Vec3 velocity_{0.0f, 0.0f, 0.0f};

    std::weak_ptr<Neuron> parent_;
    std::vector<std::shared_ptr<Neuron>> children_;
    std::vector<Synapse> incoming_synapses_;
};

} // namespace metabrain
