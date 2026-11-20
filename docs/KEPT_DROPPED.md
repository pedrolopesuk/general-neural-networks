# MetaBrain mechanisms: kept / dropped

One row per mechanism. Each row is tested against the pre-registered win condition in its spec.

| # | Mechanism | Benchmark | Verdict | Key numbers (5 seeds) | Spec |
|---|---|---|---|---|---|
| 1 | Neuromodulated encoder plasticity (surprise/novelty-gated online SGD on encoder, NCM readout) | 1b Kuzushiji-Kanji, pretrained | **DROPPED** | best final 35.3 vs SLDA bar 60.7; SLDA readout on adapted encoder 60.6; gated = shuffled = const within 0.1; tuner chose the smallest non-zero lr (1e-6) | [EXP1](EXP1_NEUROMOD_PLASTICITY.md) |
| 2 | Novelty-gated competitive Hebbian layer (Oja + k-WTA + homeostasis, K = 512) learned on 11,984 unlabelled images, SLDA readout | 1c Kuzushiji-Kanji, exposure then stream | **DROPPED** | learned code no better than random: plastic-block-only NCM 30.7 (learned, tuned η 1e-3, kwta 32) vs 30.8 (frozen random init); with SLDA readout: gated 45.5, random init 45.3, g ≡ 1 44.8 vs bar 60.7 | [EXP2](EXP2_HEBBIAN_EXPOSURE.md) |

## Experiment 2 detail

**Verdict reason:** local Hebbian learning on the exposure set produced a code no more class-informative than a random projection. Homeostasis worked, so this is not a bug.

**Plastic-layer diagnostic** (tuning stream, after full exposure):

| Layer | Units within 2× of target firing rate | Dead units | NCM, plastic block only |
|---|---|---|---|
| learned, η 1e-3, kwta 32 (gated tuned) | 512/512 | 0 | 30.7 |
| random init, kwta 32 | 511/512 | 0 | 30.8 |
| learned, η 1e-2, kwta 4 | 265/512 | 83 (one unit fires on 66% of images) | 9.2 |
| random init, kwta 4 | 484/512 | 10 | 11.2 |
| encoder only (reference) | — | — | 35.3 |

Chance is 0.17%.

**Notes:**
- **Where learning moved the code at all, it got worse.** At η 1e-2, Oja lets a few units capture most inputs faster than homeostasis (ρ 1e-3) can push back.
- **Readout (v2, diag(Σ) shrinkage):** adding the plastic block *lowers* SLDA from 63.0 (encoder alone, tuning stream) to about 45. Scaling every feature to unit variance gives 512 sparse dimensions, whose within-class covariance is noise at 5 shots per class, the same weight as the encoder dimensions. With identity shrinkage the block is ignored (63.3). Neither readout could show a gain from a code that carries no extra information.
- An earlier read of "232 of 512 units never fire" was measured *without* exposure (thresholds at 0). After exposure, homeostasis brings every unit to its target rate.
- **Rows not run** (PC restart; not needed once learned = random): K-means, stream-plastic, const g, shuffled g. Win conditions 1 and 2 fail on the finished rows.
- **v1** (identity shrinkage on L2-normalised features, a scale bug; every row about 43.7) is archived in `eval/results/exp2_v1/`.

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

## Finding: unlabelled data does not belong in SLDA's covariance (Benchmark 1c)

Feeding unlabelled exposure into SLDA's shared covariance hurts under both shrinkage methods:

| Shrinkage target | Labelled stream only | + exposure covariance |
|---|---|---|
| identity | 60.7 ± 0.8 | 58.3 ± 0.9 |
| diag(Σ) | 60.4 ± 0.8 | 57.8 ± 1.0 |

**Reason:** without labels, the covariance absorbs spread *between* classes, but LDA needs spread *within* each class. So unlabelled exposure can only help through learned features (for example a plastic layer), not through the readout's covariance. The 1c bar stays 60.7. The two shrinkage methods agree within the seed spread.

**Caveats:**
- v2's best τ = 100 sits on the grid edge. It was not extended: its SLDA readout (60.1) is already below the bar, and τ only sharpens the loss.
- The seed spread understates the variance across class sets (fixed 600-class test pool).
