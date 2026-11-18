import unreal


def build_niagara_systems():
    fx_dir = "/Game/MetaBrain/FX"
    unreal.EditorAssetLibrary.make_directory(fx_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    for sys_name in ("NS_SynapseRibbons", "NS_SignalPulses"):
        sys_path = f"{fx_dir}/{sys_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(sys_path):
            unreal.EditorAssetLibrary.delete_asset(sys_path)

        factory = unreal.NiagaraSystemFactoryNew()
        ns = asset_tools.create_asset(sys_name, fx_dir, unreal.NiagaraSystem, factory)
        if ns:
            unreal.EditorAssetLibrary.save_loaded_asset(ns)
            unreal.log(f"[MetaBrain] Created Niagara System {sys_path}")


if __name__ == "__main__":
    build_niagara_systems()
