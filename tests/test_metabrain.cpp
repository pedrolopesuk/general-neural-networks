#include "metabrain/BrainEngine.hpp"
#include "metabrain/Layout3D.hpp"
#include "metabrain/SensoryPipeline.hpp"
#include "metabrain/Serializer.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace metabrain;

#define REQUIRE(cond, msg)                                                        \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " << msg << "\n"; \
            return false;                                                         \
        }                                                                         \
    } while (0)

namespace {

bool TestSmartPointerZeroLeakRecurrence() {
    std::weak_ptr<Neuron> weak_a, weak_b, weak_child;
    {
        auto a = Neuron::Create(1, 0.1f, NeuronRole::CorticalComposite, "NodeA");
        auto b = Neuron::Create(2, 0.1f, NeuronRole::CorticalComposite, "NodeB");
        auto child = a->AddChild(3, 0.05f, "ChildA1");

        // Create mutual recurrent cycle A <-> B and Child <-> B
        a->ConnectTo(b, 0.8f);
        b->ConnectTo(a, 0.7f);
        child->ConnectTo(b, 0.5f);
        b->ConnectTo(child, 0.4f);

        weak_a = a;
        weak_b = b;
        weak_child = child;
        REQUIRE(!weak_a.expired() && !weak_b.expired() && !weak_child.expired(),
                "Nodes must be alive inside scope");
    }
    REQUIRE(weak_a.expired(), "Neuron A leaked due to cyclic reference!");
    REQUIRE(weak_b.expired(), "Neuron B leaked due to cyclic reference!");
    REQUIRE(weak_child.expired(), "Child Neuron leaked!");
    return true;
}

bool TestCompositeHierarchyAndEndocrineModulation() {
    BrainConfig cfg;
    EndocrineState calm;
    calm.fear = 0.05f;
    calm.fatigue = 0.05f;

    EndocrineState fearful = calm;
    fearful.fear = 0.95f;

    EndocrineState fatigued = calm;
    fatigued.fatigue = 0.95f;

    REQUIRE(fearful.EffectiveThreshold(cfg) < calm.EffectiveThreshold(cfg),
            "Fear must lower effective firing threshold");
    REQUIRE(fatigued.EffectiveThreshold(cfg) > calm.EffectiveThreshold(cfg),
            "Fatigue must raise effective firing threshold");

    auto run_probe = [&](const EndocrineState& state) {
        auto root = Neuron::Create(1, 0.0f, NeuronRole::RootSensory, "R");
        auto comp = Neuron::Create(2, 0.05f, NeuronRole::CorticalComposite, "C");
        uint64_t next_id = 10;
        comp->SpawnMicroCircuit(3, next_id, 99);
        comp->ConnectFrom(root, 0.55f);

        root->SetExternalInput(0.45f);
        root->LatchActivation();
        comp->LatchActivation();
        root->ProcessSignal(cfg, state);
        root->LatchActivation();
        return comp->ProcessSignal(cfg, state);
    };

    const float act_calm = run_probe(calm);
    const float act_fear = run_probe(fearful);
    const float act_fatigue = run_probe(fatigued);

    REQUIRE(act_fear > act_calm, "Fearful state must increase hyper-reactivity");
    REQUIRE(act_fatigue < act_calm, "Fatigued state must dampen firing output");
    return true;
}

bool TestOjaStabilizedHebbianAndDopamine() {
    BrainConfig cfg;
    EndocrineState low_da, high_da;
    low_da.dopamine = 0.0f;
    high_da.dopamine = 1.6f;

    auto train_pair = [&](const EndocrineState& endo, int steps) {
        auto pre = Neuron::Create(1, 0.0f, NeuronRole::RootSensory);
        auto post = Neuron::Create(2, 0.2f, NeuronRole::CorticalComposite);
        post->ConnectFrom(pre, 0.20f);

        for (int i = 0; i < steps; ++i) {
            pre->SetExternalInput(0.95f);
            pre->LatchActivation();
            post->LatchActivation();
            pre->ProcessSignal(cfg, endo);
            post->ProcessSignal(cfg, endo);
            post->Learn(cfg, endo, 1.0f);
        }
        return post->GetSynapses()[0].weight;
    };

    const float w_low = train_pair(low_da, 8);
    const float w_high = train_pair(high_da, 8);
    REQUIRE(w_high > w_low, "Dopamine boost must accelerate synaptic consolidation");

    // Verify Oja multiplicative homeostatic stabilization prevents explosion over 600 ticks
    const float w_long = train_pair(high_da, 600);
    REQUIRE(std::isfinite(w_long) && w_long <= cfg.weight_clip,
            "Oja stabilization + clipping must prevent runaway weight explosion");
    return true;
}

bool TestRewardPunishBackpropAndJsonRoundTrip() {
    BrainEngine engine;
    engine.BuildCorticalArchitecture(6, 12, 3, 2, 123);

    const auto stim = BrainEngine::EncodeTextToPulses("TEST_STIMULUS_VECTOR", 6);
    const std::vector<float> target = {0.95f, 0.05f, 0.05f};

    engine.InjectSensoryPulses(stim);
    engine.StepRecursiveOOP(false, false);
    const float initial_t0 = engine.ReadTerminalOutputs()[0].activation;

    for (int epoch = 0; epoch < 30; ++epoch) {
        engine.InjectSensoryPulses(stim);
        engine.StepRecursiveOOP(true, false);
        engine.ApplyRewardPunish(1.2f, target);
    }

    engine.InjectSensoryPulses(stim);
    engine.StepRecursiveOOP(false, false);
    const float trained_t0 = engine.ReadTerminalOutputs()[0].activation;
    REQUIRE(trained_t0 > initial_t0, "Reward backpropagation must strengthen target terminal readout");

    // Test Neurogenesis
    const size_t before_n = engine.ComputeMetrics().total_neurons;
    const size_t spawned = engine.StepNeurogenesis(true);
    REQUIRE(spawned > 0 && engine.ComputeMetrics().total_neurons == before_n + spawned,
            "Neurogenesis must dynamically add child neurons");

    Layout3D::ComputeRecursiveLayout(engine);

    // Test JSON Round-Trip
    const std::string json_blob = GraphSerializer::ToJson(engine, true);
    REQUIRE(!json_blob.empty(), "Serialized JSON must not be empty");

    BrainEngine restored;
    std::string err;
    REQUIRE(GraphSerializer::FromJson(json_blob, restored, &err),
            std::string("JSON deserialization failed: ") + err);

    const auto m1 = engine.ComputeMetrics();
    const auto m2 = restored.ComputeMetrics();
    REQUIRE(m1.total_neurons == m2.total_neurons, "Restored neuron count mismatch");
    REQUIRE(m1.total_synapses == m2.total_synapses, "Restored synapse count mismatch");
    REQUIRE(m1.max_depth == m2.max_depth, "Restored max recursion depth mismatch");

    // Test CSR Fast Parallel Step
    restored.InjectSensoryPulses(stim);
    restored.StepFastParallel(true);
    restored.SyncArenaToGraph();
    REQUIRE(restored.GetTick() == m2.tick + 1, "CSR parallel step must advance tick");

    const std::string gml = GraphSerializer::ToGraphML(restored);
    REQUIRE(gml.find("<graphml") != std::string::npos, "GraphML output must contain <graphml> root");
    return true;
}

bool TestMultiModalSensoryAndStructuralDynamics() {
    // 1. Verify 16x16 Retina Image Grid encoding differentiates horizontal vs vertical bars
    std::vector<float> horiz(16 * 16, 0.0f);
    std::vector<float> vert(16 * 16, 0.0f);
    for (uint32_t i = 0; i < 16; ++i) {
        horiz[7 * 16 + i] = 1.0f;
        horiz[8 * 16 + i] = 1.0f;
        vert[i * 16 + 7] = 1.0f;
        vert[i * 16 + 8] = 1.0f;
    }
    const auto h_feat = SensoryPipeline::EncodeImageGrid(horiz, 16, 16, 8);
    const auto v_feat = SensoryPipeline::EncodeImageGrid(vert, 16, 16, 8);
    REQUIRE(h_feat[4] > v_feat[4] && v_feat[5] > h_feat[5],
            "Image encoder must distinguish horizontal vs vertical edge orientations");

    // 2. Verify Dynamic Force Physics, Synaptogenesis & Deep Neuron Inspector JSON
    BrainEngine engine;
    engine.BuildCorticalArchitecture(8, 14, 4, 2, 555);
    Layout3D::ComputeRecursiveLayout(engine);

    engine.SetSustainedSensoryPulses(h_feat, 6);
    for (int i = 0; i < 6; ++i) {
        engine.StepRecursiveOOP(true, false);
        Layout3D::StepDynamicForces(engine, 0.04f);
    }
    REQUIRE(!engine.GetTelemetryHistory().empty(), "Telemetry history must record samples");

    auto [formed, pruned] = engine.StepSynaptogenesisAndPruning(4);
    (void)pruned;
    REQUIRE(formed <= 4, "Synaptogenesis must respect cap");

    const uint64_t sample_id = engine.GetTopLevelNeurons()[9]->GetId();
    const std::string detail = GraphSerializer::NeuronDetailToJson(engine, sample_id);
    REQUIRE(detail.find("\"incoming_synapses\"") != std::string::npos &&
            detail.find("\"children\"") != std::string::npos,
            "NeuronDetailToJson must include composite children and incoming connection weights");
    return true;
}

} // namespace

