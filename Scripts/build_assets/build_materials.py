import unreal


def srgb_to_linear(hex_str):
    h = hex_str.lstrip("#")
    r, g, b = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]

    def conv(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return unreal.LinearColor(conv(r), conv(g), conv(b), 1.0)


def build_neuron_material():
    mat_dir = "/Game/MetaBrain/Materials"
    unreal.EditorAssetLibrary.make_directory(mat_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    # 1. Create Material Parameter Collection (MPC_MetaBrain) for LensMode (0=Activity, 1=Age)
    mpc_path = f"{mat_dir}/MPC_MetaBrain"
    if unreal.EditorAssetLibrary.does_asset_exist(mpc_path):
        mpc = unreal.load_asset(mpc_path)
    else:
        mpc_factory = unreal.MaterialParameterCollectionFactoryNew()
        mpc = asset_tools.create_asset("MPC_MetaBrain", mat_dir, unreal.MaterialParameterCollection, mpc_factory)

    scalar_params = list(mpc.get_editor_property("scalar_parameters"))
    if not any(p.get_editor_property("parameter_name") == "LensMode" for p in scalar_params):
        sp = unreal.CollectionScalarParameter()
        sp.set_editor_property("parameter_name", "LensMode")
        sp.set_editor_property("default_value", 0.0)
        scalar_params.append(sp)
        mpc.set_editor_property("scalar_parameters", scalar_params)
    unreal.EditorAssetLibrary.save_loaded_asset(mpc)

    # 2. Create M_Neuron (Rough Dielectric + Subtle Subsurface, Emissive ONLY while firing)
    mat_path = f"{mat_dir}/M_Neuron"
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        unreal.EditorAssetLibrary.delete_asset(mat_path)

    mat_factory = unreal.MaterialFactoryNew()
    mat = asset_tools.create_asset("M_Neuron", mat_dir, unreal.Material, mat_factory)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SUBSURFACE)
    mat.set_editor_property("used_with_instanced_static_meshes", True)

    # Palette colors (converted accurately from approved sRGB tokens to linear space)
    col_idle = srgb_to_linear("#6B6F75")   # Matte warm-neutral gray
    col_amber = srgb_to_linear("#FFB000")  # Signal amber
    col_young = srgb_to_linear("#8FD3A8")  # Age lens: new
    col_old = srgb_to_linear("#5A6B8C")    # Age lens: established

    # PerInstanceCustomData:
    # [0] = Activation, [1] = Maturity, [2] = IsolationBrightness (0.2..1.0), [3] = FiringFlash (0..1)
    cd_maturity = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, 120)
    cd_maturity.set_editor_property("data_index", 1)

    cd_iso = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, 260)
    cd_iso.set_editor_property("data_index", 2)

    cd_flash = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, -80)
    cd_flash.set_editor_property("data_index", 3)

    # Colors
    c_idle = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, -280)
    c_idle.set_editor_property("constant", col_idle)

    c_amber = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, -180)
    c_amber.set_editor_property("constant", col_amber)

    c_young = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, 20)
    c_young.set_editor_property("constant", col_young)

    c_old = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, 200)
    c_old.set_editor_property("constant", col_old)

    # Activity BaseColor = Lerp(IdleGray, Amber, Flash)
    lerp_act = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, -180)
    mel.connect_material_expressions(c_idle, "", lerp_act, "A")
    mel.connect_material_expressions(c_amber, "", lerp_act, "B")
    mel.connect_material_expressions(cd_flash, "", lerp_act, "Alpha")

    # Age BaseColor = Lerp(Young, Old, Maturity)
    lerp_age = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 80)
    mel.connect_material_expressions(c_young, "", lerp_age, "A")
    mel.connect_material_expressions(c_old, "", lerp_age, "B")
    mel.connect_material_expressions(cd_maturity, "", lerp_age, "Alpha")

    # LensMode switch from MPC_MetaBrain
    mpc_lens = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, -650, -400)
    mpc_lens.set_editor_property("collection", mpc)
    mpc_lens.set_editor_property("parameter_name", "LensMode")

    lerp_lens = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -220, -40)
    mel.connect_material_expressions(lerp_act, "", lerp_lens, "A")
    mel.connect_material_expressions(lerp_age, "", lerp_lens, "B")
    mel.connect_material_expressions(mpc_lens, "", lerp_lens, "Alpha")

    # Multiply BaseColor by Isolation factor (1.0 normal, 0.20 dimmed when hovering another node)
    mul_iso = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -60, 0)
    mel.connect_material_expressions(lerp_lens, "", mul_iso, "A")
    mel.connect_material_expressions(cd_iso, "", mul_iso, "B")
    mel.connect_material_property(mul_iso, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Subtle Subsurface Scattering Color (0.42 * BaseColor) + Scatter Opacity 0.72 for deep microscopy translucency
    c_sss_scale = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -220, 220)
    c_sss_scale.set_editor_property("r", 0.42)
    mul_sss = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -60, 180)
    mel.connect_material_expressions(mul_iso, "", mul_sss, "A")
    mel.connect_material_expressions(c_sss_scale, "", mul_sss, "B")
    mel.connect_material_property(mul_sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)

    c_opacity = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -60, 270)
    c_opacity.set_editor_property("r", 0.72)
    mel.connect_material_property(c_opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # Roughness (0.58 satin dielectric so 3D spheres catch key + rim + neighbor firing bounce highlights) & Specular (0.42)
    c_rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -60, 340)
    c_rough.set_editor_property("r", 0.58)
    mel.connect_material_property(c_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    c_spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -60, 410)
    c_spec.set_editor_property("r", 0.42)
    mel.connect_material_property(c_spec, "", unreal.MaterialProperty.MP_SPECULAR)

    # Emissive ONLY while firing (Amber * Flash * 5.2 -> crosses BloomThreshold > 1.25 ONLY during 300ms flash!)
    c_emissive_gain = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -420, -340)
    c_emissive_gain.set_editor_property("r", 5.2)
    mul_flash_gain = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, -320)
    mel.connect_material_expressions(cd_flash, "", mul_flash_gain, "A")
    mel.connect_material_expressions(c_emissive_gain, "", mul_flash_gain, "B")

    mul_emissive = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -80, -300)
    mel.connect_material_expressions(c_amber, "", mul_emissive, "A")
    mel.connect_material_expressions(mul_flash_gain, "", mul_emissive, "B")
    mel.connect_material_property(mul_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[MetaBrain] Built MPC_MetaBrain and M_Neuron successfully.")


def build_holon_shell_material():
    mat_dir = "/Game/MetaBrain/Materials"
    unreal.EditorAssetLibrary.make_directory(mat_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    mat_path = f"{mat_dir}/M_HolonShell"
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        unreal.EditorAssetLibrary.delete_asset(mat_path)

    mat_factory = unreal.MaterialFactoryNew()
    mat = asset_tools.create_asset("M_HolonShell", mat_dir, unreal.Material, mat_factory)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("used_with_instanced_static_meshes", True)

    # Neutral cool-warm silver rim (strictly < 1.0 linear so it NEVER blooms at rest)
    c_rim = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -520, -140)
    c_rim.set_editor_property("constant", unreal.LinearColor(0.42, 0.44, 0.47, 1.0))
    mel.connect_material_property(c_rim, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Thin Fresnel rim: ~0.2% interior fill (no fill glow) -> ~5.5% grazing rim opacity
    fresnel = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -560, 60)
    fresnel.set_editor_property("exponent", 4.4)
    fresnel.set_editor_property("base_reflect_fraction", 0.03)

    c_max_op = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -520, 220)
    c_max_op.set_editor_property("r", 0.058)  # 5.8% peak rim opacity (within 3-6% spec)

    mul_rim = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -320, 100)
    mel.connect_material_expressions(fresnel, "", mul_rim, "A")
    mel.connect_material_expressions(c_max_op, "", mul_rim, "B")

    # PerInstanceCustomData[0] = ShellFade (1.0 visible -> 0.0 faded out when camera flies inside)
    cd_fade = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -340, 260)
    cd_fade.set_editor_property("data_index", 0)

    mul_fade = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -120, 140)
    mel.connect_material_expressions(mul_rim, "", mul_fade, "A")
    mel.connect_material_expressions(cd_fade, "", mul_fade, "B")
    mel.connect_material_property(mul_fade, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[MetaBrain] Built M_HolonShell (3-6% Fresnel rim translucency) successfully.")


def build_edge_and_pulse_materials():
    mat_dir = "/Game/MetaBrain/Materials"
    unreal.EditorAssetLibrary.make_directory(mat_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    col_gray = srgb_to_linear("#6B6F75")
    col_amber = srgb_to_linear("#FFB000")

    # 1. M_EdgeFilament: thin unlit gray, opacity proportional to |w|, turns amber briefly on signal
    edge_path = f"{mat_dir}/M_EdgeFilament"
    if unreal.EditorAssetLibrary.does_asset_exist(edge_path):
        unreal.EditorAssetLibrary.delete_asset(edge_path)

    edge_mat = asset_tools.create_asset("M_EdgeFilament", mat_dir, unreal.Material, unreal.MaterialFactoryNew())
    edge_mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    edge_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    edge_mat.set_editor_property("used_with_instanced_static_meshes", True)
    edge_mat.set_editor_property("used_with_niagara_ribbons", True)

    # CustomData[0] = Opacity (scaled by |w| and hover isolation), CustomData[1] = SignalFlash (0..1)
    cd_op = mel.create_material_expression(edge_mat, unreal.MaterialExpressionPerInstanceCustomData, -540, 140)
    cd_op.set_editor_property("data_index", 0)

    cd_sig = mel.create_material_expression(edge_mat, unreal.MaterialExpressionPerInstanceCustomData, -540, -60)
    cd_sig.set_editor_property("data_index", 1)

    c_gray = mel.create_material_expression(edge_mat, unreal.MaterialExpressionConstant3Vector, -540, -240)
    c_gray.set_editor_property("constant", col_gray)

    c_amber_hdr = mel.create_material_expression(edge_mat, unreal.MaterialExpressionConstant3Vector, -540, -150)
    c_amber_hdr.set_editor_property("constant", unreal.LinearColor(col_amber.r * 2.4, col_amber.g * 2.4, 0.0, 1.0))

    lerp_col = mel.create_material_expression(edge_mat, unreal.MaterialExpressionLinearInterpolate, -240, -140)
    mel.connect_material_expressions(c_gray, "", lerp_col, "A")
    mel.connect_material_expressions(c_amber_hdr, "", lerp_col, "B")
    mel.connect_material_expressions(cd_sig, "", lerp_col, "Alpha")
    mel.connect_material_property(lerp_col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(cd_op, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(edge_mat)
    unreal.EditorAssetLibrary.save_loaded_asset(edge_mat)

    # 2. M_SignalPulse: small traveling amber light pulse spawned when source fires
    pulse_path = f"{mat_dir}/M_SignalPulse"
    if unreal.EditorAssetLibrary.does_asset_exist(pulse_path):
        unreal.EditorAssetLibrary.delete_asset(pulse_path)

    pulse_mat = asset_tools.create_asset("M_SignalPulse", mat_dir, unreal.Material, unreal.MaterialFactoryNew())
    pulse_mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    pulse_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    pulse_mat.set_editor_property("used_with_instanced_static_meshes", True)
    pulse_mat.set_editor_property("used_with_niagara_sprites", True)

    cd_env = mel.create_material_expression(pulse_mat, unreal.MaterialExpressionPerInstanceCustomData, -460, 40)
    cd_env.set_editor_property("data_index", 0)

    c_pulse_hdr = mel.create_material_expression(pulse_mat, unreal.MaterialExpressionConstant3Vector, -460, -100)
    c_pulse_hdr.set_editor_property("constant", unreal.LinearColor(col_amber.r * 4.8, col_amber.g * 4.8, 0.0, 1.0))

    mul_pulse = mel.create_material_expression(pulse_mat, unreal.MaterialExpressionMultiply, -200, -40)
    mel.connect_material_expressions(c_pulse_hdr, "", mul_pulse, "A")
    mel.connect_material_expressions(cd_env, "", mul_pulse, "B")
    mel.connect_material_property(mul_pulse, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(pulse_mat)
    unreal.EditorAssetLibrary.save_loaded_asset(pulse_mat)
    unreal.log("[MetaBrain] Built M_EdgeFilament and M_SignalPulse successfully.")


def build_backdrop_material():
    mat_dir = "/Game/MetaBrain/Materials"
    unreal.EditorAssetLibrary.make_directory(mat_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    bg_path = f"{mat_dir}/M_Backdrop"
    if unreal.EditorAssetLibrary.does_asset_exist(bg_path):
        unreal.EditorAssetLibrary.delete_asset(bg_path)

    bg_mat = asset_tools.create_asset("M_Backdrop", mat_dir, unreal.Material, unreal.MaterialFactoryNew())
    bg_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    bg_mat.set_editor_property("two_sided", True)
    try:
        bg_mat.set_editor_property("is_sky", True)
    except Exception:
        pass

    c_bg = mel.create_material_expression(bg_mat, unreal.MaterialExpressionConstant3Vector, -320, 0)
    c_bg.set_editor_property("constant", srgb_to_linear("#0B0C0D"))
    mel.connect_material_property(c_bg, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(bg_mat)
    unreal.EditorAssetLibrary.save_loaded_asset(bg_mat)
    unreal.log("[MetaBrain] Built M_Backdrop (#0B0C0D) successfully.")


if __name__ == "__main__":
    build_neuron_material()
    build_holon_shell_material()
    build_edge_and_pulse_materials()
    build_backdrop_material()
