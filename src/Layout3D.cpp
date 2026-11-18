#include "metabrain/Layout3D.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace metabrain {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kGoldenAngle = 2.39996322972865332f;

void LayoutMicroClusterRecursive(const std::shared_ptr<Neuron>& parent,
                                 const LayoutConfig& cfg) {
    if (!parent || !parent->IsComposite()) {
        return;
    }

    const auto& children = parent->GetChildren();
    const size_t M = children.size();
    const uint32_t depth = parent->GetDepth() + 1;
    const float radius = cfg.child_orbit_base * std::pow(cfg.child_scale_decay, static_cast<float>(depth - 1));
    const Vec3 center = parent->GetPosition();

    std::vector<Vec3> local_pos(M);
    std::unordered_map<uint64_t, size_t> idx_map;
    idx_map.reserve(M * 2);

    // Initialize on a 3D Fibonacci sphere around origin
    for (size_t i = 0; i < M; ++i) {
        idx_map[children[i]->GetId()] = i;
        const float t = (M > 1) ? (static_cast<float>(i) / static_cast<float>(M - 1)) : 0.5f;
        const float y = 1.0f - 2.0f * t;
        const float r_xy = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float theta = kGoldenAngle * static_cast<float>(i);
        local_pos[i] = Vec3{
            std::cos(theta) * r_xy * radius,
            y * radius * 0.8f,
            std::sin(theta) * r_xy * radius
        };
    }

    // Force-directed refinement inside parent's bounding shell
    if (M >= 2 && M <= 128) {
        const float k_ideal = radius * 0.95f;
        std::vector<Vec3> disp(M);

        for (uint32_t iter = 0; iter < cfg.micro_iterations; ++iter) {
            const float temp = radius * 0.25f * (1.0f - static_cast<float>(iter) / static_cast<float>(cfg.micro_iterations));
            std::fill(disp.begin(), disp.end(), Vec3{0.0f, 0.0f, 0.0f});

            // All-pairs Coulombic repulsion
            for (size_t i = 0; i < M; ++i) {
                for (size_t j = i + 1; j < M; ++j) {
                    Vec3 delta = local_pos[i] - local_pos[j];
                    const float dist = std::max(0.4f, delta.Length());
                    const float force = (k_ideal * k_ideal) / (dist * static_cast<float>(M));
                    Vec3 fvec = delta.Normalized() * force;
                    disp[i] += fvec;
                    disp[j] -= fvec;
                }
            }

            // Synaptic spring attraction among sibling micro-neurons
            for (size_t i = 0; i < M; ++i) {
                for (const auto& syn : children[i]->GetSynapses()) {
                    auto it = idx_map.find(syn.source_id);
                    if (it != idx_map.end() && it->second != i) {
                        const size_t j = it->second;
                        Vec3 delta = local_pos[i] - local_pos[j];
                        const float dist = std::max(0.2f, delta.Length());
                        const float strength = 0.25f + std::min(1.5f, std::abs(syn.weight));
                        const float force = (dist * dist / k_ideal) * 0.25f * strength;
                        Vec3 fvec = delta.Normalized() * force;
                        disp[i] -= fvec;
                        disp[j] += fvec;
                    }
                }
            }

            // Apply displacements & softly tether inside parent's sphere
            for (size_t i = 0; i < M; ++i) {
                // Soft gravity toward orbital shell radius
                const float d_center = std::max(0.1f, local_pos[i].Length());
                disp[i] -= local_pos[i].Normalized() * ((d_center - radius * 0.82f) * 0.35f);

                const float d_len = disp[i].Length();
                if (d_len > 1e-5f) {
                    local_pos[i] += disp[i] * (std::min(d_len, temp) / d_len);
                }
                const float r_now = local_pos[i].Length();
                if (r_now > radius * 1.15f) {
                    local_pos[i] *= (radius * 1.15f / r_now);
                }
            }
        }
    }

    for (size_t i = 0; i < M; ++i) {
        children[i]->SetPosition(center + local_pos[i]);
        LayoutMicroClusterRecursive(children[i], cfg);
    }
}
} // namespace