int main() {
    std::cout << "Running MetaBrain Unit & Integration Verification Suite...\n";
    int passed = 0;
    const int total = 5;

    if (TestSmartPointerZeroLeakRecurrence()) {
        std::cout << "  [PASS] 1/5 Smart Pointer Cycle-Free Memory Safety\n";
        ++passed;
    }
    if (TestCompositeHierarchyAndEndocrineModulation()) {
        std::cout << "  [PASS] 2/5 Recursive Composite Signal & Endocrine Modulation\n";
        ++passed;
    }
    if (TestOjaStabilizedHebbianAndDopamine()) {
        std::cout << "  [PASS] 3/5 Three-Factor Oja-Stabilized Hebbian & Dopamine Gate\n";
        ++passed;
    }
    if (TestRewardPunishBackpropAndJsonRoundTrip()) {
        std::cout << "  [PASS] 4/5 Reward/Punish Backprop, Neurogenesis, CSR & JSON/GraphML Round-Trip\n";
        ++passed;
    }
    if (TestMultiModalSensoryAndStructuralDynamics()) {
        std::cout << "  [PASS] 5/5 Multi-Modal Sensory Retina, 3D Force Physics, Synaptogenesis & Deep Inspector\n";
        ++passed;
    }

    std::cout << "Result: " << passed << "/" << total << " test suites passed.\n";
    return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
