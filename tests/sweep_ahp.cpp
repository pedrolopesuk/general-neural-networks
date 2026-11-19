// Robustness sweep for the thalamic sAHP increment: AHP x seed x input strength.
// Pass band (driven, sustained over 2 s): thalamic median >= 35 Hz and max <= 100 Hz.
#include "metabrain/BrainEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>

using namespace metabrain;

struct Result { double med, mx; };

Result Run(float ahp, uint32_t seed, float gain) {
    BrainEngine brain;
    brain.BuildCorticalArchitecture(8, 18, 4, 3, seed);
    brain.SeedRng(seed * 7919u + 1u);
    spiking::SpikingConfig c;
    c.tc_ahp_increment = ahp;
    c.sensory_gain = gain;
    brain.SetSpikingConfig(c);
    brain.SetSpikingMode(true);
    for (int i = 0; i < 500; ++i) brain.StepSpiking(1.0f);
    std::unordered_map<uint64_t, int> late;
    for (int i = 0; i < 2000; ++i) {
        brain.InjectSensoryPulses(std::vector<float>(8, 0.9f));
        brain.StepSpiking(1.0f);
        if (i >= 50) for (auto id : brain.GetLastSpikes()) ++late[id];
    }
    std::vector<double> t;
    for (const auto& r : brain.GetRootNeurons()) t.push_back(late[r->GetId()] / 1.95);
    std::sort(t.begin(), t.end());
    return {t[t.size() / 2], t.back()};
}

int main() {
    const float ahps[] = {0.020f, 0.025f, 0.030f, 0.035f, 0.040f};
    const uint32_t seeds[] = {42, 7, 1234, 99, 2026};
    for (float gain : {12.0f, 18.0f}) {
        std::printf("\n=== input strength %s (sensory gain %.0f) ===\n", gain > 12 ? "1.5x" : "1.0x", gain);
        std::printf("AHP    | per-seed sustained median/max Hz                         | pass\n");
        for (float a : ahps) {
            int pass = 0;
            std::printf("%.3f  |", a);
            for (uint32_t s : seeds) {
                const Result r = Run(a, s, gain);
                const bool ok = r.med >= 35.0 && r.mx <= 100.0;
                pass += ok;
                std::printf(" %3.0f/%3.0f%s", r.med, r.mx, ok ? " " : "*");
            }
            std::printf(" | %d/5\n", pass);
        }
    }
    std::printf("\n(* = outside band: median >= 35 Hz and max <= 100 Hz)\n");
    return 0;
}
