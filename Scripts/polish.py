"""
Fix the three defects visible in the first in-game capture:
  1. the ship renders uniformly grey - the imported materials carry no colour
  2. the ocean is only 512 m across - the water zone keeps its default extent
  3. the hull floats above the surface - a 6 m collision sphere lifts the pawn

Material graphs, unlike Blueprint graphs, ARE scriptable, so we build a small
master material and drive six instances from it.
"""
import unreal

L = unreal.log
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MAT_DIR = "/Game/Materials"
MASTER = MAT_DIR + "/M_ShipMaster"
MESH = "/Game/Meshes/SM_PirateShip"
MAP = "/Game/Maps/L_OpenSea"
BP_SHIP = "/Game/Blueprints/BP_PirateShip"

# linear base colour, roughness, metallic - taken from the Blender export
PALETTE = {
    "M_Hull":     ((0.055, 0.030, 0.018), 0.72, 0.0),
    "M_Deck":     ((0.280, 0.185, 0.105), 0.85, 0.0),
    "M_Wood":     ((0.115, 0.068, 0.035), 0.80, 0.0),
    "M_Sail":     ((0.720, 0.660, 0.520), 0.92, 0.0),
    "M_DarkWood": ((0.040, 0.022, 0.013), 0.70, 0.0),
    "M_Iron":     ((0.045, 0.045, 0.050), 0.42, 0.9),
}


def sp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        L("POL WARN %s.%s: %s" % (obj.get_class().get_name(), name, str(e)[:90]))
        return False


# ------------------------------------------------------------- materials
def build_master():
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)
    mat = AT.create_asset("M_ShipMaster", MAT_DIR, unreal.Material,
                          unreal.MaterialFactoryNew())

    col = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -420, -100)
    sp(col, "parameter_name", "BaseColor")
    sp(col, "default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -420, 90)
    sp(rough, "parameter_name", "Roughness")
    sp(rough, "default_value", 0.8)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -420, 200)
    sp(metal, "parameter_name", "Metallic")
    sp(metal, "default_value", 0.0)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    EAL.save_asset(MASTER)
    L("POL master material built, expressions=%d"
      % MEL.get_num_material_expressions(mat))
    return mat


def build_instances(master):
    out = {}
    for name, (rgb, rough, metal) in PALETTE.items():
        path = "%s/MI_%s" % (MAT_DIR, name.replace("M_", ""))
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mi = AT.create_asset(path.split("/")[-1], MAT_DIR,
                             unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_vector_parameter_value(
            mi, "BaseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
        MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
        EAL.save_asset(path)
        out[name] = mi
        L("POL instance %s rgb=%.3f,%.3f,%.3f" % (name, rgb[0], rgb[1], rgb[2]))
    return out


def assign_to_mesh(instances):
    mesh = EAL.load_asset(MESH)
    slots = mesh.get_editor_property("static_materials")
    changed = 0
    rebuilt = []
    for i, slot in enumerate(slots):
        sname = slot.get_editor_property("material_slot_name")
        mi = instances.get(str(sname))
        # Structs come back as copies, so mutating them in place is lost on
        # save. Build brand new StaticMaterial entries instead.
        ns = unreal.StaticMaterial()
        ns.set_editor_property("material_slot_name", sname)
        try:
            ns.set_editor_property(
                "imported_material_slot_name",
                slot.get_editor_property("imported_material_slot_name"))
        except Exception:
            pass
        ns.set_editor_property(
            "material_interface",
            mi if mi is not None else slot.get_editor_property("material_interface"))
        if mi is not None:
            changed += 1
        else:
            L("POL slot %d '%s' has no palette entry" % (i, sname))
        rebuilt.append(ns)
    mesh.set_editor_property("static_materials", rebuilt)
    EAL.save_asset(MESH)
    L("POL assigned %d/%d slots" % (changed, len(slots)))

    # read back from a fresh load so we are not trusting our own in-memory copy
    EAL.load_asset(MESH)
    for i, slot in enumerate(EAL.load_asset(MESH).get_editor_property("static_materials")):
        mi = slot.get_editor_property("material_interface")
        L("POL verify slot %d -> %s" % (i, mi.get_name() if mi else "NONE"))


# ------------------------------------------------------------- pawn
def fix_pawn():
    gc = EAL.load_blueprint_class(BP_SHIP)
    cdo = unreal.get_default_object(gc)

    for enum_name in ("SpawnActorCollisionHandlingMethod",
                      "ESpawnActorCollisionHandlingMethod"):
        e = getattr(unreal, enum_name, None)
        if e is not None:
            sp(cdo, "spawn_collision_handling_method", e.ALWAYS_SPAWN)
            L("POL spawn handling via %s" % enum_name)
            break
    else:
        L("POL WARN no spawn-collision enum found")

    coll = cdo.get_editor_property("collision_component")
    if coll:
        sp(coll, "sphere_radius", 250.0)
        L("POL collision radius=%s" % coll.get_editor_property("sphere_radius"))

    # nudge the hull so the modelled waterline sits just under the surface
    for c in cdo.get_components_by_class(unreal.StaticMeshComponent):
        sp(c, "relative_location", unreal.Vector(0.0, 0.0, -60.0))
        L("POL mesh '%s' z=%s" % (c.get_name(),
                                  c.get_editor_property("relative_location").z))

    EAL.save_asset(BP_SHIP)
    L("POL pawn saved")


# ------------------------------------------------------------- ocean
def fix_ocean():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()

    for a in eas.get_all_level_actors():
        cn = a.get_class().get_name()
        if cn == "WaterZone":
            sp(a, "zone_extent", unreal.Vector2D(1600000.0, 1600000.0))
            sp(a, "render_target_resolution", unreal.IntPoint(2048, 2048))
            L("POL zone_extent=%s" % a.get_editor_property("zone_extent"))
        elif cn == "WaterBodyOcean":
            for c in a.get_components_by_class(unreal.ActorComponent):
                if "WaterBodyOceanComponent" not in c.get_class().get_name():
                    continue
                # give the surface some swell if the wave API is reachable
                try:
                    wa = AT.create_asset(
                        "WW_OceanSwell", "/Game/Materials",
                        unreal.WaterWavesAsset,
                        unreal.WaterWavesAssetFactory())
                    gw = unreal.GerstnerWaterWaves()
                    sp(gw, "num_waves", 8)
                    sp(wa, "water_waves", gw)
                    EAL.save_asset("/Game/Materials/WW_OceanSwell")
                    sp(c, "water_waves_asset", wa)
                    L("POL waves attached")
                except Exception as e:
                    L("POL waves unavailable: %s" % str(e)[:110])

    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    L("POL level saved=%s" % ok)


def main():
    master = build_master()
    assign_to_mesh(build_instances(master))
    fix_pawn()
    fix_ocean()
    L("POL_DONE")


main()
