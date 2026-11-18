import unreal


def build_level():
    map_pkg_dir = "/Game/MetaBrain/Maps"
    map_asset_path = f"{map_pkg_dir}/L_MetaBrain"

    unreal.EditorAssetLibrary.make_directory(map_pkg_dir)

    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    created = level_subsys.new_level(map_asset_path)
    unreal.log(f"[MetaBrain] new_level({map_asset_path}) -> {created}")

    # Place AMetaBrainSceneController at world origin
    ctrl_cls = unreal.load_class(None, "/Script/MetaBrainViz.MetaBrainSceneController")
    if ctrl_cls:
        ctrl = actor_subsys.spawn_actor_from_class(ctrl_cls, unreal.Vector(0.0, 0.0, 0.0))
        ctrl.set_actor_label("MetaBrainSceneController")

    # Place a PlayerStart facing +X toward the origin from (-980, 0, 90)
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-980.0, 0.0, 90.0))
    if ps:
        ps.set_actor_rotation(unreal.Rotator(0.0, -5.0, 0.0), False)

    level_subsys.save_current_level()
    unreal.EditorAssetLibrary.save_directory("/Game/MetaBrain", only_if_is_dirty=False, recursive=True)
    unreal.log("[MetaBrain] Saved L_MetaBrain successfully.")


if __name__ == "__main__":
    build_level()
