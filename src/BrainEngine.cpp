#include "metabrain/BrainEngine.hpp"
#include "metabrain/SensoryPipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace metabrain {

namespace {
[[nodiscard]] inline float FastSigmoidArena(float x) noexcept {
    const float clamped = std::clamp(x, -14.0f, 14.0f);
    return 1.0f / (1.0f + std::exp(-clamped));
}
} // namespace

// =============================================================================
// ParallelPool Implementation
// =============================================================================

ParallelPool::ParallelPool(size_t num_threads) {
    if (num_threads == 0) {
        const unsigned int hw = std::thread::hardware_concurrency();
        num_threads = (hw > 1) ? static_cast<size_t>(hw) : 4;
    }
    workers_.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        workers_.emplace_back([this, t, num_threads]() {
            uint64_t seen_gen = 0;
            while (true) {
                std::function<void(size_t, size_t)> task;
                size_t total = 0;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_start_.wait(lock, [&]() { return stop_ || generation_ > seen_gen; });
                    if (stop_) {
                        return;
                    }
                    seen_gen = generation_;
                    task = current_task_;
                    total = task_size_;
                }

                const size_t chunk = (total + num_threads - 1) / num_threads;
                const size_t begin = t * chunk;
                const size_t end = std::min(total, begin + chunk);
                if (begin < end && task) {
                    task(begin, end);
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (--active_workers_ == 0) {
                        cv_done_.notify_one();
                    }
                }
            }
        });
    }
}

ParallelPool::~ParallelPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_start_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
}

void ParallelPool::ParallelFor(size_t count, const std::function<void(size_t, size_t)>& chunk_fn) {
    if (count == 0) {
        return;
    }
    if (count < 512 || workers_.size() <= 1) {
        chunk_fn(0, count);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_task_ = chunk_fn;
        task_size_ = count;
        active_workers_ = workers_.size();
        ++generation_;
    }
    cv_start_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    cv_done_.wait(lock, [&]() { return active_workers_ == 0; });
}

// =============================================================================
// BrainEngine Implementation
// =============================================================================

BrainEngine::BrainEngine(BrainConfig config) : config_(config) {}

void BrainEngine::Clear() {
    top_level_neurons_.clear();
    root_neurons_.clear();
    terminal_neurons_.clear();
    id_lookup_.clear();
    arena_.compiled = false;
    tick_ = 0;
    next_neuron_id_ = 1;
    neurogenesis_events_ = 0;
    synaptogenesis_events_ = 0;
    pruned_synapses_ = 0;
    sustained_pulses_.clear();
    last_sensory_pulses_.clear();
    sustained_ticks_remaining_ = 0;
    telemetry_history_.clear();
}

void BrainEngine::RecordTelemetrySample(float mean_act, float max_act) {
    TelemetrySample s;
    s.tick = tick_;
    s.mean_activation = mean_act;
    s.max_activation = max_act;
    s.fear = endocrine_.fear;
    s.dopamine = endocrine_.dopamine;
    s.fatigue = endocrine_.fatigue;
    s.curiosity = endocrine_.curiosity;
    telemetry_history_.push_back(s);
    while (telemetry_history_.size() > 100) {
        telemetry_history_.pop_front();
    }
}

void BrainEngine::RegisterSubtree(const std::shared_ptr<Neuron>& node) {
    if (!node) {
        return;
    }
    id_lookup_[node->GetId()] = node;
    if (node->GetId() >= next_neuron_id_) {
        next_neuron_id_ = node->GetId() + 1;
    }
    for (const auto& ch : node->GetChildren()) {
        RegisterSubtree(ch);
    }
}

std::shared_ptr<Neuron> BrainEngine::CreateTopLevelNeuron(float bias,
                                                          NeuronRole role,
                                                          std::string label) {
    auto n = Neuron::Create(AllocateNeuronId(), bias, role, std::move(label));
    top_level_neurons_.push_back(n);
    id_lookup_[n->GetId()] = n;
    if (role == NeuronRole::RootSensory) {
        root_neurons_.push_back(n);
    } else if (role == NeuronRole::TerminalReadout) {
        terminal_neurons_.push_back(n);
    }
    arena_.compiled = false;
    return n;
}

std::shared_ptr<Neuron> BrainEngine::AddChildToNeuron(const std::shared_ptr<Neuron>& parent,
                                                      float bias,
                                                      std::string label) {
    if (!parent) {
        return nullptr;
    }
    auto child = parent->AddChild(AllocateNeuronId(), bias, std::move(label));
    if (child) {
        id_lookup_[child->GetId()] = child;
        arena_.compiled = false;
    }
    return child;
}

std::vector<std::shared_ptr<Neuron>> BrainEngine::SpawnMicroCircuitIn(
    const std::shared_ptr<Neuron>& parent,
    uint32_t count) {
    if (!parent || count == 0) {
        return {};
    }
    auto spawned = parent->SpawnMicroCircuit(count, next_neuron_id_, rng_());
    for (const auto& s : spawned) {
        RegisterSubtree(s);
    }
    arena_.compiled = false;
    return spawned;
}

