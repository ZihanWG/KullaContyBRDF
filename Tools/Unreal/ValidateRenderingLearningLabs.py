"""Validate the generated UE5 rendering learning labs without modifying them."""

import unreal


ASSET_LIBRARY = unreal.EditorAssetLibrary
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
STATIC_MESHES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_map(path, minimum_actor_count, required_labels):
    require(ASSET_LIBRARY.does_asset_exist(path), "Missing map: {}".format(path))
    require(LEVELS.load_level(path), "Could not load map: {}".format(path))
    actors = list(ACTORS.get_all_level_actors())
    labels = {actor.get_actor_label() for actor in actors}
    missing = sorted(set(required_labels) - labels)
    require(len(actors) >= minimum_actor_count, "{} has only {} actors".format(path, len(actors)))
    require(not missing, "{} is missing actors: {}".format(path, ", ".join(missing)))
    unreal.log("LEARNING_LABS_VALIDATE: {} actors={} required_labels=OK".format(path, len(actors)))


def validate_nanite_assets():
    off_mesh = ASSET_LIBRARY.load_asset("/Game/LearningLabs/Assets/SM_HighPolySphere_Nanite_OFF")
    on_mesh = ASSET_LIBRARY.load_asset("/Game/LearningLabs/Assets/SM_HighPolySphere_Nanite_ON")
    require(off_mesh and on_mesh, "Nanite comparison meshes are missing")
    off_settings = STATIC_MESHES.get_nanite_settings(off_mesh)
    on_settings = STATIC_MESHES.get_nanite_settings(on_mesh)
    require(not off_settings.enabled, "Nanite OFF mesh unexpectedly has Nanite enabled")
    require(on_settings.enabled, "Nanite ON mesh does not have Nanite enabled")
    unreal.log("LEARNING_LABS_VALIDATE: Nanite asset flags OFF=False ON=True")


validate_nanite_assets()
validate_map(
    "/Game/LearningLabs/Maps/L_NaniteLab",
    50,
    {"SM_HighPoly_Nanite_OFF_Hero", "SM_HighPoly_Nanite_ON_Hero", "TXT_Nanite_Title", "CAM_NaniteLab"},
)
validate_map(
    "/Game/LearningLabs/Maps/L_VSMLab",
    34,
    {"ThinPole_00", "BoxCaster_00", "NaniteCaster_00", "L_Point_Movable_MoveMe", "TXT_VSM_Title", "CAM_VSMLab"},
)
validate_map(
    "/Game/LearningLabs/Maps/L_LumenLab",
    22,
    {"Room_Left_Red", "Room_Right_Green", "Emissive_BackPanel", "L_Point_Primary_MoveMe", "TXT_Lumen_Title", "CAM_LumenLab"},
)
unreal.log("LEARNING_LABS_VALIDATE: SUCCESS")
