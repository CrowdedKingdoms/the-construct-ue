"""Reload and verify the new shell assets without signing into an account."""
import json
from pathlib import Path
import unreal

paths = [
    "/ConstructShell/UI/WBP_ConstructSignIn",
    "/ConstructShell/UI/WBP_ConstructStatus",
    "/ConstructShell/UI/BP_ConstructHUD",
    "/ConstructShell/Framework/BP_ConstructPlayerController",
    "/ConstructShell/Framework/BP_ConstructGameInstance",
    "/ConstructShell/Framework/BP_ConstructGameMode",
]
assets = {}
for path in paths:
    asset = unreal.load_asset(path)
    if asset is None or asset.generated_class() is None:
        raise RuntimeError("Shell asset failed to reload: " + path)
    assets[path] = asset

hud = unreal.get_default_object(assets[paths[2]].generated_class())
assert hud.get_editor_property("sign_in_widget_class") == assets[paths[0]].generated_class()
assert hud.get_editor_property("status_widget_class") == assets[paths[1]].generated_class()
mode = assets[paths[5]].generated_class()
mode_defaults = unreal.get_default_object(mode)
assert mode_defaults.get_editor_property("hud_class") == assets[paths[2]].generated_class()
assert mode_defaults.get_editor_property("player_controller_class") == assets[paths[3]].generated_class()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.load_level("/Game/Construct/Maps/L_ConstructStart"):
    raise RuntimeError("Startup map could not reload")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_world_settings().get_editor_property("default_game_mode") == mode
report = {"reloaded_assets": paths, "map": world.get_path_name(), "bindings_verified": True,
          "live_auth_attempted": False, "game_instance_project_default_changed": False}
out = Path(unreal.Paths.project_saved_dir()) / "ConstructLogin" / "AssetVerification.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("CONSTRUCT_LOGIN_ASSETS_VERIFIED " + json.dumps(report))