void BrainEngine::BuildCorticalArchitecture(size_t num_roots,
                                            size_t num_cortical,
                                            size_t num_terminals,
                                            uint32_t initial_sub_children,
                                            uint32_t seed) {
    Clear();
    rng_.seed(seed);

    std::uniform_real_distribution<float> bias_dist(-0.06f, 0.06f);
    std::uniform_real_distribution<float> w_exc(0.15f, 0.68f);
    std::uniform_real_distribution<float> w_inh(-0.32f, -0.05f);

    // 1. Root Sensory Receptors (Macro Input Channels)
    static const char* kSensoryNames[] = {
        "Sensory.Visual_A", "Sensory.Visual_B", "Sensory.Auditory_Low", "Sensory.Auditory_High",
        "Sensory.Somato_Touch", "Sensory.Somato_Pain", "Sensory.Olfactory", "Sensory.Interoceptive"
    };
    for (size_t i = 0; i < num_roots; ++i) {
        std::string name = (i < 8) ? kSensoryNames[i] : ("Sensory.Ch_" + std::to_string(i));
        auto root = CreateTopLevelNeuron(0.0f, NeuronRole::RootSensory, std::move(name));
        root->SetAgeTicks(180 + i * 12); // Established sensory pathways
    }

    // 2. Cortical Composite Regions (Prefrontal, Amygdala, Hippocampus, Association)
    static const char* kRegionPrefixes[] = {
        "Prefrontal", "Amygdala", "Hippocampus", "Association"
    };
    std::vector<std::shared_ptr<Neuron>> cortical;
    cortical.reserve(num_cortical);

    for (size_t i = 0; i < num_cortical; ++i) {
        std::string region = std::string(kRegionPrefixes[i % 4]) + ".Col_" + std::to_string(i / 4);
        auto col = CreateTopLevelNeuron(bias_dist(rng_), NeuronRole::CorticalComposite, std::move(region));
        col->SetAgeTicks((i % 3 == 0) ? 25 : 160); // Mix of nascent (green) & consolidated (purple)

        // Instantiate nested Depth-1 micro-circuits inside Composite Cortical Columns
        if (initial_sub_children > 0 && (i % 2 == 0 || i % 4 == 1)) {
            auto sub_nodes = SpawnMicroCircuitIn(col, initial_sub_children);
            for (size_t s = 0; s < sub_nodes.size(); ++s) {
                sub_nodes[s]->SetAgeTicks((s == 0) ? 140 : 18);
            }
            // Instantiate Depth-2 recursive sub-holon inside Prefrontal & Hippocampal hubs!
            if (!sub_nodes.empty() && (i % 6 == 0)) {
                auto depth2 = SpawnMicroCircuitIn(sub_nodes[0], 2);
                for (auto& d2 : depth2) {
                    d2->SetAgeTicks(5); // Brand-new deep recursive engram!
                }
            }
        }
        cortical.push_back(col);
    }

    // 3. Terminal Readout Neurons (Action / Decision Concepts)
    static const char* kTerminalNames[] = {
        "Output.Approach_Explore",
        "Output.Defensive_Freeze",
        "Output.Pattern_Alpha_Lock",
        "Output.Pattern_Beta_Lock"
    };
    for (size_t i = 0; i < num_terminals; ++i) {
        std::string name = (i < 4) ? kTerminalNames[i] : ("Output.Action_" + std::to_string(i));
        auto term = CreateTopLevelNeuron(-0.04f, NeuronRole::TerminalReadout, std::move(name));
        term->SetAgeTicks(200);
    }

    // 4. Wire Root -> Cortical Composite Columns (Diverse Mixed-Selectivity Receptive Fields)
    std::uniform_real_distribution<float> w_mixed(-0.65f, 0.85f);
    for (size_t c = 0; c < cortical.size(); ++c) {
        for (size_t r = 0; r < root_neurons_.size(); ++r) {
            const float w = w_mixed(rng_);
            if (std::abs(w) > 0.12f) {
                cortical[c]->ConnectFrom(root_neurons_[r], w);
            }
        }
    }

    // 5. Wire Recurrent & Lateral Cortical Associations (Excitatory + Inhibitory E/I Balance)
    for (size_t i = 0; i < cortical.size(); ++i) {
        const size_t next1 = (i + 1) % cortical.size();
        const size_t next2 = (i + 3) % cortical.size();
        cortical[next1]->ConnectFrom(cortical[i], w_exc(rng_) * 0.28f);
        if (next2 != i) {
            cortical[next2]->ConnectFrom(cortical[i], (i % 2 == 0) ? w_inh(rng_) * 0.55f : w_exc(rng_) * 0.22f);
        }
        // Cross-scale projection from nested child interneuron -> neighbor cortical hub
        if (cortical[i]->IsComposite() && !cortical[next1]->GetChildren().empty()) {
            cortical[next1]->GetChildren()[0]->ConnectFrom(
                cortical[i]->GetChildren().back(), w_exc(rng_) * 0.25f);
        }
    }

    // 6. Wire Cortical Columns -> Terminal Readout Neurons (Full Associative Readout Matrix)
    const float cort_scale = 0.65f / std::sqrt(static_cast<float>(std::max<size_t>(1, cortical.size())));
    std::uniform_real_distribution<float> w_readout(-0.7f, 0.9f);
    for (size_t t = 0; t < terminal_neurons_.size(); ++t) {
        for (size_t c = 0; c < cortical.size(); ++c) {
            terminal_neurons_[t]->ConnectFrom(cortical[c], w_readout(rng_) * cort_scale);
        }
    }

    CompileHotPath();
}

void BrainEngine::BuildBenchmarkArchitecture(size_t macro_neurons,
                                             uint32_t children_per_macro,
                                             size_t synapses_per_neuron,
                                             uint32_t seed) {
    Clear();
    rng_.seed(seed);
    std::uniform_real_distribution<float> bias_dist(-0.1f, 0.1f);
    std::uniform_real_distribution<float> weight_dist(-0.25f, 0.45f);

    const size_t num_roots = std::max<size_t>(16, macro_neurons / 20);
    const size_t num_terms = std::max<size_t>(8, macro_neurons / 40);

    for (size_t i = 0; i < macro_neurons; ++i) {
        NeuronRole role = NeuronRole::CorticalComposite;
        if (i < num_roots) {
            role = NeuronRole::RootSensory;
        } else if (i + num_terms >= macro_neurons) {
            role = NeuronRole::TerminalReadout;
        }
        auto n = CreateTopLevelNeuron(bias_dist(rng_), role);
        if (role == NeuronRole::CorticalComposite && children_per_macro > 0) {
            SpawnMicroCircuitIn(n, children_per_macro);
        }
    }

    auto all = FlattenAllNeurons();
    const size_t total = all.size();
    for (size_t i = num_roots; i < total; ++i) {
        auto& target = all[i];
        auto& syns = target->GetSynapsesMutable();
        const size_t existing = syns.size();
        if (existing >= synapses_per_neuron) {
            continue;
        }
        const size_t needed = synapses_per_neuron - existing;
        syns.reserve(synapses_per_neuron);
        // Deterministic strided wiring avoids duplicate checks & builds realistic small-world connectivity
        size_t cursor = (i * 1315423911ULL) % total;
        for (size_t k = 0; k < needed; ++k) {
            cursor = (cursor + 7 + (k % 13)) % total;
            if (cursor == i) {
                cursor = (cursor + 1) % total;
            }
            Synapse s;
            s.source = all[cursor];
            s.source_id = all[cursor]->GetId();
            s.weight = weight_dist(rng_);
            syns.push_back(std::move(s));
        }
    }

    CompileHotPath();
}

