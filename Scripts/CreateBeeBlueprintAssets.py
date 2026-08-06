import unreal


ROOT = "/Game/Blueprints/BeeBlueprint"


def ensure_folder(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_blueprint(asset_name, parent_class):
    asset_path = f"{ROOT}/{asset_name}"
    existing = unreal.EditorAssetLibrary.load_asset(asset_path)
    if existing:
        return existing

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, ROOT, unreal.Blueprint, factory
    )
    if not asset:
        raise RuntimeError(f"Could not create {asset_path}")
    unreal.EditorAssetLibrary.save_asset(asset_path)
    return asset


ensure_folder(ROOT)

bee_class = unreal.load_class(None, "/Script/LordOfTheTowers.BeeSmartBeeActor")
hive_class = unreal.load_class(None, "/Script/LordOfTheTowers.BeeSmartHiveActor")
simulation_class = unreal.load_class(None, "/Script/LordOfTheTowers.BeeSmartOOPSimulationActor")

if not bee_class or not hive_class or not simulation_class:
    raise RuntimeError("BeeSmart native classes are not available")

bee_bp = create_blueprint("BP_BeeSmartBee_Blueprint", bee_class)
hive_bp = create_blueprint("BP_BeeSmartHive_Blueprint", hive_class)
simulation_bp = create_blueprint("BP_BeeSmartSimulation_Blueprint", simulation_class)

simulation_cdo = unreal.get_default_object(simulation_bp.generated_class())
simulation_cdo.set_editor_property("bee_class", bee_bp.generated_class())
simulation_cdo.set_editor_property("hive_class", hive_bp.generated_class())
simulation_cdo.set_editor_property("bee_count", 100)
simulation_cdo.set_editor_property("hive_count", 5)
simulation_cdo.set_editor_property("quorum", 12)
unreal.EditorAssetLibrary.save_asset(f"{ROOT}/BP_BeeSmartSimulation_Blueprint")

print("Created Blueprint comparison assets:")
print(f"  {ROOT}/BP_BeeSmartBee_Blueprint")
print(f"  {ROOT}/BP_BeeSmartHive_Blueprint")
print(f"  {ROOT}/BP_BeeSmartSimulation_Blueprint")
