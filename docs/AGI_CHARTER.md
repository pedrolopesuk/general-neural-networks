# MetaBrain AGI: charter

## Goal
An agent that **learns extremely fast and keeps learning**, measured against standard baselines on public benchmarks.

## The rule
Biology is a source of mechanisms, not a constraint.

> **A mechanism stays only if it improves a benchmark number at equal compute.**

This applies to everything inherited from the bio fork: spiking, Dale's law, populations, STDP, dopamine, adaptation, AddChild growth. Any of them is replaced by a rate-based or other form if that scores better at matched parameters and compute.

## Target capabilities

| Capability | Measured by |
|---|---|
| **Sample efficiency** | Accuracy or return after *k* examples or environment steps; area under the learning curve |
| **Continual learning without forgetting** | Accuracy on old tasks after learning new ones: average forgetting and backward transfer, with no replay of old raw data unless a baseline is explicitly allowed it |
| **Online learning from its own experience** | Single pass and batch size 1; no offline epochs over a stored dataset |
| **Intrinsic motivation** | Exploration coverage and return in sparse-reward reinforcement learning against the same agent without the intrinsic signal |

## What counts as success
1. **Per mechanism:** a statistically clear gain over the strongest baseline on at least one target metric, with no regression greater than one standard deviation on the others.
   - 5 seeds, mean ± standard deviation.
   - Matched parameter count (within ±10%) and matched compute, measured in FLOPs or wall time on the same hardware.
2. **Per benchmark:** the full MetaBrain configuration beats every baseline on the benchmark's headline metric across 5 seeds.
3. **Overall:**
   - Benchmark 1 (continual few-shot classification) is solved, meaning baselines are beaten on both accuracy and forgetting.
   - Then Benchmark 2 (sample-efficient reinforcement learning) beats PPO on sample efficiency.

## How we work
- **Harness first.** No new learning code before the evaluation harness and the baseline numbers exist.
- One command per run; JSON logs; fixed seeds; learning-curve plots; results tables committed under `eval/results/`.
- **Ablations are first-class.** Every mechanism is tested on, off and replaced, and gets a table with its verdict: kept or dropped.
- **Negative results are recorded**, never deleted.

## Inherited state
- Spiking substrate with three-factor STDP, RPE critic and modulators (see `bio-main`, and the bio repo's `docs/HISTORY.md`).
- Known limits: learning at chance on the spiking discrimination task, and the single-cell E/I bridge is over-inhibited. Both are expected failures in `spiking_tests`.
