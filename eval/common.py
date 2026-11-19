"""Shared harness utilities: device check, seeding, JSON logging, plotting."""
from __future__ import annotations

import json
import os
import random
import time
from pathlib import Path

import numpy as np
import torch

ROOT = Path(__file__).resolve().parent
RESULTS = ROOT / "results"
DATA = ROOT / "data"


def require_gpu() -> torch.device:
    """Fail loudly instead of silently running on CPU (RTX 50xx needs cu128+ wheels)."""
    if os.environ.get("METABRAIN_ALLOW_CPU") == "1":
        return torch.device("cpu")
    if not torch.cuda.is_available():
        raise SystemExit("CUDA not available. Install a cu128+ torch build, or set METABRAIN_ALLOW_CPU=1.")
    a = torch.randn(512, 512, device="cuda")
    b = (a @ a).sum().item()  # raises 'no kernel image' on an incompatible build
    if not np.isfinite(b):
        raise SystemExit("GPU matmul produced non-finite output")
    return torch.device("cuda")


def seed_all(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def count_params(module: torch.nn.Module) -> int:
    return sum(p.numel() for p in module.parameters())


def save_json(obj, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=1))


def timestamp() -> str:
    return time.strftime("%Y%m%d-%H%M%S")


def mean_std(xs):
    a = np.asarray(xs, dtype=float)
    return float(a.mean()), float(a.std(ddof=1)) if len(a) > 1 else 0.0
