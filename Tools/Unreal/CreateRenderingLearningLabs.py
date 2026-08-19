"""Build controlled Nanite, VSM, and Lumen learning maps for the KC project.

Run from Unreal Editor with:
    py "E:/kc/KullaContyBRDF/Tools/Unreal/CreateRenderingLearningLabs.py"

The script owns the maps in /Game/LearningLabs and reuses generated assets.
"""

import math
import unreal


ROOT = "/Game/LearningLabs"
ASSET_ROOT = ROOT + "/Assets"
MATERIAL_ROOT = ROOT + "/Materials"
MAP_ROOT = ROOT + "/Maps"

ASSET_LIBRARY = unreal.EditorAssetLibrary
LEVEL_LIBRARY = unreal.EditorLevelLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def log(message):
    unreal.log("LEARNING_LABS: {}".format(message))


def set_properties(obj, **properties):
    for name, value in properties.items():
        try:
            obj.set_editor_property(name, value)
        except Exception as exc:
            unreal.log_warning("LEARNING_LABS: could not set {}.{}: {}".format(obj.get_name(), name, exc))


def load(path):
    asset = ASSET_LIBRARY.load_asset(path)
    if not asset:
        raise RuntimeError("Required asset not found: {}".format(path))
    return asset


def create_material(name, base_color, roughness=0.5, metallic=0.0, emissive=None):
    asset_path = MATERIAL_ROOT + "/" + name
    if ASSET_LIBRARY.does_asset_exist(asset_path):
        return load(asset_path)
    material = ASSET_TOOLS.create_asset(name, MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Failed to create material {}".format(name))

    color_node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -500, -160
    )
    set_properties(color_node, constant=unreal.LinearColor(*base_color, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        color_node, "", unreal.MaterialProperty.MP_BASE_COLOR
    )

    roughness_node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -500, 20
    )
    set_properties(roughness_node, r=roughness)
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness_node, "", unreal.MaterialProperty.MP_ROUGHNESS
    )

    metallic_node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -500, 140
    )
    set_properties(metallic_node, r=metallic)
    unreal.MaterialEditingLibrary.connect_material_property(
        metallic_node, "", unreal.MaterialProperty.MP_METALLIC
    )

    if emissive is not None:
        emissive_node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -500, 270
        )
        set_properties(emissive_node, constant=unreal.LinearColor(*emissive, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            emissive_node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    ASSET_LIBRARY.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def create_high_poly_sphere(name, nanite_enabled):
    asset_path = ASSET_ROOT + "/" + name
    if ASSET_LIBRARY.does_asset_exist(asset_path):
        log("reusing {}".format(name))
        return load(asset_path)
    dynamic_mesh = unreal.DynamicMesh()
    primitive_options = unreal.GeometryScriptPrimitiveOptions()
    unreal.GeometryScript_Primitives.append_sphere_lat_long(
        dynamic_mesh,
        primitive_options,
        unreal.Transform(),
        radius=150.0,
        steps_phi=256,
        steps_theta=512,
    )

    nanite_settings = unreal.MeshNaniteSettings()
    set_properties(
        nanite_settings,
        enabled=nanite_enabled,
        keep_percent_triangles=1.0,
        fallback_percent_triangles=0.1 if nanite_enabled else 1.0,
        fallback_relative_error=1.0 if nanite_enabled else 0.0,
    )
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    set_properties(
        options,
        enable_nanite=nanite_enabled,
        nanite_settings=nanite_settings,
        enable_collision=True,
        enable_recompute_normals=False,
        enable_recompute_tangents=False,
    )
    result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        dynamic_mesh, asset_path, options
    )
    static_mesh = result[0] if isinstance(result, tuple) else result
    if not static_mesh:
        raise RuntimeError("Failed to create {}".format(name))
    ASSET_LIBRARY.save_loaded_asset(static_mesh, only_if_is_dirty=False)
    log("created {} (Nanite={})".format(name, nanite_enabled))
    return static_mesh


def actor_folder(actor, folder):
    try:
        actor.set_folder_path(folder)
    except Exception:
        pass


def make_rotator(rotation):
    """Convert the script's (pitch, yaw, roll) tuples to Unreal's Python constructor."""
    return unreal.Rotator(roll=rotation[2], pitch=rotation[0], yaw=rotation[1])


def spawn_mesh(mesh, label, location, scale=(1.0, 1.0, 1.0), material=None, rotation=(0.0, 0.0, 0.0), folder="Geometry"):
    actor = LEVEL_LIBRARY.spawn_actor_from_object(
        mesh,
        unreal.Vector(*location),
        make_rotator(rotation),
    )
    if not actor:
        raise RuntimeError("Could not spawn mesh actor {}".format(label))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor_folder(actor, folder)
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    if material:
        component.set_material(0, material)
    return actor


def spawn_text(text, label, location, size=70.0, color=(230, 235, 255, 255), rotation=(0.0, 180.0, 0.0), folder="Guide"):
    actor = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.TextRenderActor,
        unreal.Vector(*location),
        make_rotator(rotation),
    )
    actor.set_actor_label(label)
    actor_folder(actor, folder)
    component = actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(text)
    set_properties(
        component,
        world_size=size,
        text_render_color=unreal.Color(*color),
    )
    return actor