void BrainEngine::InjectSensoryPulses(const std::vector<float>& pulses) {
    last_sensory_pulses_.assign(root_neurons_.size(), 0.0f);
    const size_t n = std::min(pulses.size(), root_neurons_.size());
    for (size_t i = 0; i < root_neurons_.size(); ++i) {
        const float val = (i < n) ? std::clamp(pulses[i], 0.0f, 1.0f) : 0.0f;
        root_neurons_[i]->SetExternalInput(val);
        last_sensory_pulses_[i] = val;
    }
    if (arena_.compiled) {
        for (size_t i = 0; i < arena_.neuron_count; ++i) {
            arena_.external_inputs[i] = arena_.neuron_ptrs[i]->GetExternalInput();
        }
    }
}

void BrainEngine::SetSustainedSensoryPulses(std::vector<float> pulses, uint32_t hold_ticks) {
    sustained_pulses_ = std::move(pulses);
    sustained_ticks_remaining_ = hold_ticks;
    if (hold_ticks > 0) {
        InjectSensoryPulses(sustained_pulses_);
    }
}

std::vector<float> BrainEngine::EncodeTextToPulses(std::string_view text, size_t channels) {
    return SensoryPipeline::EncodeText(text, channels);
}

std::vector<float> BrainEngine::EncodeImageToPulses(const std::vector<float>& pixels,
                                                    size_t channels) {
    const uint32_t side = static_cast<uint32_t>(std::max<size_t>(1, static_cast<size_t>(std::sqrt(pixels.size()))));
    return SensoryPipeline::EncodeImageGrid(pixels, side, side, channels);
}

std::vector<TerminalReadout> BrainEngine::ReadTerminalOutputs() const {
    std::vector<TerminalReadout> out;
    out.reserve(terminal_neurons_.size());
    for (const auto& t : terminal_neurons_) {
        out.push_back({t->GetId(), t->GetLabel(), t->GetActivation()});
    }
    return out;
}

TerminalReadout BrainEngine::GetBrightestTerminal() const {
    TerminalReadout best{0, "None", -1.0f};
    for (const auto& t : terminal_neurons_) {
        if (t->GetActivation() > best.activation) {
            best = {t->GetId(), t->GetLabel(), t->GetActivation()};
        }
    }
    return best;
}

void BrainEngine::StepRecursiveOOP(bool enable_hebbian, bool enable_neurogenesis) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    if (sustained_ticks_remaining_ > 0) {
        InjectSensoryPulses(sustained_pulses_);
        --sustained_ticks_remaining_;
    }

    // 1. Latch synchronous previous-tick activations across all recursive depths
    for (auto& node : top_level_neurons_) {
        node->LatchActivation();
    }

    // 2. Evaluate ProcessSignal() recursively with Curiosity-scaled exploration
    std::normal_distribution<float> noise_dist(
        0.0f, config_.curiosity_exploration * endocrine_.curiosity);

    float total_act = 0.0f;
    float max_act = 0.0f;
    for (auto& node : top_level_neurons_) {
        const float noise = (endocrine_.curiosity > 0.01f && node->GetRole() != NeuronRole::RootSensory)
                          ? noise_dist(rng_)
                          : 0.0f;
        const float a = node->ProcessSignal(config_, endocrine_, noise);
        total_act += a;
        if (a > max_act) max_act = a;
    }

    // 3. Three-Factor Oja-Stabilized Hebbian Learning
    if (enable_hebbian) {
        for (auto& node : top_level_neurons_) {
            node->Learn(config_, endocrine_, 1.0f);
        }
    }

    // 4. Optional Curiosity-Driven Recursive Neurogenesis
    if (enable_neurogenesis && (tick_ % 10 == 9)) {
        StepNeurogenesis(false);
    }

    // 5. Endocrine Homeostasis & Clear One-Shot Sensory Currents
    const float mean_act = top_level_neurons_.empty()
                         ? 0.0f
                         : (total_act / static_cast<float>(top_level_neurons_.size()));
    endocrine_.StepHomeostasis(mean_act);

    for (auto& r : root_neurons_) {
        r->ClearExternalInput();
    }

    ++tick_;
    RecordTelemetrySample(mean_act, max_act);
    arena_.compiled = false; // Graph state advanced outside CSR arena

    const auto t1 = std::chrono::high_resolution_clock::now();
    last_step_us_ = std::chrono::duration<double, std::micro>(t1 - t0).count();
}

