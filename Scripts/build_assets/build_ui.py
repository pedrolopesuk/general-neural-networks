import os
import urllib.request
import unreal


FONT_URLS = {
    "Inter-Regular.ttf": "https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz%2Cwght%5D.ttf",
    "JetBrainsMono-Regular.ttf": "https://github.com/JetBrains/JetBrainsMono/raw/master/fonts/ttf/JetBrainsMono-Regular.ttf",
}


def ensure_fonts_downloaded():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    font_dir = os.path.join(script_dir, "fonts")
    os.makedirs(font_dir, exist_ok=True)

    for fname, url in FONT_URLS.items():
        dest = os.path.join(font_dir, fname)
        if not os.path.exists(dest) or os.path.getsize(dest) < 1024:
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "MetaBrainAssetBuilder/1.0"})
                with urllib.request.urlopen(req, timeout=8) as resp, open(dest, "wb") as out_f:
                    out_f.write(resp.read())
                unreal.log(f"[MetaBrain] Downloaded {fname} ({os.path.getsize(dest)} bytes).")
            except Exception as exc:
                unreal.log_warning(f"[MetaBrain] Font download skipped for {fname} (using Slate fallback): {exc}")


def build_ui_assets():
    ensure_fonts_downloaded()

    ui_dir = "/Game/MetaBrain/UI"
    unreal.EditorAssetLibrary.make_directory(ui_dir)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # 1. Centralized Style DataAsset (/Game/MetaBrain/UI/DA_MetaBrainStyle)
    style_path = f"{ui_dir}/DA_MetaBrainStyle"
    if unreal.EditorAssetLibrary.does_asset_exist(style_path):
        unreal.EditorAssetLibrary.delete_asset(style_path)

    style_cls = unreal.load_class(None, "/Script/MetaBrainViz.MetaBrainStyleDataAsset")
    if style_cls:
        da_factory = unreal.DataAssetFactory()
        da_factory.set_editor_property("data_asset_class", style_cls)
        style_asset = asset_tools.create_asset("DA_MetaBrainStyle", ui_dir, style_cls, da_factory)
        if style_asset:
            unreal.EditorAssetLibrary.save_loaded_asset(style_asset)
            unreal.log("[MetaBrain] Built /Game/MetaBrain/UI/DA_MetaBrainStyle.")

    # 2. WidgetBlueprint (/Game/MetaBrain/UI/WBP_MetaBrainHUD) parented to UMetaBrainHUDWidget
    wbp_path = f"{ui_dir}/WBP_MetaBrainHUD"
    if unreal.EditorAssetLibrary.does_asset_exist(wbp_path):
        unreal.EditorAssetLibrary.delete_asset(wbp_path)

    hud_cls = unreal.load_class(None, "/Script/MetaBrainViz.MetaBrainHUDWidget")
    if hud_cls and hasattr(unreal, "WidgetBlueprintFactory"):
        try:
            wbp_factory = unreal.WidgetBlueprintFactory()
            wbp_factory.set_editor_property("parent_class", hud_cls)
            wbp = asset_tools.create_asset("WBP_MetaBrainHUD", ui_dir, unreal.WidgetBlueprint, wbp_factory)
            if wbp:
                unreal.EditorAssetLibrary.save_loaded_asset(wbp)
                unreal.log("[MetaBrain] Built /Game/MetaBrain/UI/WBP_MetaBrainHUD.")
        except Exception as exc:
            unreal.log_warning(f"[MetaBrain] WidgetBlueprintFactory note (C++ UMetaBrainHUDWidget direct fallback active): {exc}")


if __name__ == "__main__":
    build_ui_assets()
