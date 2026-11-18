"""Import the prepared neuron meshes into /Game/MetaBrain/Meshes (runs inside Unreal).

Inputs come from prepare_neuron_mesh.py (Scripts/build_assets/meshes/*.glb).
  SM_NeuronSoma  -> per-neuron HISM (soma + dendrites + axon initial segment), Nanite
  SM_NeuronFull  -> focused "hero" neuron (membrane / myelin / Schwann nuclei), Nanite
"""
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
MESH_SRC = os.path.join(HERE, "meshes")
MESH_DIR = "/Game/MetaBrain/Meshes"
MAT_DIR = "/Game/MetaBrain/Materials"


def _import_glb(name):
    src = os.path.join(MESH_SRC, f"{name}.glb")
    if not os.path.exists(src):
        unreal.log_error(f"[MetaBrain] Missing {src}. Run: python Scripts/build_assets/prepare_neuron_mesh.py <neuron.glb>")
        return None

    dst = f"{MESH_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)

    # Interchange writes <dest>/<file>/StaticMeshes/<mesh> + <dest>/<file>/Materials/*,
    # so import into a staging folder and move the mesh to its stable path.
    staging = f"{MESH_DIR}/_Import"
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", staging)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    mesh_path = None
    for p in task.get_editor_property("imported_object_paths"):
        if isinstance(unreal.load_asset(p), unreal.StaticMesh):
            mesh_path = p.split(".")[0]
            break
    if not mesh_path or not unreal.EditorAssetLibrary.rename_asset(mesh_path, dst):
        unreal.log_error(f"[MetaBrain] Import of {src} produced no StaticMesh")
        return None
    return unreal.load_asset(dst)


def _enable_nanite(mesh):
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", True)
    settings.set_editor_property("keep_percent_triangles", 1.0)
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sms:
        sms.set_nanite_settings(mesh, settings, apply_changes=True)
    else:
        # Commandlet: editor subsystems are absent. set_editor_property fires
        # PostEditChangeProperty, which rebuilds the render data with Nanite.
        mesh.set_editor_property("nanite_settings", settings)


def _verify_axes(mesh):
    """Compare imported bounds against what prepare_neuron_mesh.py expects (axon = +Z)."""
    try:
        exp = json.load(open(os.path.join(MESH_SRC, "SM_NeuronSoma.bounds.json")))
    except OSError:
        return
    box = mesh.get_bounding_box()
    got_min = [box.min.x, box.min.y, box.min.z]
    got_max = [box.max.x, box.max.y, box.max.z]
    err = max(abs(a - b) for a, b in zip(got_min + got_max, exp["min_cm"] + exp["max_cm"]))
    if err > 2.0:
        unreal.log_warning(f"[MetaBrain] SM_NeuronSoma axis/scale mismatch (max err {err:.1f} cm). "
                           f"expected {exp}, got min {got_min} max {got_max}")
    else:
        unreal.log_warning(f"[MetaBrain] SM_NeuronSoma bounds verified (err {err:.2f} cm)")


def build_neuron_meshes():
    unreal.EditorAssetLibrary.make_directory(MESH_DIR)

    m_neuron = unreal.load_asset(f"{MAT_DIR}/M_Neuron")
    m_myelin = unreal.load_asset(f"{MAT_DIR}/M_Myelin")
    m_nucleus = unreal.load_asset(f"{MAT_DIR}/M_SchwannNucleus")

    soma = _import_glb("SM_NeuronSoma")
    if soma:
        _enable_nanite(soma)
        if m_neuron:
            soma.set_material(0, m_neuron)
        _verify_axes(soma)
        unreal.EditorAssetLibrary.save_loaded_asset(soma)

    full = _import_glb("SM_NeuronFull")
    if full:
        _enable_nanite(full)
        for idx, mat in enumerate([m_neuron, m_myelin, m_nucleus]):
            if mat:
                full.set_material(idx, mat)
        unreal.EditorAssetLibrary.save_loaded_asset(full)

    # The glTF placeholder materials in the staging folder are no longer referenced.
    if unreal.EditorAssetLibrary.does_directory_exist(f"{MESH_DIR}/_Import"):
        unreal.EditorAssetLibrary.delete_directory(f"{MESH_DIR}/_Import")

    unreal.log_warning("[MetaBrain] Built SM_NeuronSoma + SM_NeuronFull (Nanite).")
