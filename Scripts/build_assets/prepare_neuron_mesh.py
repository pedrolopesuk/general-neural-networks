"""Prepare the scanned/sculpted neuron GLB for MetaBrain.

Runs with plain CPython + numpy (NOT inside Unreal):

    python Scripts/build_assets/prepare_neuron_mesh.py C:/path/to/neuron.glb

Source model: "Neuron" by Versal, CC-BY-4.0
https://sketchfab.com/3d-models/neuron-d40557a1e4154267b78117433bc51296

Outputs (all deterministic):
  Scripts/build_assets/meshes/SM_NeuronSoma.glb   soma + dendrites + axon initial segment.
                                                   One material slot. Used by the per-neuron
                                                   HISM (graph edges play the role of the axon).
  Scripts/build_assets/meshes/SM_NeuronFull.glb   whole cell: membrane / myelin / Schwann nuclei
                                                   as 3 material slots. Used for the focused
                                                   "hero" neuron.
  Source/MetaBrainViz/Private/MetaBrainNeuronMorphologyData.h
                                                   dendrite tip + hillock anchor points so synapse
                                                   edges dock onto real dendrite tips.

Normalised frame (both meshes): soma centre at origin, axon leaves along +Z,
soma radius = 0.5 m (= 50 UE units, same convention as /Engine/BasicShapes/Sphere).
"""
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_DIR = os.path.join(HERE, "meshes")
HEADER = os.path.join(REPO, "Source", "MetaBrainViz", "Private", "MetaBrainNeuronMorphologyData.h")

# Mesh indices inside the Sketchfab export (verified by bounding boxes)
BODY_MESH = 13                 # membrane: soma, dendrites, axon, terminals
MYELIN_MESHES = range(6, 13)   # 7 elongated sheath segments
SCHWANN_MESHES = range(0, 6)   # 6 small nuclei sitting on the sheaths


# ---------------------------------------------------------------- GLB read
def load_glb(path):
    d = open(path, "rb").read()
    jl = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + jl])
    b = d[20 + jl + 8:]
    return j, b


def accessor(j, b, i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[a["type"]]
    dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a["componentType"]]
    item = np.dtype(dt).itemsize * n
    st = bv.get("byteStride", item)
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    rows = [np.frombuffer(b, np.uint8, count=item, offset=off + k * st) for k in range(a["count"])] if st != item else None
    if rows is None:
        return np.frombuffer(b, dt, count=a["count"] * n, offset=off).reshape(a["count"], n).astype(np.float64 if dt == np.float32 else np.int64)
    return np.stack(rows).view(dt).reshape(a["count"], n).astype(np.float64 if dt == np.float32 else np.int64)


def world_matrices(j):
    out = {}

    def walk(i, m):
        n = j["nodes"][i]
        local = np.array(n["matrix"], float).reshape(4, 4).T if "matrix" in n else np.eye(4)
        m = m @ local
        if "mesh" in n:
            out[n["mesh"]] = m
        for c in n.get("children", []):
            walk(c, m)

    for root in j["scenes"][j.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))
    return out


def mesh_world(j, b, mi, mats):
    p = j["meshes"][mi]["primitives"][0]
    pos = accessor(j, b, p["attributes"]["POSITION"])
    nrm = accessor(j, b, p["attributes"]["NORMAL"])
    idx = accessor(j, b, p["indices"]).reshape(-1, 3)
    m = mats[mi]
    pos = (np.c_[pos, np.ones(len(pos))] @ m.T)[:, :3]
    nm = np.linalg.inv(m[:3, :3]).T
    nrm = nrm @ nm.T
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    if np.linalg.det(m[:3, :3]) < 0:
        idx = idx[:, ::-1]
    return pos, nrm, idx


