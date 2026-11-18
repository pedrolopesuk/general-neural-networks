#pragma once

#include "metabrain/BrainEngine.hpp"

namespace metabrain {

struct LayoutConfig {
    uint32_t macro_iterations = 75;
    uint32_t micro_iterations = 40;
    float macro_radius = 155.0f;
    float child_orbit_base = 28.0f;
    float child_scale_decay = 0.36f;
};

class Layout3D {
public:
    /**
     * Computes multi-scale 3D Force-Directed positions for Macro top-level
     * neurons and recursively nests child micro-circuits inside parent holons
     * to support seamless Infinite Zoom (Macro -> Cluster -> Micro).
     */
    static void ComputeRecursiveLayout(BrainEngine& engine,
                                       const LayoutConfig& cfg = {});

    /**
     * Real-time incremental 3D Force-Directed physics step:
     * Potentiated Hebbian synapses pull co-active neurons closer while Coulombic
     * repulsion and recursive parent-holon shells maintain multi-scale clarity.
     */
    static void StepDynamicForces(BrainEngine& engine,
                                  float dt = 0.045f,
                                  const LayoutConfig& cfg = {});
};

} // namespace metabrain