void Layout3D::ComputeRecursiveLayout(BrainEngine& engine, const LayoutConfig& cfg) {
    const auto& top = engine.GetTopLevelNeurons();
    const size_t N = top.size();
    if (N == 0) {
        return;
    }

    std::vector<Vec3> pos(N);
    std::unordered_map<uint64_t, size_t> id_to_idx;
    id_to_idx.reserve(N * 2);

    size_t root_i = 0, root_total = engine.GetRootNeurons().size();
    size_t term_i = 0, term_total = engine.GetTerminalNeurons().size();
    size_t cort_i = 0;
    const size_t cort_total = (N > root_total + term_total) ? (N - root_total - term_total) : 1;

    // 1. Seed Macro Brain Geometry (Anterior Sensory Arc -> Dual Cortical Hemispheres -> Dorsal Output Arc)
    for (size_t i = 0; i < N; ++i) {
        id_to_idx[top[i]->GetId()] = i;
        const NeuronRole role = top[i]->GetRole();

        if (role == NeuronRole::RootSensory) {
            const float u = (root_total > 1)
                          ? (static_cast<float>(root_i) / static_cast<float>(root_total - 1) - 0.5f)
                          : 0.0f;
            const float arc = u * kPi * 0.78f;
            pos[i] = Vec3{
                std::sin(arc) * cfg.macro_radius * 1.05f,
                std::cos(arc * 2.0f) * 26.0f - 18.0f,
                -cfg.macro_radius * 1.12f + (1.0f - std::cos(arc)) * 35.0f
            };
            ++root_i;
        } else if (role == NeuronRole::TerminalReadout) {
            const float u = (term_total > 1)
                          ? (static_cast<float>(term_i) / static_cast<float>(term_total - 1) - 0.5f)
                          : 0.0f;
            const float arc = u * kPi * 0.62f;
            pos[i] = Vec3{
                std::sin(arc) * cfg.macro_radius * 0.88f,
                std::cos(arc) * 28.0f + 12.0f,
                cfg.macro_radius * 1.15f - (1.0f - std::cos(arc)) * 25.0f
            };
            ++term_i;
        } else {
            // Bi-hemispheric Fibonacci ellipsoid
            const float t = (cort_total > 1)
                          ? (static_cast<float>(cort_i) / static_cast<float>(cort_total - 1))
                          : 0.5f;
            const float y = (1.0f - 2.0f * t) * cfg.macro_radius * 0.62f;
            const float r_ring = std::sqrt(std::max(0.05f, 1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f)));
            const float theta = kGoldenAngle * static_cast<float>(cort_i);
            float x = std::cos(theta) * r_ring * cfg.macro_radius * 0.82f;
            // Subtle longitudinal fissure separation between left & right hemispheres
            x += (x >= 0.0f ? 16.0f : -16.0f);
            const float z = std::sin(theta) * r_ring * cfg.macro_radius * 0.72f;
            pos[i] = Vec3{x, y, z};
            ++cort_i;
        }
    }

    // 2. Macro 3D Force-Directed Relaxation (for interactive-scale cortical graphs <= 1200 top nodes)
    if (N <= 1200) {
        const float k_ideal = cfg.macro_radius * 0.65f / std::cbrt(static_cast<float>( std::max<size_t>(1, cort_total) ));
        std::vector<Vec3> disp(N);

        for (uint32_t iter = 0; iter < cfg.macro_iterations; ++iter) {
            const float temp = 18.0f * (1.0f - static_cast<float>(iter) / static_cast<float>(cfg.macro_iterations));
            std::fill(disp.begin(), disp.end(), Vec3{0.0f, 0.0f, 0.0f});

            for (size_t i = 0; i < N; ++i) {
                if (top[i]->GetRole() != NeuronRole::CorticalComposite) continue;
                for (size_t j = 0; j < N; ++j) {
                    if (i == j) continue;
                    Vec3 delta = pos[i] - pos[j];
                    const float dist = std::max(6.0f, delta.Length());
                    const float rep = (k_ideal * k_ideal * 2.4f) / dist;
                    disp[i] += delta.Normalized() * rep;
                }
            }

            for (size_t i = 0; i < N; ++i) {
                for (const auto& syn : top[i]->GetSynapses()) {
                    auto it = id_to_idx.find(syn.source_id);
                    if (it == id_to_idx.end() || it->second == i) continue;
                    const size_t j = it->second;
                    Vec3 delta = pos[i] - pos[j];
                    const float dist = std::max(4.0f, delta.Length());
                    const float w_mag = 0.2f + std::min(1.5f, std::abs(syn.weight));
                    const float attr = (dist * dist / std::max(15.0f, k_ideal * 3.2f)) * 0.08f * w_mag;
                    Vec3 fvec = delta.Normalized() * attr;
                    if (top[i]->GetRole() == NeuronRole::CorticalComposite) disp[i] -= fvec;
                    if (top[j]->GetRole() == NeuronRole::CorticalComposite) disp[j] += fvec;
                }
            }

            for (size_t i = 0; i < N; ++i) {
                if (top[i]->GetRole() != NeuronRole::CorticalComposite) continue;
                // Gentle centering gravity so graph stays compact
                disp[i] -= pos[i] * 0.045f;
                const float d_len = disp[i].Length();
                if (d_len > 1e-4f) {
                    pos[i] += disp[i] * (std::min(d_len, temp) / d_len);
                }
            }
        }
    }

    // 3. Commit Macro Coordinates & Recursively Solve Nested Micro-Clusters
    for (size_t i = 0; i < N; ++i) {
        top[i]->SetPosition(pos[i]);
        LayoutMicroClusterRecursive(top[i], cfg);
    }
}

