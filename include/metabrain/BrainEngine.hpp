#pragma once

#include "metabrain/Config.hpp"
#include "metabrain/Neuron.hpp"
#include "metabrain/SpikingCore.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace metabrain {

struct TerminalReadout {
    uint64_t neuron_id = 0;
    std::string label;
    float activation = 0.0f;
};

struct BrainMetrics {
    uint64_t tick = 0;
    size_t total_neurons = 0;
    size_t top_level_neurons = 0;
    size_t composite_neurons = 0;
    size_t total_synapses = 0;
    uint32_t max_depth = 0;
    float mean_activation = 0.0f;
    float max_activation = 0.0f;
    uint64_t neurogenesis_events = 0;
    uint64_t synaptogenesis_events = 0;
    uint64_t pruned_synapses = 0;
    double last_step_microseconds = 0.0;
};

struct TelemetrySample {
    uint64_t tick = 0;
    float mean_activation = 0.0f;
    float max_activation = 0.0f;
    float fear = 0.0f;
    float dopamine = 0.0f;
    float fatigue = 0.0f;
    float curiosity = 0.0f;
};

struct BenchmarkReport {
    size_t total_neurons = 0;
    size_t total_synapses = 0;
    double forward_ms_per_tick = 0.0;
    double forward_msyn_per_sec = 0.0;
    double plastic_ms_per_tick = 0.0;
    double plastic_msyn_per_sec = 0.0;
};

/**
 * Persistent lightweight C++20 thread pool for parallel CSR neural wave execution.
 */
class ParallelPool {
public:
    explicit ParallelPool(size_t num_threads = 0);
    ~ParallelPool();

    ParallelPool(const ParallelPool&) = delete;
    ParallelPool& operator=(const ParallelPool&) = delete;

    void ParallelFor(size_t count, const std::function<void(size_t, size_t)>& chunk_fn);
    [[nodiscard]] size_t ThreadCount() const noexcept { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    std::function<void(size_t, size_t)> current_task_;
    size_t task_size_ = 0;
    uint64_t generation_ = 0;
    size_t active_workers_ = 0;
    bool stop_ = false;
};

/**
 * Compressed Sparse Row (CSR) + Structure-of-Arrays (SoA) Flat Execution Arena.
 * Eliminates pointer-chasing cache misses during high-frequency simulation ticks
 * while maintaining 1:1 state correspondence with the OOP Composite Neuron graph.
 */
struct FlatCSRArena {
    bool compiled = false;
    size_t neuron_count = 0;
    size_t synapse_count = 0;

    std::vector<Neuron*> neuron_ptrs;
    std::vector<uint8_t> roles;
    std::vector<int32_t> parent_idx; // -1 if top-level

    std::vector<float> biases;
    std::vector<float> potentials;
    std::vector<float> activations;
    std::vector<float> prev_activations;
    std::vector<float> external_inputs;
    std::vector<uint64_t> ages;

    // CSR Incoming Synapses per target neuron i: [row_ptr[i] .. row_ptr[i+1])
    std::vector<uint32_t> row_ptr;
    std::vector<uint32_t> col_idx;
    std::vector<float> weights;
    std::vector<float> eligibilities;
    std::vector<float> last_dw;

    // CSR Child Indices per composite parent neuron i: [child_ptr[i] .. child_ptr[i+1])
    std::vector<uint32_t> child_ptr;
    std::vector<uint32_t> child_indices;
    std::vector<uint32_t> terminal_indices;
};

/**
 * Master Orchestrator for the Recursive Emotional Brain Simulation.
 */
class BrainEngine {
public:
    explicit BrainEngine(BrainConfig config = {});
    ~BrainEngine() = default;

    // -------------------------------------------------------------------------
    // Architecture Construction & Topology Management
    // -------------------------------------------------------------------------

    void Clear();

    /**
     * Builds a rich multi-region Recursive Emotional Brain with Root Sensory
     * channels, nested Cortical Composite columns (Prefrontal, Amygdala,
     * Hippocampus, Association), and Terminal Readout neurons.
     */
    void BuildCorticalArchitecture(size_t num_roots = 8,
                                   size_t num_cortical = 18,
                                   size_t num_terminals = 4,
                                   uint32_t initial_sub_children = 3,
                                   uint32_t seed = 42);

    /**
     * Builds a massive recursive graph for throughput benchmarking.
     */
    void BuildBenchmarkArchitecture(size_t macro_neurons,
                                    uint32_t children_per_macro,
                                    size_t synapses_per_neuron,
                                    uint32_t seed = 2026);

    std::shared_ptr<Neuron> CreateTopLevelNeuron(float bias,
                                                 NeuronRole role,
                                                 std::string label = "");

