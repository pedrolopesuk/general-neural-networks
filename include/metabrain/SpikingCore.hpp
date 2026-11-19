#pragma once
// ============================================================================
// SpikingCore — biophysically grounded spiking substrate (Phase 1)
//
//  * Izhikevich (2003) neurons: 2-variable model reproducing ~20 cortical
//    firing classes (RS, IB, CH, FS, LTS, TC, RZ) at near-LIF cost.
//  * Conductance-based synapses: AMPA, NMDA (voltage-dependent Mg2+ block,
//    Jahr & Stevens 1990), GABA_A, GABA_B with physiological reversal
//    potentials and time constants.
//  * Axonal conduction delays: event-driven ring buffer (spikes are only
//    touched when they are emitted and when they arrive).
//  * Three-factor plasticity (Izhikevich 2007): pair-based STDP writes into a
//    slow eligibility trace; dopamine converts eligibility into weight change.
//
// Units: time in ms, voltage in mV, conductance dimensionless (scaled so
// g*(E-v) is in the same "current" units as the Izhikevich I term).
// ============================================================================

#include <cstdint>
#include <random>
#include <vector>

namespace metabrain::spiking {

enum class CellType : uint8_t {
    RegularSpiking,       // RS  — excitatory pyramidal, adapting
    IntrinsicBursting,    // IB  — L5 pyramidal, initial burst then tonic
    Chattering,           // CH  — L2/3 fast rhythmic bursts
    FastSpiking,          // FS  — parvalbumin basket interneuron
    LowThresholdSpiking,  // LTS — somatostatin interneuron
    ThalamoCortical,      // TC  — thalamic relay, rebound bursts
    Resonator             // RZ  — subthreshold oscillation
};

struct IzhParams {
    float a, b, c, d;
};

[[nodiscard]] IzhParams ParamsFor(CellType t) noexcept;
[[nodiscard]] bool IsInhibitory(CellType t) noexcept;

enum class Receptor : uint8_t { AMPA, NMDA, GABA_A, GABA_B };

struct SpikingConfig {
    float dt_ms = 0.1f;

    // Receptor kinetics (decay time constants, ms) and reversal potentials (mV)
    float tau_ampa = 5.0f,   E_ampa = 0.0f;
    float tau_nmda = 150.0f, E_nmda = 0.0f;
    float tau_gabaa = 6.0f,  E_gabaa = -70.0f;
    float tau_gabab = 150.0f, E_gabab = -90.0f;
    float nmda_ratio = 0.1f;   // NMDA conductance co-released per unit AMPA weight
    float gabab_ratio = 0.05f; // GABA_B per unit GABA_A weight

    // STDP (Bi & Poo 1998 window shape)
    float stdp_a_plus = 0.010f;
    float stdp_a_minus = 0.012f;   // slight depression bias -> stability
    float tau_plus = 20.0f;
    float tau_minus = 20.0f;
    float tau_eligibility = 1000.0f;  // dopamine credit-assignment window
    float w_max = 1.0f;

    float spike_peak_mv = 30.0f;

    // Spike-frequency adaptation current
    float tau_ahp = 100.0f, E_k = -90.0f;
    // sAHP increment for thalamic relay cells. 0.03 chosen by sweep (see docs/PHASE1_NOTES.md).
    float tc_ahp_increment = 0.03f;
    // Sensory current per unit of [0..1] input on thalamic relay cells (engine bridge)
    float sensory_gain = 12.0f;
};

// Slow afterhyperpolarisation (Ca2+-activated K+, sAHP): conductance added per spike.
// 0 = rely on the Izhikevich recovery variable alone.
[[nodiscard]] float AhpIncrementFor(CellType t, const SpikingConfig& cfg) noexcept;

struct Synapse {
    uint32_t post;
    float weight;        // >= 0; sign comes from the presynaptic cell type
    uint16_t delay_steps;
    float eligibility = 0.0f;
};

class SpikingNetwork {
public:
    explicit SpikingNetwork(SpikingConfig cfg = {});

    uint32_t AddNeuron(CellType type);
    // Delay in ms (rounded to dt, minimum 1 step). Weight is clamped to [0, w_max].
    void Connect(uint32_t pre, uint32_t post, float weight, float delay_ms, bool plastic = true);

    // Injected current for the next step(s); persists until changed.
    void SetInputCurrent(uint32_t id, float current) { I_ext_[id] = current; }

    // Advance one dt. Returns the indices of neurons that spiked this step.
    const std::vector<uint32_t>& Step();
    void Run(float ms);

    // Neuromodulation: dopamine level d (0 = baseline). Each step,
    // w += d * eligibility * dt  (Izhikevich 2007 three-factor rule).
    void SetDopamine(float d) { dopamine_ = d; }

    // ---- read-only access ----
    [[nodiscard]] size_t Size() const noexcept { return v_.size(); }
    [[nodiscard]] float V(uint32_t id) const { return v_[id]; }
    [[nodiscard]] size_t OutDegree(uint32_t id) const { return out_[id].size(); }
    [[nodiscard]] double TimeMs() const noexcept { return step_ * cfg_.dt_ms; }
    [[nodiscard]] const std::vector<double>& SpikeTimes(uint32_t id) const { return spike_log_[id]; }
    [[nodiscard]] const Synapse& GetSynapse(uint32_t pre, size_t k) const { return out_[pre][k]; }
    [[nodiscard]] Synapse& GetSynapseMutable(uint32_t pre, size_t k) { return out_[pre][k]; }
    [[nodiscard]] const SpikingConfig& Config() const noexcept { return cfg_; }
    void ClearSpikeLog();

private:
    struct PendingSpike {
        uint32_t pre;
        uint32_t syn;
    };

    void DeliverArrivals();
    void ApplyStdpOnPost(uint32_t post);
    void ApplyStdpOnPreArrival(uint32_t pre, Synapse& s);

    SpikingConfig cfg_;
    uint64_t step_ = 0;
    float dopamine_ = 0.0f;

    // Neuron state (SoA)
    std::vector<CellType> type_;
    std::vector<IzhParams> p_;
    std::vector<float> v_, u_, I_ext_;
    std::vector<float> g_ampa_, g_nmda_, g_gabaa_, g_gabab_;
    std::vector<float> g_ahp_, ahp_inc_;
    std::vector<float> x_pre_;   // presynaptic STDP trace (per neuron)
    std::vector<float> y_post_;  // postsynaptic STDP trace (per neuron)
    std::vector<std::vector<double>> spike_log_;

    // Connectivity
    std::vector<std::vector<Synapse>> out_;
    std::vector<std::vector<uint8_t>> plastic_;
    // Incoming index for post-spike STDP: (pre, k)
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> in_;

    // Delay ring: slot = arrival step % ring size
    std::vector<std::vector<PendingSpike>> ring_;
    uint32_t max_delay_steps_ = 1;

    std::vector<uint32_t> fired_;
};

}  // namespace metabrain::spiking
