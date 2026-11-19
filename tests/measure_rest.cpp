// Measures true resting firing-rate distribution (no sensory input, no ambient drive)
#include "metabrain/BrainEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

using namespace metabrain;

int main() {
    BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
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
