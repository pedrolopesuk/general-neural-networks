// Measures true resting firing-rate distribution (no sensory input, no ambient drive)
#include "metabrain/BrainEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace metabrain;

// Mirrors FMetaBrainSimBridge::InitializeDefaultBrain (the brain UE actually loads)
static void BuildUeBrain(BrainEngine& e) {
    e.BuildCorticalArchitecture(8, 20, 4, 3, 777);
    e.GetConfig().learning_rate = 0.0018f;
    e.GetConfig().backprop_learning_rate = 0.11f;
    const auto A = BrainEngine::EncodeTextToPulses("HARMONY_REWARD_EXPLORE_SIGNAL", 8);
    const auto B = BrainEngine::EncodeTextToPulses("HAZARD_PREDATOR_FREEZE_ALARM", 8);
    const std::vector<float> TA = {0.20f, 0.02f, 0.98f, 0.02f}, TB = {0.02f, 0.98f, 0.02f, 0.20f};
    for (int ep = 0; ep < 65; ++ep) {
        e.ResetTransientActivity();
        for (int s = 0; s < 3; ++s) { e.InjectSensoryPulses(A); e.StepRecursiveOOP(true, false); }
        e.ApplyRewardPunish(1.2f, TA);
        e.ResetTransientActivity();
        for (int s = 0; s < 3; ++s) { e.InjectSensoryPulses(B); e.StepRecursiveOOP(true, false); }
        e.ApplyRewardPunish(1.2f, TB);
    }
    auto& en = e.GetEndocrine();
    en.curiosity = en.tonic_curiosity = 0.50f;
    en.fear = en.tonic_fear = 0.12f;
    en.dopamine = en.tonic_dopamine = 0.25f;
    en.fatigue = en.tonic_fatigue = 0.08f;
    for (int k = 0; k < 4; ++k) e.StepNeurogenesis(true);
}

int main(int argc, char** argv) {
    BrainEngine brain;
    const bool ue = argc > 1 && std::string(argv[1]) == "ue";
    if (ue) BuildUeBrain(brain); else brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
    std::printf("brain: %s\n", ue ? "UE pretrained (seed 777)" : "test network (seed 42)");
    brain.SetSpikingMode(true);
    for (int i = 0; i < 500; ++i) brain.StepSpiking(1.0f);  // 0.5 s settle

    std::unordered_map<uint64_t, int> count;
    const int ms = 20000;  // 20 s of biological time
    for (int i = 0; i < ms; ++i) {
        brain.StepSpiking(1.0f);
        for (auto id : brain.GetLastSpikes()) ++count[id];
    }
    std::map<int, std::vector<double>> by_role;
    std::vector<double> all;
    for (const auto& n : brain.FlattenAllNeurons()) {
        const double hz = count[n->GetId()] / (ms / 1000.0);
        by_role[static_cast<int>(n->GetRole())].push_back(hz);
        all.push_back(hz);
    }
    auto report = [](const char* name, std::vector<double> v) {
        std::sort(v.begin(), v.end());
        double mean = 0; for (double x : v) mean += x; mean /= v.size();
        const double silent = std::count(v.begin(), v.end(), 0.0) * 100.0 / v.size();
        std::printf("%-18s n=%3zu  median %6.2f Hz  mean %6.2f Hz  p90 %6.2f Hz  max %6.2f Hz  silent %4.0f%%\n",
                    name, v.size(), v[v.size() / 2], mean, v[v.size() * 9 / 10], v.back(), silent);
    };
    const char* names[] = {"RootSensory(TC)", "Cortical(IB)", "Micro(CH)", "Terminal(RS)"};
    for (auto& [r, v] : by_role) report(names[r], v);
    report("ALL", all);
    return 0;
}
