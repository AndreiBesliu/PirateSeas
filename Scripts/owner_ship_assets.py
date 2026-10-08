"""
Brings the owner's ships into the project: the textures they are painted with,
and each converted mesh (Scripts/owner_ship.py writes Scripts/SM_Ship_<X>.fbx
and .json first).

  Scripts/run_py.ps1 owner_ship_assets.py OWNERSHIP

Run it TWICE, like every importer here: import_asset_tasks ends by asking the
Content Browser to select what it imported, a commandlet has no Slate, and the
editor dies AFTER the asset is on disk. The second run imports nothing and
checks everything - and the check is the point:

  - every texture present, colour, sRGB, wrapping, and (the flags are 512 x 320)
    stretched to a power of two so they keep their mips: a flag without mips
    shimmers at the range a flag is seen from;
  - the mesh's triangle count equal to the converter's, to the triangle - fewer
    means the import took a stale file;
  - its material slots exactly the twelve parts, by name, because the game
    repaints them by name;
  - every gun mouth present as a SOCKET at the position the converter wrote,
    to the millimetre: the game puts the smoke and the flash there, and a
    socket that silently failed to import would put them at the hull origin.

Materials are NOT made here. ship_materials.py owns every ship material and
assigns the default (the brig's own pirate livery) to the slots.
"""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(os.path.dirname(HERE), "Art", "OwnerShips", "textures")
TEX_DEST = "/Game/Ships/Textures"
MESH_DEST = "/Game/Ships"

# The ships the game uses, and the liveries it paints them in. Only these are
# imported: an asset nothing references is weight in every clone and every
# cook, and is imported the day something does.
SHIPS = ["SM_Ship_Brig"]
LIVERIES = ["pirate", "navy", "merchant"]
THEMED = ["HullPlanks", "HullBottom", "HullBand", "HullTrim", "Sail", "Flag"]
SHARED_TEX = ["Deck_Planks", "Spar_Wood"]
PARTS = sorted(THEMED + ["Deck", "Spar", "Iron", "Rope", "DarkInterior", "WindowGlow"])

BAD = []


def L(m):
    unreal.log("OWNERSHIP " + m)


