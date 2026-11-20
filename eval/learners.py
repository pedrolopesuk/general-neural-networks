"""Online continual learners behind one interface.

    learner.observe(x[1,1,28,28], y:int)      # one example, one pass, never revisited
    learner.logits(x[B,1,28,28], n_seen:int)  # scores over classes seen so far

Baselines: NCM (nearest class mean), online MLP (SGD), MLP + online EWC, tiny ViT (online).
In the "pretrained" track every learner gets the same frozen encoder as its input map.
"""
from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F


class Flatten(nn.Module):
    out_dim = 784

    def forward(self, x):
        return x.flatten(1)


class Learner:
    name = "base"

    def __init__(self, n_max: int, device, encoder: nn.Module | None = None):
        self.n_max, self.device = n_max, device
        self.enc = (encoder or Flatten()).to(device).eval()
        for p in self.enc.parameters():
            p.requires_grad_(False)

    def feats(self, x):
        with torch.no_grad():
            return self.enc(x)

    def expose(self, x) -> None:  # Benchmark 1c: one unlabelled image; default learners ignore it
        pass

    def end_of_task(self, n_seen: int) -> None:  # optional hook every `task_size` classes
        pass

    def n_params(self) -> int:
        return 0

    def flops_per_example(self) -> int:  # forward + backward, approximate (2*MACs fwd, x3 with bwd)
        return 0


class NCM(Learner):
    """Nearest class mean on L2-normalised features. Cannot forget by construction."""
    name = "ncm"

    def __init__(self, n_max, device, encoder=None):
        super().__init__(n_max, device, encoder)
        d = self.enc.out_dim
        self.sum = torch.zeros(n_max, d, device=device)
        self.cnt = torch.zeros(n_max, device=device)

    def observe(self, x, y):
        self.sum[y] += F.normalize(self.feats(x), dim=1)[0]
        self.cnt[y] += 1

    def logits(self, x, n_seen):
        mu = F.normalize(self.sum[:n_seen] / self.cnt[:n_seen, None].clamp(min=1), dim=1)
        return F.normalize(self.feats(x), dim=1) @ mu.T

    def n_params(self):
        return self.sum.numel()  # stored statistics (not trained weights)

    def flops_per_example(self):
        return 2 * self.enc.out_dim * self.n_max


def mlp(d_in, hidden, n_out):
    return nn.Sequential(nn.Linear(d_in, hidden), nn.ReLU(), nn.Linear(hidden, hidden), nn.ReLU(), nn.Linear(hidden, n_out))


class OnlineMLP(Learner):
    name = "mlp_sgd"

    def __init__(self, n_max, device, encoder=None, hidden=256, lr=0.03, steps_per_example=1):
        super().__init__(n_max, device, encoder)
        self.net = mlp(self.enc.out_dim, hidden, n_max).to(device)
        self.opt = torch.optim.SGD(self.net.parameters(), lr=lr)
        self.steps = steps_per_example

    def loss(self, x, y, n_seen):
        out = self.net(self.feats(x))[:, :n_seen]
        return F.cross_entropy(out, torch.as_tensor([y], device=self.device))

    fixed_seen = None  # i.i.d. mode: full label space from the start

    def observe(self, x, y):
        for _ in range(self.steps):
            self.opt.zero_grad(set_to_none=True)
            l = self.loss(x, y, self.fixed_seen or (y + 1))  # continual: classes arrive in order, seen = 0..y
            l = l + self.penalty()
            l.backward()
            self.opt.step()

    def penalty(self):
        return 0.0

    def logits(self, x, n_seen):
        with torch.no_grad():
            return self.net(self.feats(x))[:, :n_seen]

    def n_params(self):
        return sum(p.numel() for p in self.net.parameters())

    def flops_per_example(self):
        return 6 * self.n_params() * self.steps


