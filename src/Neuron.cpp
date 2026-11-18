#include "metabrain/Neuron.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace metabrain {

namespace {
[[nodiscard]] inline float FastSigmoid(float x) noexcept {
    // Clamped standard logistic function with smooth saturation
    const float clamped = std::clamp(x, -14.0f, 14.0f);
    return 1.0f / (1.0f + std::exp(-clamped));
}
} // namespace

Neuron::Neuron(uint64_t id, float bias, NeuronRole role, std::string label)
    : id_(id),
      label_(label.empty() ? ("N_" + std::to_string(id)) : std::move(label)),
      role_(role),
      bias_(bias) {}

std::shared_ptr<Neuron> Neuron::Create(uint64_t id,
                                       float bias,
                                       NeuronRole role,
                                       std::string label) {
    return std::make_shared<Neuron>(id, bias, role, std::move(label));
}

void Neuron::SetDepth(uint32_t depth) noexcept {
    depth_ = depth;
    for (auto& child : children_) {
        if (child) {
            child->SetDepth(depth_ + 1);
        }
    }
}

std::shared_ptr<Neuron> Neuron::AddChild(std::shared_ptr<Neuron> child) {
    if (!child || child.get() == this) {
        return nullptr;
    }
    child->parent_ = weak_from_this();
    child->SetDepth(depth_ + 1);
    if (child->GetRole() == NeuronRole::CorticalComposite) {
        child->SetRole(NeuronRole::MicroInterneuron);
    }

    // Place child near parent initially with slight orbital offset
    const float idx = static_cast<float>(children_.size());
    const float angle = idx * 2.3999632f; // Golden angle spiral
    const float orbit_r = 18.0f / static_cast<float>(depth_ + 1);
    child->SetPosition({
        position_.x + std::cos(angle) * orbit_r,
        position_.y + std::sin(angle * 0.7f) * orbit_r * 0.6f,
        position_.z + std::sin(angle) * orbit_r
    });

    children_.push_back(child);
    return child;
}

std::shared_ptr<Neuron> Neuron::AddChild(uint64_t child_id,
                                         float child_bias,
                                         std::string label) {
    auto child = Neuron::Create(
        child_id,
        child_bias,
        NeuronRole::MicroInterneuron,
        label.empty() ? (label_ + ".sub_" + std::to_string(children_.size())) : std::move(label));
    return AddChild(std::move(child));
}

std::vector<std::shared_ptr<Neuron>> Neuron::SpawnMicroCircuit(
    uint32_t num_children,
    uint64_t& next_id_counter,
    uint32_t rng_seed) {
    std::vector<std::shared_ptr<Neuron>> spawned;
    if (num_children == 0) {
        return spawned;
    }

    std::mt19937 rng(rng_seed + static_cast<uint32_t>(id_ * 2654435761ULL));
    std::uniform_real_distribution<float> bias_dist(-0.08f, 0.08f);
    std::uniform_real_distribution<float> weight_dist(0.18f, 0.62f);

    spawned.reserve(num_children);
    for (uint32_t i = 0; i < num_children; ++i) {
        auto sub = AddChild(next_id_counter++, bias_dist(rng));
        if (sub) {
            // Couple parent somatic state -> child micro-neuron
            sub->ConnectFrom(shared_from_this(), weight_dist(rng));
            spawned.push_back(sub);
        }
    }

    // Wire lateral / recurrent ring + motif connections inside the micro-circuit
    for (size_t i = 0; i < spawned.size(); ++i) {
        const size_t next = (i + 1) % spawned.size();
        if (next != i) {
            spawned[next]->ConnectFrom(spawned[i], weight_dist(rng) * 0.65f);
        }
        if (spawned.size() >= 3) {
            const size_t skip = (i + 2) % spawned.size();
            // Inhibitory stabilizing interneuron motif
            spawned[skip]->ConnectFrom(spawned[i], -weight_dist(rng) * 0.35f);
        }
    }

    return spawned;
}

void Neuron::ConnectFrom(const std::shared_ptr<Neuron>& presynaptic, float weight) {
    if (!presynaptic || presynaptic.get() == this) {
        return;
    }
    const uint64_t src_id = presynaptic->GetId();
    for (auto& syn : incoming_synapses_) {
        if (syn.source_id == src_id) {
            syn.source = presynaptic;
            syn.weight = weight;
            return;
        }
    }

    Synapse s;
    s.source = presynaptic;
    s.source_id = src_id;
    s.weight = weight;
    s.eligibility_trace = 0.0f;
    s.last_delta_weight = 0.0f;
    s.age_ticks = 0;
    incoming_synapses_.push_back(std::move(s));
}

void Neuron::ConnectTo(const std::shared_ptr<Neuron>& postsynaptic, float weight) {
    if (postsynaptic) {
        postsynaptic->ConnectFrom(shared_from_this(), weight);
    }
}

void Neuron::LatchActivation() noexcept {
    prev_activation_ = activation_;
    for (auto& child : children_) {
        if (child) {
            child->LatchActivation();
        }
    }
}

