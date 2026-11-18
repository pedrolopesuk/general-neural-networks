import unreal


def build_level():
    map_pkg_dir = "/Game/MetaBrain/Maps"
    map_asset_path = f"{map_pkg_dir}/L_MetaBrain"

    unreal.EditorAssetLibrary.make_directory(map_pkg_dir)

    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Detach from L_MetaBrain if loaded as EditorStartupMap so rebuild is 100% idempotent
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if unreal.EditorAssetLibrary.does_asset_exist(map_asset_path):
        unreal.EditorAssetLibrary.delete_asset(map_asset_path)

    level_subsys.new_level(map_asset_path)

    # 1. Soft Key Light for rough dielectric + subsurface neuron shading
    key_light = actor_subsys.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(-400.0, -300.0, 500.0))
    if key_light:
        key_light.set_actor_label("KeyLight")
        key_light.set_actor_rotation(unreal.Rotator(-38.0, 32.0, 0.0), False)
        lc = key_light.light_component
        lc.set_editor_property("intensity", 3.2)
        lc.set_editor_property("light_color", unreal.Color(246, 247, 250, 255))

    # 2. MetaBrain Scene Controller
    ctrl_cls = unreal.load_class(None, "/Script/MetaBrainViz.MetaBrainSceneController")
    if ctrl_cls:
        ctrl = actor_subsys.spawn_actor_from_class(ctrl_cls, unreal.Vector(0.0, 0.0, 0.0))
        ctrl.set_actor_label("MetaBrainSceneController")

    # 3. Default Macro Camera Start at (-680, 0, 22) looking at (0, 0, 0)
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-680.0, 0.0, 22.0))
    if ps:
        ps.set_actor_rotation(unreal.Rotator(-1.5, 0.0, 0.0), False)

    level_subsys.save_current_level()
    unreal.EditorAssetLibrary.save_directory("/Game/MetaBrain", only_if_is_dirty=False, recursive=True)
    unreal.log("[MetaBrain] Saved L_MetaBrain successfully.")


if __name__ == "__main__":
    build_level()