    std::shared_ptr<Neuron> AddChildToNeuron(const std::shared_ptr<Neuron>& parent,
                                             float bias = 0.0f,
                                             std::string label = "");

    std::vector<std::shared_ptr<Neuron>> SpawnMicroCircuitIn(
        const std::shared_ptr<Neuron>& parent,
        uint32_t count);

    void MarkTopologyDirty() noexcept { arena_.compiled = false; }

    // -------------------------------------------------------------------------
    // Input / Output & Sensory Encoding
    // -------------------------------------------------------------------------

    /**
     * Injects float [0..1] array into Root Sensory Neurons.
     */
    void InjectSensoryPulses(const std::vector<float>& pulses);

    /**
     * Latches a sensory pulse vector across `hold_ticks` simulation steps.
     */
    void SetSustainedSensoryPulses(std::vector<float> pulses, uint32_t hold_ticks = 12);

    [[nodiscard]] const std::vector<float>& GetLastSensoryPulses() const noexcept {
        return last_sensory_pulses_;
    }

    /**
     * Encodes arbitrary text into normalized [0.0..1.0] root sensory activations.
     */
    [[nodiscard]] static std::vector<float> EncodeTextToPulses(std::string_view text,
                                                               size_t channels);

    /**
     * Downsamples/projects an image patch or raw signal vector into [0.0..1.0] channels.
     */
    [[nodiscard]] static std::vector<float> EncodeImageToPulses(const std::vector<float>& pixels,
                                                                size_t channels);

    [[nodiscard]] std::vector<TerminalReadout> ReadTerminalOutputs() const;
    [[nodiscard]] TerminalReadout GetBrightestTerminal() const;

    // -------------------------------------------------------------------------
    // Simulation Stepping, Hebbian Plasticity, Backprop & Neurogenesis
    // -------------------------------------------------------------------------

    /**
     * Executes one simulation tick on the OOP Recursive Composite Graph.
     */
    void StepRecursiveOOP(bool enable_hebbian = true, bool enable_neurogenesis = false);

    /**
     * Compiles the recursive graph into the contiguous CSR Arena for SIMD/parallel speed.
     */
    void CompileHotPath();

    /**
     * Executes one ultra-fast parallel simulation tick on the compiled CSR Arena.
     */
    void StepFastParallel(bool enable_hebbian = true);

    /**
     * Synchronizes flat CSR Arena weights, activations, and ages back into OOP Neurons.
     */
    void SyncArenaToGraph();

    /**
     * Biophysical spiking tick (Phase 1): advances `bio_ms` of biological time on the
     * Izhikevich / conductance / STDP+dopamine substrate compiled from the graph.
     * Writes firing-rate activations and learned weights back into the graph, so
     * every existing consumer (REST, UE, serializer) keeps working unchanged.
     */
    void StepSpiking(float bio_ms = 10.0f);
    void SetSpikingMode(bool on) noexcept { spiking_mode_ = on; }
    /// Substrate parameters used at the next (re)compile. Call before stepping.
    void SetSpikingConfig(const spiking::SpikingConfig& c) { snn_base_cfg_ = c; snn_.reset(); }
    [[nodiscard]] const spiking::SpikingConfig& GetSpikingConfig() const noexcept { return snn_base_cfg_; }
    void SeedRng(uint32_t seed) { rng_.seed(seed); }
    [[nodiscard]] bool IsSpikingMode() const noexcept { return spiking_mode_; }
    /// Neuron ids (graph ids) that emitted at least one spike during the last StepSpiking.
    [[nodiscard]] const std::vector<uint64_t>& GetLastSpikes() const noexcept { return last_spike_ids_; }
    /// Spike count per id in GetLastSpikes() (same order).
    [[nodiscard]] const std::vector<uint16_t>& GetLastSpikeCounts() const noexcept { return last_spike_counts_; }
    [[nodiscard]] const spiking::SpikingNetwork* GetSpikingNetwork() const noexcept { return snn_.get(); }

    /**
     * Clears short-term membrane potentials, activations, and eligibility traces
     * between independent stimulus episodes while preserving all learned weights & ages.
     */
    void ResetTransientActivity();

    /**
     * Global "Reward / Punish" Feedback Loop:
     *  - Triggers Dopamine burst (if reward > 0) or Fear/Alert shift (if punish < 0).
     *  - Executes recursive Error Backpropagation from Terminal Readout neurons
     *    backward through cortical layers and nested child sub-networks.
     */
    void ApplyRewardPunish(float valence,
                           const std::vector<float>& target_terminal_pattern = {});

    /**
     * Curiosity-driven structural neurogenesis: dynamically spawns nested child
     * micro-circuits inside overloaded neurons when Curiosity is active.
     */
    size_t StepNeurogenesis(bool force_one = false);

