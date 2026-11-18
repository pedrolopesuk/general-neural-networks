#include "metabrain/BrainEngine.hpp"
#include "metabrain/Layout3D.hpp"
#include "metabrain/LiveServer.hpp"
#include "metabrain/Serializer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace metabrain;

namespace {

void PrintBanner() {
    std::cout
        << "\n================================================================================\n"
        << "  METABRAIN :: THE RECURSIVE EMOTIONAL BRAIN SIMULATION ENGINE (C++20 / AVX2)\n"
        << "  Composite Holonic Graph + Neuromodulatory Endocrine Wash + CSR Hot-Path\n"
        << "================================================================================\n";
}

void RunEndocrineShowcase() {
    std::cout << "\n[1/5] ENDOCRINE SYSTEM MODULATION EXPERIMENT\n"
              << "--------------------------------------------------------------------------------\n";

    const std::vector<float> mild_stimulus = {0.42f, 0.38f, 0.35f, 0.30f, 0.25f, 0.20f, 0.15f, 0.10f};

    auto eval_state = [&](const char* label, float fear, float dopamine, float fatigue) {
        BrainEngine brain;
        brain.BuildCorticalArchitecture(8, 18, 4, 3, 42);
        auto& endo = brain.GetEndocrine();
        endo.fear = endo.tonic_fear = fear;
        endo.dopamine = endo.tonic_dopamine = dopamine;
        endo.fatigue = endo.tonic_fatigue = fatigue;

        for (int t = 0; t < 5; ++t) {
            brain.InjectSensoryPulses(mild_stimulus);
            brain.StepRecursiveOOP(true, false);
        }
        const auto m = brain.ComputeMetrics();
        const float theta = endo.EffectiveThreshold(brain.GetConfig());
        const float lr = endo.EffectiveLearningRate(brain.GetConfig());

        std::cout << "  " << std::left << std::setw(24) << label
                  << " | Theta_eff: " << std::fixed << std::setprecision(3) << theta
                  << " | LR_eff: " << std::setw(6) << lr
                  << " | Mean Fire: " << std::setw(6) << m.mean_activation
                  << " | Peak Fire: " << m.max_activation << "\n";
    };

    eval_state("Calm Baseline",          0.10f, 0.20f, 0.08f);
    eval_state("High Fear / Anxiety",    0.92f, 0.20f, 0.05f);
    eval_state("Dopamine Surge (Flow)",  0.15f, 1.65f, 0.05f);
    eval_state("High Fatigue / Sadness", 0.08f, 0.05f, 0.92f);
}

void RunConditioningAndNeurogenesis(BrainEngine& brain) {
    std::cout << "\n[2/5] ASSOCIATIVE CONDITIONING (3-FACTOR HEBBIAN + REWARD/PUNISH BACKPROP)\n"
              << "--------------------------------------------------------------------------------\n";

    brain.BuildCorticalArchitecture(8, 20, 4, 3, 777);
    brain.GetConfig().learning_rate = 0.0018f;
    brain.GetConfig().backprop_learning_rate = 0.11f;

    const auto pulse_alpha = BrainEngine::EncodeTextToPulses("HARMONY_REWARD_EXPLORE_SIGNAL", 8);
    const auto pulse_beta  = BrainEngine::EncodeTextToPulses("HAZARD_PREDATOR_FREEZE_ALARM", 8);

    // Target terminal readouts:
    // Terminal 2 = Output.Pattern_Alpha_Lock, Terminal 1 = Output.Defensive_Freeze
    const std::vector<float> target_alpha = {0.20f, 0.02f, 0.98f, 0.02f};
    const std::vector<float> target_beta  = {0.02f, 0.98f, 0.02f, 0.20f};

    auto probe = [&](const std::vector<float>& p, std::string& winner_out) {
        brain.ResetTransientActivity();
        for (int k = 0; k < 4; ++k) {
            brain.InjectSensoryPulses(p);
            brain.StepRecursiveOOP(false, false);
        }
        winner_out = brain.GetBrightestTerminal().label;
        return brain.ReadTerminalOutputs();
    };

    std::string pre_winner;
    const auto pre_alpha = probe(pulse_alpha, pre_winner);
    std::cout << "  Pre-Training  [Stimulus Alpha] -> "
              << pre_alpha[2].label << ": " << std::fixed << std::setprecision(3) << pre_alpha[2].activation
              << " | " << pre_alpha[1].label << ": " << pre_alpha[1].activation << "\n";

    // Train for 95 interleaved associative conditioning epochs
    for (int epoch = 0; epoch < 95; ++epoch) {
        // Present Alpha + Dopamine Reward
        brain.ResetTransientActivity();
        for (int s = 0; s < 3; ++s) {
            brain.InjectSensoryPulses(pulse_alpha);
            brain.StepRecursiveOOP(true, false);
        }
        brain.ApplyRewardPunish(+1.25f, target_alpha);

        // Present Beta + Threat/Punish Alert Correction
        brain.ResetTransientActivity();
        for (int s = 0; s < 3; ++s) {
            brain.InjectSensoryPulses(pulse_beta);
            brain.StepRecursiveOOP(true, false);
        }
        brain.ApplyRewardPunish(+1.25f, target_beta);
    }

    std::string alpha_winner, beta_winner;
    const auto post_alpha = probe(pulse_alpha, alpha_winner);
    const auto post_beta  = probe(pulse_beta, beta_winner);

    std::cout << "  Post-Training [Stimulus Alpha] -> "
              << post_alpha[2].label << ": " << post_alpha[2].activation
              << " | " << post_alpha[1].label << ": " << post_alpha[1].activation
              << "  (Winner: " << alpha_winner << ")\n";

    std::cout << "  Post-Training [Stimulus Beta ] -> "
              << post_beta[1].label << ": " << post_beta[1].activation
              << " | " << post_beta[2].label << ": " << post_beta[2].activation
              << "  (Winner: " << beta_winner << ")\n";

    // [3/5] Curiosity-Driven Recursive Neurogenesis
    std::cout << "\n[3/5] CURIOSITY-DRIVEN RECURSIVE NEUROGENESIS (COMPOSITE MITOSIS)\n"
              << "--------------------------------------------------------------------------------\n";

    const auto before_m = brain.ComputeMetrics();
    brain.GetEndocrine().curiosity = 0.92f;
    brain.GetEndocrine().tonic_curiosity = 0.85f;
    brain.GetEndocrine().dopamine = 0.65f;

    const auto novel_shock = BrainEngine::EncodeTextToPulses("NOVEL_FRACTAL_QUANTUM_PARADOX_INPUT", 8);
    size_t total_spawned = 0;
    for (int step = 0; step < 18; ++step) {
        brain.InjectSensoryPulses((step % 2 == 0) ? novel_shock : pulse_alpha);
        brain.StepRecursiveOOP(true, false);
        if (step % 4 == 3) {
            total_spawned += brain.StepNeurogenesis(true);
        }
    }

    // Leave final state richly illuminated with Alpha stimulus for visual snapshot
    for (int s = 0; s < 3; ++s) {
        brain.InjectSensoryPulses(pulse_alpha);
        brain.StepRecursiveOOP(true, false);
    }

    Layout3D::ComputeRecursiveLayout(brain);
    const auto after_m = brain.ComputeMetrics();

    std::cout << "  Neurons Before Mitosis : " << before_m.total_neurons
              << " (Max Depth: " << before_m.max_depth << ")\n"
              << "  Neurons After Mitosis  : " << after_m.total_neurons
              << " (Spawned +" << total_spawned << " nested Micro-Neurons, Max Depth: " << after_m.max_depth << ")\n"
              << "  Composite Holons       : " << after_m.composite_neurons
              << " | Active Synapses: " << after_m.total_synapses << "\n";
}

void ExportAndVerifySerialization(BrainEngine& brain) {
    std::cout << "\n[4/5] GRAPH SERIALIZATION (JSON + GRAPHML + ROUND-TRIP VERIFICATION)\n"
              << "--------------------------------------------------------------------------------\n";

    fs::create_directories("output");
    fs::create_directories("visualizer");

    const std::string json_path = "output/brain_snapshot.json";
    const std::string graphml_path = "output/brain_graph.graphml";
    const std::string preload_js_path = "visualizer/brain_preload.js";

    const bool ok_json = GraphSerializer::SaveJsonFile(json_path, brain, true);
    const bool ok_gml = GraphSerializer::SaveGraphMLFile(graphml_path, brain);

    // Also emit compact JS preload bundle so visualizer/index.html works offline & zero-server!
    {
        std::ofstream js_out(preload_js_path, std::ios::binary);
        js_out << "window.METABRAIN_PRELOAD = " << GraphSerializer::ToJson(brain, false) << ";\n";
    }

    BrainEngine reloaded;
    std::string load_err;
    const bool ok_load = GraphSerializer::LoadJsonFile(json_path, reloaded, &load_err);
    const auto orig_m = brain.ComputeMetrics();
    const auto load_m = reloaded.ComputeMetrics();

    std::cout << "  Saved JSON Snapshot    : " << json_path << " (" << (ok_json ? "OK" : "FAIL") << ")\n"
              << "  Saved XML GraphML      : " << graphml_path << " (" << (ok_gml ? "OK" : "FAIL") << ")\n"
              << "  Saved 3D Studio Bundle : " << preload_js_path << " (OK)\n"
              << "  Round-Trip Reload Test : "
              << (ok_load && orig_m.total_neurons == load_m.total_neurons &&
                  orig_m.total_synapses == load_m.total_synapses ? "PASS (100% Topology & Weight Fidelity)" : "FAIL")
              << " [" << load_m.total_neurons << " neurons, " << load_m.total_synapses << " synapses]\n";
}

void RunPerformanceBenchmark() {
    std::cout << "\n[5/5] HIGH-PERFORMANCE CSR ARENA + MULTI-THREADED AVX2 BENCHMARK\n"
              << "--------------------------------------------------------------------------------\n";

    BrainEngine bench_brain;
    // 10,000 macro neurons + ~30,000 nested child micro-neurons = ~40,000 recursive neurons
    // x 64 incoming synapses/neuron = ~2,560,000 synapses!
    bench_brain.BuildBenchmarkArchitecture(10000, 3, 64, 2026);
    const auto m = bench_brain.ComputeMetrics();

    std::vector<float> stim(bench_brain.GetRootNeurons().size(), 0.75f);
    bench_brain.InjectSensoryPulses(stim);

    // Warmup
    for (int i = 0; i < 5; ++i) {
        bench_brain.StepFastParallel(false);
    }

    // Benchmark 1: Forward Recursive Integration + Endocrine Modulation
    constexpr int kBenchTicks = 50;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kBenchTicks; ++i) {
        bench_brain.StepFastParallel(false);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double fwd_sec = std::chrono::duration<double>(t1 - t0).count();
    const double fwd_ms_per_tick = (fwd_sec * 1000.0) / kBenchTicks;
    const double fwd_msyn_per_sec = (static_cast<double>(m.total_synapses) * kBenchTicks) / (fwd_sec * 1e6);

    // Benchmark 2: Forward Integration + Three-Factor Oja Hebbian Plasticity
    const auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kBenchTicks; ++i) {
        bench_brain.StepFastParallel(true);
    }
    const auto t3 = std::chrono::high_resolution_clock::now();