class OnlineEWC(OnlineMLP):
    """Online EWC (Schwarz et al. 2018): Fisher accumulated at each task boundary."""
    name = "mlp_ewc"

    def __init__(self, n_max, device, encoder=None, hidden=256, lr=0.03, lam=100.0, gamma=1.0, **kw):
        super().__init__(n_max, device, encoder, hidden, lr, **kw)
        self.lam, self.gamma = lam, gamma
        self.fisher = None
        self.anchor = None
        self.buf = []  # current task's examples, used only to estimate Fisher at the boundary

    def observe(self, x, y):
        self.buf.append((x, y))
        super().observe(x, y)

    def end_of_task(self, n_seen):
        fisher = [torch.zeros_like(p) for p in self.net.parameters()]
        for x, y in self.buf:
            self.net.zero_grad(set_to_none=True)
            self.loss(x, y, n_seen).backward()
            for f, p in zip(fisher, self.net.parameters()):
                if p.grad is not None:
                    f += p.grad.detach() ** 2 / len(self.buf)
        self.fisher = fisher if self.fisher is None else [self.gamma * a + b for a, b in zip(self.fisher, fisher)]
        self.anchor = [p.detach().clone() for p in self.net.parameters()]
        self.buf = []

    def penalty(self):
        if self.fisher is None:
            return 0.0
        return 0.5 * self.lam * sum((f * (p - a) ** 2).sum() for f, p, a in zip(self.fisher, self.net.parameters(), self.anchor))


