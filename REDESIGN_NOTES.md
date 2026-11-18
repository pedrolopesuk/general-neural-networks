# MetaBrain Unreal Engine 5.8 Visualizer — Architecture & Redesign Notes

## 1. Existing C++ Simulation Engine Audit

### 1.1 Location & Untouched Files
The core simulation engine lives in standard C++20 with zero external engine dependencies:
- **Headers (`include/metabrain/`)**:
  - `Config.hpp`: Defines `Vec3`, `BrainConfig` (`learning_rate`, `backprop_learning_rate`, `eligibility_decay`, `oja_stabilization`, `weight_clip`, `base_threshold`, `base_gain`, `membrane_leak`, `composite_child_mix`, neuromodulator sensitivities, and `mature_memory_ticks`), and `EndocrineState` (`fear`, `dopamine`, `fatigue`, `curiosity` plus tonic baselines, `StepHomeostasis()`, `EffectiveThreshold()`, `EffectiveGain()`, `EffectiveLearningRate()`, and `AmplitudeScale()`).
  - `Neuron.hpp` / `src/Neuron.cpp`: Implements the Composite pattern (`Neuron : public std::enable_shared_from_this<Neuron>`). Owns `children_` (`std::vector<std::shared_ptr<Neuron>>`) and stores cycle-free `incoming_synapses_` (`std::weak_ptr<Neuron>`). Core methods: `AddChild()`, `SpawnMicroCircuit()`, `PruneWeakSynapses()`, `ProcessSignal()`, and Oja-stabilized 3-factor Hebbian `Learn()`.
  - `SensoryPipeline.hpp` / `src/SensoryPipeline.cpp`: Transduces text (`EncodeText`), 2D retina grids (`EncodeImageGrid`), and manual `[0..1]` float arrays (`NormalizeManualPulses`) into Root Sensory Neuron currents.
  - `BrainEngine.hpp` / `src/BrainEngine.cpp`: Orchestrates graph creation (`BuildCorticalArchitecture`, `BuildBenchmarkArchitecture`), stepping (`StepRecursiveOOP` and multi-threaded CSR `StepFastParallel`), feedback backpropagation (`ApplyRewardPunish`), structural growth (`StepNeurogenesis`, `StepSynaptogenesisAndPruning`), terminal readouts (`ReadTerminalOutputs`), telemetry history (`GetTelemetryHistory`), and throughput benchmarking (`RunBenchmarkSuite`).
  - `Serializer.hpp` / `src/Serializer.cpp`: Lossless JSON (`ToJson`, `FromJson`, `SaveJsonFile`, `LoadJsonFile`) and XML GraphML (`ToGraphML`, `SaveGraphMLFile`) serialization.

> **Hard Invariant**: None of the simulation math, activation equations, Hebbian/Oja plasticity rules, endocrine couplings, reward/punish backpropagation, or JSON/GraphML formats are modified.

---

## 2. Public Engine API & Tick Model

### 2.1 Tick Model
- `BrainEngine` advances discretely one tick per `StepRecursiveOOP()` (for structural/interactive cortical graphs) or `StepFastParallel()` (compiled CSR + worker pool for `1,000–10,000+` neuron high-scale runs).
- `BrainEngine` itself is single-owner and not internally synchronized; concurrent readers on the Unreal Game Thread must never touch `Neuron` pointers while a tick or structural mutation (`AddChild` / `StepSynaptogenesisAndPruning`) is running.

### 2.2 `MetaBrainCore` Bridge Architecture (No `UObject` Dependencies)
`Source/MetaBrainCore` wraps `metabrain::BrainEngine` using only Unreal's `Core` module (`FRunnable`, `FRunnableThread`, `TQueue`, `FCriticalSection`, `TSharedPtr`):
1. **Worker Thread (`FMetaBrainSimWorker : public FRunnable`)**:
   - Runs at a configurable tick rate (default `18 Hz`, decoupled completely from UE frame rate; can also switch to `1,000` or `10,000` neuron scale on demand).
   - Drains a lock-free MPSC command queue (`TQueue<FMetaBrainSimCommand, EQueueMode::Mpsc>`) before each tick to apply UI commands (`SetEndocrine`, `InjectText`, `InjectRetina`, `InjectManual`, `RewardPunish`, `AddChild`, `WireCoActive`, `SetAutoGrow`, `SetPaused`, `SaveSnapshot`, `RunBenchmark`, `ResetGraph`).