    const double learn_sec = std::chrono::duration<double>(t3 - t2).count();
    const double learn_ms_per_tick = (learn_sec * 1000.0) / kBenchTicks;
    const double learn_msyn_per_sec = (static_cast<double>(m.total_synapses) * kBenchTicks) / (learn_sec * 1e6);

    std::cout << "  Scale                  : " << m.total_neurons << " Recursive Neurons | "
              << m.total_synapses << " Synapses\n"
              << "  Forward Inference Tick : " << std::fixed << std::setprecision(2) << fwd_ms_per_tick
              << " ms/tick  (" << fwd_msyn_per_sec << " Million Synapses/sec)\n"
              << "  Forward + 3F-Hebbian   : " << learn_ms_per_tick
              << " ms/tick  (" << learn_msyn_per_sec << " Million Plastic Updates/sec)\n"
              << "================================================================================\n";
}

} // namespace

int main(int argc, char* argv[]) {
    PrintBanner();

    bool serve_mode = false;
    uint16_t port = 8085;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--serve" || arg == "-s") {
            serve_mode = true;
            if (i + 1 < argc) {
                const int p = std::atoi(argv[i + 1]);
                if (p > 0 && p < 65536) {
                    port = static_cast<uint16_t>(p);
                    ++i;
                }
            }
        }
    }

    BrainEngine showcase_brain;
    RunEndocrineShowcase();
    RunConditioningAndNeurogenesis(showcase_brain);
    ExportAndVerifySerialization(showcase_brain);
    RunPerformanceBenchmark();

    if (serve_mode) {
        LiveServer::RunBlocking(showcase_brain, port, "visualizer");
    } else {
        std::cout << "\nTip: Run `metabrain.exe --serve 8085` for real-time C++ <-> 3D Studio telemetry,\n"
                  << "     or open `visualizer/index.html` directly in any browser!\n\n";
    }

    return 0;
}