class TinyViT(nn.Module):
    def __init__(self, n_out, dim=104, depth=3, heads=4, patch=7):
        super().__init__()
        self.patch = patch
        n_tok = (28 // patch) ** 2
        self.embed = nn.Linear(patch * patch, dim)
        self.pos = nn.Parameter(torch.zeros(1, n_tok, dim))
        layer = nn.TransformerEncoderLayer(dim, heads, dim * 2, dropout=0.0, batch_first=True, norm_first=True)
        self.body = nn.TransformerEncoder(layer, depth)
        self.norm = nn.LayerNorm(dim)
        self.head = nn.Linear(dim, n_out)

    def forward(self, x):
        p = self.patch
        t = x.unfold(2, p, p).unfold(3, p, p).reshape(x.shape[0], -1, p * p)
        h = self.body(self.embed(t) + self.pos)
        return self.head(self.norm(h.mean(1)))


class OnlineViT(OnlineMLP):
    name = "vit_online"

    def __init__(self, n_max, device, encoder=None, lr=1e-3, **kw):
        Learner.__init__(self, n_max, device, None)  # operates on pixels (scratch track only)
        self.feats = lambda x: x  # no flattening: the ViT patches the image itself
        self.net = TinyViT(n_max).to(device)
        self.opt = torch.optim.Adam(self.net.parameters(), lr=lr)
        self.steps = kw.get("steps_per_example", 1)

    def flops_per_example(self):
        # tokens x params dominates for the encoder; head once
        body = sum(p.numel() for n, p in self.net.named_parameters() if not n.startswith("head"))
        return 6 * (16 * body + self.net.head.weight.numel()) * self.steps


class Replay(OnlineMLP):
    """Online MLP + class-balanced replay buffer. Memory budget = NCM's: one stored
    example (its input-map features) per class, i.e. n_max x feature_dim floats."""
    name = "mlp_replay"

    def __init__(self, n_max, device, encoder=None, hidden=256, lr=1e-3, replay_batch=10, **kw):
        super().__init__(n_max, device, encoder, hidden, lr, **kw)
        self.mem_x = torch.zeros(n_max, self.enc.out_dim, device=device)
        self.has = torch.zeros(n_max, dtype=torch.bool, device=device)
        self.rb = replay_batch

    def observe(self, x, y):
        f = self.feats(x)
        n_seen = self.fixed_seen or (y + 1)
        for _ in range(self.steps):
            xs, ys = f, torch.as_tensor([y], device=self.device)
            idx = torch.nonzero(self.has[:n_seen]).flatten()
            if len(idx):
                pick = idx[torch.randint(len(idx), (min(self.rb, len(idx)),), device=self.device)]
                xs, ys = torch.cat([xs, self.mem_x[pick]]), torch.cat([ys, pick])
            self.opt.zero_grad(set_to_none=True)
            F.cross_entropy(self.net(xs)[:, :n_seen], ys).backward()
            self.opt.step()
        self.mem_x[y], self.has[y] = f[0], True  # keep the latest example of each class

    def n_params(self):
        return sum(p.numel() for p in self.net.parameters())

    def flops_per_example(self):
        return 6 * self.n_params() * self.steps * (1 + self.rb)


class _Norm(nn.Module):
    """L2-normalised encoder output (the enc_norm preprocessing knob)."""

    def __init__(self, enc):
        super().__init__()
        self.enc, self.out_dim = enc, enc.out_dim

    def forward(self, x):
        return F.normalize(self.enc(x), dim=1)


class SLDA(Learner):
    """Deep Streaming LDA (Hayes & Kanan 2020): class means + one shared covariance,
    both updated online; prediction with a shrinkage-regularised precision matrix.
    The update matches the authors' reference code exactly, including weighting by the
    total update count n/(n+1) and class means starting at zero."""
    name = "slda"

    def __init__(self, n_max, device, encoder=None, shrinkage=1e-4, target="identity", enc_norm=False):
        # target: identity = original (scale-dependent); diag = shrink toward diag(cov), scale-free per feature
        assert target in ("identity", "diag")
        if enc_norm:
            encoder = _Norm(encoder)
        super().__init__(n_max, device, encoder)
        self.target = target
        d = self.enc.out_dim
        self.mu = torch.zeros(n_max, d, device=device)
        self.cnt = torch.zeros(n_max, device=device)
        self.cov = torch.zeros(d, d, device=device)
        self.n = 0
        self.eps = shrinkage
        self.prec = None
        self.g_mu, self.g_n = torch.zeros(d, device=device), 0

    def expose(self, x):
        """Unlabelled: streaming covariance around the running global mean (no class means touched)."""
        f = self.feats(x)[0]
        dlt = (f - self.g_mu)[:, None]
        self.cov = (self.n * self.cov + (dlt @ dlt.T) * self.n / (self.n + 1)) / (self.n + 1)
        self.g_mu += dlt[:, 0] / (self.g_n + 1)
        self.g_n += 1
        self.n += 1
        self.prec = None

    def observe(self, x, y):
        f = self.feats(x)[0]
        dlt = (f - self.mu[y])[:, None]
        delta = (dlt @ dlt.T) * self.n / (self.n + 1)
        self.cov = (self.n * self.cov + delta) / (self.n + 1)
        self.mu[y] += dlt[:, 0] / (self.cnt[y] + 1)
        self.cnt[y] += 1
        self.n += 1
        self.prec = None

    def logits(self, x, n_seen):
        if self.prec is None:
            d = self.cov.shape[0]
            if self.target == "diag":
                dg = self.cov.diagonal()
                dg = dg.clamp(min=1e-6 * float(dg.mean()) + 1e-12)  # dead units (zero variance) stay invertible
                tgt = torch.diag(dg)
            else:
                tgt = torch.eye(d, device=self.device)
            self.prec = torch.linalg.inv((1 - self.eps) * self.cov + self.eps * tgt)
        mu = self.mu[:n_seen]
        w = mu @ self.prec
        b = -0.5 * (w * mu).sum(1)
        return self.feats(x) @ w.T + b

    def n_params(self):
        return self.mu.numel() + self.cov.numel()

    def flops_per_example(self):
        d = self.cov.shape[0]
        return 2 * d * d + 2 * d * self.n_max


class Linear(OnlineMLP):
    """Regularised linear classifier on the input map (logistic regression, Adam + weight decay).
    Intended as the i.i.d. ceiling on frozen features (--iid); also runnable online."""
    name = "linear"

    def __init__(self, n_max, device, encoder=None, lr=1e-3, wd=1e-4, **kw):
        Learner.__init__(self, n_max, device, encoder)
        self.net = nn.Linear(self.enc.out_dim, n_max).to(device)
        self.opt = torch.optim.AdamW(self.net.parameters(), lr=lr, weight_decay=wd)
        self.steps = kw.get("steps_per_example", 1)


ENC_FLOPS_FWD_BWD = 56.9e6  # measured (torch FlopCounterMode) for the 3-block conv encoder, one 28x28 image
ENC_FLOPS_FWD = ENC_FLOPS_FWD_BWD / 3


def _trainable(enc):
    """Unfreeze encoder weights; BatchNorm stays in eval mode (frozen statistics: no AdaBN shortcut)."""
    import copy
    enc = copy.deepcopy(enc).eval()
    for p in enc.parameters():
        p.requires_grad_(True)
    return enc


class NeuromodEncoder(Learner):
    """Experiment 1: NCM readout on an encoder whose weights change online, gated by a
    neuromodulator g = sigmoid(k * (surprise + beta * novelty - running_mean(surprise))).
    gate: gated | one | const | shuffled (const/shuffled take gate_seq from the gated run).
    refresh: none (v1, drifting means) | exemplar (v2, 1 image per class shifts its mean)."""
    name = "neuromod"

    def __init__(self, n_max, device, encoder=None, lr=1e-4, tau=10.0, k=5.0, beta=0.5,
                 gate="gated", gate_seq=None, refresh="none", refresh_every=100):
        assert encoder is not None, "neuromod needs the pretrained encoder"
        super().__init__(n_max, device, encoder)
        self.enc = _trainable(encoder)
        self.opt = torch.optim.SGD(self.enc.parameters(), lr=lr)
        self.lr, self.tau, self.k, self.beta = lr, tau, k, beta
        self.gate, self.gate_seq, self.refresh, self.R = gate, gate_seq, refresh, refresh_every
        d = self.enc.out_dim
        self.sum = torch.zeros(n_max, d, device=device)
        self.cnt = torch.zeros(n_max, device=device)
        self.theta, self.t = 0.5, 0
        self.g_log, self.s_log = [], []
        self.ex = torch.zeros(n_max, 1, 28, 28, device=device)
        self.ex_f = torch.zeros(n_max, d, device=device)
        self.all_x, self.all_y = [], []  # oracle-refresh diagnostic only
        self.refresh_flops = 0.0

    def _means(self, n):
        return F.normalize(self.sum[:n] / self.cnt[:n, None].clamp(min=1), dim=1)

    def observe(self, x, y):
        self.all_x.append(x); self.all_y.append(y)
        n = y + 1
        f = F.normalize(self.enc(x), dim=1)
        mu = self._means(n).clone()
        novel = self.cnt[y] == 0
        if novel:
            mu[y] = f[0].detach()
        logit = self.tau * f @ mu.T
        loss = F.cross_entropy(logit, torch.as_tensor([y], device=self.device))
        s = 1 - torch.softmax(logit.detach(), 1)[0, y].item()
        g_gated = 1 / (1 + math.exp(-self.k * (s + self.beta * float(novel) - self.theta)))
        self.theta += (s - self.theta) / (self.t + 2)  # running mean of surprise
        if self.gate == "gated":
            g = g_gated
        elif self.gate == "one":
            g = 1.0
        else:  # const / shuffled: replayed from the gated run's per-seed g sequence
            g = float(self.gate_seq[self.t])
        self.g_log.append(g); self.s_log.append(s)
        if n > 1 and g > 0:
            for pg in self.opt.param_groups:
                pg["lr"] = self.lr * g
            self.opt.zero_grad(set_to_none=True)
            loss.backward()
            self.opt.step()
        self.sum[y] += f[0].detach()
        self.cnt[y] += 1
        if self.refresh == "exemplar":
            if novel:
                self.ex[y], self.ex_f[y] = x[0], f[0].detach()
            self.t += 1
            if self.t % self.R == 0:
                with torch.no_grad():
                    now = F.normalize(self.enc(self.ex[:n]), dim=1)
                self.sum[:n] += (now - self.ex_f[:n]) * self.cnt[:n, None]
                self.ex_f[:n] = now
                self.refresh_flops += n * ENC_FLOPS_FWD
        else:
            self.t += 1

    def logits(self, x, n_seen):
        with torch.no_grad():
            return F.normalize(self.enc(x), dim=1) @ self._means(n_seen).T

    @torch.no_grad()
    def oracle_means(self):
        """Diagnostic: every class mean recomputed from all its shots with the final encoder."""
        x = torch.cat(self.all_x); y = torch.as_tensor(self.all_y, device=self.device)
        f = torch.cat([F.normalize(self.enc(x[i:i + 1024]), dim=1) for i in range(0, len(x), 1024)])
        self.sum.zero_(); self.cnt.zero_()
        self.sum.index_add_(0, y, f); self.cnt.index_add_(0, y, torch.ones_like(y, dtype=torch.float))

    def adapted_encoder(self):
        return self.enc

    def n_params(self):
        return sum(p.numel() for p in self.enc.parameters()) + self.sum.numel()

    def flops_per_example(self):
        t = max(self.t, 1)
        return int(ENC_FLOPS_FWD_BWD + 2 * self.enc.out_dim * self.n_max + self.refresh_flops / t)


class ReplayFull(Learner):
    """Competitor: experience replay updating the WHOLE network (encoder + linear head),
    one stored image per class, `replay_batch` replayed images per step."""
    name = "replay_full"

    def __init__(self, n_max, device, encoder=None, lr=1e-3, replay_batch=1):
        assert encoder is not None
        super().__init__(n_max, device, encoder)
        self.enc = _trainable(encoder)
        self.head = nn.Linear(self.enc.out_dim, n_max).to(device)
        self.opt = torch.optim.SGD(list(self.enc.parameters()) + list(self.head.parameters()), lr=lr)
        self.ex = torch.zeros(n_max, 1, 28, 28, device=device)
        self.has = torch.zeros(n_max, dtype=torch.bool, device=device)
        self.rb = replay_batch

    def observe(self, x, y):
        n = y + 1
        xs, ys = x, torch.as_tensor([y], device=self.device)
        idx = torch.nonzero(self.has[:n]).flatten()
        if len(idx) and self.rb:
            pick = idx[torch.randint(len(idx), (min(self.rb, len(idx)),), device=self.device)]
            xs, ys = torch.cat([xs, self.ex[pick]]), torch.cat([ys, pick])
        self.opt.zero_grad(set_to_none=True)
        F.cross_entropy(self.head(self.enc(xs))[:, :n], ys).backward()
        self.opt.step()
        self.ex[y], self.has[y] = x[0], True

    def logits(self, x, n_seen):
        with torch.no_grad():
            return self.head(self.enc(x))[:, :n_seen]

    def adapted_encoder(self):
        return self.enc

    def n_params(self):
        return sum(p.numel() for p in self.enc.parameters()) + self.head.weight.numel()

    def flops_per_example(self):
        return int(ENC_FLOPS_FWD_BWD * (1 + self.rb))


class _Concat(nn.Module):
    """[frozen encoder features (L2-normalised), sparse plastic-layer responses]."""

    def __init__(self, enc, heb, enc_norm=True):
        super().__init__()
        self.enc, self.heb, self.enc_norm = enc, heb, enc_norm
        self.out_dim = enc.out_dim + heb.K

    def forward(self, x):
        raw = self.enc(x)
        f = F.normalize(raw, dim=1)  # the plastic layer always sees the normalised input (cosine units)
        return torch.cat([f if self.enc_norm else raw, self.heb.respond(f)], 1)


class Hebbian(Learner):
    """Experiment 2: plastic layer of K units on the frozen encoder, learned WITHOUT labels and
    without backprop: competitive (k-WTA) Hebbian learning with Oja normalisation and homeostatic
    thresholds, gated by novelty g = sigmoid(kg * (novelty - running mean)), novelty = 1 - max cos.
    Readout: SLDA on [encoder, plastic] (unlabelled exposure also feeds SLDA's covariance).
    rule: oja | kmeans (online sequential k-means, competitor) ; gate: gated | one | const | shuffled.
    eta = 0 keeps the random init frozen (sanity row = random-init ablation)."""
    name = "hebbian"

    def __init__(self, n_max, device, encoder=None, K=512, eta=1e-2, kwta=4, kg=5.0, rho=1e-3,
                 shrinkage=0.9, rule="oja", gate="gated", gate_seq=None, stream_plastic=False, cov_src="exposure",
                 target="identity", enc_norm=True):
        assert encoder is not None and cov_src in ("exposure", "labelled")
        self.cov_src = cov_src
        super().__init__(n_max, device, encoder)
        d = self.enc.out_dim
        gen = torch.Generator(device="cpu").manual_seed(12345)
        self.W = F.normalize(torch.randn(K, d, generator=gen), dim=1).to(device)
        self.b = torch.zeros(K, device=device)
        self.cnt_k = torch.zeros(K, device=device)
        self.K, self.eta, self.kwta, self.kg, self.rho = K, eta, kwta, kg, rho
        self.rule, self.gate, self.gate_seq, self.stream_plastic = rule, gate, gate_seq, stream_plastic
        self.nov_mean, self.t = 0.5, 0
        self.g_log, self.s_log = [], []
        self.enc_norm = enc_norm
        self.slda = SLDA(n_max, device, encoder=_Concat(self.enc, self, enc_norm), shrinkage=shrinkage, target=target)

    def respond(self, f):
        a = f @ self.W.T - self.b
        top = a.topk(self.kwta, dim=1)
        return torch.zeros_like(a).scatter_(1, top.indices, top.values.clamp(min=0))

    @torch.no_grad()
    def _plastic(self, x):
        f = F.normalize(self.enc(x), dim=1)[0]
        a = self.W @ f
        nov = 1 - a.max().item()
        g_gated = 1 / (1 + math.exp(-self.kg * (nov - self.nov_mean)))
        self.nov_mean += (nov - self.nov_mean) / (self.t + 2)
        g = g_gated if self.gate == "gated" else 1.0 if self.gate == "one" else float(self.gate_seq[self.t])
        self.g_log.append(g); self.s_log.append(nov)
        self.t += 1
        win = (a - self.b).topk(self.kwta).indices
        if self.rule == "kmeans":  # sequential k-means: winner moves to its running mean
            self.cnt_k[win] += 1
            self.W[win] += (f - self.W[win]) / self.cnt_k[win, None]
        elif self.eta > 0:
            y = a[win].clamp(min=0)[:, None]
            self.W[win] += self.eta * g * y * (f[None] - y * self.W[win])  # Oja: Hebbian growth, self-normalising
        hit = torch.zeros(self.K, device=self.device); hit[win] = 1
        self.b += self.rho * (hit - self.kwta / self.K)  # homeostasis: busy units raise their threshold

    def expose(self, x):
        self._plastic(x)
        self.slda.expose(x)  # always accumulated; end_exposure() discards it if cov_src == "labelled"

    def end_exposure(self):
        """cov_src (tuned): "exposure" keeps the unlabelled covariance; "labelled" resets SLDA so its
        covariance comes from labelled within-class spread only. The plastic layer is unaffected."""
        if self.cov_src == "labelled":
            s = self.slda
            s.cov.zero_(); s.g_mu.zero_(); s.n = 0; s.g_n = 0; s.prec = None

    def observe(self, x, y):
        if self.stream_plastic:
            self._plastic(x)
        self.slda.observe(x, y)

    def logits(self, x, n_seen):
        return self.slda.logits(x, n_seen)

    def adapted_encoder(self):
        return _Concat(self.enc, self)

    def n_params(self):
        return self.W.numel() + self.slda.n_params()

    def flops_per_example(self):
        return int(2 * self.enc.out_dim * self.K * 2 + self.slda.flops_per_example())


REGISTRY = {c.name: c for c in (NCM, OnlineMLP, OnlineEWC, OnlineViT, Replay, SLDA, Linear, NeuromodEncoder, ReplayFull, Hebbian)}
