"""Turn real neuron reconstructions (NeuroMorpho.org .swc) into MetaBrain meshes.

Runs with plain CPython + numpy (NOT inside Unreal):

    python Scripts/build_assets/swc_to_mesh.py

Inputs : Scripts/build_assets/swc/<variant>.swc  (see swc/SOURCES.md for provenance)
Outputs: Scripts/build_assets/meshes/SM_Neuron_<Variant>.glb
         Source/MetaBrainViz/Private/MetaBrainNeuronVariantsData.h

Same frame as prepare_neuron_mesh.py: soma at origin, axon initial segment along +Z,
soma radius 0.5 m (= 50 UE units). The arbor is normalised so its 95th-percentile
reach is ARBOR_REACH soma radii and branch thickness is exaggerated so it stays
visible at brain scale (true proportions would make dendrites sub-pixel).

The axon is cut after the initial segment: in Brain view every graph edge *is* an
axon, so the reconstruction's own axon would be a second, dangling one.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from prepare_neuron_mesh import write_glb  # noqa: E402

SWC_DIR = os.path.join(HERE, "swc")
OUT_DIR = os.path.join(HERE, "meshes")
HEADER = os.path.join(REPO, "Source", "MetaBrainViz", "Private", "MetaBrainNeuronVariantsData.h")

# Order defines the variant index used by C++ (EMetaBrainNeuronVariant)
VARIANTS = [
    ("PyramidalCortex", "pyramidal_cortex.swc"),
    ("PyramidalCA1", "pyramidal_ca1.swc"),
    ("Interneuron", "interneuron.swc"),
    ("Purkinje", "purkinje.swc"),
]

SOMA_R = 0.5          # m, mesh-local
ARBOR_REACH = 6.0     # soma radii (95th percentile dendrite reach)
THICKEN = 1.8         # branch radius exaggeration vs true proportion
MIN_R, MAX_R = 0.009, 0.16
SIDES = 10
MIN_SPACING = 0.09    # m between kept nodes along unbranched chains
HILLOCK_LEN = 1.6     # m of axon kept (initial segment); edges take over after
MAX_TIPS = 24


def load_swc(path):
    rows = []
    for line in open(path, encoding="utf-8", errors="ignore"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split()
        rows.append((int(p[0]), int(p[1]), float(p[2]), float(p[3]), float(p[4]), float(p[5]), int(p[6])))
    ids = {r[0]: i for i, r in enumerate(rows)}
    typ = np.array([r[1] for r in rows])
    pos = np.array([r[2:5] for r in rows])
    rad = np.array([r[5] for r in rows])
    par = np.array([ids.get(r[6], -1) for r in rows])
    return typ, pos, rad, par


def frame_basis(z):
    z = z / np.linalg.norm(z)
    helper = np.array([0.0, 0.0, 1.0]) if abs(z[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = np.cross(helper, z); x /= np.linalg.norm(x)
    return np.stack([x, np.cross(z, x), z])


def uv_sphere(r, nu=18, nv=12):
    P, N, I = [], [], []
    for j in range(nv + 1):
        v = np.pi * j / nv
        for i in range(nu):
            u = 2 * np.pi * i / nu
            n = np.array([np.sin(v) * np.cos(u), np.sin(v) * np.sin(u), np.cos(v)])
            P.append(n * r); N.append(n)
    for j in range(nv):
        for i in range(nu):
            a = j * nu + i; b = j * nu + (i + 1) % nu
            c = a + nu; d = b + nu
            I += [(a, c, b), (b, c, d)]
    return np.array(P), np.array(N), np.array(I)


def build(name, path):
    typ, pos, rad, par = load_swc(path)
    n = len(typ)
    root = int(np.where(par < 0)[0][0])
    soma_c = pos[typ == 1].mean(0) if (typ == 1).any() else pos[root]
    soma_r_um = max(float(rad[root]), 2.0)

    children = [[] for _ in range(n)]
    for i, p in enumerate(par):
        if p >= 0:
            children[p].append(i)

    dend = (typ == 3) | (typ == 4)
    axon = typ == 2

    # Axon direction: first ~30 um of axon, else opposite of the apical/longest dendrite mass
    if axon.any():
        d = np.linalg.norm(pos - soma_c, axis=1)
        near = axon & (d < np.percentile(d[axon], 15))
        ax_dir = pos[near].mean(0) - soma_c
    else:
        apical = typ == 4
        ref = pos[apical] if apical.any() else pos[dend]
        ax_dir = soma_c - ref.mean(0)
    if np.linalg.norm(ax_dir) < 1e-6:
        ax_dir = np.array([0.0, 0.0, -1.0])
    R = frame_basis(ax_dir)

    reach = np.percentile(np.linalg.norm(pos[dend] - soma_c, axis=1), 95)
    scale = ARBOR_REACH * SOMA_R / reach            # um -> m
    p_loc = (pos - soma_c) @ R.T * scale
    r_loc = np.clip(rad * scale * THICKEN, MIN_R, MAX_R)

    # Keep dendrites everywhere + axon only within the initial segment
    keep = dend.copy()
    if axon.any():
        keep |= axon & (np.linalg.norm(p_loc, axis=1) < HILLOCK_LEN + SOMA_R)
    keep[root] = True

    # Synthetic initial segment for reconstructions without an axon
    extra_seg = None
    if not axon.any():
        extra_seg = [(np.array([0, 0, SOMA_R * 0.8]), 0.09), (np.array([0, 0, SOMA_R + HILLOCK_LEN]), 0.03)]

    # Build chains (branch point -> branch point/tip) over kept nodes, resampled
    P_all, N_all, I_all = [], [], []
    off = 0

    def add(p, nn, ii):
        nonlocal off
        P_all.append(p); N_all.append(nn); I_all.append(ii + off); off += len(p)

    def tube(points, radii):
        if len(points) < 2:
            return
        pts = np.array(points); rs = np.array(radii)
        t = np.gradient(pts, axis=0)
        t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
        ref = np.array([0.0, 0.0, 1.0]) if abs(t[0][2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u = np.cross(t[0], ref); u /= np.linalg.norm(u)
        rings_p, rings_n = [], []
        ang = np.linspace(0, 2 * np.pi, SIDES, endpoint=False)
        for k in range(len(pts)):
            u = u - t[k] * np.dot(u, t[k])  # parallel transport
            u /= max(np.linalg.norm(u), 1e-9)
            v = np.cross(t[k], u)
            ring_n = np.cos(ang)[:, None] * u + np.sin(ang)[:, None] * v
            rings_n.append(ring_n); rings_p.append(pts[k] + ring_n * rs[k])
        P = np.vstack(rings_p); N = np.vstack(rings_n)
        I = []
        for k in range(len(pts) - 1):
            for s in range(SIDES):
                a = k * SIDES + s; b = k * SIDES + (s + 1) % SIDES
                c = a + SIDES; d = b + SIDES
                I += [(a, b, c), (b, d, c)]
        # cap the far end
        tip = len(P)
        P = np.vstack([P, pts[-1] + t[-1] * rs[-1]]); N = np.vstack([N, t[-1]])
        base = (len(pts) - 1) * SIDES
        for s in range(SIDES):
            I.append((base + s, base + (s + 1) % SIDES, tip))
        add(P, N, np.array(I))

    tips = []
    stack = [(c, root) for c in children[root] if keep[c]]
    while stack:
        start, from_node = stack.pop()
        chain = [from_node, start]
        cur = start
        while True:
            kc = [c for c in children[cur] if keep[c]]
            if len(kc) == 1:
                cur = kc[0]; chain.append(cur)
                continue
            for c in kc:
                stack.append((c, cur))
            if not kc and dend[cur]:
                tips.append(p_loc[cur])
            break
        # resample: keep endpoints + nodes at least MIN_SPACING apart
        pts, rs = [p_loc[chain[0]]], [r_loc[chain[1]]]
        for node in chain[1:]:
            if np.linalg.norm(p_loc[node] - pts[-1]) >= MIN_SPACING or node == chain[-1]:
                pts.append(p_loc[node]); rs.append(r_loc[node])
        # start trunks from inside the soma so they merge into it
        if chain[0] == root:
            dvec = pts[1] - pts[0]
            pts[0] = dvec / max(np.linalg.norm(dvec), 1e-9) * SOMA_R * 0.6
            rs[0] = max(rs[1], 0.12)
        tube(pts, rs)

    if extra_seg:
        tube([s[0] for s in extra_seg], [s[1] for s in extra_seg])

    sp, sn, si = uv_sphere(SOMA_R)
    add(sp, sn, si)

    P = np.vstack(P_all); N = np.vstack(N_all); I = np.vstack(I_all)

    # Docking tips: farthest-point sample of dendrite terminals
    tips = np.array(tips) if tips else np.zeros((1, 3))
    chosen = [int(np.argmax(np.linalg.norm(tips, axis=1)))]
    while len(chosen) < min(MAX_TIPS, len(tips)):
        dmin = np.min(np.linalg.norm(tips[:, None] - tips[chosen][None], axis=2), axis=1)
        chosen.append(int(np.argmax(dmin)))
    hill = np.array([0.0, 0.0, SOMA_R + HILLOCK_LEN])
    return P, N, I, tips[chosen], hill


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    flip = np.array([1.0, -1.0, 1.0])  # glTF -> UE import mirrors Y
    lines = [
        "// GENERATED by Scripts/build_assets/swc_to_mesh.py - do not edit.",
        "// Real human neuron reconstructions from NeuroMorpho.org (see Scripts/build_assets/swc/SOURCES.md).",
        "// Units: soma radii, UE mesh-local frame (soma centre = origin, axon along +Z).",
        "#pragma once", "", "namespace MetaBrainNeuronVariants", "{",
        f"\tstatic constexpr int32 NumVariants = {len(VARIANTS)};",
        "\tstatic constexpr const TCHAR* MeshPaths[] = {",
    ]
    for name, _ in VARIANTS:
        lines.append(f'\t\tTEXT("/Game/MetaBrain/Meshes/SM_Neuron_{name}.SM_Neuron_{name}"),')
    lines.append("\t};")
    tip_blocks, counts, hills = [], [], []
    for name, fn in VARIANTS:
        P, N, I, tips, hill = build(name, os.path.join(SWC_DIR, fn))
        write_glb(os.path.join(OUT_DIR, f"SM_Neuron_{name}.glb"),
                  [("Membrane", P, N, I, "Membrane", (0.42, 0.40, 0.39, 1.0), 0.55)])
        print(f"{name:16s} {len(P):7d} verts {len(I):7d} tris  {len(tips)} tips")
        counts.append(len(tips)); hills.append(hill)
        tip_blocks.append([t * flip / SOMA_R for t in tips])
    lines.append(f"\tstatic constexpr int32 MaxTips = {MAX_TIPS};")
    lines.append("\tstatic constexpr int32 NumTips[] = { " + ", ".join(map(str, counts)) + " };")
    lines.append("\tstatic constexpr double HillockTipZ[] = { " + ", ".join(f"{h[2] / SOMA_R:.4f}" for h in hills) + " };")
    lines.append("\tstatic constexpr double Tips[][MaxTips][3] = {")
    for blk in tip_blocks:
        lines.append("\t\t{")
        for t in blk:
            lines.append(f"\t\t\t{{ {t[0]:.4f}, {t[1]:.4f}, {t[2]:.4f} }},")
        lines.append("\t\t},")
    lines += ["\t};", "}", ""]
    open(HEADER, "w", newline="\n").write("\n".join(lines))


if __name__ == "__main__":
    main()