    /**
     * Activity-dependent Synaptogenesis ("neurons that fire together wire together")
     * paired with homeostatic pruning of atrophied synapses.
     * Returns {new_synapses_formed, weak_synapses_pruned}.
     */
    std::pair<size_t, size_t> StepSynaptogenesisAndPruning(size_t max_new_synapses = 6);

    /**
     * Runs an isolated high-scale CSR + AVX2 multi-threaded throughput benchmark.
     */
    [[nodiscard]] static BenchmarkReport RunBenchmarkSuite(size_t macro_neurons = 10000,
                                                           uint32_t children_per_macro = 3,
                                                           size_t synapses_per_neuron = 64);

    // -------------------------------------------------------------------------
    // Inspection & State Accessors
    // -------------------------------------------------------------------------

    [[nodiscard]] BrainConfig& GetConfig() noexcept { return config_; }
    [[nodiscard]] const BrainConfig& GetConfig() const noexcept { return config_; }

    [[nodiscard]] EndocrineState& GetEndocrine() noexcept { return endocrine_; }
    [[nodiscard]] const EndocrineState& GetEndocrine() const noexcept { return endocrine_; }

    [[nodiscard]] const std::vector<std::shared_ptr<Neuron>>& GetTopLevelNeurons() const noexcept {
        return top_level_neurons_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<Neuron>>& GetRootNeurons() const noexcept {
        return root_neurons_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<Neuron>>& GetTerminalNeurons() const noexcept {
        return terminal_neurons_;
    }

    /**
     * Collects every Neuron across all recursive depths in pre-order traversal.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Neuron>> FlattenAllNeurons() const;

    [[nodiscard]] std::shared_ptr<Neuron> FindNeuronById(uint64_t id) const;

    [[nodiscard]] BrainMetrics ComputeMetrics() const;
    [[nodiscard]] uint64_t GetTick() const noexcept { return tick_; }
    void SetTick(uint64_t t) noexcept { tick_ = t; }
    [[nodiscard]] uint64_t AllocateNeuronId() noexcept { return next_neuron_id_++; }
    void EnsureNextIdAtLeast(uint64_t min_next) noexcept {
        if (next_neuron_id_ < min_next) next_neuron_id_ = min_next;
    }

    [[nodiscard]] const std::deque<TelemetrySample>& GetTelemetryHistory() const noexcept {
        return telemetry_history_;
    }

    /**
     * Rebuilds internal lookup caches after deserializing a graph from JSON.
     */
    void RebuildIndicesFromTopLevel(std::vector<std::shared_ptr<Neuron>> top_nodes);

private:
    BrainConfig config_;
    EndocrineState endocrine_;

    uint64_t tick_ = 0;
    uint64_t next_neuron_id_ = 1;
    uint64_t neurogenesis_events_ = 0;
    uint64_t synaptogenesis_events_ = 0;
    uint64_t pruned_synapses_ = 0;
    double last_step_us_ = 0.0;

    std::vector<float> sustained_pulses_;
    std::vector<float> last_sensory_pulses_;
    uint32_t sustained_ticks_remaining_ = 0;
    std::deque<TelemetrySample> telemetry_history_;

    std::vector<std::shared_ptr<Neuron>> top_level_neurons_;
    std::vector<std::shared_ptr<Neuron>> root_neurons_;
    std::vector<std::shared_ptr<Neuron>> terminal_neurons_;
    std::unordered_map<uint64_t, std::weak_ptr<Neuron>> id_lookup_;

    FlatCSRArena arena_;

    // ---- Spiking substrate state ----
    bool spiking_mode_ = false;
    spiking::SpikingConfig snn_base_cfg_;
    std::unique_ptr<spiking::SpikingNetwork> snn_;
    std::vector<Neuron*> snn_neurons_;            // snn index -> graph neuron
    std::vector<uint8_t> snn_inhib_;              // Dale's law sign per neuron
    struct SnnSynLink { Synapse* graph; uint32_t pre; uint32_t k; float written; };
    std::vector<SnnSynLink> snn_links_;
    std::vector<float> snn_rate_hz_;
    std::vector<float> snn_excitability_;          // per-cell intrinsic offset -> lognormal-like rate spread              // exponential firing-rate estimate
    std::vector<uint64_t> last_spike_ids_;
    std::vector<uint16_t> last_spike_counts_;
    size_t snn_sig_neurons_ = 0, snn_sig_synapses_ = 0;
    void RebuildSpikingNetwork();
    ParallelPool pool_;
    std::mt19937 rng_{133742};

    void RegisterSubtree(const std::shared_ptr<Neuron>& node);
    void RecordTelemetrySample(float mean_act, float max_act);
};

} // namespace metabrain
