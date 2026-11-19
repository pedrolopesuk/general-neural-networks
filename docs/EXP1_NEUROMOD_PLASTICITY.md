# Experiment 1: neuromodulated encoder plasticity (Benchmark 1b)

Status: **draft spec**. Not run yet; waits for the Benchmark 1b baselines (`eval/run_kkanji.ps1`).

## Question

When the world changes (Omniglot-trained encoder, Kuzushiji-Kanji stream), does a learner gain from changing its own features online? And does gating those changes with a neuromodulator signal help beyond plain online updates?

## Mechanism (`learners.py: NeuromodEncoder`, name `neuromod`)

- **Readout:** nearest class mean (NCM) on L2-normalised features. Class means are running sums, as in `NCM`. Any gain must therefore come from better features, not a better classifier.
- **Plasticity:** on each example `(x, y)`, one SGD step on the encoder parameters, at learning rate `lr · g`:
  - The loss is prototype cross-entropy: `CE(τ · cos(f(x), μ_c) over seen classes c, y)`.
  - For the first shot of a new class, the prototype is the example itself, so the loss pulls it away from old classes only.
- **Neuromodulator `g` ∈ [0, 1]**, computed *before* the update from the current prediction:
  - Surprise (RPE analog): `s = 1 − p(y | x)` under the NCM readout.
  - Novelty (curiosity analog): `n = 1` on the first shot of a new class, else `0`.
  - Gate: `g = σ(k · (s + β·n − θ))`, where `θ` is a running mean of `s`. This mirrors the RPE critic in the spiking engine (baseline-subtracted).
- **Same encoder and seed** as the pretrained track: `encoder_v1_s{seed}`. BatchNorm stays in eval mode, so its running statistics are frozen.
  - This deliberately blocks the AdaBN-style shortcut: any win must come from weight changes.
- **Stale means:** stored means mix features from many encoder states. Both versions run from the start:
  - **v1 (`refresh=none`):** drifting means, with `mean_drift` logged.
  - **v2 (`refresh=exemplar`):** 1 stored image per class, the same memory as replay. Every `R` = 100 examples, each seen class's mean is shifted by the drift of its exemplar's feature: `μ_c += f_now(e_c) − f_stored(e_c)`. The refresh FLOPs are counted.
  - **Oracle refresh (diagnostic, not a competitor):** at the end, every mean is recomputed from all of the class's shots with the final encoder. It measures feature quality with no staleness. If oracle wins and v1 loses, the features improved and only the readout failed.

## Comparisons (pretrained track, Benchmark 1b, 5 seeds)

| Row | What | Purpose |
|---|---|---|
| NCM frozen | baseline | the bar |
| SLDA frozen | baseline | **the bar to beat** |
| Replay (1 exemplar per class, compute-matched) | baseline | gradient competitor |
| `neuromod`, `g ≡ 1` | ablation | encoder updates without gating |
| `neuromod`, `g ≡ ḡ` (constant = gated run's mean g) | ablation | same *total* plasticity, no timing |
| `neuromod`, shuffled g (the gated run's own g sequence, per-seed permutation) | ablation | same distribution and variance, no link to surprise: **the decisive gating control** |
| Replay, full network (encoder + linear head, 1 stored image per class) | competitor | the standard *encoder-adapting* baseline: a win over it means the design matters, not just "fine-tuning helps under shift" |
| `neuromod`, surprise only (`β = 0`) | ablation | which signal matters |
| `neuromod` (full) | mechanism | |

**Compute matching** (FLOPs per example, measured):
- `neuromod` costs about **57 MFLOPs**: encoder forward plus backward (56.9 M), plus the readout.
- Replay gets the same budget: about 57 MFLOPs, minus one encoder forward of about 19 M, gives roughly 2 MLP steps of 11 replayed examples each. The exact step count is recorded in the result file.
- The table reports FLOPs per example for every row.

## Tuning

- **Stream:** length-matched Kuzushiji-Kanji tuning stream (600 classes × 5 shots), seed 0, disjoint from the test pool.
- **Grid** (≤ 36 configurations):
  - `lr` ∈ {1e-5, 3e-5, 1e-4, 3e-4}
  - `τ` ∈ {10, 30}
  - `k` ∈ {5, 20}
  - `β` ∈ {0, 0.5}
  - Ablations use the same grid minus their removed parameters.
- **Grid edges:** if a best value lands on the edge of its range, the grid is extended and re-tuned before the test seeds run (same rule as the baselines).
- **Full-network replay compute:** one replayed image per step costs 2 × 57 MFLOPs, twice neuromod's. That favours the competitor, so it is conservative, and the table records it.
- **Estimate:** about 2–3 min per configuration, so roughly 1.5 h of tuning, then about 15 min for the 5 seeds.

## Measurements

- The Benchmark 1b metrics: `final_acc`, `forgetting`, `acc_new@1/3/5`.
- **Omniglot retention:** after the Kuzushiji-Kanji stream, run fresh NCM with the *adapted* encoder on the Omniglot 600-class stream (seeds 0–4). Compare with the frozen encoder's 73.9 ± 0.6. Report the drop in points.
- Logs: `mean_drift` and the distribution of `g` (mean, and its correlation with surprise).

## Win condition (decided before the run)

A **keep** needs all four. The bar is SLDA frozen at **60.7 ± 0.8** on Benchmark 1b, versus NCM at 35.1:
1. `final_acc` is **≥ 2 points above SLDA frozen**, and above it on **every one of the 5 seeds**: a paired comparison, same seed and class order.
2. Full `neuromod` beats `g ≡ 1`, `g ≡ ḡ` **and shuffled g** by more than the pooled std, and beats full-network replay. Otherwise the result is filed as "encoder plasticity helps, gating does not".
3. The Omniglot retention drop is **≤ 3 points**. A Kuzushiji-Kanji win that wrecks the original features is not a keep.
4. FLOPs per example are ≤ replay's matched budget.

**Otherwise it's a drop**, recorded in the kept/dropped table with the numbers.

## Caveats

- The seed spread understates the variance across class sets: all seeds use the same 600 test classes (see `eval/README.md`).
- One mechanism at a time. Three-factor learning, adaptation and AddChild growth come after this, each against the same table.