void BrainEngine::CompileHotPath() {
    auto all = FlattenAllNeurons();
    const size_t N = all.size();

    arena_ = FlatCSRArena{};
    arena_.neuron_count = N;
    arena_.neuron_ptrs.resize(N);
    arena_.roles.resize(N);
    arena_.parent_idx.assign(N, -1);
    arena_.biases.resize(N);
    arena_.potentials.resize(N);
    arena_.activations.resize(N);
    arena_.prev_activations.resize(N);
    arena_.external_inputs.resize(N);
    arena_.ages.resize(N);

    std::unordered_map<uint64_t, uint32_t> id_to_idx;
    id_to_idx.reserve(N * 2);

    for (size_t i = 0; i < N; ++i) {
        Neuron* ptr = all[i].get();
        arena_.neuron_ptrs[i] = ptr;
        id_to_idx[ptr->GetId()] = static_cast<uint32_t>(i);

        arena_.roles[i] = static_cast<uint8_t>(ptr->GetRole());
        if (ptr->GetRole() == NeuronRole::TerminalReadout) {
            arena_.terminal_indices.push_back(static_cast<uint32_t>(i));
        }
        arena_.biases[i] = ptr->GetBias();
        arena_.potentials[i] = ptr->GetMembranePotential();
        arena_.activations[i] = ptr->GetActivation();
        arena_.prev_activations[i] = ptr->GetPrevActivation();
        arena_.external_inputs[i] = ptr->GetExternalInput();
        arena_.ages[i] = ptr->GetAgeTicks();
    }

    // Build CSR Parent->Children Index Table
    arena_.child_ptr.resize(N + 1, 0);
    for (size_t i = 0; i < N; ++i) {
        const auto& children = arena_.neuron_ptrs[i]->GetChildren();
        uint32_t valid_count = 0;
        for (const auto& ch : children) {
            if (ch) {
                auto it = id_to_idx.find(ch->GetId());
                if (it != id_to_idx.end()) {
                    arena_.child_indices.push_back(it->second);
                    arena_.parent_idx[it->second] = static_cast<int32_t>(i);
                    ++valid_count;
                }
            }
        }
        arena_.child_ptr[i + 1] = arena_.child_ptr[i] + valid_count;
    }

    // Build CSR Incoming Synapse Table
    arena_.row_ptr.resize(N + 1, 0);
    size_t est_syns = 0;
    for (size_t i = 0; i < N; ++i) {
        est_syns += arena_.neuron_ptrs[i]->GetSynapses().size();
    }
    arena_.col_idx.reserve(est_syns);
    arena_.weights.reserve(est_syns);
    arena_.eligibilities.reserve(est_syns);
    arena_.last_dw.reserve(est_syns);

    for (size_t i = 0; i < N; ++i) {
        const auto& syns = arena_.neuron_ptrs[i]->GetSynapses();
        uint32_t count = 0;
        for (const auto& syn : syns) {
            auto it = id_to_idx.find(syn.source_id);
            if (it != id_to_idx.end()) {
                arena_.col_idx.push_back(it->second);
                arena_.weights.push_back(syn.weight);
                arena_.eligibilities.push_back(syn.eligibility_trace);
                arena_.last_dw.push_back(syn.last_delta_weight);
                ++count;
            }
        }
        arena_.row_ptr[i + 1] = arena_.row_ptr[i] + count;
    }

    arena_.synapse_count = arena_.weights.size();
    arena_.compiled = true;
}

void BrainEngine::StepFastParallel(bool enable_hebbian) {
    if (!arena_.compiled) {
        CompileHotPath();
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    const size_t N = arena_.neuron_count;
    if (N == 0) {
        return;
    }

    // 1. Latch activations seamlessly in contiguous memory
    std::copy(arena_.activations.begin(), arena_.activations.end(), arena_.prev_activations.begin());

    const float theta_eff = endocrine_.EffectiveThreshold(config_);
    const float gain_eff = endocrine_.EffectiveGain(config_);
    const float amp_scale = endocrine_.AmplitudeScale();
    const float leak = config_.membrane_leak;
    const float inv_leak = 1.0f - leak;
    const float child_mix = config_.composite_child_mix;
    const float eta = endocrine_.EffectiveLearningRate(config_);
    const float elig_decay = config_.eligibility_decay;
    const float oja_beta = config_.oja_stabilization;
    const float w_clip = config_.weight_clip;
    const float fear = endocrine_.fear;
    const float fatigue = endocrine_.fatigue;

    const uint32_t* __restrict row_ptr = arena_.row_ptr.data();
    const uint32_t* __restrict col_idx = arena_.col_idx.data();
    float* __restrict weights = arena_.weights.data();
    float* __restrict eligibilities = arena_.eligibilities.data();
    float* __restrict last_dw = arena_.last_dw.data();

    const uint32_t* __restrict child_ptr = arena_.child_ptr.data();
    const uint32_t* __restrict child_indices = arena_.child_indices.data();

    const float* __restrict prev_act = arena_.prev_activations.data();
    const float* __restrict biases = arena_.biases.data();
    float* __restrict ext_in = arena_.external_inputs.data();
    float* __restrict potentials = arena_.potentials.data();
    float* __restrict activations = arena_.activations.data();
    uint64_t* __restrict ages = arena_.ages.data();
    const uint8_t* __restrict roles = arena_.roles.data();

    // 2. Parallel SIMD-friendly CSR Forward Integration + Optional Hebbian Update
    pool_.ParallelFor(N, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const uint32_t syn_start = row_ptr[i];
            const uint32_t syn_end = row_ptr[i + 1];

            float sum = biases[i] + ext_in[i];
            for (uint32_t k = syn_start; k < syn_end; ++k) {
                sum += weights[k] * prev_act[col_idx[k]];
            }

            // Composite pooling from previous-tick child sub-network states
            const uint32_t c_start = child_ptr[i];
            const uint32_t c_end = child_ptr[i + 1];
            if (c_end > c_start) {
                float c_sum = 0.0f;
                for (uint32_t c = c_start; c < c_end; ++c) {
                    c_sum += prev_act[child_indices[c]];
                }
                const float mean_child = c_sum / static_cast<float>(c_end - c_start);
                sum = (1.0f - child_mix) * sum + child_mix * (sum + mean_child);
            }

            const float v_new = leak * potentials[i] + inv_leak * sum;
            potentials[i] = v_new;

            float post = 0.0f;
            if (roles[i] == static_cast<uint8_t>(NeuronRole::RootSensory) &&
                syn_start == syn_end && ext_in[i] > 0.0f) {
                post = std::clamp(ext_in[i] + 0.15f * fear - 0.12f * fatigue, 0.0f, 1.0f);
            } else {
                post = amp_scale * FastSigmoidArena(gain_eff * (v_new - theta_eff));
            }
            post = std::clamp(post, 0.0f, 1.0f);
            activations[i] = post;
            ages[i] += 1;
            ext_in[i] = 0.0f;

            if (enable_hebbian && syn_end > syn_start) {
                const float post_sq = post * post;
                const bool post_active = (post > 0.08f);
                for (uint32_t k = syn_start; k < syn_end; ++k) {
                    const float pre = prev_act[col_idx[k]];
                    const float co = (post_active && pre > 0.08f) ? (pre * post) : 0.0f;
                    const float e_new = elig_decay * eligibilities[k] + co;
                    eligibilities[k] = e_new;

                    const float dw = eta * (e_new - oja_beta * post_sq * weights[k]);
                    weights[k] = std::clamp(weights[k] + dw, -w_clip, w_clip);
                    last_dw[k] = dw;
                }
            }
        }
    });

    // Update terminal neuron activations immediately so readout queries stay zero-cost
    for (uint32_t tidx : arena_.terminal_indices) {
        arena_.neuron_ptrs[tidx]->SetActivation(arena_.activations[tidx]);
    }
    endocrine_.StepHomeostasis(0.15f);
    ++tick_;

    const auto t1 = std::chrono::high_resolution_clock::now();
    last_step_us_ = std::chrono::duration<double, std::micro>(t1 - t0).count();
}

