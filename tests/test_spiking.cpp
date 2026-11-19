// Phase 1 validation: each test checks the model against a known experimental /
// reference behaviour. If one fails, the substrate is not biologically faithful.
#include "metabrain/BrainEngine.hpp"
#include "metabrain/SpikingCore.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace metabrain::spiking;

#define REQUIRE(cond, msg)                                                                    \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " << msg << "\n"; \
            return false;                                                                     \
        }                                                                                     \
    } while (0)

namespace {

std::vector<double> Isis(const std::vector<double>& t) {
    std::vector<double> d;
    for (size_t i = 1; i < t.size(); ++i) d.push_back(t[i] - t[i - 1]);
    return d;
}

std::vector<double> DriveSingle(CellType type, float I, float ms) {
    SpikingNetwork net;
    const auto n = net.AddNeuron(type);
    net.Run(10.0f);  // settle at rest
    net.SetInputCurrent(n, I);
    net.Run(ms);
    return net.SpikeTimes(n);
}

// RS: tonic firing with spike-frequency adaptation (ISIs lengthen)
bool TestRegularSpikingAdapts() {
    const auto t = DriveSingle(CellType::RegularSpiking, 10.0f, 500.0f);
    REQUIRE(t.size() >= 5, "RS must fire tonically, got " << t.size());
    const auto d = Isis(t);
    REQUIRE(d.back() > d.front() * 1.3, "RS must adapt: first ISI " << d.front() << " last " << d.back());
    return true;
}

// FS: high-frequency, non-adapting firing; faster than RS at the same drive
bool TestFastSpikingNoAdaptation() {
    const auto fs = DriveSingle(CellType::FastSpiking, 10.0f, 500.0f);
    const auto rs = DriveSingle(CellType::RegularSpiking, 10.0f, 500.0f);
    REQUIRE(fs.size() > rs.size() * 2, "FS rate " << fs.size() << " should far exceed RS " << rs.size());
    // Real PV cells adapt weakly; the defining property is far less adaptation than RS
    // Adaptation index at a physiological suprathreshold drive (well above FS rheobase)
    const auto d = Isis(DriveSingle(CellType::FastSpiking, 20.0f, 500.0f));
    const auto r = Isis(DriveSingle(CellType::RegularSpiking, 20.0f, 500.0f));
    const double fs_adapt = d.back() / d.front(), rs_adapt = r.back() / r.front();
    std::printf("    adaptation index (last/first ISI @ I=20): FS %.2f  RS %.2f\n", fs_adapt, rs_adapt);
    // Recorded PV basket cells: adaptation ratio ~1.1-1.6 vs several-fold for pyramidal cells
    REQUIRE(fs_adapt < 1.6 && fs_adapt * 3.0 < rs_adapt,
            "FS adaptation " << fs_adapt << " must be weak vs RS " << rs_adapt);
    return true;
}

// CH: rhythmic bursts -> bimodal ISIs (short intra-burst, long inter-burst)
bool TestChatteringBursts() {
    const auto d = Isis(DriveSingle(CellType::Chattering, 10.0f, 500.0f));
    REQUIRE(d.size() > 6, "CH must fire repeatedly");
    double mn = 1e9, mx = 0;
    for (double x : d) { mn = std::min(mn, x); mx = std::max(mx, x); }
    REQUIRE(mx > 3.0 * mn, "CH ISIs must be bimodal (bursts): min " << mn << " max " << mx);
    return true;
}

// f-I curve: silent below rheobase, monotonic increase above
bool TestFICurveMonotonic() {
    int prev = -1;
    const int silent = static_cast<int>(DriveSingle(CellType::RegularSpiking, 2.0f, 1000.0f).size());
    REQUIRE(silent == 0, "RS must be silent below rheobase, fired " << silent);
    for (float I = 5.0f; I <= 25.0f; I += 5.0f) {
        const int n = static_cast<int>(DriveSingle(CellType::RegularSpiking, I, 1000.0f).size());
        REQUIRE(n > prev, "f-I must increase: I=" << I << " rate " << n << " prev " << prev);
        prev = n;
    }
    return true;
}

// Axonal delay: postsynaptic response starts exactly after the conduction delay
bool TestConductionDelay() {
    SpikingNetwork net;
    const auto a = net.AddNeuron(CellType::RegularSpiking);
    const auto b = net.AddNeuron(CellType::RegularSpiking);
    net.Connect(a, b, 1.0f, 8.0f, false);
    net.Run(300.0f);  // settle b at its true resting potential (~-70 mV for RS)
    net.SetInputCurrent(a, 40.0f);
    double t_pre = -1;
    float v_rest = 0, v_b_at_arrival_minus = 0, v_b_after = 0;
    for (int s = 0; s < 400; ++s) {
        const auto& f = net.Step();
        for (auto i : f) if (i == a && t_pre < 0) { t_pre = net.TimeMs(); v_rest = net.V(b); }
        if (t_pre >= 0) {
            const double since = net.TimeMs() - t_pre;
            if (std::fabs(since - 7.5) < 0.05) v_b_at_arrival_minus = net.V(b);
            if (std::fabs(since - 9.0) < 0.05) { v_b_after = net.V(b); break; }
        }
    }
    REQUIRE(t_pre >= 0, "presynaptic cell must fire");
    REQUIRE(std::fabs(v_b_at_arrival_minus - v_rest) < 0.3f, "no response before delay, v=" << v_b_at_arrival_minus);
    REQUIRE(v_b_after > v_rest + 3.0f, "EPSP must appear after delay, v=" << v_b_after);
    return true;
}

// Inhibition: a GABAergic (FS) input hyperpolarises toward E_GABA
bool TestInhibitoryHyperpolarises() {
    SpikingNetwork net;
    const auto i = net.AddNeuron(CellType::FastSpiking);
    const auto p = net.AddNeuron(CellType::RegularSpiking);
    net.Connect(i, p, 1.0f, 1.0f, false);
    net.SetInputCurrent(i, 40.0f);
    float vmin = 0;
    for (int s = 0; s < 300; ++s) { net.Step(); vmin = std::min(vmin, net.V(p)); }
    REQUIRE(vmin < -66.5f, "IPSP must hyperpolarise below rest, min v=" << vmin);
    return true;
}

// STDP window (Bi & Poo 1998): pre-before-post -> LTP, post-before-pre -> LTD,
// magnitude decays with |dt|
float StdpEligibility(float dt_ms) {
    SpikingNetwork net;
    const auto pre = net.AddNeuron(CellType::RegularSpiking);
    const auto post = net.AddNeuron(CellType::RegularSpiking);
    net.Connect(pre, post, 0.0f, 0.1f);  // zero weight: timing is fully controlled
        const float t_pre = 100.0f, t_post = 100.0f + dt_ms;
    auto kick = [](float t, float t0) { return (t >= t0 && t < t0 + 0.3f) ? 1500.0f : 0.0f; };  // exactly one spike
    for (int s = 0; s < 2000; ++s) {
        const float t = static_cast<float>(net.TimeMs());
        net.SetInputCurrent(pre, kick(t, t_pre));
        net.SetInputCurrent(post, kick(t, t_post));
        net.Step();
    }
    return net.GetSynapse(pre, 0).eligibility;
}

bool TestStdpWindow() {
    const float ltp5 = StdpEligibility(+5.0f), ltp30 = StdpEligibility(+30.0f);
    const float ltd5 = StdpEligibility(-5.0f), ltd30 = StdpEligibility(-30.0f);
    std::printf("    STDP window: +5ms %.5f  +30ms %.5f  -5ms %.5f  -30ms %.5f\n", ltp5, ltp30, ltd5, ltd30);
    REQUIRE(ltp5 > 0 && ltp30 > 0, "pre->post must potentiate");
    REQUIRE(ltd5 < 0 && ltd30 < 0, "post->pre must depress");
    REQUIRE(ltp5 > ltp30 && ltd5 < ltd30, "magnitude must decay with |dt|");
    return true;
}

// Three-factor learning: eligibility alone changes nothing; dopamine commits it
bool TestDopamineGatesLearning() {
    auto run = [](float da) {
        SpikingNetwork net;
        const auto pre = net.AddNeuron(CellType::RegularSpiking);
        const auto post = net.AddNeuron(CellType::RegularSpiking);
        net.Connect(pre, post, 0.3f, 1.0f);
        for (int rep = 0; rep < 10; ++rep) {
            for (int s = 0; s < 1000; ++s) {
                const float t = static_cast<float>(std::fmod(net.TimeMs(), 100.0));
                net.SetInputCurrent(pre, std::fabs(t - 10.0f) < 0.5f ? 500.0f : 0.0f);
                net.SetInputCurrent(post, std::fabs(t - 15.0f) < 0.5f ? 500.0f : 0.0f);
                net.Step();
            }
            net.SetDopamine(da);  // reward arrives after the pairing (distal reward)
            net.Run(200.0f);
            net.SetDopamine(0.0f);
        }
        return net.GetSynapse(pre, 0).weight;
    };
    const float w_none = run(0.0f), w_da = run(1.0f);
    std::printf("    weight after pairing: no dopamine %.4f, with dopamine %.4f\n", w_none, w_da);
    REQUIRE(std::fabs(w_none - 0.3f) < 1e-6f, "without dopamine weights must not change");
    REQUIRE(w_da > 0.3f + 0.005f, "dopamine must commit LTP");
    return true;
}


// Integration: the real MetaBrain graph running on the spiking substrate
bool TestEngineSpikingIntegration() {
    metabrain::BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
    brain.SetSpikingMode(true);

    auto mean_root_act = [&]() {
        float a = 0;
        for (const auto& r : brain.GetRootNeurons()) a += r->GetActivation();
        return a / static_cast<float>(brain.GetRootNeurons().size());
    };
    for (int i = 0; i < 20; ++i) brain.StepSpiking(10.0f);  // 200 ms spontaneous
    const float quiet = mean_root_act();
    const auto m0 = brain.ComputeMetrics();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 20; ++i) {
        brain.InjectSensoryPulses(std::vector<float>(8, 0.9f));
        brain.StepSpiking(10.0f);
    }
    const double ms_per_bio_s = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count() / 0.2;
    const float driven = mean_root_act();
    const auto m1 = brain.ComputeMetrics();
    std::printf("    %zu neurons, %zu synapses | root activation quiet %.3f -> driven %.3f | mean %.3f | %zu spiking cells | %.1f ms wall per 1 s biology\n",
                m1.total_neurons, m1.total_synapses, quiet, driven, m1.mean_activation,
                brain.GetLastSpikes().size(), ms_per_bio_s);
    REQUIRE(driven > quiet + 0.2f, "sensory drive must raise root firing");
    REQUIRE(m1.mean_activation > 0.01f && m1.mean_activation < 0.95f, "network must be neither dead nor saturated: " << m1.mean_activation);
    REQUIRE(!brain.GetLastSpikes().empty(), "spike stream must be non-empty under drive");
    (void)m0;