def point_camera(location, target):
    dx = target[0] - location[0]
    dy = target[1] - location[1]
    dz = target[2] - location[2]
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.sqrt(dx * dx + dy * dy)))
    return unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)


def add_camera(location, target, fov=55.0, label="LearningCamera"):
    rotation = point_camera(location, target)
    camera = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.CameraActor, unreal.Vector(*location), rotation
    )
    camera.set_actor_label(label)
    actor_folder(camera, "Cameras")
    set_properties(camera.get_component_by_class(unreal.CameraComponent), field_of_view=fov)
    try:
        LEVELS.set_level_viewport_camera_info(unreal.Vector(*location), rotation)
        LEVELS.set_level_viewport_fov(fov)
    except Exception as exc:
        unreal.log_warning("LEARNING_LABS: viewport camera setup skipped: {}".format(exc))
    return camera


def add_post_process(label="PPV_LearningLab"):
    volume = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator()
    )
    volume.set_actor_label(label)
    actor_folder(volume, "Lighting")
    set_properties(volume, unbound=True, blend_weight=1.0, enabled=True)
    return volume


def add_daylight(rotation=(-38.0, -28.0, 0.0), intensity=5.0, source_angle=2.0):
    sun = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1200.0), make_rotator(rotation)
    )
    sun.set_actor_label("L_Directional_Movable")
    actor_folder(sun, "Lighting")
    set_properties(
        sun.get_component_by_class(unreal.DirectionalLightComponent),
        mobility=unreal.ComponentMobility.MOVABLE,
        intensity=intensity,
        light_source_angle=source_angle,
        cast_shadows=True,
        atmosphere_sun_light=True,
    )

    sky = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator()
    )
    sky.set_actor_label("L_Sky_Movable")
    actor_folder(sky, "Lighting")
    set_properties(
        sky.get_component_by_class(unreal.SkyLightComponent),
        mobility=unreal.ComponentMobility.MOVABLE,
        intensity=0.45,
        real_time_capture=True,
    )

    atmosphere = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(), unreal.Rotator()
    )
    atmosphere.set_actor_label("SkyAtmosphere")
    actor_folder(atmosphere, "Lighting")
    return sun


def add_floor(cube, material, center=(300.0, 0.0, -30.0), scale=(36.0, 24.0, 0.6), label="SM_Floor"):
    return spawn_mesh(cube, label, center, scale, material, folder="Architecture")