void BrainEngine::SyncArenaToGraph() {
    if (!arena_.compiled) {
        return;
    }
    const size_t N = arena_.neuron_count;
    for (size_t i = 0; i < N; ++i) {
        Neuron* n = arena_.neuron_ptrs[i];
        n->SetMembranePotential(arena_.potentials[i]);
        n->SetActivation(arena_.activations[i]);
        n->SetPrevActivation(arena_.prev_activations[i]);
        n->SetAgeTicks(arena_.ages[i]);

        auto& syns = n->GetSynapsesMutable();
        const uint32_t start = arena_.row_ptr[i];
        const uint32_t count = arena_.row_ptr[i + 1] - start;
        if (count == syns.size()) {
            for (uint32_t k = 0; k < count; ++k) {
                syns[k].weight = arena_.weights[start + k];
                syns[k].eligibility_trace = arena_.eligibilities[start + k];
                syns[k].last_delta_weight = arena_.last_dw[start + k];
            }
        }
    }
}

void BrainEngine::ResetTransientActivity() {
    if (arena_.compiled) {
        SyncArenaToGraph();
    }
    auto all = FlattenAllNeurons();
    for (auto& n : all) {
        n->SetMembranePotential(0.0f);
        n->SetActivation(0.0f);
        n->SetPrevActivation(0.0f);
        n->ClearExternalInput();
        for (auto& syn : n->GetSynapsesMutable()) {
            syn.eligibility_trace = 0.0f;
        }
    }
    arena_.compiled = false;
}

void BrainEngine::ApplyRewardPunish(float valence,
                                    const std::vector<float>& target_terminal_pattern) {
    if (arena_.compiled) {
        SyncArenaToGraph();
    }

    valence = std::clamp(valence, -1.5f, 1.5f);

    // 1. Global Endocrine Feedback Response
    if (valence > 0.0f) {
        // Reward spikes Dopamine ("lock in recent pathways") and eases Anxiety
        endocrine_.dopamine = std::clamp(endocrine_.dopamine + 0.65f * valence, -0.5f, 2.0f);
        endocrine_.fear = std::clamp(endocrine_.fear - 0.15f * valence, 0.0f, 1.0f);
    } else if (valence < 0.0f) {
        // Punishment triggers Fear/Anxiety vigilance and suppresses Dopamine
        const float mag = -valence;
        endocrine_.fear = std::clamp(endocrine_.fear + 0.40f * mag, 0.0f, 1.0f);
        endocrine_.dopamine = std::clamp(endocrine_.dopamine - 0.35f * mag, -0.5f, 2.0f);
    }

    // 2. Recursive Error Backpropagation + Eligibility Trace Credit Assignment
    auto all = FlattenAllNeurons();
    for (auto& n : all) {
        n->SetErrorGradient(0.0f);
    }

    // Compute Terminal Output Error Signals (Sigmoid + BCE clean gradient: target - y)
    if (!target_terminal_pattern.empty()) {
        for (size_t i = 0; i < terminal_neurons_.size(); ++i) {
            const float y = terminal_neurons_[i]->GetActivation();
            const float target = (i < target_terminal_pattern.size())
                               ? std::clamp(target_terminal_pattern[i], 0.0f, 1.0f)
                               : 0.0f;
            terminal_neurons_[i]->SetErrorGradient((target - y) * std::abs(valence));
        }
    } else {
        // Reinforcement-style Winner-Take-All credit assignment on brightest terminal
        auto brightest = GetBrightestTerminal();
        for (auto& t : terminal_neurons_) {
            const float y = t->GetActivation();
            if (t->GetId() == brightest.neuron_id) {
                const float desired = (valence >= 0.0f) ? 0.96f : 0.04f;
                t->SetErrorGradient(desired - y);
            } else if (valence < 0.0f) {
                t->SetErrorGradient((0.35f - y) * 0.35f);
            }
        }
    }

    const float bp_lr = (config_.backprop_learning_rate * 0.32f) *
                        std::max(0.45f, 1.0f + 0.35f * std::max(0.0f, endocrine_.dopamine));

    // Propagate gradients backward (2 passes to cover Terminal -> Cortical -> Child Micro-Circuits)
    for (int pass = 0; pass < 2; ++pass) {
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            auto& post_node = *it;
            const float delta = post_node->GetErrorGradient();
            if (std::abs(delta) < 1e-6f) {
                continue;
            }

            // Propagate error into nested child sub-neurons if Composite!
            if (post_node->IsComposite()) {
                const float child_share = delta * config_.composite_child_mix;
                for (auto& ch : post_node->GetChildren()) {
                    if (ch) {
                        const float cy = ch->GetActivation();
                        const float cderiv = std::max(0.05f, cy * (1.0f - cy));
                        ch->AccumulateErrorGradient(child_share * cderiv);
                    }
                }
            }

            // Update incoming synaptic weights & accumulate presynaptic error gradients
            for (auto& syn : post_node->GetSynapsesMutable()) {
                auto pre_node = syn.source.lock();
                if (!pre_node) {
                    continue;
                }
                const float pre_act = std::max(pre_node->GetActivation(), pre_node->GetPrevActivation());
                const float pre_deriv = std::max(0.05f, pre_act * (1.0f - pre_act));

                if (pass == 0) {
                    pre_node->AccumulateErrorGradient(delta * syn.weight * pre_deriv);
                } else {
                    // Blend exact presynaptic-gated gradient step with synaptic eligibility trace
                    const float trace_boost = 1.0f + 0.8f * std::max(0.0f, syn.eligibility_trace);
                    const float dw = bp_lr * delta * pre_act * trace_boost;
                    syn.weight = std::clamp(syn.weight + dw, -config_.weight_clip, config_.weight_clip);
                    syn.last_delta_weight = dw;
                }
            }

            if (pass == 1 && post_node->GetRole() != NeuronRole::RootSensory) {
                post_node->SetBias(std::clamp(post_node->GetBias() + bp_lr * 0.25f * delta, -1.5f, 1.5f));
            }
        }
    }

    arena_.compiled = false;
}

