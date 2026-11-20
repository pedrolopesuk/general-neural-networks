"""Benchmark 1: continual few-shot classification on Omniglot.

One command per run:
    python eval/run_bench.py --learner ncm --track scratch
    python eval/run_bench.py --learner mlp_sgd --track pretrained --tune

Protocol (both tracks): 600 classes from the Omniglot *evaluation* alphabets arrive one at a
time; each class contributes `shots` training examples, seen once, batch size 1, in order,
and never revisited. 15 held-out drawings per class are used only for evaluation.

Metrics:
  acc_new@k  accuracy on a class's held-out drawings after k of its shots (k = 1, 3, 5),
             scored among all classes seen so far: speed of learning a new class
  acc_seen   accuracy over all seen classes, every `eval_every` classes (learning curve)
  final_acc  acc_seen after the whole stream
  forgetting mean over classes of (accuracy just after learning it - accuracy at the end)

Tracks:
  scratch     no pretraining; learners see pixels
  pretrained  every learner gets the same frozen conv encoder, trained on the background
              alphabets with a fixed budget (ENC_STEPS x ENC_BATCH), seed-matched
Hyper-parameters are selected (--tune) on a stream built from background alphabets only.
"""
from __future__ import annotations

import argparse
import copy
import itertools
import json
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

from common import RESULTS, DATA, count_params, mean_std, require_gpu, save_json, seed_all, timestamp
from learners import REGISTRY
import kkanji
import omniglot
from omniglot import TUNE_CLASSES, background_set

DATASETS = {"omniglot": omniglot.make_stream, "kkanji": kkanji.make_stream}

ENC_STEPS, ENC_BATCH, ENC_DIM = 3000, 256, 256
SHRINK = (1e-4, 1e-2, 1e-1, 0.3, 0.5, 0.7, 0.9, 0.99)
GRIDS = {
    "ncm": [{}],
    "mlp_sgd": [{"lr": lr} for lr in (1e-4, 3e-4, 1e-3, 3e-3, 1e-2)],
    "mlp_ewc": [{"lr": lr, "lam": lam} for lr in (1e-4, 3e-4, 1e-3, 3e-3) for lam in (0.01, 0.1, 1.0, 10.0, 100.0, 1000.0)],
    "vit_online": [{"lr": lr} for lr in (1e-5, 3e-5, 1e-4, 3e-4, 1e-3)],
    "mlp_replay": [{"lr": lr} for lr in (1e-4, 3e-4, 1e-3, 3e-3, 1e-2)],
    "linear": [{"lr": lr, "wd": wd} for lr in (1e-4, 3e-4, 1e-3, 3e-3) for wd in (0.0, 1e-4, 1e-3, 1e-2, 1e-1)],
    "neuromod": [{"lr": lr, "tau": t, "k": k, "beta": b} for lr in (0.0, 1e-6, 3e-6, 1e-5, 3e-5, 1e-4, 3e-4)
                 for t in (10.0, 30.0, 100.0) for k in (5.0, 20.0) for b in (0.0, 0.5)],
    "neuromod_one": [{"lr": lr, "tau": t} for lr in (0.0, 1e-6, 3e-6, 1e-5, 3e-5, 1e-4, 3e-4) for t in (10.0, 30.0, 100.0)],
    "replay_full": [{"lr": lr} for lr in (1e-5, 3e-5, 1e-4, 3e-4, 1e-3, 3e-3, 1e-2, 3e-2)],
    "hebbian": [{"eta": eta, "kwta": k, "shrinkage": sh, "cov_src": cs} for eta in (0.0, 1e-3, 1e-2, 1e-1)
                for k in (4, 32) for sh in SHRINK for cs in ("exposure", "labelled")],
    "hebbian_kmeans": [{"kwta": k, "shrinkage": sh, "cov_src": cs} for k in (1, 4)
                       for sh in SHRINK for cs in ("exposure", "labelled")],
    "slda_diag": [{"shrinkage": e, "enc_norm": n} for e in SHRINK for n in (False, True)],  # 1c bar, scale-free target
    "slda": [{"shrinkage": e} for e in (1e-4, 1e-2, 1e-1, 0.3, 0.5, 0.7, 0.9, 0.99)],  # 0.99 ~ NCM limit
}


