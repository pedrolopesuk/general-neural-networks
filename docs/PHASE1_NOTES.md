# Phase 1 — Biophysical spiking substrate: notes and measurements

## What exists
- `SpikingCore` (`include/metabrain/SpikingCore.hpp`, `src/SpikingCore.cpp`):
  - Izhikevich 2003 neurons in 7 cell classes.
  - Conductance synapses: AMPA, NMDA with Mg²⁺ block, GABA_A, GABA_B.
  - Axonal delays.
  - Pair STDP into a 1 s eligibility trace, made permanent by dopamine (three-factor rule, Izhikevich 2007).
  - sAHP adaptation current on thalamic relay cells.
- `BrainEngine::StepSpiking` compiles the MetaBrain graph onto the substrate. It is the default live engine; `POST /api/control {"engine":"rate"}` switches back to the old one.
  - Dale's law per cell.
  - Delays follow 3D distance.
  - Rates and weights are written back to the graph.
  - Time is real: 1 s of wall clock = 1 s of biology, at dt = 0.1 ms.

## Validation (`build2/spiking_tests.exe`, 11 tests)
- **Single cells:**
  - Regular-spiking cells adapt.
  - Fast-spiking cells adapt weakly (1.5× against 8× for regular-spiking).
  - Chattering cells burst.
  - Firing rate rises monotonically with input current.
- **Synapses:**
  - Conduction delays are exact.
  - Inhibitory input hyperpolarises.
  - The STDP window has the right sign and decay.
  - Dopamine gates learning.
- **Network at rest (fixed seed, 20 s):**
  - Median 0.5–3 Hz (measured 1.10).
  - Mean greater than median.
  - 5–40% of cells near-silent (measured 22%).
  - No cell above 50 Hz.
  - Thalamic median 3–20 Hz (measured 5.6).
- **Driven thalamus (2 s at input 0.9):**
  - Onset below 500 Hz (measured 180, a brief peak).
  - Sustained median at least 35 Hz.
  - Sustained maximum at most 100 Hz (measured 73 / 87).

## Runaway fix history (driven thalamus, sustained median / max)

| Step | Median / max |
|---|---|
| Baseline | 234 / 276 Hz |
| + sAHP 0.02 | 95 / 112 Hz (fail) |
| + sAHP 0.03 | 73 / 87 Hz (pass) |

## sAHP robustness sweep (`build2/sweep_ahp.exe`)
Sustained thalamic median / max in Hz, 5 seeds each (42, 7, 1234, 99, 2026). Pass means median ≥ 35 and max ≤ 100.

| AHP | Input 1.0× | Input 1.5× |
|---|---|---|
| 0.020 | 95/112, 0/5 pass | 143/160, 0/5 pass |
| 0.025 | 83/98, 5/5 pass | 124/138, 0/5 pass |
| **0.030** | **74/87, 5/5 pass** | 110/123, 0/5 pass |
| 0.035 | 66/77, 5/5 pass | 98/110, 0/5 pass |
| 0.040 | 60/71, 5/5 pass | 89/100, 3/5 pass |

**Conclusion:**
- At 1.0× input, 0.03 is robust across 0.025–0.04 and across seeds (±2 Hz).
- At 1.5× input, the response scales almost linearly with input and exceeds 100 Hz. Nothing caps the gain. This is not recurrent runaway, since there is no feedback loop involved.

**Consequence:** short-term synaptic depression at the sensory driver input becomes a **Phase 2 requirement**, not a refinement. This matches thalamic biology, where retinogeniculate depression limits relay rates. Feedforward inhibition and synaptic scaling (slow, long-term only) stay as refinements.

## Phase 2 acceptance for short-term depression: the input-output curve
Once short-term depression lands, test the shape of the curve, not one number. Measure the sustained thalamic rate at 0.5×, 1×, 1.5×, 2× and 3× input:
- The rate must rise monotonically with input.
- It must be sublinear: 3× input gives less than 3× the rate.
- It must saturate at roughly 150–200 Hz.

When this passes, replace the fixed 100 Hz ceiling and remove `max_sensory_input`, the interim 0.9 input clamp in `SpikingConfig`.

## Known limits
- 0.03 was tuned against the band; the sweep above shows how much room it has.
- The spike stream is not yet sent to UE (Phase 1, step 3).
- The core is not event-driven yet; it scans every cell and synapse each step (Phase 1, step 4).