def bad(m):
    BAD.append(m)
    unreal.log_error("OWNERSHIP " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s=%s: %s" % (n, v, str(e)[:80]))
        return False


def textures():
    names = SHARED_TEX + ["%s_%s" % (lv, p) for lv in LIVERIES for p in THEMED]
    for n in names:
        src = os.path.join(ART, n + ".png")
        dst = TEX_DEST + "/T_" + n
        if not os.path.exists(src):
            bad("texture source missing: %s" % src)
            continue
        if EAL.does_asset_exist(dst):
            continue
        task = unreal.AssetImportTask()
        sp(task, "filename", src)
        sp(task, "destination_path", TEX_DEST)
        sp(task, "destination_name", "T_" + n)
        sp(task, "automated", True)
        sp(task, "replace_existing", True)
        sp(task, "save", True)
        sp(task, "factory", unreal.TextureFactory())
        L("importing texture %s" % n)
        AT.import_asset_tasks([task])
    ok = 0
    for n in names:
        dst = TEX_DEST + "/T_" + n
        t = EAL.load_asset(dst)
        if t is None:
            bad("texture not in the project: %s" % dst)
            continue
        sp(t, "srgb", True)
        sp(t, "compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        sp(t, "address_x", unreal.TextureAddress.TA_WRAP)
        sp(t, "address_y", unreal.TextureAddress.TA_WRAP)
        w, h = t.blueprint_get_size_x(), t.blueprint_get_size_y()
        if (w & (w - 1)) or (h & (h - 1)):
            sp(t, "power_of_two_mode",
               unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
        EAL.save_asset(dst)
        t = EAL.load_asset(dst)
        p2 = t.get_editor_property("power_of_two_mode")
        L("texture T_%-22s %dx%d srgb=%s pow2=%s" % (n, w, h, t.get_editor_property("srgb"), p2))
        if not t.get_editor_property("srgb"):
            bad("T_%s is not sRGB" % n)
        if ((w & (w - 1)) or (h & (h - 1))) and \
                p2 != unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO:
            bad("T_%s is %dx%d and was not stretched to a power of two" % (n, w, h))
        ok += 1
    L("textures %d/%d" % (ok, len(names)))


def mesh(name):
    fbx = os.path.join(HERE, name + ".fbx")
    facts = json.load(open(os.path.join(HERE, name + ".json")))
    asset = MESH_DEST + "/" + name
    if not EAL.does_asset_exist(asset):
        opts = unreal.FbxImportUI()
        sp(opts, "import_mesh", True)
        sp(opts, "import_as_skeletal", False)
        sp(opts, "import_materials", False)
        sp(opts, "import_textures", False)
        sp(opts, "import_animations", False)
        sm = opts.static_mesh_import_data
        sp(sm, "combine_meshes", True)
        sp(sm, "generate_lightmap_u_vs", True)
        sp(sm, "auto_generate_collision", False)
        sp(sm, "convert_scene", True)
        sp(sm, "normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        task = unreal.AssetImportTask()
        sp(task, "filename", fbx)
        sp(task, "destination_path", MESH_DEST)
        sp(task, "destination_name", name)
        sp(task, "automated", True)
        sp(task, "replace_existing", True)
        sp(task, "save", True)
        sp(task, "options", opts)
        L("importing %s" % fbx)
        AT.import_asset_tasks([task])
    m = EAL.load_asset(asset)
    if m is None:
        bad("%s not in the project" % asset)
        return
    tris = m.get_num_triangles(0)
    b = m.get_bounds()
    slots = sorted(str(s.get_editor_property("material_slot_name"))
                   for s in m.get_editor_property("static_materials"))
    # Unreal's build drops degenerate triangles, so the comparison is with the
    # converter's count of triangles that have an area - computed there, on
    # the same triangulation, not fitted here to whatever came back.
    want = facts["tris"] - facts["tris_degenerate"]
    L("%s tris=%d (converter %d, of which %d degenerate -> %d) origin=(%.0f,%.0f,%.0f) "
      "extent=(%.0f,%.0f,%.0f) slots=%s"
      % (name, tris, facts["tris"], facts["tris_degenerate"], want, b.origin.x, b.origin.y,
         b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z, slots))
    if tris != want:
        bad("%s has %d triangles, the converter wrote %d with an area: a stale or partial import"
            % (name, tris, want))
    if slots != PARTS:
        bad("%s slots %s, expected %s" % (name, slots, PARTS))
    # The bounds against the converter's, to the centimetre: the frame and the
    # unit survived (a mesh imported in metres would be 1/100, a mirrored one
    # would have its Y range reversed - which a symmetric hull would hide, and
    # which the sockets below would not).
    lo = [b.origin.x - b.box_extent.x, b.origin.y - b.box_extent.y, b.origin.z - b.box_extent.z]
    hi = [b.origin.x + b.box_extent.x, b.origin.y + b.box_extent.y, b.origin.z + b.box_extent.z]
    err = max(max(abs(a - c) for a, c in zip(lo, facts["bounds"]["min"])),
              max(abs(a - c) for a, c in zip(hi, facts["bounds"]["max"])))
    L("%s bounds agree with the converter to %.1f cm" % (name, err))
    if err > 1.0:
        bad("%s bounds are off by %.1f cm" % (name, err))
    # The socket array is protected from Python; FindSocket is not.
    socks = {}
    for mz in facts["muzzles"]:
        s = m.find_socket(mz["socket"])
        if s is not None:
            socks[mz["socket"]] = s.get_editor_property("relative_location")
    if m.find_socket("NoSuchSocket_ProbeOfFindSocket") is not None:
        bad("find_socket answers for a socket that does not exist; the check below means nothing")
    worst, missing = 0.0, []
    for mz in facts["muzzles"]:
        loc = socks.get(mz["socket"])
        if loc is None:
            missing.append(mz["socket"])
            continue
        worst = max(worst, abs(loc.x - mz["pos"][0]), abs(loc.y - mz["pos"][1]),
                    abs(loc.z - mz["pos"][2]))
    L("%s sockets=%d of %d muzzles, worst %.2f cm from the converter's, port side y<0: %s"
      % (name, len(socks), len(facts["muzzles"]), worst,
         all(socks[mz["socket"]].y < 0 for mz in facts["muzzles"]
             if mz["side"] == "port" and mz["socket"] in socks)))
    if missing:
        bad("%s has no socket for %s" % (name, missing))
    if worst > 0.1:
        bad("%s sockets are %.2f cm from where the converter put the muzzles" % (name, worst))


textures()
for s in SHIPS:
    mesh(s)
L("DONE" if not BAD else "FAILED %d" % len(BAD))
