import unreal


def srgb_hex_to_linear(hex_str):
    h = hex_str.lstrip("#")
    r, g, b = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]

    def conv(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return unreal.LinearColor(conv(r), conv(g), conv(b), 1.0)


def build_level():
    map_pkg_dir = "/Game/MetaBrain/Maps"
    map_asset_path = f"{map_pkg_dir}/L_MetaBrain"

    unreal.EditorAssetLibrary.make_directory(map_pkg_dir)

    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if unreal.EditorAssetLibrary.does_asset_exist(map_asset_path):
        unreal.EditorAssetLibrary.delete_asset(map_asset_path)

    level_subsys.new_level(map_asset_path)

    # 1. Near-black (#0B0C0D) Enclosure Sphere so background matches --bg token seamlessly
    sphere_mesh = unreal.load_asset("/Engine/BasicShapes/Sphere")
    bg_mat = unreal.load_asset("/Game/MetaBrain/Materials/M_Backdrop")
    if sphere_mesh and bg_mat:
        enclosure = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
        enclosure.set_actor_label("BackdropEnclosure")
        enclosure.set_actor_scale3d(unreal.Vector(90.0, 90.0, 90.0))  # 4,500 cm radius
        smc = enclosure.static_mesh_component
        smc.set_static_mesh(sphere_mesh)
        smc.set_material(0, bg_mat)
        smc.set_editor_property("cast_shadow", False)
        smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

    # 2. One Soft Key Light (Museum / Light-Sheet Microscopy Directional Key)
    key_light = actor_subsys.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(-500.0, -350.0, 520.0))
    if key_light:
        key_light.set_actor_label("SoftKeyLight")
        key_light.set_actor_rotation(unreal.Rotator(-36.0, 34.0, 0.0), False)
        lc = key_light.light_component
        lc.set_editor_property("intensity", 3.6)
        lc.set_editor_property("light_color", unreal.Color(248, 246, 242, 255))
        lc.set_editor_property("light_source_angle", 6.0)
        lc.set_editor_property("volumetric_scattering_intensity", 0.35)

    # 3. Subtle Neutral Fill SkyLight for Lumen GI subsurface shadow definition
    sky = actor_subsys.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 400.0))
    if sky:
        sky.set_actor_label("SubtleFillSkyLight")
        skc = sky.light_component
        skc.set_editor_property("real_time_capture", False)
        skc.set_editor_property("intensity", 2.2)
        skc.set_editor_property("volumetric_scattering_intensity", 0.20)
        skc.recapture_sky()

    # 4. Exponential Height Fog + Low-Density Volumetric Fog (#0B0C0D depth)
    fog = actor_subsys.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, -200.0))
    if fog:
        fog.set_actor_label("MicroscopyDepthFog")
        fc = fog.component
        fc.set_editor_property("fog_density", 0.0045)
        fc.set_editor_property("fog_height_falloff", 0.02)
        fc.set_editor_property("fog_inscattering_luminance", srgb_hex_to_linear("#0B0C0D"))
        fc.set_volumetric_fog(True)
        fc.set_editor_property("volumetric_fog_scattering_distribution", 0.25)
        fc.set_editor_property("volumetric_fog_albedo", unreal.Color(26, 28, 30, 255))
        fc.set_editor_property("volumetric_fog_extinction_scale", 0.40)

    # 5. Unbound PostProcessVolume: Lumen GI, ACES, Amber-only BloomThreshold, Cinematic DOF & Film Grain
    ppv = actor_subsys.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
    if ppv:
        ppv.set_actor_label("GlobalPostProcess")
        ppv.set_editor_property("unbound", True)
        s = ppv.settings

        # Lumen GI & Reflections
        s.set_editor_property("override_dynamic_global_illumination_method", True)
        s.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.LUMEN)
        s.set_editor_property("override_reflection_method", True)
        s.set_editor_property("reflection_method", unreal.ReflectionMethod.LUMEN)

        # Locked Exposure (prevents #0B0C0D dark scene from pumping gray at rest)
        s.set_editor_property("override_auto_exposure_method", True)
        s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_BASIC)
        s.set_editor_property("override_auto_exposure_min_brightness", True)
        s.set_editor_property("auto_exposure_min_brightness", 1.0)
        s.set_editor_property("override_auto_exposure_max_brightness", True)
        s.set_editor_property("auto_exposure_max_brightness", 1.0)
        s.set_editor_property("override_auto_exposure_bias", True)
        s.set_editor_property("auto_exposure_bias", 0.15)

        # Bloom threshold set strictly > 1.0 so ONLY emissive amber (#FFB000, >2.4 HDR) blooms; nothing glows at rest
        s.set_editor_property("override_bloom_intensity", True)
        s.set_editor_property("bloom_intensity", 0.60)
        s.set_editor_property("override_bloom_threshold", True)
        s.set_editor_property("bloom_threshold", 1.35)

        # Cinematic Depth of Field (dynamically focused on selected neuron / holon by AMetaBrainSceneController)
        s.set_editor_property("override_depth_of_field_focal_distance", True)
        s.set_editor_property("depth_of_field_focal_distance", 680.0)
        s.set_editor_property("override_depth_of_field_fstop", True)
        s.set_editor_property("depth_of_field_fstop", 3.2)
        s.set_editor_property("override_depth_of_field_sensor_width", True)
        s.set_editor_property("depth_of_field_sensor_width", 36.0)

        # Subtle Film Grain & Light Chromatic Aberration
        s.set_editor_property("override_film_grain_intensity", True)
        s.set_editor_property("film_grain_intensity", 0.08)
        s.set_editor_property("override_scene_fringe_intensity", True)
        s.set_editor_property("scene_fringe_intensity", 0.06)

        # Disable Vignette heavy darkening so corners stay true #0B0C0D
        s.set_editor_property("override_vignette_intensity", True)
        s.set_editor_property("vignette_intensity", 0.18)

        ppv.settings = s

    # 6. MetaBrain Scene Controller
    ctrl_cls = unreal.load_class(None, "/Script/MetaBrainViz.MetaBrainSceneController")
    if ctrl_cls:
        ctrl = actor_subsys.spawn_actor_from_class(ctrl_cls, unreal.Vector(0.0, 0.0, 0.0))
        ctrl.set_actor_label("MetaBrainSceneController")

    # 7. Default Macro Camera Start at (-680, 0, 22) looking at (0, 0, 0)
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-680.0, 0.0, 22.0))
    if ps:
        ps.set_actor_rotation(unreal.Rotator(-1.5, 0.0, 0.0), False)

    level_subsys.save_current_level()
    unreal.EditorAssetLibrary.save_directory("/Game/MetaBrain", only_if_is_dirty=False, recursive=True)
    unreal.log("[MetaBrain] Saved L_MetaBrain with Lumen GI, Volumetric Fog, ACES, Amber Bloom & DOF.")


if __name__ == "__main__":
    build_level()
