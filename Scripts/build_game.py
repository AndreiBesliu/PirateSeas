"""
Build the PirateSeas prototype: import the ship, make a sailable pawn,
create the open-sea level. Pure data - no Blueprint nodes are needed because
ADefaultPawn already binds movement input in C++ and UFloatingPawnMovement can
be constrained to the water plane.

  UnrealEditor-Cmd.exe PirateSeas.uproject -run=pythonscript -script=build_game.py
"""
import os
import unreal

L = unreal.log
FBX = os.environ.get("SHIP_FBX", "")

MESH_DIR = "/Game/Meshes"
BP_DIR = "/Game/Blueprints"
MAP_DIR = "/Game/Maps"
SHIP_MESH = MESH_DIR + "/SM_PirateShip"
BP_SHIP = BP_DIR + "/BP_PirateShip"
BP_GM = BP_DIR + "/BP_SeaGameMode"
MAP = MAP_DIR + "/L_OpenSea"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
SDS = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


# ------------------------------------------------------------------ import
def import_ship():
    if EAL.does_asset_exist(SHIP_MESH):
        L("GAME mesh already present")
        return EAL.load_asset(SHIP_MESH)
    if not FBX or not os.path.exists(FBX):
        L("GAME ERROR fbx missing: %s" % FBX)
        return None

    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_as_skeletal", False)
    opts.set_editor_property("import_materials", True)
    opts.set_editor_property("import_textures", False)
    sm = opts.static_mesh_import_data
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("generate_lightmap_u_vs", True)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("convert_scene", True)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", FBX)
    task.set_editor_property("destination_path", MESH_DIR)
    task.set_editor_property("destination_name", "SM_PirateShip")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", opts)
    AT.import_asset_tasks([task])

    mesh = EAL.load_asset(SHIP_MESH)
    L("GAME imported=%s" % (mesh is not None))
    return mesh


# ------------------------------------------------------------------ helpers
def sp(obj, name, value):
    """Set an editor property, logging instead of dying on API name drift."""
    if obj is None:
        L("GAME WARN set %s on None" % name)
        return False
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        L("GAME WARN %s.%s failed: %s" % (obj.get_class().get_name(), name, e))
        return False


def add_component(bp, comp_class, name, parent_handle=None):
    handles = SDS.k2_gather_subobject_data_for_blueprint(bp)
    root = parent_handle if parent_handle is not None else handles[0]
    params = unreal.AddNewSubobjectParams(
        parent_handle=root, new_class=comp_class, blueprint_context=bp)
    handle, fail = SDS.add_new_subobject(params)
    if not fail.is_empty():
        L("GAME ERROR add %s: %s" % (name, fail))
        return None, None
    SDS.rename_subobject(handle, unreal.Text(name))
    data = SDS.k2_find_subobject_data_from_handle(handle)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    return handle, obj


def root_handle(bp):
    handles = SDS.k2_gather_subobject_data_for_blueprint(bp)
    # handles[0] is the actor itself; the scene root is the first component
    for h in handles[1:]:
        d = SDS.k2_find_subobject_data_from_handle(h)
        o = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        if isinstance(o, unreal.SceneComponent):
            return h, o
    return handles[0], None