float Neuron::ProcessSignal(const BrainConfig& cfg,
                            const EndocrineState& endocrine,
                            float exploration_noise) {
    // 1. Weighted integration of incoming presynaptic signals
    float synaptic_sum = bias_ + external_input_ + exploration_noise;

    for (auto& syn : incoming_synapses_) {
        if (auto src = syn.source.lock()) {
            synaptic_sum += syn.weight * src->GetPrevActivation();
        }
    }

    // 2. Recursive Composite Delegation:
    // If this Neuron contains a nested sub-network (children_), route somatic drive
    // into its internal micro-circuit and integrate their emergent pooled response.
    float integrated_drive = synaptic_sum;
    if (!children_.empty()) {
        float child_pool = 0.0f;
        uint32_t valid_children = 0;
        for (auto& child : children_) {
            if (child) {
                child->SetExternalInput(synaptic_sum * 0.45f);
                child_pool += child->ProcessSignal(cfg, endocrine, exploration_noise * 0.5f);
                child->ClearExternalInput();
                ++valid_children;
            }
        }
        if (valid_children > 0) {
            const float mean_child = child_pool / static_cast<float>(valid_children);
            integrated_drive = (1.0f - cfg.composite_child_mix) * synaptic_sum
                             + cfg.composite_child_mix * (synaptic_sum + mean_child);
        }
    }

    // 3. Leaky Membrane Potential Integration
    membrane_potential_ = cfg.membrane_leak * membrane_potential_
                        + (1.0f - cfg.membrane_leak) * integrated_drive;

    // 4. Global Endocrine Threshold & Gain Modulation
    const float theta_eff = endocrine.EffectiveThreshold(cfg);
    const float gain_eff = endocrine.EffectiveGain(cfg);
    const float amp_scale = endocrine.AmplitudeScale();

    float next_act = 0.0f;
    if (role_ == NeuronRole::RootSensory && incoming_synapses_.empty() && external_input_ > 0.0f) {
        // Sensory receptors faithfully transduce external [0..1] pulse while still feeling endocrine state
        const float raw = std::clamp(external_input_ + 0.15f * endocrine.fear - 0.12f * endocrine.fatigue, 0.0f, 1.0f);
        next_act = raw;
    } else {
        next_act = amp_scale * FastSigmoid(gain_eff * (membrane_potential_ - theta_eff));
    }

    // Track local activation surprise (prediction variance) for Curiosity-driven Neurogenesis
    const float delta_act = std::abs(next_act - prev_activation_);
    surprise_ema_ = 0.88f * surprise_ema_ + 0.12f * delta_act;

    activation_ = std::clamp(next_act, 0.0f, 1.0f);
    ++age_ticks_;
    return activation_;
}

void Neuron::Learn(const BrainConfig& cfg,
                   const EndocrineState& endocrine,
                   float reward_modulation) {
    const float eta = endocrine.EffectiveLearningRate(cfg) * reward_modulation;
    const float post = activation_;
    const float post_sq = post * post;

    for (auto& syn : incoming_synapses_) {
        auto src = syn.source.lock();
        if (!src) {
            continue;
        }
        const float pre = src->GetPrevActivation();

        const float co_activity = (pre > 0.08f && post > 0.08f) ? (pre * post) : 0.0f;

        // Update decaying eligibility trace across all synapses
        syn.eligibility_trace = cfg.eligibility_decay * syn.eligibility_trace + co_activity;

        // Terminal decision readouts consolidate via Reward/Punish feedback loop;
        // Cortical & Nested Micro-Circuits self-organize via Oja-stabilized Hebbian learning!
        if (role_ != NeuronRole::TerminalReadout) {
            const float hebbian_term = syn.eligibility_trace;
            const float oja_decay = cfg.oja_stabilization * post_sq * syn.weight;
            const float dw = eta * (hebbian_term - oja_decay);

            syn.weight = std::clamp(syn.weight + dw, -cfg.weight_clip, cfg.weight_clip);
            syn.last_delta_weight = dw;
        }
        ++syn.age_ticks;
    }

    // Recursively trigger plasticity in all nested child sub-networks
    for (auto& child : children_) {
        if (child) {
            child->Learn(cfg, endocrine, reward_modulation);
        }
    }
}

std::vector<float> Neuron::GetConnectionWeights() const {
    std::vector<float> weights;
    weights.reserve(incoming_synapses_.size());
    for (const auto& syn : incoming_synapses_) {
        weights.push_back(syn.weight);
    }
    return weights;
}

size_t Neuron::PruneWeakSynapses(float min_abs_weight, uint64_t min_age_ticks) {
    // Keep at least 1 incoming connection if non-root so nodes aren't orphaned
    if (incoming_synapses_.size() <= 1) {
        return 0;
    }
    const size_t before = incoming_synapses_.size();
    incoming_synapses_.erase(
        std::remove_if(incoming_synapses_.begin(), incoming_synapses_.end(),
                       [&](const Synapse& s) {
                           return s.source.expired() ||
                                  (s.age_ticks >= min_age_ticks && std::abs(s.weight) < min_abs_weight);
                       }),
        incoming_synapses_.end());

    size_t pruned = before - incoming_synapses_.size();
    for (auto& ch : children_) {
        if (ch) {
            pruned += ch->PruneWeakSynapses(min_abs_weight, min_age_ticks);
        }
    }
    return pruned;
}

} // namespace metabrain