def start_level(path):
    log("building {}".format(path))
    if ASSET_LIBRARY.does_asset_exist(path):
        if not LEVELS.load_level(path):
            raise RuntimeError("Could not load existing level {}".format(path))
        for actor in list(LEVEL_LIBRARY.get_all_level_actors()):
            LEVEL_LIBRARY.destroy_actor(actor)
    elif not LEVELS.new_level(path):
        raise RuntimeError("Could not create level {}".format(path))
    world = LEVEL_LIBRARY.get_editor_world()
    world_settings = world.get_world_settings()
    set_properties(world_settings, force_no_precomputed_lighting=True)
    return world


def finish_level():
    if not LEVELS.save_current_level():
        raise RuntimeError("Failed to save current learning level")


def build_nanite_lab(assets, materials):
    start_level(MAP_ROOT + "/L_NaniteLab")
    cube = assets["cube"]
    off_mesh = assets["high_poly_off"]
    on_mesh = assets["high_poly_on"]
    add_floor(cube, materials["dark_floor"], center=(350.0, 0.0, -35.0), scale=(38.0, 24.0, 0.7))
    add_daylight(rotation=(-36.0, -35.0, 0.0), intensity=5.0, source_angle=1.0)
    add_post_process()

    spawn_mesh(off_mesh, "SM_HighPoly_Nanite_OFF_Hero", (-100.0, -470.0, 330.0), (2.0, 2.0, 2.0), materials["warm"], folder="Nanite_OFF")
    spawn_mesh(on_mesh, "SM_HighPoly_Nanite_ON_Hero", (-100.0, 470.0, 330.0), (2.0, 2.0, 2.0), materials["cool"], folder="Nanite_ON")

    for row in range(4):
        for column in range(5):
            x = 650.0 + row * 390.0
            y_offset = column * 175.0
            z = 125.0
            spawn_mesh(off_mesh, "OFF_{:02d}_{:02d}".format(row, column), (x, -950.0 + y_offset, z), (0.72, 0.72, 0.72), materials["warm"], folder="Nanite_OFF/Instances")
            spawn_mesh(on_mesh, "ON_{:02d}_{:02d}".format(row, column), (x, 250.0 + y_offset, z), (0.72, 0.72, 0.72), materials["cool"], folder="Nanite_ON/Instances")

    spawn_text("NANITE LAB", "TXT_Nanite_Title", (-500.0, 0.0, 900.0), 105.0)
    spawn_text("NANITE OFF\n~261k triangles", "TXT_Nanite_OFF", (-80.0, -470.0, 760.0), 54.0, color=(255, 165, 90, 255))
    spawn_text("NANITE ON\nSame source mesh", "TXT_Nanite_ON", (-80.0, 470.0, 760.0), 54.0, color=(90, 200, 255, 255))
    spawn_text("Use View Mode > Nanite Visualization\nOverview / Clusters / Triangles / Overdraw", "TXT_Nanite_Help", (1200.0, 0.0, 900.0), 42.0)
    add_camera((-2550.0, 0.0, 900.0), (300.0, 0.0, 340.0), 58.0, "CAM_NaniteLab")
    finish_level()


