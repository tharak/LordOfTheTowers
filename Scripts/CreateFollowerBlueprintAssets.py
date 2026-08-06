import unreal

ROOT = "/Game/Blueprints/Follower"


def ensure_folder(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_blueprint(name, parent_class):
    path = f"{ROOT}/{name}"
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset:
        return asset
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT, unreal.Blueprint, factory)
    if not asset:
        raise RuntimeError(f"Could not create {path}")
    unreal.EditorAssetLibrary.save_asset(path)
    return asset


def compile_and_save_blueprint(asset):
    unreal.KismetEditorUtilities.compile_blueprint(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)


ensure_folder(ROOT)
simulation_class = unreal.load_class(None, "/Script/LordOfTheTowers.FollowerSimulationActor")
turtle_class = unreal.load_class(None, "/Script/LordOfTheTowers.FollowerTurtleActor")
if not simulation_class or not turtle_class:
    raise RuntimeError("Follower native classes are not available")

turtle_bp = create_blueprint("BP_FollowerTurtle_Blueprint", turtle_class)
simulation_bp = create_blueprint("BP_FollowerSimulation_Blueprint", simulation_class)
compile_and_save_blueprint(turtle_bp)
compile_and_save_blueprint(simulation_bp)
cdo = unreal.get_default_object(simulation_bp.generated_class())
cdo.set_editor_property("turtle_class", turtle_bp.generated_class())
unreal.EditorAssetLibrary.save_asset(f"{ROOT}/BP_FollowerSimulation_Blueprint")

map_path = "/Game/Maps/FollowerBlueprint"
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    unreal.EditorLevelLibrary.new_level(map_path)
    unreal.EditorLevelLibrary.spawn_actor_from_class(simulation_bp.generated_class(), unreal.Vector(0, 0, 0))
    unreal.EditorLevelLibrary.save_current_level()

print("Created Follower Blueprint comparison assets:")
print(f"  {ROOT}/BP_FollowerTurtle_Blueprint")
print(f"  {ROOT}/BP_FollowerSimulation_Blueprint")
print(f"  {map_path}")
