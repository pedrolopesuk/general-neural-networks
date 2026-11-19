// E/I balance sweep: input conductance target G x I/E ratio x feedforward-inhibition gain.
// Reports rest median, cortical response at drive 0.3/0.6/0.9, readout rate at 0.9.
#include "metabrain/BrainEngine.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace metabrain;

struct R { double rest_med, c3, c6, c9, term9, max9; };

static void Rates(BrainEngine& b, int ms, float drive, std::vector<double>& hz) {
    std::vector<int> cnt;
    for (int t = 0; t < ms; ++t) {
        if (drive > 0) b.InjectSensoryPulses(std::vector<float>(8, drive));
        b.StepSpiking(1.0f);
        const auto& c = b.GetLastSpikeCountsAll();
        cnt.resize(c.size());
        for (size_t i = 0; i < c.size(); ++i) cnt[i] += c[i];
    }
    hz.assign(cnt.size(), 0.0);
    for (size_t i = 0; i < cnt.size(); ++i) hz[i] = cnt[i] * 1000.0 / ms;
}

R Run(float G, float ratio, float ff) {
    R r{};
    for (int phase = 0; phase < 4; ++phase) {
        BrainEngine b;
        b.BuildCorticalArchitecture(8, 18, 4, 3, 42);
        spiking::SpikingConfig c;
        c.input_conductance_target = G;
        c.inh_exc_ratio = ratio;
        c.feedforward_inh_gain = ff;
        b.SetSpikingConfig(c);
        b.SetSpikingMode(true);
        for (int i = 0; i < 500; ++i) b.StepSpiking(1.0f);
        const float drive = phase == 0 ? 0.0f : (phase == 1 ? 0.3f : (phase == 2 ? 0.6f : 0.9f));
        std::vector<double> hz;
        Rates(b, phase == 0 ? 8000 : 2000, drive, hz);
        const auto all = b.FlattenAllNeurons();
        std::vector<double> g;
        double ctx = 0, term = 0, mx = 0;
        int nc = 0, nt = 0;
        for (size_t i = 0; i < all.size(); ++i) {
            g.push_back(hz[i]);
            mx = std::max(mx, hz[i]);
            const auto role = all[i]->GetRole();
            if (role == NeuronRole::TerminalReadout) { term += hz[i]; ++nt; }
            else if (role != NeuronRole::RootSensory) { ctx += hz[i]; ++nc; }
        }
        ctx /= nc; term /= nt;
        std::sort(g.begin(), g.end());
        if (phase == 0) r.rest_med = g[g.size() / 2];
        if (phase == 1) r.c3 = ctx;
        if (phase == 2) r.c6 = ctx;
        if (phase == 3) { r.c9 = ctx; r.term9 = term; r.max9 = mx; }
    }
    return r;
}

int main() {
    std::printf("   G   ratio  ff  | rest med | cortex @0.3  @0.6  @0.9 | readout @0.9 | max @0.9 | ok\n");
    for (float G : {0.0f, 0.15f, 0.3f, 0.5f})
        for (float ratio : {4.0f, 6.0f})
            for (float ff : {0.5f, 1.0f, 2.0f}) {
                const R r = Run(G, ratio, ff);
                const bool ok = r.rest_med >= 0.5 && r.rest_med <= 3.0 && r.c3 < r.c6 && r.c6 < r.c9 && r.max9 < 150 && r.term9 >= 5.0;
                std::printf("%5.2f  %4.1f  %3.1f | %6.2f   | %8.2f %6.2f %6.2f | %10.2f   | %6.0f   | %s\n",
                            G, ratio, ff, r.rest_med, r.c3, r.c6, r.c9, r.term9, r.max9, ok ? "YES" : "");
            }
    return 0;
}
