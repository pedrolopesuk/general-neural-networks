"""Benchmark 1b data: Kuzushiji-Kanji (Clanuwat et al. 2018, ROIS-DS CODH), license CC BY-SA 4.0.
http://codh.rois.ac.jp/kmnist/  -- 3,832 classes of historical Japanese kanji, 64x64 PNG.

Fixed, seed-independent class split (never changes between runs):
  test pool    600 random classes with >= 20 drawings  (5 shots + 15 held-out per class)
  tuning pool  the other >= 20 classes first, then rare (10-19) classes to reach stream length
  (pool difficulty checked: NCM on tuning vs test stream must agree within ~2 points)
Tuning streams are length-matched to the test stream (same number of classes and shots),
so hyper-parameters are chosen at the stream length they are used at.
Images: 28x28, ink = 1 (as Omniglot after loading), cached to data/kkanji.npz.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
import torch

from omniglot import Stream

DATA = Path(__file__).resolve().parent / "data"
SRC = DATA / "kkanji_dl" / "kkanji2"
CACHE = DATA / "kkanji.npz"
TEST_POOL = 600  # = stream length; the remaining >=20 classes all go to tuning
CAP = 20  # drawings kept per class (classes go up to 1,768)


def _build_cache():
    from PIL import Image
    xs, ys, names = [], [], []
    for d in sorted(p for p in SRC.iterdir() if p.is_dir()):
        files = sorted(d.glob("*.png"))
        if len(files) < 10:
            continue
        files = [files[i] for i in np.random.default_rng(0).permutation(len(files))[:CAP]]
        for f in files:
            im = Image.open(f).convert("L").resize((28, 28), Image.BILINEAR)
            xs.append(np.asarray(im, dtype=np.uint8))
            ys.append(len(names))
        names.append(d.name)
    x = np.stack(xs)
    if x.mean() > 127:  # make ink bright on dark background, like loaded Omniglot
        x = 255 - x
    np.savez_compressed(CACHE, x=x, y=np.array(ys, dtype=np.int32), names=np.array(names))


def _load():
    if not CACHE.exists():
        _build_cache()
    z = np.load(CACHE)
    return z["x"].astype(np.float32) / 255.0, z["y"]


def split():
    """(test_classes, tuning_classes): disjoint and fixed."""
    _, y = _load()
    cnt = np.bincount(y)
    big = np.flatnonzero(cnt >= 20)
    big = np.random.default_rng(20261119).permutation(big)
    rare = np.random.default_rng(20261120).permutation(np.flatnonzero((cnt >= 10) & (cnt < 20)))
    # tuning pool ORDER matters: >=20-drawing classes first, rare ones only to fill the stream length
    return np.sort(big[:TEST_POOL]), np.concatenate([big[TEST_POOL:], rare])


def make_stream(seed: int, n_classes: int = 600, shots: int = 5, test_per_class: int = 15,
                device="cpu", background: bool = False, shift: str = "none") -> Stream:
    assert shift == "none", "kkanji is itself the shift"
    x, y = _load()
    test_c, tune_c = split()
    pool = tune_c if background else test_c
    if background:
        test_per_class = min(test_per_class, 5)
    assert n_classes <= len(pool), (n_classes, len(pool))
    rng = np.random.default_rng(seed)
    trx, tr_y, tex, te_y = [], [], [], []
    chosen = rng.permutation(pool[:n_classes] if background else pool)[:n_classes]  # tuning: big classes first
    for new_id, c in enumerate(chosen):
        idx = rng.permutation(np.flatnonzero(y == c))
        trx.append(x[idx[:shots]]); tr_y += [new_id] * shots
        te = idx[shots:shots + test_per_class]
        tex.append(x[te]); te_y += [new_id] * len(te)
    t = lambda a: torch.tensor(np.concatenate(a), dtype=torch.float32, device=device).unsqueeze(1)
    return Stream(t(trx), torch.tensor(tr_y, device=device), t(tex), torch.tensor(te_y, device=device), n_classes, shots)


def tuning_set(device="cpu"):
    """All drawings of the tuning-pool classes (labels re-indexed). Used ONLY for the headroom
    oracle encoder: disjoint from the test pool, so training on it is legitimate."""
    x, y = _load()
    keep = np.isin(y, split()[1])
    x, y = x[keep], np.unique(y[keep], return_inverse=True)[1]
    return torch.tensor(x, dtype=torch.float32, device=device).unsqueeze(1), torch.tensor(y, dtype=torch.long, device=device)


EXPOSURE_CACHE = DATA / "kkanji_exposure.npz"


def exposure_set(device="cpu", with_labels=False):
    """Benchmark 1c unlabelled exposure: ONE fixed set shared by tuning and test, disjoint from both
    streams: every drawing of (a) tuning-pool classes outside the 600-class tuning stream and
    (b) classes with < 10 drawings (in neither pool). Fixed order. Labels only for the oracle row."""
    if not EXPOSURE_CACHE.exists():
        from PIL import Image
        names = np.load(CACHE)["names"]
        _, tune_c = split()
        dirs = [SRC / names[c] for c in tune_c[600:]]
        dirs += [d for d in sorted(SRC.iterdir()) if d.is_dir() and len(list(d.glob("*.png"))) < 10]
        xs, ys = [], []
        for i, d in enumerate(dirs):
            for f in sorted(d.glob("*.png")):
                xs.append(np.asarray(Image.open(f).convert("L").resize((28, 28), Image.BILINEAR), dtype=np.uint8))
                ys.append(i)
        order = np.random.default_rng(7).permutation(len(xs))
        np.savez_compressed(EXPOSURE_CACHE, x=np.stack(xs)[order], y=np.array(ys)[order])
    z = np.load(EXPOSURE_CACHE)
    x = torch.tensor(z["x"].astype(np.float32) / 255.0, device=device).unsqueeze(1)
    if not with_labels:
        return x
    return x, torch.tensor(np.unique(z["y"], return_inverse=True)[1], dtype=torch.long, device=device)


if __name__ == "__main__":
    xx, yy = _load()
    a, b = split()
    print(f"cached {len(xx)} images, {yy.max() + 1} classes; test pool {len(a)}, tuning pool {len(b)}; ink mean {xx.mean():.3f}")
