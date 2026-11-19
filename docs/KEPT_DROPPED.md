# MetaBrain mechanisms: kept / dropped

One row per mechanism. Each row is tested against the pre-registered win condition in its spec.

| # | Mechanism | Benchmark | Verdict | Key numbers (5 seeds) | Spec |
|---|---|---|---|---|---|
| 1 | Neuromodulated encoder plasticity (surprise/novelty-gated online SGD on encoder, NCM readout) | 1b Kuzushiji-Kanji, pretrained | **DROPPED** | best final 35.3 vs SLDA bar 60.7; SLDA readout on adapted encoder 60.6; gated = shuffled = const within 0.1; tuner chose the smallest non-zero lr (1e-6) | [EXP1](EXP1_NEUROMOD_PLASTICITY.md) |

## Experiment 1 detail (extended grid: lr 0…3e-4, τ 10…100)

| Row | Final | SLDA readout | Oracle refresh | Omniglot retention |
|---|---|---|---|---|
| neuromod lr = 0 (sanity) | 35.08 = frozen NCM ✓ | 60.68 = frozen SLDA ✓ | 35.1 | 73.9 |
| full gated v1 (lr 1e-6) | 35.3 ± 1.7 | 60.6 | 35.0 | 74.1 |
| full gated v2 (exemplar refresh, lr 3e-5) | 35.3 ± 5.0 | 60.1 | 40.4 | 67.1 |
| g ≡ 1 (lr 1e-6) | 35.2 ± 1.7 | 60.6 | 34.9 | 74.1 |
| surprise only | 35.3 ± 1.7 | 60.6 | 35.0 | 74.1 |
| g ≡ ḡ | 35.2 ± 1.7 | 60.6 | 34.9 | 74.1 |
| shuffled g | 35.2 ± 1.7 | 60.6 | 34.9 | 74.1 |
| replay_full (lr 3e-3, 2× FLOPs) | 6.5 ± 2.5 | — | — | 45.8 |

First pass (narrower grid, archived in `eval/results/exp1a/`): gated 17.2 ± 4.2, shuffled 15.8 ± 3.8, g ≡ 1 9.4. Any real plasticity was destructive.

## Headroom decomposition (Benchmark 1b, SLDA readout; oracle rows tuned on test)

| Encoder | SLDA (best shrinkage) | vs bar 60.7 |
|---|---|---|
| Omniglot, frozen (the bar) | 60.7 ± 0.8 | — |
| Same data: fine-tuned offline on the stream's 3,000 shots (best of 100/250/500 steps) | 63.5 ± 1.1 | **+2.8** (same-data headroom) |
| Kanji tuning pool, i.i.d. (~12k drawings) | 71.6 ± 0.6 | **+10.9** (total headroom) |
| neuromod, online | 60.6 | **−0.1** (captured online) |

**Reading:**
- Most of the headroom (about 8 of the 11 points) needs more data than one 600×5 stream holds.
- The ~3 points available from this data were not captured online. The tuner turned plasticity off.

**Caveats:**
- v2's best τ = 100 sits on the grid edge. It was not extended: its SLDA readout (60.1) is already below the bar, and τ only sharpens the loss.
- The seed spread understates the variance across class sets (fixed 600-class test pool).