def build_vsm_lab(assets, materials):
    start_level(MAP_ROOT + "/L_VSMLab")
    cube = assets["cube"]
    cylinder = assets["cylinder"]
    sphere = assets["sphere"]
    nanite_mesh = assets["high_poly_on"]

    add_floor(cube, materials["grey"], center=(450.0, 0.0, -30.0), scale=(44.0, 28.0, 0.6))
    add_daylight(rotation=(-42.0, -24.0, 0.0), intensity=6.0, source_angle=3.0)
    add_post_process()

    for index, x in enumerate((-700.0, -150.0, 400.0, 950.0, 1500.0, 2050.0)):
        height = 2.0 + index * 0.7
        spawn_mesh(cylinder, "ThinPole_{:02d}".format(index), (x, -650.0, 100.0 * height), (0.18, 0.18, height), materials["white"], folder="ShadowCasters/Thin")
        spawn_mesh(cube, "BoxCaster_{:02d}".format(index), (x, 0.0, 110.0 + index * 25.0), (1.4, 1.4, 2.2 + index * 0.5), materials["warm"], rotation=(0.0, index * 12.0, 0.0), folder="ShadowCasters/Boxes")
        spawn_mesh(nanite_mesh, "NaniteCaster_{:02d}".format(index), (x, 680.0, 150.0), (1.0, 1.0, 1.0), materials["cool"], folder="ShadowCasters/Nanite")

    for index, y in enumerate((-1000.0, -500.0, 0.0, 500.0, 1000.0)):
        spawn_mesh(sphere, "ContactSphere_{:02d}".format(index), (2300.0, y, 60.0), (1.15, 1.15, 1.15), materials["metal"], folder="ShadowCasters/Contact")

    point = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.PointLight, unreal.Vector(250.0, 0.0, 620.0), unreal.Rotator()
    )
    point.set_actor_label("L_Point_Movable_MoveMe")
    actor_folder(point, "Lighting")
    set_properties(
        point.get_component_by_class(unreal.PointLightComponent),
        mobility=unreal.ComponentMobility.MOVABLE,
        intensity=3200.0,
        attenuation_radius=1700.0,
        source_radius=70.0,
        cast_shadows=True,
        light_color=unreal.Color(255, 185, 120, 255),
    )

    spawn_text("VIRTUAL SHADOW MAP LAB", "TXT_VSM_Title", (-1100.0, 0.0, 1020.0), 92.0)
    spawn_text("THIN GEOMETRY", "TXT_VSM_Thin", (-500.0, -650.0, 850.0), 46.0)
    spawn_text("BOXES + SOFT PENUMBRA", "TXT_VSM_Boxes", (-500.0, 0.0, 850.0), 46.0)
    spawn_text("NANITE SHADOW CASTERS", "TXT_VSM_Nanite", (-500.0, 680.0, 850.0), 46.0)
    spawn_text("Move L_Point_Movable_MoveMe\nChange Directional Source Angle\nVisualize: Virtual Shadow Map > Cache", "TXT_VSM_Help", (1500.0, 0.0, 1100.0), 38.0)
    add_camera((-3100.0, 0.0, 1150.0), (500.0, 0.0, 260.0), 60.0, "CAM_VSMLab")
    finish_level()


def build_lumen_lab(assets, materials):
    start_level(MAP_ROOT + "/L_LumenLab")
    cube = assets["cube"]
    sphere = assets["sphere"]
    add_post_process("PPV_Lumen_Lab")

    spawn_mesh(cube, "Room_Floor", (150.0, 0.0, -20.0), (16.0, 13.0, 0.4), materials["white"], folder="Architecture")
    spawn_mesh(cube, "Room_Ceiling", (150.0, 0.0, 1020.0), (16.0, 13.0, 0.4), materials["white"], folder="Architecture")
    spawn_mesh(cube, "Room_Back", (930.0, 0.0, 500.0), (0.4, 13.0, 10.0), materials["white"], folder="Architecture")
    spawn_mesh(cube, "Room_Left_Red", (150.0, -650.0, 500.0), (16.0, 0.4, 10.0), materials["red"], folder="Architecture")
    spawn_mesh(cube, "Room_Right_Green", (150.0, 650.0, 500.0), (16.0, 0.4, 10.0), materials["green"], folder="Architecture")

    spawn_mesh(cube, "Tall_Box", (420.0, 300.0, 260.0), (3.1, 3.0, 5.2), materials["white"], rotation=(0.0, -16.0, 0.0), folder="Interior")
    spawn_mesh(cube, "Short_Box", (160.0, -310.0, 150.0), (3.8, 3.2, 3.0), materials["white"], rotation=(0.0, 20.0, 0.0), folder="Interior")
    spawn_mesh(cube, "Emissive_BackPanel", (900.0, 0.0, 730.0), (0.25, 3.8, 1.25), materials["emissive"], folder="Lighting/Emissive")

    for index, roughness in enumerate((0.02, 0.18, 0.38, 0.65, 0.95)):
        y = -440.0 + index * 220.0
        spawn_mesh(sphere, "Roughness_{:.2f}".format(roughness), (-230.0, y, 115.0), (2.0, 2.0, 2.0), materials["roughness"][index], folder="MaterialStudy/Roughness")
        spawn_text("R={:.2f}".format(roughness), "TXT_Roughness_{:02d}".format(index), (-220.0, y, 280.0), 26.0)

    point = LEVEL_LIBRARY.spawn_actor_from_class(
        unreal.PointLight, unreal.Vector(50.0, 0.0, 760.0), unreal.Rotator()
    )
    point.set_actor_label("L_Point_Primary_MoveMe")
    actor_folder(point, "Lighting")
    set_properties(
        point.get_component_by_class(unreal.PointLightComponent),
        mobility=unreal.ComponentMobility.MOVABLE,
        intensity=4200.0,
        attenuation_radius=1850.0,
        source_radius=85.0,
        cast_shadows=True,
        light_color=unreal.Color(255, 214, 170, 255),
    )

    spawn_text("LUMEN GI + REFLECTION LAB", "TXT_Lumen_Title", (-700.0, 0.0, 900.0), 86.0)
    spawn_text("RED / GREEN COLOR BLEED", "TXT_Lumen_Bounce", (650.0, 0.0, 920.0), 38.0)
    spawn_text("Move L_Point_Primary_MoveMe\nToggle Lumen GI / Reflections\nView: Lumen Overview / Surface Cache", "TXT_Lumen_Help", (600.0, 0.0, 1120.0), 34.0)
    add_camera((-2050.0, 0.0, 560.0), (120.0, 0.0, 430.0), 55.0, "CAM_LumenLab")
    finish_level()