class ConvEncoder(nn.Module):
    out_dim = ENC_DIM

    def __init__(self):
        super().__init__()
        blk = lambda i, o: nn.Sequential(nn.Conv2d(i, o, 3, padding=1), nn.BatchNorm2d(o), nn.ReLU(), nn.MaxPool2d(2))
        self.f = nn.Sequential(blk(1, 64), blk(64, 64), blk(64, 64), nn.Flatten(), nn.Linear(64 * 3 * 3, ENC_DIM))

    def forward(self, x):
        return self.f(x)


def pretrained_encoder(seed: int, device, source: str = "omniglot") -> nn.Module:
    """source=omniglot: the shared v1 encoder. source=kkanji: headroom oracle, same architecture and
    budget, trained i.i.d. on the Kuzushiji-Kanji TUNING pool (disjoint from the test pool)."""
    path = DATA / {"omniglot": f"encoder_v1_s{seed}_{ENC_STEPS}x{ENC_BATCH}.pt",
                   "kkanji": f"encoder_kk_s{seed}_{ENC_STEPS}x{ENC_BATCH}.pt",
                   "exposure": f"encoder_exp_s{seed}_{ENC_STEPS}x{ENC_BATCH}.pt"}[source]
    enc = ConvEncoder().to(device)
    if path.exists():
        enc.load_state_dict(torch.load(path, map_location=device))
        return enc.eval()
    seed_all(seed)
    x, y = {"omniglot": lambda: background_set(device), "kkanji": lambda: kkanji.tuning_set(device),
            "exposure": lambda: kkanji.exposure_set(device, with_labels=True)}[source]()
    head = nn.Linear(ENC_DIM, int(y.max()) + 1).to(device)
    opt = torch.optim.Adam(list(enc.parameters()) + list(head.parameters()), lr=1e-3)
    enc.train()
    for _ in range(ENC_STEPS):
        idx = torch.randint(0, len(x), (ENC_BATCH,), device=device)
        loss = F.cross_entropy(head(F.relu(enc(x[idx]))), y[idx])
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()
    torch.save(enc.state_dict(), path)
    return enc.eval()


_EXPOSURE = {}


def expose_all(learner, device):
    """Benchmark 1c exposure phase: every image of the shared exposure set, once, fixed order, no labels."""
    if "x" not in _EXPOSURE:
        _EXPOSURE["x"] = kkanji.exposure_set(device)
    x = _EXPOSURE["x"]
    for i in range(len(x)):
        learner.expose(x[i:i + 1])
    if hasattr(learner, "end_exposure"):
        learner.end_exposure()


STREAM_ENC_STEPS = 500  # x ENC_BATCH: ~43 passes over the 3,000 stream shots


def stream_encoder(seed, st, device, steps=STREAM_ENC_STEPS):
    """Same-data oracle: the Omniglot v1 encoder (neuromod's starting point) fine-tuned OFFLINE,
    i.i.d., many passes, on this stream's own training shots only. Not an online learner."""
    enc = pretrained_encoder(seed, device)
    for p in enc.parameters():
        p.requires_grad_(True)
    seed_all(seed)
    head = nn.Linear(ENC_DIM, st.n_classes).to(device)
    opt = torch.optim.Adam(list(enc.parameters()) + list(head.parameters()), lr=1e-3)
    enc.train()
    for _ in range(steps):
        idx = torch.randint(0, len(st.train_y), (ENC_BATCH,), device=device)
        loss = F.cross_entropy(head(F.relu(enc(st.train_x[idx]))), st.train_y[idx])
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()
    return enc.eval()


@torch.no_grad()
def accuracy(learner, x, y, n_seen, bs=1024):
    if len(x) == 0:
        return float("nan")
    correct = 0
    for i in range(0, len(x), bs):
        correct += (learner.logits(x[i:i + bs], n_seen).argmax(1) == y[i:i + bs]).sum().item()
    return correct / len(x)


def run_stream(learner, st, task_size=10, eval_every=50):
    n, k = st.n_classes, st.shots
    acc_new = {1: [], 3: [], 5: []}
    after_learning = np.zeros(n)
    curve = []
    t0 = time.time()
    for c in range(n):
        te = st.test_y == c
        for s in range(k):
            i = c * k + s
            learner.observe(st.train_x[i:i + 1], int(st.train_y[i]))
            if (s + 1) in acc_new:
                acc_new[s + 1].append(accuracy(learner, st.test_x[te], st.test_y[te], c + 1))
        after_learning[c] = acc_new[k][-1] if k in acc_new else accuracy(learner, st.test_x[te], st.test_y[te], c + 1)
        if (c + 1) % task_size == 0:
            learner.end_of_task(c + 1)
        if (c + 1) % eval_every == 0 or c + 1 == n:
            seen = st.test_y < c + 1
            curve.append((c + 1, accuracy(learner, st.test_x[seen], st.test_y[seen], c + 1)))
    end = np.array([accuracy(learner, st.test_x[st.test_y == c], st.test_y[st.test_y == c], n) for c in range(n)])
    return {
        "acc_new": {f"k{kk}": float(np.mean(v)) for kk, v in acc_new.items() if v},
        "curve": curve,
        "final_acc": curve[-1][1],
        "forgetting": float(np.mean(after_learning - end)),
        "wall_s": time.time() - t0,
    }