# ------------------------------------------------------------------ pawn
def build_ship_bp(mesh):
    if EAL.does_asset_exist(BP_SHIP):
        EAL.delete_asset(BP_SHIP)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.DefaultPawn)
    bp = AT.create_asset("BP_PirateShip", BP_DIR, unreal.Blueprint, factory)

    rh, rcomp = root_handle(bp)
    L("GAME ship root=%s" % (rcomp.get_name() if rcomp else "?"))

    # hull mesh
    _, mc = add_component(bp, unreal.StaticMeshComponent, "ShipMesh", rh)
    if mc:
        sp(mc, "static_mesh", mesh)
        sp(mc, "relative_location", unreal.Vector(0, 0, -120))
        try:
            mc.set_collision_profile_name("NoCollision")
        except Exception as e:
            L("GAME WARN mesh collision profile: %s" % e)

    # third person camera on a spring arm
    ah, arm = add_component(bp, unreal.SpringArmComponent, "CameraBoom", rh)
    if arm:
        sp(arm, "target_arm_length", 4200.0)
        sp(arm, "relative_location", unreal.Vector(0, 0, 900))
        sp(arm, "relative_rotation", unreal.Rotator(0, -18, 0))
        sp(arm, "use_pawn_control_rotation", True)
        sp(arm, "do_collision_test", False)
        sp(arm, "enable_camera_lag", True)
        sp(arm, "camera_lag_speed", 2.5)
        sp(arm, "enable_camera_rotation_lag", True)
        sp(arm, "camera_rotation_lag_speed", 4.0)
    _, cam = add_component(bp, unreal.CameraComponent, "ShipCamera", ah)
    if cam:
        sp(cam, "field_of_view", 85.0)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    # class defaults: sailing feel + lock the hull to the water plane
    gc = EAL.load_blueprint_class(BP_SHIP)
    cdo = unreal.get_default_object(gc)
    coll = cdo.get_editor_property("collision_component")
    if coll:
        sp(coll, "sphere_radius", 600.0)
    mov = cdo.get_editor_property("movement_component")
    if mov:
        sp(mov, "max_speed", 1100.0)
        sp(mov, "acceleration", 300.0)
        sp(mov, "deceleration", 260.0)
        sp(mov, "turning_boost", 1.4)
        sp(mov, "constrain_to_plane", True)
        sp(mov, "plane_constraint_normal", unreal.Vector(0, 0, 1))
        sp(mov, "plane_constraint_origin", unreal.Vector(0, 0, 0))
        L("GAME movement tuned max_speed=%s constrained=%s" % (
            mov.get_editor_property("max_speed"),
            mov.get_editor_property("constrain_to_plane")))
    # The hull turns with the mouse but must never pitch or roll with it,
    # otherwise looking up tips the whole ship out of the water.
    sp(cdo, "use_controller_rotation_yaw", True)
    sp(cdo, "use_controller_rotation_pitch", False)
    sp(cdo, "use_controller_rotation_roll", False)
    L("GAME rotation yaw=%s pitch=%s roll=%s" % (
        cdo.get_editor_property("use_controller_rotation_yaw"),
        cdo.get_editor_property("use_controller_rotation_pitch"),
        cdo.get_editor_property("use_controller_rotation_roll")))

    EAL.save_asset(BP_SHIP)
    L("GAME ship blueprint saved")
    return gc


