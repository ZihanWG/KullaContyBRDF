import unreal


def report(name):
    value = getattr(unreal, name, None)
    unreal.log("LEARNING_LAB_API {}={}".format(name, value is not None))
    if value is not None:
        unreal.log("LEARNING_LAB_DIR {} {}".format(name, [item for item in dir(value) if not item.startswith("_")][:120]))


for api_name in (
    "EditorLevelLibrary",
    "LevelEditorSubsystem",
    "EditorAssetLibrary",
    "AssetToolsHelpers",
    "MaterialEditingLibrary",
    "DynamicMesh",
    "GeometryScript_Primitives",
    "GeometryScript_NewAssetUtils",
    "GeometryScriptPrimitiveOptions",
    "GeometryScriptCreateNewStaticMeshAssetOptions",
    "MeshNaniteSettings",
    "TextRenderActor",
    "CameraActor",
    "PostProcessVolume",
):
    report(api_name)

try:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    unreal.log("LEARNING_LAB_LEVEL_SUBSYSTEM {}".format([item for item in dir(subsystem) if not item.startswith("_")]))
except Exception as exc:
    unreal.log_error("LEARNING_LAB_PROBE_FAILED {}".format(exc))
