# Evaluation harness

```powershell
python -m venv .venv
.venv\Scripts\pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128   # RTX 50xx needs cu128+
.venv\Scripts\pip install matplotlib
.venv\Scripts\python eval\run_bench.py --learner ncm --track pretrained --tune     # one benchmark run
powershell -File eval\run_baselines.ps1                                           # every baseline
```

- The harness refuses to run on CPU unless `METABRAIN_ALLOW_CPU=1` is set. It runs a GPU matmul at startup, so a wheel without kernels for this GPU fails loudly.
- Each run writes `eval/results/omniglot/<learner>_<track>_c<classes>_s<shots>.json` and a learning-curve `.png` (mean ± std over seeds).
- Tuning choices go to `eval/results/tuning/`.

## Benchmark 1: continual few-shot classification on Omniglot

**Why Omniglot rather than split-CIFAR:**
- It has 1,623 classes × 20 drawings, so a long stream of new classes (600) with few examples each comes naturally, and every class is unseen.
- It is the dataset of the direct prior art (OML, ANML), so published numbers are comparable.
- Split-CIFAR has 100 classes with 500 images each: a short stream, few-shot only by subsampling, and far more compute per example.

**Protocol:**
- 600 classes from the evaluation alphabets, in a seed-specific order.
- `shots` training drawings per class, batch size 1, single pass, never revisited.
- Held-out drawings per class are used only for evaluation.
- 5 seeds, reported as mean ± std.
- Default is 5-shot / 15 test. The ANML-comparable protocol is `--shots 15 --test-per-class 5`.

**Metrics:**

| Metric | Meaning |
|---|---|
| `acc_new_k1/k3/k5` | Accuracy on a new class's held-out drawings after 1, 3 or 5 of its shots, among all classes seen so far (speed of acquisition) |
| `final_acc` | Accuracy over all 600 classes at the end of the stream (the headline metric) |
| `forgetting` | Mean over classes of (accuracy just after learning it − accuracy at the end). This mixes true forgetting with **interference** from the growing label set: NCM cannot change old class means, yet still shows about 0.10, so that is the interference floor |

**Tracks:** see `docs/AGI_CHARTER.md`. In short: **scratch** (pixels, no pretraining) and **pretrained** (a shared frozen conv encoder trained on background alphabets with a fixed budget).

**Baselines:**

| Baseline | What it is |
|---|---|
| `ncm` | Nearest class mean on L2-normalised features; cannot change old class means |
| `mlp_sgd` | Online MLP trained with SGD |
| `mlp_ewc` | MLP with online EWC, task = 10 classes |
| `vit_online` | Tiny ViT trained online with Adam (scratch track only) |

The MLP and ViT are parameter-matched at about 420k and 455k parameters.

## Benchmark 1b: Kuzushiji-Kanji (domain shift)

- **Data:** Kuzushiji-Kanji, Clanuwat et al. 2018 (ROIS-DS Center for Open Data in the Humanities), http://codh.rois.ac.jp/kmnist/. **License: CC BY-SA 4.0.** Downloaded to `eval/data/kkanji_dl/` (gitignored); cache `eval/data/kkanji.npz`.
- **Split (fixed, `eval/kkanji.py`):** 600 test-pool classes (≥ 20 drawings); tuning stream = the other 480 classes with ≥ 20 drawings + 120 rarer (10–19) ones. Pool difficulty checked with NCM. At most 20 drawings kept per class; images resized 64→28.
- **Stream:** 600 classes × 5 shots, 15 held out per class. Same metrics as Benchmark 1. The pretrained track uses the Omniglot-background encoder unchanged, so the dataset itself is the shift.
- **Tuning:** a length-matched stream (600 classes × 5 shots, 5 held out) drawn from the tuning pool, never from the test pool.
- **Run:** `python eval/run_bench.py --dataset kkanji --learner ncm --track pretrained --tune`
- **Inverted-ink Omniglot (`--shift invert`)** is a single data point, not a benchmark. Normalisation re-centering (AdaBN/TENT-style) can undo it.
- **Seed spread caveat:** the test pool is exactly 600 classes, so every seed uses the *same* classes. Seeds vary only the class order and which drawings are shown or held out. Reported ± values therefore understate the variance across different class sets.
