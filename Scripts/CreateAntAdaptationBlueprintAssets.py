import unreal

ROOT = "/Game/Blueprints/AntAdaptation"


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
simulation_class = unreal.load_class(None, "/Script/LordOfTheTowers.AntAdaptationSimulationActor")
ant_class = unreal.load_class(None, "/Script/LordOfTheTowers.AntAdaptationAntActor")
nest_class = unreal.load_class(None, "/Script/LordOfTheTowers.AntAdaptationNestActor")
flower_class = unreal.load_class(None, "/Script/LordOfTheTowers.AntAdaptationFlowerActor")

if not all((simulation_class, ant_class, nest_class, flower_class)):
    raise RuntimeError("Ant Adaptation native classes are not available")

ant_bp = create_blueprint("BP_AntAdaptationAnt_Blueprint", ant_class)
nest_bp = create_blueprint("BP_AntAdaptationNest_Blueprint", nest_class)
flower_bp = create_blueprint("BP_AntAdaptationFlower_Blueprint", flower_class)
simulation_bp = create_blueprint("BP_AntAdaptationSimulation_Blueprint", simulation_class)

for blueprint in (ant_bp, nest_bp, flower_bp, simulation_bp):
    compile_and_save_blueprint(blueprint)

cdo = unreal.get_default_object(simulation_bp.generated_class())
cdo.set_editor_property("ant_class", ant_bp.generated_class())
cdo.set_editor_property("nest_class", nest_bp.generated_class())
cdo.set_editor_property("flower_class", flower_bp.generated_class())
unreal.EditorAssetLibrary.save_asset(f"{ROOT}/BP_AntAdaptationSimulation_Blueprint")

map_path = "/Game/Maps/AntAdaptationBlueprint"
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    unreal.EditorLevelLibrary.new_level(map_path)
    unreal.EditorLevelLibrary.spawn_actor_from_class(simulation_bp.generated_class(), unreal.Vector(0, 0, 0))
    unreal.EditorLevelLibrary.save_current_level()

print("Created Ant Adaptation Blueprint comparison assets:")
for name in ("BP_AntAdaptationAnt_Blueprint", "BP_AntAdaptationNest_Blueprint", "BP_AntAdaptationFlower_Blueprint", "BP_AntAdaptationSimulation_Blueprint"):
    print(f"  {ROOT}/{name}")
print(f"  {map_path}")
