"""Build the Brain-view human brain shell + region anchors from BodyParts3D.

Runs with plain CPython + numpy (NOT inside Unreal):

    python Scripts/build_assets/prepare_brain_mesh.py <dir with partof_BP3D_4.0_obj_99.zip + partof_element_parts.txt>

Source: BodyParts3D, (c) The Database Center for Life Science, CC-BY-SA 2.1 Japan.
https://dbarchive.biosciencedbc.jp/en/bodyparts3d/

Outputs:
  Scripts/build_assets/meshes/SM_BrainShell.glb   cerebrum lobes + cerebellum + brainstem (3 slots)
  Source/MetaBrainViz/Private/MetaBrainBrainAtlasData.h
        anatomical anchor (cm, UE frame) + radius for every sim region

Frame: UE (X anterior, Y right, Z up), brain centred on the origin,
MM_TO_CM = scene centimetres per real millimetre.
"""
import json
import os
import sys
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from prepare_neuron_mesh import write_glb  # noqa: E402

OUT_DIR = os.path.join(HERE, "meshes")
HEADER = os.path.join(REPO, "Source", "MetaBrainViz", "Private", "MetaBrainBrainAtlasData.h")
MM_TO_CM = 5.4          # 170 mm brain -> ~9 m in scene (old layout envelope ~6.5 m + rails)
TARGET_TRIS = 520_000   # shell is translucent (no Nanite); fine on a modern GPU

CEREBRUM = ["FMA72969", "FMA72970", "FMA72971", "FMA72972", "FMA72973", "FMA72974", "FMA72975", "FMA72976"]
CEREBELLUM = ["FMA67944"]
BRAINSTEM = ["FMA79876"]
ANCHOR_PARTS = {
    "HippoL": ["FMA72714"], "HippoR": ["FMA72713"],
    "FrontalL": ["FMA72970"], "FrontalR": ["FMA72969"],
    "ParietalL": ["FMA72974"], "ParietalR": ["FMA72973"],
    "Hypothalamus": ["FMA62008"], "Cerebellum": CEREBELLUM,
    "Occipital": ["FMA72975", "FMA72976"],
    "HemiL": ["FMA72970", "FMA72972", "FMA72974", "FMA72976"],
    "HemiR": ["FMA72969", "FMA72971", "FMA72973", "FMA72975"],
}


SHEET_N = 640


def load_elements(src_dir):
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(src_dir, "partof_element_parts.txt"), encoding="utf-8")][1:]
    by_concept = {}
    for r in rows:
        if len(r) >= 3:
            by_concept.setdefault(r[0], []).append(r[2])
    return by_concept


def read_obj(z, fj):
    v, f = [], []
    for line in z.read(f"partof_BP3D_4.0_obj_99/{fj}.obj").decode("utf-8", "ignore").splitlines():
        if line.startswith("v "):
            v.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("f "):
            f.append([int(t.split("/")[0]) - 1 for t in line.split()[1:4]])
    return np.array(v), np.array(f)


def gather(z, by_concept, concepts, cache):
    V, F, off = [], [], 0
    for c in concepts:
        for fj in by_concept.get(c, []):
            if fj not in cache:
                cache[fj] = read_obj(z, fj)
            v, f = cache[fj]
            V.append(v); F.append(f + off); off += len(v)
    return np.vstack(V), np.vstack(F)


def decimate(v, f, target):
    """Vertex-clustering decimation (fast, robust, good enough for a translucent shell)."""
    if len(f) <= target:
        return v, f
    span = np.ptp(v, axis=0).max()
    cell = span / 400.0
    for _ in range(30):
        key = np.floor(v / cell).astype(np.int64)
        _, inv = np.unique(key, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        f2 = inv[f]
        f2 = f2[(f2[:, 0] != f2[:, 1]) & (f2[:, 1] != f2[:, 2]) & (f2[:, 0] != f2[:, 2])]
        f2 = np.unique(np.sort(f2, axis=1), axis=0, return_index=True)[1]
        if len(f2) <= target:
            break
        cell *= 1.15
    nv = np.zeros((inv.max() + 1, 3)); cnt = np.zeros(inv.max() + 1)
    np.add.at(nv, inv, v); np.add.at(cnt, inv, 1)
    nv /= cnt[:, None]
    ff = inv[f]
    ff = ff[(ff[:, 0] != ff[:, 1]) & (ff[:, 1] != ff[:, 2]) & (ff[:, 0] != ff[:, 2])]
    _, idx = np.unique(np.sort(ff, axis=1), axis=0, return_index=True)
    return nv, ff[np.sort(idx)]


def taubin_smooth(v, f, iters=12, lam=0.5, mu=-0.53):
    """Volume-preserving smoothing: removes the faceted look of the source scan without shrinking it."""
    n = len(v)
    e = np.vstack([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]])
    e = np.vstack([e, e[:, ::-1]])
    deg = np.bincount(e[:, 0], minlength=n).astype(float)
    deg[deg == 0] = 1
    for _ in range(iters):
        for w in (lam, mu):
            acc = np.zeros_like(v)
            np.add.at(acc, e[:, 0], v[e[:, 1]])
            v = v + w * (acc / deg[:, None] - v)
    return v