2. **Deterministic 3D Hierarchical Sphere Packing + Local Force Relaxation (`FMetaBrainSpherePacker`)**:
   - Groups top-level cortical columns into Region Holons (`Hippocampus`, `Association`, `Prefrontal`, `Amygdala`) and recursively packs child spheres inside parent holon spheres (`Region -> Column -> Depth 1..3 Composite Sub-Holons -> Leaf Neurons`), followed by bounded deterministic force relaxation inside each parent shell.
   - Layout recomputes only when topology changes (`topology_revision` increments), guaranteeing **zero inter-frame position jitter**.
3. **Double-Buffered Immutable Snapshot (`FMetaBrainSnapshot`)**:
   - At the end of each worker tick, `FMetaBrainSimWorker` constructs a `TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe>` containing flat contiguous arrays of:
     - `Neurons` (`id`, `parent_id`, `depth`, `role`, `position_cm`, `radius_cm`, `activation`, `bias`, `potential`, `maturity`, `age_ticks`, `child_count`, `did_fire_this_tick`, `last_fire_time_sec`, `label`)
     - `Edges` (`source_idx`, `target_idx`, `source_id`, `target_id`, `weight`, `abs_weight_rank_pct`, `carrying_signal`)
     - `Holons` (`key`, `parent_key`, `label`, `level`, `neuron_id`, `center_cm`, `radius_cm`, `aggregate_activity`)
     - `Terminals`, `Endocrine`, `Metrics`, and latest `TraceSample` (`tick`, `act`, `da`, `fear`).
   - Published via an atomic/spin-swapped pointer slot (`LatestSnapshot`). `MetaBrainViz` on the Game Thread grabs a thread-safe `TSharedPtr<const FMetaBrainSnapshot>` in `< 1 µs` and **never blocks the simulation thread**.

---

## 3. `MetaBrainViz` Runtime & Asset Pipeline (`UE 5.8.4`)

| Subsystem | UE 5.8 Implementation |
| :--- | :--- |
| **Neurons (10k+ scale)** | Single `UHierarchicalInstancedStaticMeshComponent` (`HISM`) with `/Engine/BasicShapes/Sphere` (or low-poly icosphere), passing 4 floats of `PerInstanceCustomData`: `[0] Activation`, `[1] AgeMaturity`, `[2] SelectedOrHovered`, `[3] FiringFlashAlpha` (300 ms exponential decay on Game Thread). |
| **Holon Containment** | Dynamic pool of translucent sphere static mesh components (`M_HolonShell` Fresnel rim opacity `3–6%`, zero fill glow) + world-space `UTextRenderComponent` / `UWidgetComponent` labels (`Hippocampus`, `Prefrontal`, `Association`, `Amygdala`, etc.) anchored at the top pole of each shell. |
| **Edges & Signal Pulses** | `UNiagaraComponent` systems (`NS_SynapseRibbons`, `NS_SignalPulses`) fed via `UNiagaraDataInterfaceArrayFunctionLibrary` (`User.EdgeStarts`, `User.EdgeEnds`, `User.EdgeColors`, `User.PulsePositions`), paired with a synchronized `HISM` filament & amber pulse renderer for deterministic crispness across all GPU drivers. Default filter shows top `~20%` of edges by `|w|` and dims non-incident edges to `20%` on hover/select. |
| **Lighting & Post** | `#0B0C0D` dark environment, Lumen GI, single soft key light, Exponential Height Fog with low-density Volumetric Fog, ACES Filmic tonemapping, high `BloomThreshold` (`> 1.2`) so **only emissive amber (`#FFB000`, HDR > 2.5) blooms** and nothing glows at rest, Cinematic DOF tracking focal distance to selected holon/neuron, subtle film grain (`0.08`). |
| **Camera & Zoom** | `AMetaBrainPawn` + `AMetaBrainPlayerController` using `EnhancedInput` (orbit, pan, dolly with smooth velocity damping/inertia, `Esc` step-up, `400 ms` cubic ease-out fly-in across `Macro`, `Cluster`, and `Micro`, fading outer shells out and inner neurons in). |
| **UI & Oscilloscope** | Single `UMetaBrainStyleDataAsset` / `FMetaBrainSlateStyle` (`#131416` flat panels, `1 px #26282B` borders, `2 px` radius, `Inter` sans + `JetBrains Mono` tabular numbers), `UMetaBrainHUDWidget` + custom `SMetaBrainTraceWidget : public SLeafWidget` (`600-tick` ring buffer, `4 × 10` grid, click-to-pause) + screen-space anchored `Inspector` popover. |
| **Reproducible Assets** | All `.uasset` / `.umap` files generated via `/Scripts/build_assets/build_all.py` invoked through `UnrealEditor-Cmd.exe MetaBrainUE.uproject -run=pythonscript -script=...`. |

---

## 4. Unreal Insights & Performance Profiling Log
*(To be populated in Task 10 with measured 1440p frame times at 1,000 neurons / 10,000 edges and 10,000 neurons.)*