def run_iid(learner, st, epochs, seed):
    """Ceiling: every class available from the start, shuffled, several passes, batch size 1."""
    n = st.n_classes
    learner.fixed_seen = n
    g = torch.Generator(device="cpu").manual_seed(seed)
    t0 = time.time()
    for _ in range(epochs):
        for i in torch.randperm(len(st.train_y), generator=g).tolist():
            learner.observe(st.train_x[i:i + 1], int(st.train_y[i]))
    acc = accuracy(learner, st.test_x, st.test_y, n)
    return {"acc_new": {"k1": float("nan"), "k3": float("nan"), "k5": float("nan")},
            "curve": [(n, acc)], "final_acc": acc, "forgetting": 0.0, "wall_s": time.time() - t0}


def build(name, track, seed, device, n_max, hp):
    enc = (pretrained_encoder(seed, device) if track == "pretrained"
           else pretrained_encoder(seed, device, "kkanji") if track == "kkanji_enc"
           else pretrained_encoder(seed, device, "exposure") if track == "exposure_enc" else None)
    if name == "vit_online" and track == "pretrained":
        raise SystemExit("vit_online runs on pixels: scratch track only")
    return REGISTRY[name](n_max, device, encoder=enc, **hp)


def tune(name, track, device, args, fixed, hp_file):
    best = None
    grid = GRIDS["neuromod_one"] if name == "neuromod" and fixed.get("gate") == "one" else GRIDS[name]
    if name == "slda" and fixed.get("target") == "diag":
        grid = GRIDS["slda_diag"]
    if name == "hebbian" and fixed.get("rule") == "kmeans":
        grid = GRIDS["hebbian_kmeans"]
    if name == "hebbian" and fixed.get("eta") == 0.0:
        grid = [g for g in grid if g["eta"] == 0.0]  # frozen random init: tunes its own kwta + shrinkage
    if name == "hebbian" and fixed.get("gate") == "one":
        grid = [g for g in grid if g["eta"] > 0]
    if name == "neuromod" and fixed.get("beta") == 0.0:
        grid = [g for g in grid if g["beta"] == 0.0]
    exposed = {}  # hebbian: the plastic layer after exposure does not depend on the readout knobs -> cache it
    readout = ("shrinkage", "cov_src")
    for hp in grid:
        hp = {**hp, **fixed}
        seed_all(0)
        st = DATASETS[args.dataset](0, n_classes=args.tune_classes, shots=args.shots, device=device, background=True, shift=args.shift)
        key = tuple(sorted((k, str(v)) for k, v in hp.items() if k not in readout))
        if args.exposure and name == "hebbian" and key in exposed:
            lr_ = copy.deepcopy(exposed[key])  # exposure-cov state included; readout knobs set below
            lr_.slda.eps, lr_.cov_src = hp["shrinkage"], hp["cov_src"]
            lr_.end_exposure()
        else:
            lr_ = build(name, track, 0, device, st.n_classes, hp)
            # tune for the task actually run: i.i.d. ceilings are tuned i.i.d.
            if args.exposure:
                if name == "hebbian":
                    lr_.cov_src, cs = "exposure", lr_.cov_src  # cache the exposure-cov version
                    expose_all(lr_, device)
                    exposed[key] = copy.deepcopy(lr_)
                    lr_.cov_src = cs
                    lr_.end_exposure()
                else:
                    expose_all(lr_, device)
        r = run_iid(lr_, st, args.iid, 0) if args.iid else run_stream(lr_, st, eval_every=args.tune_classes)
        print(f"  tune {name} {hp}: final {r['final_acc']:.3f}")
        if best is None or r["final_acc"] > best[1]:
            best = (hp, r["final_acc"])
    save_json({"hp": best[0], "background_final_acc": best[1], "grid": grid}, hp_file)
    return best[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--learner", required=True, choices=sorted(REGISTRY))
    ap.add_argument("--track", default="scratch", choices=["scratch", "pretrained", "kkanji_enc", "stream_enc", "exposure_enc"],
                    help="kkanji_enc = headroom oracle encoder (Benchmark 1b only; tune on the TEST stream is not allowed, use --hp)")
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--classes", type=int, default=600)
    ap.add_argument("--shots", type=int, default=5)
    ap.add_argument("--test-per-class", type=int, default=15, help="ANML/OML protocol: --shots 15 --test-per-class 5")
    ap.add_argument("--tune", action="store_true")
    ap.add_argument("--tune-classes", type=int, default=100)
    ap.add_argument("--hp", default="{}", help="JSON overrides (also applied during --tune)")
    ap.add_argument("--variant", default="", help="name suffix for results/tuning, e.g. x11 for a compute-matched variant")
    ap.add_argument("--protocol", default="default", choices=["default", "anml"],
                    help="anml = Beaulieu et al. 2020 meta-test: 600 classes, 15 shots, 5 held out")
    ap.add_argument("--dataset", default="omniglot", choices=sorted(DATASETS),
                    help="kkanji = Benchmark 1b (Kuzushiji-Kanji, length-matched tuning stream)")
    ap.add_argument("--exposure", action="store_true",
                    help="Benchmark 1c: unlabelled exposure phase (kkanji.exposure_set) before the stream, tuning included")
    ap.add_argument("--shift", default="none", choices=["none", "rot90", "rot180", "invert"],
                    help="domain shift: rotate every stream image (test, train and tuning)")
    ap.add_argument("--iid", type=int, default=0, metavar="EPOCHS",
                    help="ceiling: train on the whole stream shuffled for EPOCHS passes (no continual constraint)")
    args = ap.parse_args()
    if args.protocol == "anml":
        args.classes, args.shots, args.test_per_class = 600, 15, 5
    if args.track in ("kkanji_enc", "stream_enc", "exposure_enc") and args.tune:
        raise SystemExit("kkanji_enc: the tuning pool trained this encoder, so tuning on it is biased; pass fixed --hp")
    if args.dataset == "kkanji" and args.tune_classes == 100:
        args.tune_classes = args.classes  # length-matched tuning stream
    device = require_gpu()

    fixed = json.loads(args.hp)
    if args.exposure:
        assert args.dataset == "kkanji", "--exposure is Benchmark 1c (kkanji)"
    vtag = ("-exp" if args.exposure else "") + (f"-{args.dataset}" if args.dataset != "omniglot" else "") + (f"-{args.variant}" if args.variant else "") + (f"-{args.shift}" if args.shift != "none" else "")
    hp_file = RESULTS / "tuning" / (f"{args.learner}{vtag}_{args.track}_s{args.shots}" + (f"_iid{args.iid}" if args.iid else "") + ".json")
    hp = tune(args.learner, args.track, device, args, fixed, hp_file) if args.tune else (
        json.loads(hp_file.read_text())["hp"] if hp_file.exists() else {})
    hp.update(fixed)

    runs = []
    for seed in range(args.seeds):
        seed_all(seed)
        st = DATASETS[args.dataset](seed, n_classes=args.classes, shots=args.shots, test_per_class=args.test_per_class, device=device, shift=args.shift)
        hp_l = dict(hp)
        src = hp_l.pop("gate_from", None)
        if src:  # const / shuffled gate controls replay the gated run's per-seed g sequence
            seq = np.array(json.loads(open(src).read())["runs"][seed]["g_log"])
            hp_l["gate_seq"] = (np.full_like(seq, seq.mean()) if hp_l.get("gate") == "const"
                                else np.random.default_rng(1000 + seed).permutation(seq))
        if args.track == "stream_enc":
            learner = REGISTRY[args.learner](st.n_classes, device, encoder=stream_encoder(seed, st, device, hp_l.pop("enc_steps", STREAM_ENC_STEPS)), **hp_l)
        else:
            learner = build(args.learner, args.track, seed, device, st.n_classes, hp_l)
        if args.exposure:
            r_exp_t = time.time(); expose_all(learner, device)
        r = run_iid(learner, st, args.iid, seed) if args.iid else run_stream(learner, st)
        if hasattr(learner, "g_log"):
            r["g_log"] = [round(g, 5) for g in learner.g_log]
            r["g_mean"] = float(np.mean(learner.g_log))
            r["g_surprise_corr"] = float(np.corrcoef(learner.g_log, learner.s_log)[0, 1]) if np.std(learner.g_log) > 0 else float("nan")
        if hasattr(learner, "oracle_means"):
            # SLDA readout on the adapted encoder (diagnostic): stream order, frozen-SLDA's tuned shrinkage
            shr = json.loads((RESULTS / "tuning" / "slda-kkanji_pretrained_s5.json").read_text())["hp"]["shrinkage"]
            sl = REGISTRY["slda"](st.n_classes, device, encoder=learner.adapted_encoder(), shrinkage=shr)
            for i in range(len(st.train_y)):
                sl.observe(st.train_x[i:i + 1], int(st.train_y[i]))
            r["slda_readout_acc"] = accuracy(sl, st.test_x, st.test_y, st.n_classes)
            for p_ in learner.adapted_encoder().parameters():
                p_.requires_grad_(True)  # SLDA's Learner init froze them; restore for retention bookkeeping
            learner.oracle_means()
            r["oracle_final_acc"] = accuracy(learner, st.test_x, st.test_y, st.n_classes)
        if hasattr(learner, "adapted_encoder"):  # retention on the original domain: fresh NCM, adapted encoder
            ost = omniglot.make_stream(seed, n_classes=600, shots=5, test_per_class=15, device=device)
            r["omniglot_retention"] = run_stream(REGISTRY["ncm"](600, device, encoder=learner.adapted_encoder()), ost)["final_acc"]
        r["seed"] = seed
        runs.append(r)
        print(f"seed {seed}: final {r['final_acc']:.3f}  forgetting {r['forgetting']:+.3f}  "
              f"new@1/3/5 {r['acc_new']['k1']:.3f}/{r['acc_new']['k3']:.3f}/{r['acc_new']['k5']:.3f}  {r['wall_s']:.0f}s")

    summary = {k: mean_std([r[k] for r in runs]) for k in ("final_acc", "forgetting")}
    for k in ("slda_readout_acc", "oracle_final_acc", "omniglot_retention", "g_mean", "g_surprise_corr"):
        if k in runs[0]:
            summary[k] = mean_std([r[k] for r in runs])
    for kk in ("k1", "k3", "k5"):
        summary[f"acc_new_{kk}"] = mean_std([r["acc_new"][kk] for r in runs])
    out = {
        "benchmark": "omniglot_continual_fewshot", "learner": args.learner + vtag, "track": args.track,
        "classes": args.classes, "shots": args.shots, "seeds": args.seeds,
        "params": learner.n_params(), "hp": {k: v for k, v in hp.items()}, "flops_per_example": learner.flops_per_example(),
        "encoder_budget": f"{ENC_STEPS}x{ENC_BATCH} on background minus {TUNE_CLASSES} tuning classes (v1)" if args.track == "pretrained" else None,
        "dataset": args.dataset, "exposure": (f"{len(kkanji.exposure_set())} unlabelled kkanji images (shared, disjoint)" if args.exposure else None),
        "tuning": (f"{args.tune_classes} disjoint kkanji tuning-pool classes (length-matched), seed 0" if args.dataset == "kkanji"
                   else f"{TUNE_CLASSES} held-out background classes, seed 0"),
        "shift": args.shift, "summary": summary, "runs": runs,
    }
    tag = f"{args.learner}{vtag}_{args.track}_{args.protocol}_c{args.classes}_s{args.shots}" + (f"_iid{args.iid}" if args.iid else "")
    save_json(out, RESULTS / "omniglot" / f"{tag}.json")
    plot(out, RESULTS / "omniglot" / f"{tag}.png")
    print(json.dumps({"learner": args.learner, "track": args.track, **summary}))


def plot(out, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    xs = [c for c, _ in out["runs"][0]["curve"]]
    ys = np.array([[a for _, a in r["curve"]] for r in out["runs"]])
    plt.figure(figsize=(5, 3.2))
    plt.plot(xs, ys.mean(0), label=f"{out['learner']} ({out['track']})")
    plt.fill_between(xs, ys.mean(0) - ys.std(0), ys.mean(0) + ys.std(0), alpha=0.25)
    plt.xlabel("classes seen"); plt.ylabel("accuracy on all seen classes"); plt.ylim(0, 1)
    plt.legend(); plt.tight_layout(); plt.savefig(path, dpi=120); plt.close()


if __name__ == "__main__":
    main()