    // Learned weights must stay finite and within the clip after sustained activity
    for (const auto& n : brain.FlattenAllNeurons())
        for (const auto& s : n->GetSynapses())
            REQUIRE(std::isfinite(s.weight) && std::fabs(s.weight) <= brain.GetConfig().weight_clip + 1e-3f, "weight out of range: " << s.weight);

    // Topology change (neurogenesis) is picked up without losing learned weights
    const size_t before = brain.FlattenAllNeurons().size();
    brain.StepNeurogenesis(true);
    brain.StepSpiking(10.0f);
    REQUIRE(brain.FlattenAllNeurons().size() > before, "neurogenesis must add a neuron");
    REQUIRE(brain.GetSpikingNetwork()->Size() == brain.FlattenAllNeurons().size(), "substrate must recompile after growth");
    return true;
}


// Resting-state regression bands (fixed seeds: graph seed 42, engine RNG fixed).
// Catches the network turning epileptic or dead without breaking on harmless tuning.
bool TestRestingRateBands() {
    metabrain::BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
    brain.SetSpikingMode(true);
    for (int i = 0; i < 500; ++i) brain.StepSpiking(1.0f);
    std::unordered_map<uint64_t, int> count;
    const int ms = 20000;
    for (int i = 0; i < ms; ++i) {
        brain.StepSpiking(1.0f);
        for (auto id : brain.GetLastSpikes()) ++count[id];
    }
    std::vector<double> all, thal;
    for (const auto& n : brain.FlattenAllNeurons()) {
        const double hz = count[n->GetId()] / (ms / 1000.0);
        all.push_back(hz);
        if (n->GetRole() == metabrain::NeuronRole::RootSensory) thal.push_back(hz);
    }
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    double mean = 0, mx = 0;
    int silent = 0;
    for (double x : all) { mean += x; mx = std::max(mx, x); silent += (x < 0.1) ? 1 : 0; }
    mean /= all.size();
    const double med = median(all), silent_pct = 100.0 * silent / all.size(), thal_med = median(thal);
    std::printf("    rest: median %.2f Hz, mean %.2f Hz, near-silent %.0f%%, max %.1f Hz, thalamic median %.1f Hz\n",
                med, mean, silent_pct, mx, thal_med);
    REQUIRE(med >= 0.5 && med <= 3.0, "network median " << med << " Hz outside 0.5-3");
    REQUIRE(mean > med, "rate distribution must stay right-skewed (mean > median)");
    REQUIRE(silent_pct >= 5.0 && silent_pct <= 40.0, "near-silent fraction " << silent_pct << "% outside 5-40%");
    REQUIRE(mx <= 50.0, "a cell fires " << mx << " Hz at rest (> 50)");
    REQUIRE(thal_med >= 3.0 && thal_med <= 20.0, "thalamic median " << thal_med << " Hz outside 3-20");
    return true;
}

