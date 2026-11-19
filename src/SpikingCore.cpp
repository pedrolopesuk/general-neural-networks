#include "metabrain/SpikingCore.hpp"

#include <algorithm>
#include <cmath>

namespace metabrain::spiking {

IzhParams ParamsFor(CellType t) noexcept {
    // Izhikevich 2003, Fig. 2 parameter sets
    switch (t) {
        case CellType::RegularSpiking:      return {0.02f, 0.20f, -65.0f, 8.0f};
        case CellType::IntrinsicBursting:   return {0.02f, 0.20f, -55.0f, 4.0f};
        case CellType::Chattering:          return {0.02f, 0.20f, -50.0f, 2.0f};
        case CellType::FastSpiking:         return {0.10f, 0.20f, -65.0f, 2.0f};
        case CellType::LowThresholdSpiking: return {0.02f, 0.25f, -65.0f, 2.0f};
        case CellType::ThalamoCortical:     return {0.02f, 0.25f, -65.0f, 0.05f};
        case CellType::Resonator:           return {0.10f, 0.26f, -65.0f, 2.0f};
    }
    return {0.02f, 0.20f, -65.0f, 8.0f};
}

bool IsInhibitory(CellType t) noexcept {
    return t == CellType::FastSpiking || t == CellType::LowThresholdSpiking;
}

float AhpIncrementFor(CellType t, const SpikingConfig& cfg) noexcept {
    // Thalamic relay cells in tonic mode adapt via I_AHP; the Izhikevich TC set
    // (d = 0.05) has almost no built-in adaptation, so it is supplied explicitly.
    return t == CellType::ThalamoCortical ? cfg.tc_ahp_increment : 0.0f;
}

SpikingNetwork::SpikingNetwork(SpikingConfig cfg) : cfg_(cfg) {
    ring_.resize(2);
}

uint32_t SpikingNetwork::AddNeuron(CellType type) {
    const auto id = static_cast<uint32_t>(v_.size());
    const IzhParams p = ParamsFor(type);
    type_.push_back(type);
    p_.push_back(p);
    v_.push_back(-65.0f);
    u_.push_back(p.b * -65.0f);
    I_ext_.push_back(0.0f);
    g_ampa_.push_back(0.0f);
    g_nmda_.push_back(0.0f);
    g_gabaa_.push_back(0.0f);
    g_gabab_.push_back(0.0f);
    g_ahp_.push_back(0.0f);
    refr_left_.push_back(0);
    ahp_inc_.push_back(AhpIncrementFor(type, cfg_));
    x_pre_.push_back(0.0f);
    y_post_.push_back(0.0f);
    spike_log_.emplace_back();
    out_.emplace_back();
    plastic_.emplace_back();
    in_.emplace_back();
    return id;
}

void SpikingNetwork::Connect(uint32_t pre, uint32_t post, float weight, float delay_ms, bool plastic) {
    const auto steps = static_cast<uint16_t>(std::max(1.0f, std::round(delay_ms / cfg_.dt_ms)));
    out_[pre].push_back({post, std::clamp(weight, 0.0f, cfg_.w_max), steps, 0.0f});
    plastic_[pre].push_back(plastic ? 1 : 0);
    in_[post].emplace_back(pre, static_cast<uint32_t>(out_[pre].size() - 1));
    if (steps + 1u > max_delay_steps_) {
        max_delay_steps_ = steps + 1u;
        // Ring must hold every in-flight spike; rebuild preserving nothing in flight is
        // only safe before simulation starts, which is when wiring normally happens.
        ring_.assign(max_delay_steps_ + 1, {});
    }
}

void SpikingNetwork::ClearSpikeLog() {
    for (auto& l : spike_log_) l.clear();
}

void SpikingNetwork::ApplyStdpOnPreArrival(uint32_t pre, Synapse& s) {
    (void)pre;
    // Pre arrives after post fired -> depression proportional to the post trace
    s.eligibility -= cfg_.stdp_a_minus * y_post_[s.post];
}

void SpikingNetwork::ApplyStdpOnPost(uint32_t post) {
    // Post fires after pre arrivals -> potentiation proportional to each pre trace
    for (const auto& [pre, k] : in_[post]) {
        if (!plastic_[pre][k]) continue;
        out_[pre][k].eligibility += cfg_.stdp_a_plus * x_pre_[pre];
    }
}

void SpikingNetwork::DeliverArrivals() {
    auto& slot = ring_[step_ % ring_.size()];
    for (const PendingSpike& ps : slot) {
        Synapse& s = out_[ps.pre][ps.syn];
        if (IsInhibitory(type_[ps.pre])) {
            g_gabaa_[s.post] += s.weight;
            g_gabab_[s.post] += s.weight * cfg_.gabab_ratio;
        } else {
            g_ampa_[s.post] += s.weight;
            g_nmda_[s.post] += s.weight * cfg_.nmda_ratio;
        }
        if (plastic_[ps.pre][ps.syn]) ApplyStdpOnPreArrival(ps.pre, s);
    }
    slot.clear();
}

const std::vector<uint32_t>& SpikingNetwork::Step() {
    const float dt = cfg_.dt_ms;
    const size_t N = v_.size();
    fired_.clear();

    // 1. Spikes whose axonal delay elapses now open their receptors
    //    (the arriving pre trace is the one bumped at emission time below).
    DeliverArrivals();

    const float dA = std::exp(-dt / cfg_.tau_ampa), dN = std::exp(-dt / cfg_.tau_nmda);
    const float dGa = std::exp(-dt / cfg_.tau_gabaa), dGb = std::exp(-dt / cfg_.tau_gabab);
    const float dAhp = std::exp(-dt / cfg_.tau_ahp);
    const float dX = std::exp(-dt / cfg_.tau_plus), dY = std::exp(-dt / cfg_.tau_minus);

    // 2. Membrane integration (forward Euler at 0.1 ms is accurate for Izhikevich)
    for (size_t i = 0; i < N; ++i) {
        const float v = v_[i];
        const float vm = (v + 80.0f) / 60.0f;
        const float mg_block = (vm * vm) / (1.0f + vm * vm);  // Izhikevich 2008 NMDA gate
        const float I_syn = g_ampa_[i] * (cfg_.E_ampa - v)
                          + g_nmda_[i] * mg_block * (cfg_.E_nmda - v)
                          + g_gabaa_[i] * (cfg_.E_gabaa - v)
                          + g_gabab_[i] * (cfg_.E_gabab - v)
                          + g_ahp_[i] * (cfg_.E_k - v);
        const float I = I_ext_[i] + I_syn;
        const IzhParams& p = p_[i];

        float vn = v + dt * (0.04f * v * v + 5.0f * v + 140.0f - u_[i] + I);
        float un = u_[i] + dt * p.a * (p.b * v - u_[i]);
        if (refr_left_[i] > 0) {  // absolute refractory: membrane clamped at reset
            --refr_left_[i];
            vn = p.c;
        }

        if (vn >= cfg_.spike_peak_mv) {
            refr_left_[i] = static_cast<uint16_t>(std::lround(cfg_.refractory_ms / dt));
            vn = p.c;
            un += p.d;
            fired_.push_back(static_cast<uint32_t>(i));
            g_ahp_[i] += ahp_inc_[i];
        }
        v_[i] = vn;
        u_[i] = un;

        g_ampa_[i] *= dA;
        g_nmda_[i] *= dN;
        g_gabaa_[i] *= dGa;
        g_gabab_[i] *= dGb;
        g_ahp_[i] *= dAhp;
        x_pre_[i] *= dX;
        y_post_[i] *= dY;
    }

    // 3. Spike bookkeeping: schedule axonal arrivals, update STDP traces
    const double t = TimeMs();
    for (uint32_t i : fired_) {
        spike_log_[i].push_back(t);
        ApplyStdpOnPost(i);
        y_post_[i] += 1.0f;
        x_pre_[i] += 1.0f;
        for (uint32_t k = 0; k < out_[i].size(); ++k) {
            const uint64_t arrive = step_ + out_[i][k].delay_steps;
            ring_[arrive % ring_.size()].push_back({i, k});
        }
    }

    // 4. Three-factor rule: dopamine turns eligibility into lasting weight change
    const float dC = std::exp(-dt / cfg_.tau_eligibility);
    const float da_gain = dopamine_ * dt * 0.001f;  // integral over tau_c ~= eligibility * dopamine
    for (size_t i = 0; i < N; ++i) {
        for (size_t k = 0; k < out_[i].size(); ++k) {
            Synapse& s = out_[i][k];
            if (!plastic_[i][k] || s.eligibility == 0.0f) continue;
            if (da_gain != 0.0f) {
                s.weight = std::clamp(s.weight + da_gain * s.eligibility, 0.0f, cfg_.w_max);
            }
            s.eligibility *= dC;
            if (std::fabs(s.eligibility) < 1e-7f) s.eligibility = 0.0f;
        }
    }

    ++step_;
    return fired_;
}

void SpikingNetwork::Run(float ms) {
    const auto steps = static_cast<uint64_t>(std::round(ms / cfg_.dt_ms));
    for (uint64_t s = 0; s < steps; ++s) Step();
}

}  // namespace metabrain::spiking
