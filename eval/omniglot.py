"""Omniglot loading and the continual few-shot stream.

Background alphabets (964 classes) = optional pretraining ("pretrained" track only).
Evaluation alphabets (659 classes) = the stream, in every track.
Each class has 20 drawings: the first `shots` (after a per-seed shuffle) are the
training stream, the remaining 20 - shots... capped at `test_per_class` are held out.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import torch

from common import DATA


def _load_split(background: bool) -> tuple[np.ndarray, np.ndarray]:
    cache = DATA / f"omniglot_{'bg' if background else 'eval'}_28.npz"
    if cache.exists():
        z = np.load(cache)
        return z["x"], z["y"]
    from torchvision import datasets, transforms

    ds = datasets.Omniglot(str(DATA), background=background, download=True,
                           transform=transforms.Compose([transforms.Resize(28), transforms.ToTensor()]))
    xs, ys = [], []
    for img, label in ds:
        xs.append((1.0 - img.numpy()[0]).astype(np.float16))  # ink = 1
        ys.append(label)
    x, y = np.stack(xs), np.asarray(ys, dtype=np.int32)
    DATA.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(cache, x=x, y=y)
    return x, y


TUNE_CLASSES = 100  # background classes held out from encoder pretraining; the only tuning classes


def tuning_classes() -> np.ndarray:
    """Fixed, seed-independent set of background classes reserved for hyper-parameter tuning."""
    _, y = _load_split(background=True)
    return np.random.default_rng(20261119).permutation(np.unique(y))[:TUNE_CLASSES]


@dataclass
class Stream:
    train_x: torch.Tensor   # [n_classes*shots, 1, 28, 28], in stream order
    train_y: torch.Tensor   # stream-local class ids 0..n_classes-1
    test_x: torch.Tensor    # [n_classes*test_per_class, 1, 28, 28]
    test_y: torch.Tensor
    n_classes: int
    shots: int


def make_stream(seed: int, n_classes: int = 600, shots: int = 5, test_per_class: int = 15,
                device: torch.device | str = "cpu", background: bool = False, shift: str = "none") -> Stream:
    """background=True builds a stream from the background alphabets (hyper-parameter tuning only)."""
    x, y = _load_split(background=background)
    rng = np.random.default_rng(seed)
    pool = tuning_classes() if background else np.unique(y)  # tuning only ever sees the held-out classes
    classes = rng.permutation(pool)[:n_classes]
    trx, tr_y, tex, te_y = [], [], [], []
    for new_id, c in enumerate(classes):
        idx = rng.permutation(np.flatnonzero(y == c))
        trx.append(x[idx[:shots]]); tr_y += [new_id] * shots
        te = idx[shots:shots + test_per_class]
        tex.append(x[te]); te_y += [new_id] * len(te)
    rot = {"none": 0, "rot90": 1, "rot180": 2, "invert": 0}[shift]  # domain shift: the frozen encoder never saw rotated glyphs
    t = lambda a: (lambda z: 1 - z if shift == "invert" else z)(torch.rot90(torch.tensor(np.concatenate(a), dtype=torch.float32, device=device).unsqueeze(1), rot, (2, 3)))
    return Stream(t(trx), torch.tensor(tr_y, device=device), t(tex), torch.tensor(te_y, device=device), n_classes, shots)


def background_set(device="cpu", exclude_tuning: bool = True) -> tuple[torch.Tensor, torch.Tensor]:
    """Encoder pretraining data: background alphabets minus the tuning classes (labels re-indexed)."""
    x, y = _load_split(background=True)
    if exclude_tuning:
        keep = ~np.isin(y, tuning_classes())
        x, y = x[keep], np.unique(y[keep], return_inverse=True)[1]
    return torch.tensor(x, dtype=torch.float32, device=device).unsqueeze(1), torch.tensor(y, dtype=torch.long, device=device)