// Driven response: brief onset peak is allowed (100-200+ Hz), a sustained runaway is not
bool TestDrivenPeakVsSustained() {
    metabrain::BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
    brain.SetSpikingMode(true);
    for (int i = 0; i < 500; ++i) brain.StepSpiking(1.0f);
    std::unordered_map<uint64_t, int> onset, late;
    for (int i = 0; i < 2000; ++i) {
        brain.InjectSensoryPulses(std::vector<float>(8, 0.9f));
        brain.StepSpiking(1.0f);
        for (auto id : brain.GetLastSpikes()) ++((i < 50) ? onset : late)[id];
    }
    double peak = 0, sustained = 0, ctx_sustained = 0;
    std::vector<double> thal_sus;
    int nctx = 0;
    for (const auto& n : brain.FlattenAllNeurons()) {
        const double o = onset[n->GetId()] / 0.05, l = late[n->GetId()] / 1.95;
        if (n->GetRole() == metabrain::NeuronRole::RootSensory) { peak = std::max(peak, o); sustained = std::max(sustained, l); thal_sus.push_back(l); }
        else { ctx_sustained += l; ++nctx; }
    }
    ctx_sustained /= std::max(1, nctx);
    std::sort(thal_sus.begin(), thal_sus.end());
    const double thal_med = thal_sus[thal_sus.size() / 2];
    std::printf("    driven thalamus: onset peak %.0f Hz, sustained median %.0f / max %.0f Hz | cortex sustained mean %.1f Hz\n",
                peak, thal_med, sustained, ctx_sustained);
    REQUIRE(peak < 500.0, "onset rate " << peak << " Hz exceeds the physical (refractory) limit");
    REQUIRE(sustained <= 100.0, "sustained thalamic runaway: " << sustained << " Hz (> 100) over 2 s");
    REQUIRE(thal_med >= 35.0, "driven thalamus too suppressed: median " << thal_med << " Hz (< 35), network would ignore input");
    return true;
}

}  // namespace

int main() {
    struct T { const char* name; bool (*fn)(); };
    const T tests[] = {
        {"RS adapts", TestRegularSpikingAdapts},
        {"FS fast, non-adapting", TestFastSpikingNoAdaptation},
        {"CH bursts", TestChatteringBursts},
        {"f-I curve", TestFICurveMonotonic},
        {"conduction delay", TestConductionDelay},
        {"GABA hyperpolarises", TestInhibitoryHyperpolarises},
        {"STDP window", TestStdpWindow},
        {"dopamine gates learning", TestDopamineGatesLearning},
        {"MetaBrain graph on spiking engine", TestEngineSpikingIntegration},
        {"resting-rate bands", TestRestingRateBands},
        {"driven peak vs sustained", TestDrivenPeakVsSustained},
    };
    int failed = 0;
    for (const auto& t : tests) {
        const bool ok = t.fn();
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
        failed += ok ? 0 : 1;
    }
    std::printf("%d/%zu biological validation tests passed\n",
                static_cast<int>(sizeof(tests) / sizeof(T)) - failed, sizeof(tests) / sizeof(T));
    return failed == 0 ? 0 : 1;
}