# ---------------------------------------------------------------- GLB write
def write_glb(path, prims):
    """prims: list of (name, pos, nrm, idx, material_name, base_rgba, roughness)."""
    blob = bytearray()
    views, accs, gl_prims, mats = [], [], [], []

    def add(arr, target, ctype, typ, minmax=False):
        nonlocal blob
        while len(blob) % 4:
            blob.append(0)
        raw = arr.tobytes()
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw), "target": target})
        blob += raw
        a = {"bufferView": len(views) - 1, "componentType": ctype, "count": int(arr.shape[0]), "type": typ}
        if minmax:
            a["min"] = arr.min(0).tolist()
            a["max"] = arr.max(0).tolist()
        accs.append(a)
        return len(accs) - 1

    for name, pos, nrm, idx, mname, rgba, rough in prims:
        # MetaBrain frame is Z-up (axon +Z); glTF is Y-up. Rotate -90deg about X so
        # the importer's Y-up -> Z-up conversion lands the axon on UE +Z.
        pos = np.c_[pos[:, 0], pos[:, 2], -pos[:, 1]]
        nrm = np.c_[nrm[:, 0], nrm[:, 2], -nrm[:, 1]]
        ip = add(pos.astype(np.float32), 34962, 5126, "VEC3", True)
        inn = add(nrm.astype(np.float32), 34962, 5126, "VEC3")
        ii = add(idx.astype(np.uint32).reshape(-1), 34963, 5125, "SCALAR")
        mats.append({"name": mname, "doubleSided": False,
                     "pbrMetallicRoughness": {"baseColorFactor": rgba, "metallicFactor": 0.0, "roughnessFactor": rough}})
        gl_prims.append({"attributes": {"POSITION": ip, "NORMAL": inn}, "indices": ii, "material": len(mats) - 1, "mode": 4})

    name = os.path.splitext(os.path.basename(path))[0]
    gj = {
        "asset": {"version": "2.0", "generator": "MetaBrain prepare_neuron_mesh.py",
                  "copyright": "Neuron by Versal (sketchfab.com/versal), CC-BY-4.0"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": gl_prims}],
        "materials": mats, "accessors": accs, "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    js = json.dumps(gj, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(blob) % 4:
        blob.append(0)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))


def compact(pos, nrm, idx):
    used = np.unique(idx)
    remap = -np.ones(len(pos), np.int64)
    remap[used] = np.arange(len(used))
    return pos[used], nrm[used], remap[idx]


