"""Export UE 5.8's precomputed GGX energy LUT for numerical validation.

Run through UnrealEditor-Cmd with -ExecutePythonScript. The engine asset is
read-only; the exported EXR is written under the project's Intermediate folder.
"""

import os

import unreal


ASSET_PATH = "/Engine/EngineMaterials/EnergyConservation/GGX_ReflectionEnergy"
OUTPUT_DIRECTORY = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_intermediate_dir()),
    "KullaConty",
)
OUTPUT_PATH = os.path.join(OUTPUT_DIRECTORY, "UE58_GGX_ReflectionEnergy.exr")


def main() -> None:
    texture = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if texture is None:
        raise RuntimeError(f"Could not load engine energy LUT: {ASSET_PATH}")

    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object = texture
    task.filename = OUTPUT_PATH
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.write_empty_files = False

    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError(f"UE failed to export {ASSET_PATH} to {OUTPUT_PATH}")
    if not os.path.isfile(OUTPUT_PATH) or os.path.getsize(OUTPUT_PATH) == 0:
        raise RuntimeError(f"Export reported success but produced no EXR: {OUTPUT_PATH}")

    unreal.log(
        "KullaConty: exported UE GGX energy LUT to "
        f"{OUTPUT_PATH} ({os.path.getsize(OUTPUT_PATH)} bytes)"
    )


main()
