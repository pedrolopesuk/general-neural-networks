# Benchmark 1c + Experiment 2: unlabelled exposure, then few-shot learning

Status: **DROPPED**. The learned code was no better than random init; see `KEPT_DROPPED.md`. Original spec below. Time-box: about 1 day. Benchmark 2 (MiniGrid/Crafter) comes next regardless of the result.

## Benchmark 1c

1. **Exposure phase:** **11,984** Kuzushiji-Kanji images, without labels, seen once in a fixed random order (`kkanji.exposure_set`).
   - Sources: every drawing of the 366 tuning-pool classes outside the tuning stream, plus every drawing of the 2,266 classes with fewer than 10 drawings, which are in neither pool.
   - **One set, shared by tuning and test,** disjoint from both streams.
   - Oracle reference: an encoder trained *with labels* on this exact set (`--track exposure_enc`), read out with SLDA. That's the headroom reachable from this exposure.
2. **Then the Benchmark 1b stream,** unchanged: 600 test-pool classes × 5 labelled shots, 15 held out per class, same seeds and metrics.
3. **Tuning:** on a length-matched stream from the tuning pool. Its exposure set excludes that stream's classes, so tuning never sees its own test classes unlabelled.

Learners get `expose(x)`, called once per image during the exposure phase. Labels are never passed.

## Reference rows

| Row | Uses exposure? | Role |
|---|---|---|
| SLDA frozen (1b bar) | no | 60.7: what we had |
| **SLDA + exposure covariance** | covariance initialised from the 10k unlabelled features | **the bar for 1c.** A linear "learn from unlabelled data" baseline: whitening for free. A Hebbian rule that only finds the principal components (Oja) should roughly tie it |
| Offline self-supervised encoder (SimCLR-style, Omniglot start, trained on the same 10k, oracle-tuned) | yes, offline, many passes | unlabelled-data oracle |
| Kanji-pool encoder, supervised (oracle) | labels, ~12k drawings | 71.6: upper reference |

## Experiment 2 mechanism (`hebbian`)

- **Architecture:** frozen Omniglot encoder (256-d), then a **plastic layer** of K units (K ∈ {256, 512}), then SLDA readout on `[encoder features, plastic features]`.
- **Plasticity, unlabelled and local (no backprop):**
  - Competitive Hebbian learning with Oja normalisation: `Δw_j = η·g·y_j·(x − y_j·w_j)`, with k-winners-take-all on the responses.
  - Adaptive thresholds (homeostasis) keep every unit in use.
- **Novelty gate:** `g = σ(k·(novelty − running mean))`. Novelty is `1 − max_j cos(x, w_j)`, meaning "nothing I know matches this", the curiosity analog.
- **Phases:** plasticity is on during exposure and optionally on during the labelled stream. Both are tuned.

**Ablations:**
- `g ≡ 1`
- shuffled g
- plastic layer frozen at random init (tests whether the learned features matter at all)
- K-means features at the same K (the standard non-neural competitor for competitive learning)

**Compute:** FLOPs per example are reported for every row. The plastic layer is about 2·256·K per image, which is cheap.

## Tuning and headline

- **Shrinkage:** every row tunes its own SLDA shrinkage over {0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99} on the shared tuning stream. The concatenated features are 768-d, not 256-d, so frozen SLDA's shrinkage is never reused.
- **Covariance source** (`cov_src`, fixed before any Experiment 2 result): tuned per row, the same way as shrinkage, over {`exposure`: unlabelled exposure + labelled stream; `labelled`: labelled stream only}. Applies to every row: gated, g ≡ 1, random init, K-means, stream-plastic. Const and shuffled g inherit the gated row's value. The test seeds use whatever won in tuning; no analysis is chosen after seeing results.
  - Why: the 1c reference rows showed unlabelled covariance *hurts* SLDA (58.3 vs 60.7 frozen), because without labels it absorbs between-class spread. Exposure should help through the plastic layer, not the covariance.
  - **Bar for 1c = 60.7** (frozen SLDA, which ignores exposure) = max(60.7, 58.3). Exposure-set oracle encoder + SLDA: 67.6 ± 0.4 (+6.9 headroom).
  - Implementation: the plastic layer after exposure is cached per (eta, kwta, …) and only the SLDA readout is refitted per (shrinkage, cov_src). Checked: the cached learner equals a freshly built one exactly.
- **v2 fix: scale-free shrinkage.** Fixed before any v2 result.
  - **Cause:** v1 (archived in `results/exp2_v1/`) regressed toward the identity. The readout L2-normalises the encoder block, which cuts its variance about 56,000× (0.0007 vs 40), so the same shrinkage was far stronger than the bar's.
  - **v1 symptoms:** every row picked the lowest grid value (0.01) and scored about 43.7. On the tuning stream, normalised SLDA reached 60.1 at shrinkage 1e-4, against 63.4 for raw features.
  - **v2 change:** SLDA everywhere regresses toward `diag(Σ)` (`target=diag`). Each feature is shrunk relative to its own variance, which handles the mixed encoder and plastic blocks.
  - **Shrinkage grid** for the bar and every row: {1e-4, 0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99}. If a best value lands on an edge, extend the grid and re-tune.
  - **Re-tuned bar:** frozen SLDA, with and without exposure covariance, both with `target=diag`. The 1c bar is the best of 60.7 and those two, so it can only go up.
  - **Encoder preprocessing** (`enc_norm`: L2-normalised or raw) is identical for the bar and every Hebbian row. It comes from the bar's tuning: whichever bar row scores higher on the tuning stream sets it. The plastic layer always takes normalised input (cosine units); `enc_norm` only affects the encoder block that the readout sees.
  - **Const/shuffled g** also inherit the gated row's `cov_src`. v1 dropped it, which was a bug.
- **Random init:** the frozen random-init row tunes its own `kwta` and shrinkage.
- **Two headline numbers in the verdict:** (a) Hebbian minus the 1c bar (60.7); (b) **Hebbian minus frozen random init**, the mechanism's own effect, separated from "more dimensions".

## Win condition (pre-registered)

A **keep** needs all four:
1. `final_acc` ≥ 2 points above **the 1c bar, 60.7** (the better of frozen SLDA and SLDA + exposure covariance), on every one of the 5 paired seeds.
2. Beats random-init, K-means and shuffled-g by more than the pooled std.
3. Omniglot retention ≤ 3 points drop (that is, Omniglot NCM using `[enc, plastic]`).
4. Sanity row: learning rate 0 reproduces the "plastic layer frozen at random init" row exactly.

Otherwise it's a drop, recorded in `KEPT_DROPPED.md`.