namespace {
void StepMicroDynamicRecursive(const std::shared_ptr<Neuron>& parent,
                               const Vec3& parent_delta,
                               float dt,
                               const LayoutConfig& cfg) {
    if (!parent || !parent->IsComposite()) return;
    const auto& children = parent->GetChildren();
    const size_t M = children.size();
    if (M == 0) return;

    const uint32_t depth = parent->GetDepth() + 1;
    const float target_r = cfg.child_orbit_base * std::pow(cfg.child_scale_decay, static_cast<float>(depth - 1));
    const Vec3 center = parent->GetPosition();

    // Translate children along with parent first
    for (const auto& ch : children) {
        ch->SetPosition(ch->GetPosition() + parent_delta);
    }

    if (M <= 64) {
        for (size_t i = 0; i < M; ++i) {
            Vec3 force{0.0f, 0.0f, 0.0f};
            const Vec3 pi = children[i]->GetPosition();

            // Sibling repulsion
            for (size_t j = 0; j < M; ++j) {
                if (i == j) continue;
                Vec3 diff = pi - children[j]->GetPosition();
                const float dist = std::max(1.2f, diff.Length());
                force += diff.Normalized() * ((target_r * target_r * 0.55f) / (dist * dist));
            }

            // Synaptic spring attraction inside micro-circuit
            for (const auto& syn : children[i]->GetSynapses()) {
                if (auto src = syn.source.lock()) {
                    if (src->GetParent().get() == parent.get()) {
                        Vec3 diff = src->GetPosition() - pi;
                        const float w_pull = 0.2f + std::min(2.0f, std::abs(syn.weight));
                        force += diff * (0.28f * w_pull);
                    }
                }
            }

            // Orbital tether to parent center
            Vec3 rel = pi - center;
            const float r_len = std::max(0.2f, rel.Length());
            force -= rel.Normalized() * ((r_len - target_r * 0.84f) * 2.2f);

            // Subtle tangential orbital drift proportional to activation
            Vec3 tangent{-rel.z, 0.15f * rel.y, rel.x};
            force += tangent.Normalized() * (0.8f + 2.5f * children[i]->GetActivation());

            Vec3 vel = children[i]->GetVelocity() * 0.82f + force * dt;
            const float v_len = vel.Length();
            if (v_len > 3.5f) vel *= (3.5f / v_len);

            const Vec3 next_p = pi + vel * dt * 6.0f;
            const Vec3 d_child = next_p - pi;
            children[i]->SetVelocity(vel);
            children[i]->SetPosition(next_p);

            StepMicroDynamicRecursive(children[i], d_child, dt, cfg);
        }
    }
}
} // namespace

void Layout3D::StepDynamicForces(BrainEngine& engine, float dt, const LayoutConfig& cfg) {
    const auto& top = engine.GetTopLevelNeurons();
    const size_t N = top.size();
    if (N == 0 || N > 600) return;

    const float k_rep = 2100.0f;

    for (size_t i = 0; i < N; ++i) {
        const auto& node = top[i];
        if (node->GetRole() != NeuronRole::CorticalComposite) {
            StepMicroDynamicRecursive(node, Vec3{0.0f, 0.0f, 0.0f}, dt, cfg);
            continue;
        }

        const Vec3 pi = node->GetPosition();
        Vec3 force{0.0f, 0.0f, 0.0f};

        // Repulsion from other macro nodes
        for (size_t j = 0; j < N; ++j) {
            if (i == j) continue;
            Vec3 diff = pi - top[j]->GetPosition();
            const float dist_sq = std::max(64.0f, diff.LengthSq());
            force += diff.Normalized() * (k_rep / dist_sq);
        }

        // Hebbian Synaptic Spring Attraction (stronger positive weights pull closer)
        for (const auto& syn : node->GetSynapses()) {
            if (auto src = syn.source.lock()) {
                if (src->GetDepth() == 0 && src->GetRole() == NeuronRole::CorticalComposite) {
                    Vec3 diff = src->GetPosition() - pi;
                    const float dist = diff.Length();
                    if (dist > 32.0f) {
                        const float pull = 0.025f * (0.2f + std::max(0.0f, syn.weight));
                        force += diff * pull;
                    }
                }
            }
        }

        // Soft containment inside cortical ellipsoid & hemispheric separation
        force -= Vec3{pi.x * 0.04f, pi.y * 0.06f, pi.z * 0.045f};
        if (std::abs(pi.x) < 18.0f) {
            force.x += (pi.x >= 0.0f ? 1.0f : -1.0f) * 3.5f;
        }

        Vec3 vel = node->GetVelocity() * 0.84f + force * dt;
        const float v_len = vel.Length();
        if (v_len > 6.0f) vel *= (6.0f / v_len);

        const Vec3 next_p = pi + vel * dt * 4.5f;
        const Vec3 delta_p = next_p - pi;
        node->SetVelocity(vel);
        node->SetPosition(next_p);

        StepMicroDynamicRecursive(node, delta_p, dt, cfg);
    }
}

} // namespace metabrain