def build_gamemode(ship_class):
    if EAL.does_asset_exist(BP_GM):
        EAL.delete_asset(BP_GM)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.GameModeBase)
    bp = AT.create_asset("BP_SeaGameMode", BP_DIR, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    gc = EAL.load_blueprint_class(BP_GM)
    cdo = unreal.get_default_object(gc)
    sp(cdo, "default_pawn_class", ship_class)
    EAL.save_asset(BP_GM)
    L("GAME gamemode default_pawn=%s" %
      cdo.get_editor_property("default_pawn_class"))
    return gc


# ------------------------------------------------------------------ level
def comp(actor, cls):
    """Robust component lookup - actor-specific accessors are not exposed."""
    if actor is None:
        return None
    try:
        c = actor.get_component_by_class(cls)
        if c is None:
            L("GAME WARN no %s on %s" % (cls.get_name(), actor.get_name()))
        return c
    except Exception as e:
        L("GAME WARN component lookup %s: %s" % (cls.get_name(), e))
        return None


def build_level(gm_class):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.new_level(MAP)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None):
        a = eas.spawn_actor_from_class(cls, unreal.Vector(*loc),
                                       unreal.Rotator(*rot))
        if a and label:
            a.set_actor_label(label)
        return a

    # --- sky & light ----------------------------------------------------
    sun = spawn(unreal.DirectionalLight, (0, 0, 20000), (0, -38, 55), "Sun")
    if sun:
        c = comp(sun, unreal.DirectionalLightComponent)
        sp(c, "intensity", 6.0)
        sp(c, "light_source_angle", 0.6)
        sp(c, "atmosphere_sun_light", True)
        sp(c, "dynamic_shadow_distance_movable_light", 60000.0)

    spawn(unreal.SkyAtmosphere, (0, 0, 0), (0, 0, 0), "SkyAtmosphere")

    skyl = spawn(unreal.SkyLight, (0, 0, 3000), (0, 0, 0), "SkyLight")
    if skyl:
        sc = comp(skyl, unreal.SkyLightComponent)
        sp(sc, "real_time_capture", True)
        sp(sc, "intensity_scale", 1.0)

    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -500), (0, 0, 0), "Fog")
    if fog:
        fc = comp(fog, unreal.ExponentialHeightFogComponent)
        sp(fc, "fog_density", 0.008)
        sp(fc, "fog_height_falloff", 0.06)
        sp(fc, "start_distance", 2000.0)

    try:
        spawn(unreal.VolumetricCloud, (0, 0, 0), (0, 0, 0), "Clouds")
    except Exception as e:
        L("GAME clouds skipped: %s" % e)

    # --- ocean ----------------------------------------------------------
    zone = spawn(unreal.WaterZone, (0, 0, 0), (0, 0, 0), "WaterZone")
    if zone:
        try:
            zc = comp(zone, unreal.WaterZoneComponent)
            sp(zc, "zone_extent",
                                   unreal.Vector2D(1600000.0, 1600000.0))
        except Exception as e:
            L("GAME zone_extent skipped: %s" % e)

    ocean = spawn(unreal.WaterBodyOcean, (0, 0, 0), (0, 0, 0), "Ocean")
    L("GAME ocean_spawned=%s zone_spawned=%s"
      % (ocean is not None, zone is not None))

    # --- player ---------------------------------------------------------
    spawn(unreal.PlayerStart, (0, 0, 0), (0, 0, 0), "ShipStart")

    ws = unreal.GameplayStatics.get_game_mode(unreal.EditorLevelLibrary.get_editor_world()) \
        if False else None

    # world settings: use our game mode for this map
    world = unreal.EditorLevelLibrary.get_editor_world()
    settings = world.get_world_settings()
    sp(settings, "default_game_mode", gm_class)

    actors = eas.get_all_level_actors()
    L("GAME spawned %d actors before save" % len(actors))
    for a in actors:
        L("GAME   actor %-22s %s" % (a.get_actor_label(), a.get_class().get_name()))

    # Is this a World Partition level? If so its actors live in external
    # packages and save_current_level alone silently leaves them behind.
    try:
        wp = world.get_editor_property("world_partition")
        L("GAME world_partition=%s" % ("yes" if wp else "no"))
    except Exception as e:
        L("GAME world_partition unknown: %s" % e)

    # save_current_level() returns False inside a commandlet, so try every
    # documented save path and stop at the first one that actually reports true.
    saved = False
    attempts = []

    try:
        r = les.save_current_level()
        attempts.append(("save_current_level", r))
        saved = saved or bool(r)
    except Exception as e:
        attempts.append(("save_current_level", "EXC %s" % e))

    if not saved:
        try:
            r = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
            attempts.append(("save_map", r))
            saved = saved or bool(r)
        except Exception as e:
            attempts.append(("save_map", "EXC %s" % e))

    if not saved:
        try:
            pkg = world.get_outer()
            r = unreal.EditorLoadingAndSavingUtils.save_packages([pkg], False)
            attempts.append(("save_packages", r))
            saved = saved or bool(r)
        except Exception as e:
            attempts.append(("save_packages", "EXC %s" % e))

    if not saved:
        try:
            r = unreal.EditorAssetLibrary.save_asset(MAP, False)
            attempts.append(("save_asset", r))
            saved = saved or bool(r)
        except Exception as e:
            attempts.append(("save_asset", "EXC %s" % e))

    for name, r in attempts:
        L("GAME save %-20s -> %s" % (name, r))
    L("GAME save_ok=%s" % saved)


# ------------------------------------------------------------------ main
def main():
    mesh = import_ship()
    if mesh is None:
        L("GAME ABORT no mesh")
        return
    b = mesh.get_bounding_box()
    L("GAME mesh bounds min=%s max=%s" % (b.min, b.max))
    L("GAME mesh tris=%d" % mesh.get_num_triangles(0))

    ship_class = build_ship_bp(mesh)
    gm_class = build_gamemode(ship_class)
    build_level(gm_class)
    L("GAME_DONE")


main()