# ---------------------------------------------------------------- main
def main(src):
    j, b = load_glb(src)
    mats = world_matrices(j)
    body_p, body_n, body_i = mesh_world(j, b, BODY_MESH, mats)

    myelin = [mesh_world(j, b, m, mats) for m in MYELIN_MESHES]
    schwann = [mesh_world(j, b, m, mats) for m in SCHWANN_MESHES]
    myelin_centres = np.array([p.mean(0) for p, _, _ in myelin])

    # Soma = the one big sphere in the membrane. Every vertex on a sphere of radius r
    # "votes" for p - n*r; the soma's votes pile up on one point, tube-shaped
    # dendrites/axon only smear theirs along lines. Pick the strongest (centre, r).
    span = float(np.ptp(body_p, axis=0).max())
    cell = span / 200.0
    cands = []
    for r in np.linspace(span * 0.02, span * 0.07, 11):
        votes = body_p - body_n * r
        keys, counts = np.unique(np.floor(votes / cell).astype(np.int64), axis=0, return_counts=True)
        for k in np.argsort(-counts)[:6]:
            cands.append((int(counts[k]), (keys[k] + 0.5) * cell, r))
    # Axon terminals also end in bulbs; the soma is the sphere sitting in the
    # densest arbor (many branching dendrites), so score = membrane mass nearby.
    def arbor_mass(c):
        return int((np.linalg.norm(body_p - c, axis=1) < span * 0.15).sum())
    strong = [c for c in cands if c[0] >= 0.15 * max(x[0] for x in cands)]
    _, soma_c, soma_r = max(strong, key=lambda c: (arbor_mass(c[1]), c[0]))
    for _ in range(4):  # refine: average the votes near the winning cell
        votes = body_p - body_n * soma_r
        near = np.linalg.norm(votes - soma_c, axis=1) < soma_r * 0.25
        soma_c = votes[near].mean(0)
        soma_r = float(np.median(np.linalg.norm(body_p[near] - soma_c, axis=1)))

    # Axon direction: soma -> mean of the 3 nearest myelin segments (the first one
    # sits slightly off-axis, a single segment tilts the frame)
    nearest3 = myelin_centres[np.argsort(np.linalg.norm(myelin_centres - soma_c, axis=1))[:3]]
    axon = nearest3.mean(0) - soma_c
    axon /= np.linalg.norm(axon)

    # Rotation taking axon -> +Z
    z = axon
    helper = np.array([0.0, 0.0, 1.0]) if abs(z[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = np.cross(helper, z); x /= np.linalg.norm(x)
    y = np.cross(z, x)
    R = np.stack([x, y, z])  # rows
    scale = 0.5 / soma_r     # soma radius -> 0.5 m

    def xf(p, n):
        return ((p - soma_c) @ R.T) * scale, n @ R.T

    bp, bn = xf(body_p, body_n)

    # --- Soma mesh: drop the axon beyond the initial segment ---
    radial = np.linalg.norm(bp[:, :2], axis=1)
    hillock_len = 1.6  # metres = 3.2 soma radii, then the graph edge takes over
    is_axon = (bp[:, 2] > hillock_len) & (radial < 0.6)
    is_far_axon = bp[:, 2] > 3.0  # anything far down the axon (terminals included)
    drop_v = is_axon | is_far_axon
    keep_tri = ~drop_v[body_i].any(axis=1)
    sp, sn, si = compact(bp, bn, body_i[keep_tri])

    # Hillock tip: furthest kept vertex along +Z near the axis
    near_axis = np.linalg.norm(sp[:, :2], axis=1) < 0.4
    hill_tip = np.array([0.0, 0.0, float(sp[near_axis, 2].max())])

    # Dendrite tips: vertices that are local maxima of distance from soma, clustered
    ds = np.linalg.norm(sp, axis=1)
    cand = np.where((ds > 1.4) & (sp[:, 2] < 0.6 * ds))[0]  # not on the axon side
    order = cand[np.argsort(-ds[cand])]
    tips = []
    for vi in order:
        if all(np.linalg.norm(sp[vi] - t) > 0.9 for t in tips):
            tips.append(sp[vi])
        if len(tips) >= 24:
            break
    tips = np.array(tips)

    # --- Full mesh: membrane / myelin / Schwann nuclei ---
    def cat(parts):
        P, N, I, off = [], [], [], 0
        for p, n, i in parts:
            p2, n2 = xf(p, n)
            P.append(p2); N.append(n2); I.append(i + off); off += len(p)
        return np.vstack(P), np.vstack(N), np.vstack(I)

    mp, mn, mi = cat(myelin)
    np_, nn, ni = cat(schwann)

    os.makedirs(OUT_DIR, exist_ok=True)
    membrane = (0.42, 0.40, 0.39, 1.0)
    write_glb(os.path.join(OUT_DIR, "SM_NeuronSoma.glb"),
              [("Membrane", sp, sn, si, "Membrane", membrane, 0.55)])
    write_glb(os.path.join(OUT_DIR, "SM_NeuronFull.glb"), [
        ("Membrane", bp, bn, body_i, "Membrane", membrane, 0.55),
        ("Myelin", mp, mn, mi, "Myelin", (0.86, 0.82, 0.72, 1.0), 0.38),
        ("SchwannNuclei", np_, nn, ni, "SchwannNucleus", (0.45, 0.38, 0.55, 1.0), 0.6),
    ])

    full_len = float(bp[:, 2].max())

    def ue(p):
        # glTF (right-handed) -> UE (left-handed) import mirrors Y; data handed to
        # C++ / the import check must be in UE's frame.
        return p * np.array([1.0, -1.0, 1.0])
    # Expected UE bounds (cm, Z-up) so the Unreal import script can verify the axis mapping
    with open(os.path.join(OUT_DIR, "SM_NeuronSoma.bounds.json"), "w") as f:
        json.dump({"min_cm": (ue(sp).min(0) * 100).round(2).tolist(), "max_cm": (ue(sp).max(0) * 100).round(2).tolist()}, f)
    with open(HEADER, "w", newline="\n") as f:
        f.write("// GENERATED by Scripts/build_assets/prepare_neuron_mesh.py - do not edit.\n")
        f.write("// Units: soma radii, UE mesh-local frame (soma centre = origin, axon along +Z). Multiply by SomaRadiusCm.\n")
        f.write("#pragma once\n\nnamespace MetaBrainNeuronMorphology\n{\n")
        f.write(f"\tstatic constexpr double HillockTip[3] = {{ 0.0, 0.0, {hill_tip[2] * 2:.4f} }};\n")
        f.write(f"\tstatic constexpr double FullAxonLength = {full_len * 2:.4f};\n")
        f.write(f"\tstatic constexpr int32 NumDendriteTips = {len(tips)};\n")
        f.write("\tstatic constexpr double DendriteTips[][3] = {\n")
        for t in ue(tips) * 2:
            f.write(f"\t\t{{ {t[0]:.4f}, {t[1]:.4f}, {t[2]:.4f} }},\n")
        f.write("\t};\n}\n")

    print(f"soma centre {soma_c.round(1)} radius {soma_r:.1f} src units, axon dir {axon.round(3)}")
    print(f"soma mesh: {len(sp)} verts / {len(si)} tris; full: {len(bp) + len(mp) + len(np_)} verts")
    print(f"hillock tip z={hill_tip[2]*2:.2f} R, {len(tips)} dendrite tips, full length {full_len*2:.1f} R")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Downloads/neuron.glb"))