def outer_envelope(v, f, bins=(48, 96), keep=0.86):
    """Lobes are separate closed meshes, so a translucent render shows their shared inner
    walls as crinkled lines. Keep only triangles near the outer envelope: per direction from
    the brain centre, triangles within `keep` x the max radius in that direction."""
    c = (v.min(0) + v.max(0)) / 2
    tc = v[f].mean(1) - c
    r = np.linalg.norm(tc, axis=1)
    th = np.clip((np.arccos(np.clip(tc[:, 2] / np.maximum(r, 1e-9), -1, 1)) / np.pi * bins[0]).astype(int), 0, bins[0] - 1)
    ph = np.clip(((np.arctan2(tc[:, 1], tc[:, 0]) + np.pi) / (2 * np.pi) * bins[1]).astype(int), 0, bins[1] - 1)
    cell = th * bins[1] + ph
    rmax = np.zeros(bins[0] * bins[1])
    np.maximum.at(rmax, cell, r)
    return f[r >= keep * rmax[cell]]


def normals(v, f):
    n = np.zeros_like(v)
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    for k in range(3):
        np.add.at(n, f[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


def main(src_dir):
    by_concept = load_elements(src_dir)
    z = zipfile.ZipFile(os.path.join(src_dir, "partof_BP3D_4.0_obj_99.zip"))
    cache = {}

    parts = {k: gather(z, by_concept, c, cache) for k, c in ANCHOR_PARTS.items()}
    cen = {k: p[0].mean(0) for k, p in parts.items()}

    # Derive anatomical axes from the data itself (BodyParts3D frame differs from UE)
    right = cen["HemiR"] - cen["HemiL"]; right /= np.linalg.norm(right)
    anterior = cen["FrontalL"] + cen["FrontalR"] - 2 * cen["Occipital"]  # both at mid-height -> level axis
    anterior -= right * np.dot(anterior, right); anterior /= np.linalg.norm(anterior)
    up = np.cross(anterior, right)  # left-handed UE: X fwd, Y right, Z up => Z = X x Y
    if np.dot(up, cen["ParietalL"] - cen["Hypothalamus"]) < 0:
        up = -up
    Rm = np.stack([anterior, right, up])

    cer_v, cer_f = gather(z, by_concept, CEREBRUM, cache)
    origin = (cer_v.min(0) + cer_v.max(0)) / 2

    def to_ue(p):
        return ((p - origin) @ Rm.T) * MM_TO_CM

    # ---- anchors (cm, UE frame) ----
    def anc(k):
        return to_ue(cen[k])

    hippo_l, hippo_r = anc("HippoL"), anc("HippoR")
    # Amygdala: ~10 mm anterior + slightly superior of the hippocampal head (no BP3D mesh)
    def head(k):
        pts = to_ue(parts[k][0]); return pts[np.argsort(-pts[:, 0])[: max(20, len(pts) // 15)]].mean(0)
    amyg_l = head("HippoL") + np.array([10.0, 0.0, 3.0]) * MM_TO_CM
    amyg_r = head("HippoR") + np.array([10.0, 0.0, 3.0]) * MM_TO_CM
    # Prefrontal = anterior third of the frontal lobes
    def anterior_part(k, frac=0.33):
        pts = to_ue(parts[k][0]); return pts[pts[:, 0] > np.percentile(pts[:, 0], 100 * (1 - frac))].mean(0)
    pfc_l, pfc_r = anterior_part("FrontalL"), anterior_part("FrontalR")
    par_l, par_r = anc("ParietalL"), anc("ParietalR")
    # Thalamus sits dorsal/posterior to the hypothalamus on the midline
    thal = anc("Hypothalamus") + np.array([-10.0, 0.0, 14.0]) * MM_TO_CM
    cereb = anc("Cerebellum")

    anchors = [  # name, left, right, radius_cm
        ("Hippocampus", hippo_l, hippo_r, 13 * MM_TO_CM),
        ("Amygdala", amyg_l, amyg_r, 8 * MM_TO_CM),
        ("Prefrontal", pfc_l, pfc_r, 22 * MM_TO_CM),
        ("Association", par_l, par_r, 24 * MM_TO_CM),
        ("Thalamus", thal, thal, 10 * MM_TO_CM),
        ("Cerebellum", cereb, cereb, 22 * MM_TO_CM),
    ]

    # ---- shell mesh ----
    os.makedirs(OUT_DIR, exist_ok=True)
    prims = []
    shells = {}
    budget = {"Cerebrum": 0.72, "Cerebellum": 0.18, "Brainstem": 0.10}
    for name, concepts in (("Cerebrum", CEREBRUM), ("Cerebellum", CEREBELLUM), ("Brainstem", BRAINSTEM)):
        v, f = gather(z, by_concept, concepts, cache)
        v, f = decimate(v, f, int(TARGET_TRIS * budget[name]))
        v = taubin_smooth(v, f)
        if name == "Cerebrum":
            f = outer_envelope(v, f)
        shells[name] = (to_ue(v), f)
        p = to_ue(v) / 100.0                      # cm -> m for glTF
        p = p * np.array([1.0, -1.0, 1.0])        # pre-undo the importer's Y mirror
        f = f[:, ::-1]                            # mirror flips winding
        n = normals(p, f)
        # write_glb expects the MetaBrain Z-up frame (it converts to glTF Y-up itself)
        prims.append((name, p, n, f, name, (0.80, 0.70, 0.68, 1.0), 0.6))
        print(f"{name:10s} {len(p):7d} verts {len(f):7d} tris")
    write_glb(os.path.join(OUT_DIR, "SM_BrainShell.glb"), prims)

    ext = to_ue(cer_v)
    with open(os.path.join(OUT_DIR, "SM_BrainShell.bounds.json"), "w") as fjson:
        json.dump({"min_cm": ext.min(0).round(1).tolist(), "max_cm": ext.max(0).round(1).tolist()}, fjson)

    L = ["// GENERATED by Scripts/build_assets/prepare_brain_mesh.py - do not edit.",
         "// BodyParts3D (c) DBCLS, CC-BY-SA 2.1 JP. Units: scene cm, UE frame (X anterior, Y right, Z up).",
         "#pragma once", "", "namespace MetaBrainBrainAtlas", "{",
         f"\tstatic constexpr double MmToCm = {MM_TO_CM};",
         f"\tstatic constexpr int32 NumAnchors = {len(anchors)};",
         "\tstruct FAnchor { const TCHAR* Region; double Left[3]; double Right[3]; double RadiusCm; };",
         "\tstatic const FAnchor Anchors[] = {"]
    for name, l, r, rad in anchors:
        L.append(f'\t\t{{ TEXT("{name}"), {{ {l[0]:.1f}, {l[1]:.1f}, {l[2]:.1f} }}, {{ {r[0]:.1f}, {r[1]:.1f}, {r[2]:.1f} }}, {rad:.1f} }},')
    L += ["\t};"]

    # ---- cortical sheets: neurons of cortical structures live ON the folded surface ----
    # Per (anchor, side): SHEET_N surface samples (position cm + inward unit normal), taken
    # from the pial envelope nearest the anchor (cerebellum: its own folia surface).
    rng = np.random.default_rng(7)

    def sheet(name, anchor_pt, side):
        v, f = shells["Cerebellum" if name == "Cerebellum" else "Cerebrum"]
        n = normals(v, f)
        c = v.mean(0)
        n[np.sum(n * (v - c), axis=1) < 0] *= -1      # outward
        used = np.unique(f)
        v, n = v[used], n[used]
        keep = (v[:, 1] * side) > 2.0
        v, n = v[keep], n[keep]
        d = np.linalg.norm(v - anchor_pt, axis=1)
        idx = rng.choice(np.argsort(d)[: SHEET_N * 3], SHEET_N, replace=False)
        return v[idx], -n[idx]

    sheets = []
    for name, l, r, rad in anchors:
        sheets.append((sheet(name, l, -1), sheet(name, r, +1)) if name in ("Prefrontal", "Association", "Cerebellum") else None)
    L.append(f"\tstatic constexpr int32 SheetN = {SHEET_N};")
    L.append("\t// [anchor][side L/R][sample] = position cm xyz + inward unit normal xyz")
    L.append("\tstatic constexpr bool HasSheet[] = { " + ", ".join("true" if x else "false" for x in sheets) + " };")
    L.append(f"\tstatic const float Sheet[{len(anchors)}][2][{SHEET_N}][6] = {{")
    for x in sheets:
        L.append("\t\t{")
        for side in range(2):
            if x is None:
                L.append("\t\t\t{ { 0 } },")
                continue
            pts, nn = x[side]
            L.append("\t\t\t{ " + ", ".join("{%.1ff,%.1ff,%.1ff,%.3ff,%.3ff,%.3ff}" % (*a, *b) for a, b in zip(pts, nn)) + " },")
        L.append("\t\t},")
    L += ["\t};", "}", ""]
    open(HEADER, "w", newline="\n").write("\n".join(L))
    for name, l, r, rad in anchors:
        print(f"{name:12s} L {l.round(0)} R {r.round(0)} r {rad:.0f}")
    print("cerebrum extent cm", ext.min(0).round(0), ext.max(0).round(0))


if __name__ == "__main__":
    main(sys.argv[1])