size_t BrainEngine::StepNeurogenesis(bool force_one) {
    if (arena_.compiled) {
        SyncArenaToGraph();
    }

    if (!force_one && endocrine_.curiosity < 0.20f) {
        return 0;
    }

    auto all = FlattenAllNeurons();
    std::shared_ptr<Neuron> best_candidate = nullptr;
    float highest_score = -1.0f;

    for (const auto& n : all) {
        if (n->GetRole() == NeuronRole::RootSensory ||
            n->GetRole() == NeuronRole::TerminalReadout) {
            continue;
        }
        if (n->GetDepth() >= config_.max_recursion_depth) {
            continue;
        }
        if (n->GetChildren().size() >= config_.max_children_per_neuron) {
            continue;
        }

        const float score = n->GetSurpriseEma() * (0.5f + endocrine_.curiosity)
                          + (force_one ? (0.3f * n->GetActivation() + 0.05f) : 0.0f);
        if ((force_one || score >= config_.neurogenesis_surprise_threshold) && score > highest_score) {
            highest_score = score;
            best_candidate = n;
        }
    }

    if (!best_candidate) {
        return 0;
    }

    const uint32_t new_count = best_candidate->IsComposite() ? 2u : 3u;
    auto spawned = SpawnMicroCircuitIn(best_candidate, new_count);
    for (auto& ch : spawned) {
        ch->SetAgeTicks(0); // Brand-new nascent green neuron for Age Heatmap Lens!
        ch->SetActivation(best_candidate->GetActivation() * 0.75f);
    }

    best_candidate->SetSurpriseEma(0.0f);
    neurogenesis_events_ += spawned.size();
    arena_.compiled = false;
    return spawned.size();
}

std::pair<size_t, size_t> BrainEngine::StepSynaptogenesisAndPruning(size_t max_new_synapses) {
    if (arena_.compiled) {
        SyncArenaToGraph();
    }

    // 1. Prune long-atrophied weak synapses
    size_t pruned = 0;
    for (auto& top : top_level_neurons_) {
        pruned += top->PruneWeakSynapses(0.015f, 90);
    }
    pruned_synapses_ += pruned;

    // 2. Activity-dependent Synaptogenesis ("neurons that fire together wire together")
    auto all = FlattenAllNeurons();
    std::vector<std::shared_ptr<Neuron>> active_pool;
    for (const auto& n : all) {
        if (n->GetActivation() > 0.42f) {
            active_pool.push_back(n);
        }
    }

    size_t formed = 0;
    for (size_t i = 0; i < active_pool.size() && formed < max_new_synapses; ++i) {
        for (size_t j = i + 1; j < active_pool.size() && formed < max_new_synapses; ++j) {
            auto& pre = active_pool[i];
            auto& post = active_pool[j];
            if (post->GetRole() == NeuronRole::RootSensory) {
                continue;
            }
            bool exists = false;
            for (const auto& syn : post->GetSynapses()) {
                if (syn.source_id == pre->GetId()) {
                    exists = true;
                    break;
                }
            }
            if (!exists && post->GetSynapses().size() < 28) {
                const float init_w = 0.22f * pre->GetActivation() * post->GetActivation();
                post->ConnectFrom(pre, init_w);
                ++formed;
            }
        }
    }

    synaptogenesis_events_ += formed;
    if (formed > 0 || pruned > 0) {
        arena_.compiled = false;
    }
    return {formed, pruned};
}

BenchmarkReport BrainEngine::RunBenchmarkSuite(size_t macro_neurons,
                                               uint32_t children_per_macro,
                                               size_t synapses_per_neuron) {
    BrainEngine bench;
    bench.BuildBenchmarkArchitecture(macro_neurons, children_per_macro, synapses_per_neuron, 2026);
    const auto m = bench.ComputeMetrics();

    std::vector<float> stim(bench.GetRootNeurons().size(), 0.75f);
    bench.InjectSensoryPulses(stim);

    for (int i = 0; i < 4; ++i) {
        bench.StepFastParallel(false);
    }

    constexpr int kTicks = 40;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kTicks; ++i) {
        bench.StepFastParallel(false);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kTicks; ++i) {
        bench.StepFastParallel(true);
    }
    const auto t3 = std::chrono::high_resolution_clock::now();

    const double fwd_sec = std::chrono::duration<double>(t1 - t0).count();
    const double pls_sec = std::chrono::duration<double>(t3 - t2).count();

    BenchmarkReport rep;
    rep.total_neurons = m.total_neurons;
    rep.total_synapses = m.total_synapses;
    rep.forward_ms_per_tick = (fwd_sec * 1000.0) / kTicks;
    rep.forward_msyn_per_sec = (static_cast<double>(m.total_synapses) * kTicks) / (fwd_sec * 1e6);
    rep.plastic_ms_per_tick = (pls_sec * 1000.0) / kTicks;
    rep.plastic_msyn_per_sec = (static_cast<double>(m.total_synapses) * kTicks) / (pls_sec * 1e6);
    return rep;
}

std::vector<std::shared_ptr<Neuron>> BrainEngine::FlattenAllNeurons() const {
    std::vector<std::shared_ptr<Neuron>> result;
    result.reserve(id_lookup_.size() + 16);

    std::function<void(const std::shared_ptr<Neuron>&)> dfs =
        [&](const std::shared_ptr<Neuron>& node) {
            if (!node) {
                return;
            }
            result.push_back(node);
            for (const auto& ch : node->GetChildren()) {
                dfs(ch);
            }
        };

    for (const auto& top : top_level_neurons_) {
        dfs(top);
    }
    return result;
}

