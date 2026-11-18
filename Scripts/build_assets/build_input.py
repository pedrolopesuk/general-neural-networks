import unreal


def _create_input_action(asset_tools, pkg_dir, name, value_type):
    path = f"{pkg_dir}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    ia = asset_tools.create_asset(name, pkg_dir, unreal.InputAction, unreal.DataAssetFactory())
    ia.set_editor_property("value_type", value_type)
    unreal.EditorAssetLibrary.save_loaded_asset(ia)
    return ia


def _make_key(key_name):
    k = unreal.Key()
    try:
        k.import_text(key_name)
    except Exception:
        try:
            k.set_editor_property("key_name", key_name)
        except Exception:
            pass
    return k


def build_enhanced_input():
    inp_dir = "/Game/MetaBrain/Input"
    unreal.EditorAssetLibrary.make_directory(inp_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    ia_primary = _create_input_action(asset_tools, inp_dir, "IA_PrimaryPointer", unreal.InputActionValueType.BOOLEAN)
    ia_orbit = _create_input_action(asset_tools, inp_dir, "IA_OrbitHold", unreal.InputActionValueType.BOOLEAN)
    ia_pan = _create_input_action(asset_tools, inp_dir, "IA_PanHold", unreal.InputActionValueType.BOOLEAN)
    ia_look = _create_input_action(asset_tools, inp_dir, "IA_LookDelta", unreal.InputActionValueType.AXIS2D)
    ia_zoom = _create_input_action(asset_tools, inp_dir, "IA_ZoomWheel", unreal.InputActionValueType.AXIS1D)
    ia_step_up = _create_input_action(asset_tools, inp_dir, "IA_StepUp", unreal.InputActionValueType.BOOLEAN)

    imc_path = f"{inp_dir}/IMC_MetaBrainCamera"
    if unreal.EditorAssetLibrary.does_asset_exist(imc_path):
        unreal.EditorAssetLibrary.delete_asset(imc_path)

    imc = asset_tools.create_asset("IMC_MetaBrainCamera", inp_dir, unreal.InputMappingContext, unreal.DataAssetFactory())

    bindings = [
        (ia_primary, "LeftMouseButton"),
        (ia_orbit, "RightMouseButton"),
        (ia_pan, "MiddleMouseButton"),
        (ia_look, "Mouse2D"),
        (ia_zoom, "MouseWheelAxis"),
        (ia_step_up, "Escape"),
    ]
    for action, key_str in bindings:
        k = _make_key(key_str)
        try:
            imc.map_key(action, k)
        except Exception as exc:
            unreal.log_warning(f"[MetaBrain] map_key fallback note for {key_str}: {exc}")

    unreal.EditorAssetLibrary.save_loaded_asset(imc)
    unreal.log("[MetaBrain] Built Enhanced Input IA_* and IMC_MetaBrainCamera assets.")


if __name__ == "__main__":
    build_enhanced_input()