def main():
    log("building maps and reusing generated assets in {}".format(ROOT))
    ASSET_LIBRARY.make_directory(ASSET_ROOT)
    ASSET_LIBRARY.make_directory(MATERIAL_ROOT)
    ASSET_LIBRARY.make_directory(MAP_ROOT)

    materials = {
        "white": create_material("M_Lab_White", (0.68, 0.68, 0.68), 0.62),
        "grey": create_material("M_Lab_Grey", (0.22, 0.24, 0.27), 0.72),
        "dark_floor": create_material("M_Lab_DarkFloor", (0.035, 0.045, 0.06), 0.78),
        "red": create_material("M_Lab_Red", (0.58, 0.025, 0.018), 0.62),
        "green": create_material("M_Lab_Green", (0.018, 0.42, 0.055), 0.62),
        "warm": create_material("M_Lab_Warm", (0.72, 0.14, 0.035), 0.34, 0.15),
        "cool": create_material("M_Lab_Cool", (0.025, 0.28, 0.76), 0.34, 0.15),
        "metal": create_material("M_Lab_Metal", (0.72, 0.76, 0.82), 0.24, 1.0),
        "emissive": create_material("M_Lab_Emissive", (1.0, 0.32, 0.035), 0.35, 0.0, (18.0, 4.2, 0.35)),
    }
    materials["roughness"] = []
    for index, roughness in enumerate((0.02, 0.18, 0.38, 0.65, 0.95)):
        materials["roughness"].append(
            create_material("M_Lab_Metal_R{:02d}".format(index), (0.72, 0.76, 0.82), roughness, 1.0)
        )

    assets = {
        "cube": load("/Engine/BasicShapes/Cube.Cube"),
        "sphere": load("/Engine/BasicShapes/Sphere.Sphere"),
        "cylinder": load("/Engine/BasicShapes/Cylinder.Cylinder"),
        "high_poly_off": create_high_poly_sphere("SM_HighPolySphere_Nanite_OFF", False),
        "high_poly_on": create_high_poly_sphere("SM_HighPolySphere_Nanite_ON", True),
    }

    build_nanite_lab(assets, materials)
    build_vsm_lab(assets, materials)
    build_lumen_lab(assets, materials)
    ASSET_LIBRARY.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("SUCCESS - generated Nanite, VSM, and Lumen labs")


try:
    main()
except Exception as error:
    unreal.log_error("LEARNING_LABS: FAILED: {}".format(error))
    raise