std::shared_ptr<Neuron> BrainEngine::FindNeuronById(uint64_t id) const {
    auto it = id_lookup_.find(id);
    if (it != id_lookup_.end()) {
        return it->second.lock();
    }
    return nullptr;
}

BrainMetrics BrainEngine::ComputeMetrics() const {
    BrainMetrics m;
    m.tick = tick_;
    m.top_level_neurons = top_level_neurons_.size();
    m.neurogenesis_events = neurogenesis_events_;
    m.synaptogenesis_events = synaptogenesis_events_;
    m.pruned_synapses = pruned_synapses_;
    m.last_step_microseconds = last_step_us_;

    if (arena_.compiled && arena_.neuron_count > 0) {
        m.total_neurons = arena_.neuron_count;
        m.total_synapses = arena_.synapse_count;
        float sum = 0.0f;
        float max_a = 0.0f;
        for (size_t i = 0; i < arena_.neuron_count; ++i) {
            const float a = arena_.activations[i];
            sum += a;
            if (a > max_a) max_a = a;
            if (arena_.child_ptr[i + 1] > arena_.child_ptr[i]) {
                ++m.composite_neurons;
            }
            const uint32_t d = arena_.neuron_ptrs[i]->GetDepth();
            if (d > m.max_depth) m.max_depth = d;
        }
        m.mean_activation = sum / static_cast<float>(arena_.neuron_count);
        m.max_activation = max_a;
        return m;
    }

    auto all = FlattenAllNeurons();
    m.total_neurons = all.size();
    float sum = 0.0f;
    for (const auto& n : all) {
        m.total_synapses += n->GetSynapses().size();
        if (n->IsComposite()) {
            ++m.composite_neurons;
        }
        if (n->GetDepth() > m.max_depth) {
            m.max_depth = n->GetDepth();
        }
        const float a = n->GetActivation();
        sum += a;
        if (a > m.max_activation) {
            m.max_activation = a;
        }
    }
    if (!all.empty()) {
        m.mean_activation = sum / static_cast<float>(all.size());
    }
    return m;
}

void BrainEngine::RebuildIndicesFromTopLevel(std::vector<std::shared_ptr<Neuron>> top_nodes) {
    top_level_neurons_ = std::move(top_nodes);
    root_neurons_.clear();
    terminal_neurons_.clear();
    id_lookup_.clear();
    next_neuron_id_ = 1;

    for (const auto& n : top_level_neurons_) {
        if (!n) continue;
        n->SetDepth(0);
        if (n->GetRole() == NeuronRole::RootSensory) {
            root_neurons_.push_back(n);
        } else if (n->GetRole() == NeuronRole::TerminalReadout) {
            terminal_neurons_.push_back(n);
        }
        RegisterSubtree(n);
    }
    arena_.compiled = false;
}


// ============================================================================
// Phase 1 - biophysical spiking substrate bridge
// ============================================================================
namespace {
constexpr float kSnnConductanceScale = 0.03f;  // graph weight -> conductance: |w|=1 gives a ~1 mV unitary EPSP, as in cortex
}

void BrainEngine::RebuildSpikingNetwork() {
    // Learned weights live in the graph (written back every tick), so a rebuild only
    // loses transient membrane state, never memory.
    auto all = FlattenAllNeurons();
    spiking::SpikingConfig cfg = snn_base_cfg_;
    cfg.w_max = config_.weight_clip * kSnnConductanceScale;
    snn_ = std::make_unique<spiking::SpikingNetwork>(cfg);
    snn_neurons_.clear();
    snn_inhib_.clear();
    snn_links_.clear();
    last_spike_ids_.clear();

    std::unordered_map<const Neuron*, uint32_t> index;
    index.reserve(all.size());
    for (const auto& n : all) index[n.get()] = static_cast<uint32_t>(index.size());

    // Dale's law: each cell is excitatory OR inhibitory, decided by the net sign of
    // its outgoing weights (graph synapses are stored on the postsynaptic side).
    std::vector<float> out_sum(all.size(), 0.0f);
    size_t syn_total = 0;
    for (const auto& post : all) {
        for (const auto& s : post->GetSynapses()) {
            if (auto pre = s.source.lock()) {
                auto it = index.find(pre.get());
                if (it != index.end()) out_sum[it->second] += s.weight;
            }
            ++syn_total;
        }
    }

    for (size_t i = 0; i < all.size(); ++i) {
        Neuron* n = all[i].get();
        const bool inhib = out_sum[i] < 0.0f && n->GetRole() != NeuronRole::RootSensory;
        spiking::CellType type = spiking::CellType::RegularSpiking;
        switch (n->GetRole()) {
            case NeuronRole::RootSensory:       type = spiking::CellType::ThalamoCortical; break;
            case NeuronRole::CorticalComposite: type = spiking::CellType::IntrinsicBursting; break;  // L5 pyramidal
            case NeuronRole::MicroInterneuron:  type = spiking::CellType::Chattering; break;        // L2/3
            case NeuronRole::TerminalReadout:   type = spiking::CellType::RegularSpiking; break;
        }
        if (inhib) type = spiking::CellType::FastSpiking;
        snn_->AddNeuron(type);
        snn_neurons_.push_back(n);
        snn_inhib_.push_back(inhib ? 1 : 0);
    }

    auto delay_ms = [](const Neuron* a, const Neuron* b) {
        // Myelinated conduction: delay grows with physical distance (1..20 ms)
        const float d = (a->GetPosition() - b->GetPosition()).Length();
        return std::clamp(1.0f + d * 0.02f, 1.0f, 20.0f);
    };

    for (size_t post = 0; post < all.size(); ++post) {
        for (auto& s : all[post]->GetSynapsesMutable()) {
            auto pre = s.source.lock();
            if (!pre) continue;
            auto it = index.find(pre.get());
            if (it == index.end()) continue;
            const uint32_t p = it->second;
            snn_->Connect(p, static_cast<uint32_t>(post), std::fabs(s.weight) * kSnnConductanceScale,
                          delay_ms(pre.get(), all[post].get()), true);
            snn_links_.push_back({&s, p, static_cast<uint32_t>(snn_->OutDegree(p) - 1), s.weight});
        }
        // Composite holon wiring: parent drives its micro-circuit, children pool back
        for (const auto& ch : all[post]->GetChildren()) {
            auto it = index.find(ch.get());
            if (it == index.end()) continue;
            const uint32_t c = it->second, par = static_cast<uint32_t>(post);
            const float g = config_.composite_child_mix * kSnnConductanceScale * 2.0f;
            if (!snn_inhib_[par]) snn_->Connect(par, c, g, 1.0f, false);
            if (!snn_inhib_[c]) snn_->Connect(c, par, g, 1.0f, false);
        }
    }

    snn_rate_hz_.assign(all.size(), 0.0f);
    // Cell-to-cell heterogeneity (stable per neuron id): real neurons differ in
    // input resistance/rheobase, which turns uniform drive into a skewed rate distribution.
    snn_excitability_.resize(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        std::mt19937 g(static_cast<uint32_t>(all[i]->GetId() * 2654435761u + 17u));
        snn_excitability_[i] = std::normal_distribution<float>(0.0f, 1.2f)(g);
    }
    snn_sig_neurons_ = all.size();
    snn_sig_synapses_ = syn_total;
}

