// Reward-modulated STDP discrimination task on the spiking engine.
//   Stimulus A -> output X (terminal 0), stimulus B -> output Y (terminal 1).
//   Trial: 100 ms stimulus, choice = terminal (X or Y) with more spikes, reward 1 if correct.
//   Then 1 s quiet so dopamine commits that trial's eligibility before the next one.
// Conditions: rpe (critic), shuffled (random reward, RPE), raw (no critic, legacy valence).
#include "metabrain/BrainEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace metabrain;

struct Out { double acc_first, acc_last, w_start, w_end; int ties_last, decided_last; double out_spikes; };

static double SensoryWeightSum(const BrainEngine& b) {
    double s = 0;
    for (const auto& n : b.FlattenAllNeurons())
        for (const auto& syn : n->GetSynapses())
            if (auto src = syn.source.lock(); src && src->GetRole() == NeuronRole::RootSensory) s += std::fabs(syn.weight);
    return s;
}

static float gTarget = 0.0f;

Out RunTask(uint32_t seed, const std::string& mode, int trials) {
    BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, seed);
    brain.SeedRng(seed * 31u + 5u);
    spiking::SpikingConfig sc; sc.input_conductance_target = gTarget; brain.SetSpikingConfig(sc);
    brain.SetSpikingMode(true);
    auto& en = brain.GetEndocrine();
    en.dopamine = en.tonic_dopamine;  // no startup dopamine burst
    for (int i = 0; i < 500; ++i) brain.StepSpiking(1.0f);

    const auto A = BrainEngine::EncodeTextToPulses("HARMONY_REWARD_EXPLORE_SIGNAL", 8);
    const auto B = BrainEngine::EncodeTextToPulses("HAZARD_PREDATOR_FREEZE_ALARM", 8);
    const uint64_t X = brain.GetTerminalNeurons()[0]->GetId(), Y = brain.GetTerminalNeurons()[1]->GetId();
    std::mt19937 rng(seed);
    const double w0 = SensoryWeightSum(brain);
    int correct_first = 0, correct_last = 0, ties_last = 0, decided_first = 0, decided_last = 0;
    long out_spk = 0;
    const int window = trials / 5;

    for (int t = 0; t < trials; ++t) {
        const bool isA = (rng() & 1u) != 0;
        int sx = 0, sy = 0;
        for (int ms = 0; ms < 200; ++ms) {  // 200 ms counting window
            brain.InjectSensoryPulses(isA ? A : B);
            brain.StepSpiking(1.0f);
            const auto& ids = brain.GetLastSpikes();
            const auto& c = brain.GetLastSpikeCounts();
            for (size_t k = 0; k < ids.size(); ++k) { if (ids[k] == X) sx += c[k]; if (ids[k] == Y) sy += c[k]; }
        }
        out_spk += sx + sy;
        const bool tie = (sx == sy);
        const bool choseX = tie ? ((rng() & 1u) != 0) : (sx > sy);
        const bool ok = (choseX == isA);
        // Accuracy counts decided trials only; ties are reported separately
        if (t < window && !tie) { correct_first += ok; ++decided_first; }
        if (t >= trials - window) { if (tie) ++ties_last; else { correct_last += ok; ++decided_last; } }

        float reward = ok ? 1.0f : 0.0f;
        if (mode == "shuffled") reward = (rng() & 1u) ? 1.0f : 0.0f;
        if (mode == "nodopamine") { /* no reward at all */ }
        else if (mode == "raw") brain.GetEndocrine().dopamine = brain.GetEndocrine().tonic_dopamine + 0.65f * 1.2f * (reward * 2.0f - 1.0f) * 0.5f;
        else brain.DeliverReward(reward);  // context = the stimulus still latched in last_sensory_pulses_

        for (int ms = 0; ms < 1000; ++ms) brain.StepSpiking(1.0f);
    }
    auto pct = [](int a, int b) { return b ? 100.0 * a / b : -1.0; };
    return {pct(correct_first, decided_first), pct(correct_last, decided_last), w0, SensoryWeightSum(brain),
            ties_last, decided_last, static_cast<double>(out_spk) / trials};
}

int main(int argc, char** argv) {
    const int trials = argc > 1 ? std::atoi(argv[1]) : 400;
    const uint32_t seeds[] = {42, 7, 1234, 99, 2026};
    const char* only = argc > 2 ? argv[2] : nullptr;
    if (argc > 3) gTarget = static_cast<float>(std::atof(argv[3]));
    std::printf("input_conductance_target = %.3f\n", gTarget);
    for (const char* mode : {"nodopamine", "rpe", "shuffled", "raw"}) {
        if (only && std::strcmp(only, mode) != 0) continue;
        std::printf("\n=== %s (%d trials) ===\nseed  | acc (decided) first -> last | ties/decided last | out spikes/trial | sensory sum|w| start -> end\n", mode, trials);
        for (uint32_t s : seeds) {
            const Out o = RunTask(s, mode, trials);
            std::printf("%5u | %5.1f%% -> %5.1f%%        | %3d / %3d         | %6.2f           | %8.2f -> %8.2f (%+.2f%%)\n", s,
                        o.acc_first, o.acc_last, o.ties_last, o.decided_last, o.out_spikes,
                        o.w_start, o.w_end, 100.0 * (o.w_end - o.w_start) / o.w_start);
        }
    }
    return 0;
}