void BrainEngine::StepSpiking(float bio_ms) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    // Topology changed (neurogenesis / synaptogenesis / pruning / load) -> recompile
    size_t n_now = 0, s_now = 0;
    for (const auto& n : FlattenAllNeurons()) { ++n_now; s_now += n->GetSynapses().size(); }
    if (!snn_ || n_now != snn_sig_neurons_ || s_now != snn_sig_synapses_) RebuildSpikingNetwork();

    if (sustained_ticks_remaining_ > 0) {
        InjectSensoryPulses(sustained_pulses_);
        --sustained_ticks_remaining_;
    }

    // Weights edited outside the substrate (reward backprop, REST, load) flow in
    for (auto& L : snn_links_) {
        if (L.graph->weight != L.written) {
            snn_->GetSynapseMutable(L.pre, L.k).weight =
                std::clamp(std::fabs(L.graph->weight) * kSnnConductanceScale, 0.0f, snn_->Config().w_max);
            L.written = L.graph->weight;
        }
    }

    // Phasic dopamine (above tonic baseline) gates the three-factor rule
    snn_->SetDopamine(2.0f * (endocrine_.dopamine - endocrine_.tonic_dopamine));

    const size_t N = snn_neurons_.size();
    const float dt = snn_->Config().dt_ms;
    const int sub = static_cast<int>(std::round(1.0f / dt));
    const float rate_decay = std::exp(-dt / 100.0f);  // 100 ms rate window
    const float rate_kick = 1000.0f / 100.0f;         // Hz per spike
    // Endocrine state as membrane bias: fear depolarises, fatigue hyperpolarises
    const float mod_bias = 1.5f * endocrine_.fear - 2.0f * endocrine_.fatigue;
    const float noise_sd = 2.0f + 2.0f * endocrine_.curiosity;  // synaptic bombardment
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::vector<uint8_t> spiked(N, 0);

    const int total_ms = std::max(1, static_cast<int>(bio_ms));
    for (int ms = 0; ms < total_ms; ++ms) {
        for (size_t i = 0; i < N; ++i) {
            const Neuron* n = snn_neurons_[i];
            float I = snn_excitability_[i] + mod_bias + noise_sd * noise(rng_);
            // Thalamic relay cells are quiet at rest and driven by their sensory input;
            // cortical cells carry their learned bias as tonic depolarisation.
            if (n->GetRole() == NeuronRole::RootSensory) I += snn_->Config().sensory_gain * n->GetExternalInput() - 1.0f;
            else I += 2.0f * n->GetBias();
            snn_->SetInputCurrent(static_cast<uint32_t>(i), I);
        }
        for (int s = 0; s < sub; ++s) {
            for (size_t i = 0; i < N; ++i) snn_rate_hz_[i] *= rate_decay;
            for (uint32_t f : snn_->Step()) {
                snn_rate_hz_[f] += rate_kick;
                spiked[f] = 1;
            }
        }
    }

    // Write back: firing rate -> activation, learned conductance -> graph weight
    float total_act = 0.0f, max_act = 0.0f;
    last_spike_ids_.clear();
    for (size_t i = 0; i < N; ++i) {
        const float a = 1.0f - std::exp(-snn_rate_hz_[i] / 25.0f);
        Neuron* n = snn_neurons_[i];
        n->SetPrevActivation(n->GetActivation());
        n->SetActivation(a);
        n->SetMembranePotential(snn_->V(static_cast<uint32_t>(i)));
        n->SetAgeTicks(n->GetAgeTicks() + 1);
        total_act += a;
        max_act = std::max(max_act, a);
        if (spiked[i]) last_spike_ids_.push_back(n->GetId());
    }
    for (auto& L : snn_links_) {
        const auto& s = snn_->GetSynapse(L.pre, L.k);
        const float sign = (L.graph->weight < 0.0f) ? -1.0f : 1.0f;  // keep the graph's own sign
        const float w = sign * s.weight / kSnnConductanceScale;
        L.graph->last_delta_weight = w - L.graph->weight;
        L.graph->eligibility_trace = s.eligibility;
        L.graph->weight = w;
        L.written = w;
    }
    snn_->ClearSpikeLog();

    const float mean_act = N ? total_act / static_cast<float>(N) : 0.0f;
    endocrine_.StepHomeostasis(mean_act);
    for (auto& r : root_neurons_) r->ClearExternalInput();
    ++tick_;
    RecordTelemetrySample(mean_act, max_act);
    arena_.compiled = false;

    const auto t1 = std::chrono::high_resolution_clock::now();
    last_step_us_ = std::chrono::duration<double, std::micro>(t1 - t0).count();
}

} // namespace metabrain
