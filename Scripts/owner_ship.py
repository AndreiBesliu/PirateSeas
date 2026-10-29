"""
Turns one of the OWNER'S ships (Art/OwnerShips/FBX, made in Blender 5.2 for a
Unity-style import) into what this project's Unreal import expects.

  blender -b --factory-startup -P owner_ship.py -- <owner.fbx> <out_dir> <AssetName>

WHAT COMES IN (measured on Ship_Brig.fbx, not taken from its README): metres;
+Y up and the bow towards +Z, port towards +X; the origin at the STERN, on the
waterline; a "_Rock" empty under the root that rolls, pitches and heaves the
whole ship in a 240-frame loop; every sail and the flag carrying shape keys.

WHAT GOES OUT is Scripts/ship.py's frame, because the import, the hull origin
and every gun-port constant in ShipPawn.cpp are written against it: metres,
bow +X, port +Y, up +Z, origin on the waterline at MID-HULL; one static mesh
whose material slots are named for the PART, not the livery (HullPlanks, not
pirate_HullPlanks), so the game can paint any hull in any colours; and every
gun's mouth as a SOCKET_ empty, which Unreal turns into a socket on the mesh -
the barrels are where the model says they are, asked of the model.

THREE THINGS THE FBX DOES NOT SAY AND THIS FILE MUST NOT GET WRONG.

1. THE SHIP IS ANIMATED, AND NOT WHERE THE README SAYS. The generator rocks
   one empty ("_Rock") and parents everything to it - but the FBX exporter
   baked the motion into EVERY part's own curves as well, so the parts do not
   follow that empty: putting the "_Rock" back to identity (the first version
   of this file) left the brig pitched 1.7 degrees and lifted up to a metre at
   the bow, and only the waterline check below saw it. What IS true, measured
   across frames 1-181: the parts move RIGIDLY with the hull (within the
   exporter's 1.6 cm of curve simplification), and the hull's world transform
   is exactly the generator's rock pose - at frame 1 (a forced key, so exact)
   0.179 m of heave and 0.0149 rad of pitch, which are 0.18*(L/25)^0.3*sin(1.3)
   and 1.4*sqrt(25/L)*sin(0.7) degrees of ships_lib2.py with L = 28. The hull
   was built at the origin, so its rest is identity, and every part's rest is
   its pose RELATIVE TO THE HULL on frame 1. The game rocks the ship itself.

2. THE WATERLINE IS CHECKED, NOT ASSUMED. The generator puts a ring of hull
   vertices at z = 0 exactly (levels() starts with 0.0), and the bottom paint
   is every face below it. So after the transform, the vertices where the
   bottom paint meets the topsides must be at z = 0: if the rock were not at
   rest, or the axes swapped, that ring would tilt or lift. Printed as a fit
   (height, slope along the hull, worst point) and FAILED past a centimetre.

3. THE SIDES ARE CHECKED, NOT ASSUMED. A port muzzle must come out at +Y and
   a starboard one at -Y, the rudder must be aft of mid-hull and the masts
   above the deck - an axis map that is a reflection, or a quarter turn wrong,
   breaks at least one of those.

Writes <out_dir>/<AssetName>.fbx and <out_dir>/<AssetName>.json - the second
in UNREAL's frame and units (centimetres, Y to STARBOARD), because that is the
frame the game and the gate read it in. Then re-imports its own FBX into an
empty scene and reports what is actually in the file.

AND WHAT A CANNONBALL MEETS (17.10), three more meshes the game never draws:

  <AssetName>_Shot.fbx       the timber: _Hull, _Deck, _DeckProps, _Rudder.
                             The hull is one closed skin (no free edge); the
                             props are the port frames and lids, the chain
                             wales, the stern windows and the quarterdeck
                             front - things that are there to be hit. Not the
                             guns: their barrels are the battery, and seven
                             a side against the game's four is the owner's.
  <AssetName>_ForeCloth.fbx  the fore-and-aft sails of the foremost mast
  <AssetName>_MainCloth.fbx  ... and of the aftmost (jibs; spanker).

A sail is FORE-AND-AFT when it is longer than it is wide (seen from above),
square otherwise - measured, not read off its name, because the next ship may
be a schooner. Square sails are NOT written as meshes: from the beam a square
sail is a sliver one metre deep, and the game keeps a box for them (its
thickness is the game's, ShipPawn.cpp); the facts give the box its width,
height and floor. The rail profile is written so the gate can derive that
floor the way the game does.
"""
import bpy
import hashlib
import json
import math
import os
import sys
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT, NAME = argv[0], argv[1], argv[2]
os.makedirs(OUT, exist_ok=True)

FAIL = []


def say(m):
    print("SHIPCONV " + m)


def fail(m):
    FAIL.append(m)
    print("SHIPCONV FAIL " + m)


# Material names in the owner's file -> the part they paint. Themed parts carry
# the livery as a prefix (pirate_, pirate_red_, navy_, merchant_, galleon_);
# the shared ones are the same in every ship. Anything not in this table stops
# the run: a slot the game does not know about would draw in default grey.
THEMED = ("HullPlanks", "HullBottom", "HullBand", "HullTrim", "Sail", "Flag")
SHARED = {"Deck_Planks": "Deck", "Spar_Wood": "Spar", "Iron": "Iron",
          "Rope": "Rope", "Dark_Interior": "DarkInterior",
          "Window_Glow": "WindowGlow", "Gilded_Trim": "GildedTrim"}


def part_of(matname):
    base = matname.split(".")[0]
    if base in SHARED:
        return SHARED[base], None
    for p in THEMED:
        if base.endswith("_" + p):
            return p, base[:-(len(p) + 1)]
    return None, None


# ------------------------------------------------------------------ import
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
sha = hashlib.sha1(open(SRC, "rb").read()).hexdigest()
say("source %s sha1=%s" % (os.path.basename(SRC), sha))

hulls = [o for o in bpy.data.objects if o.type == "MESH" and o.name.endswith("_Hull")]
if len(hulls) != 1:
    fail("expected one _Hull mesh, got %s" % [o.name for o in hulls])
    sys.exit(1)
hull_obj = hulls[0]

# 1. Every part to its rest: its pose relative to the hull, on frame 1 - and
# the same taken on another frame, to show it IS rigid and not just assumed so.
def rel_poses(frame):
    bpy.context.scene.frame_set(frame)
    hi = hull_obj.matrix_world.inverted()
    return {o.name: hi @ o.matrix_world for o in bpy.data.objects
            if o.type in ("MESH", "EMPTY") and not o.name.endswith("_Rock") and o.parent}


check = rel_poses(121)
bpy.context.scene.frame_set(1)
pose = hull_obj.matrix_world.copy()
rest = rel_poses(1)
drift = max(max(abs((rest[n] - check[n])[i][j]) for i in range(3) for j in range(4))
            for n in rest)
say("hull pose on frame 1: heave %.4f m along the source's up, rotation %s rad"
    % (pose.to_translation().y, tuple(round(c, 4) for c in pose.to_euler())))
say("parts relative to the hull: frame 1 vs frame 121 agree to %.4f (m / rotation terms)" % drift)
if drift > 0.03:
    fail("the parts do not move rigidly with the hull (%.4f)" % drift)
for o in bpy.data.objects:
    if o.animation_data:
        o.animation_data_clear()
for name, m in rest.items():
    o = bpy.data.objects[name]
    o.parent = None
    o.matrix_world = m
bpy.context.view_layer.update()

# Sails and flag: the shape keys go, the basis stays. A static mesh cannot
# carry them, and Unreal's static import would drop them silently anyway; done
# here, on purpose, the basis is what is kept and the count is said.
keyed = 0
for o in bpy.data.objects:
    if o.type == "MESH" and o.data.shape_keys:
        keyed += 1
        o.shape_key_clear()
say("shape keys cleared on %d meshes (basis kept)" % keyed)
bpy.context.view_layer.update()

# ------------------------------------------------- the frame, and the checks
# Imported frame -> ship.py's: x = bow (was z'), y = port (was x'), z = up (was
# y'). A cyclic permutation, so a rotation and never a mirror; asserted anyway.
R = Matrix(((0, 0, 1, 0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
if abs(R.to_3x3().determinant() - 1.0) > 1e-9:
    fail("axis map is not a rotation")

hw = R @ hull_obj.matrix_world
hv = [hw @ v.co for v in hull_obj.data.vertices]
x_lo = min(p.x for p in hv)
x_hi = max(p.x for p in hv)
mid = 0.5 * (x_lo + x_hi)
T = Matrix.Translation((-mid, 0.0, 0.0)) @ R
say("hull runs %.3f..%.3f m along the bow axis; mid-hull %.3f m from the source origin"
    % (x_lo, x_hi, mid))

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
muzzles = [o for o in bpy.data.objects if o.type == "EMPTY" and o.name.startswith("Muzzle_")]
mz_pos = {o.name: T @ o.matrix_world.translation for o in muzzles}

# Bake every mesh into the new frame, unparented.
for o in meshes:
    mw = T @ o.matrix_world
    o.parent = None
    o.data.transform(mw)
    o.matrix_world = Matrix.Identity(4)
bpy.context.view_layer.update()

# 2. The waterline ring. Vertices shared by a bottom-paint face and a face of
# any other paint, on the OUTER skin: the solidify modifier's inner copy of
# that ring sits 22 cm inboard, and lower or higher by however much the side
# slopes there, so only the vertices on the outboard half of each station are
# taken.
me = hull_obj.data
bottom = None
for i, m in enumerate(me.materials):
    if m and part_of(m.name)[0] == "HullBottom":
        bottom = i
if bottom is None:
    fail("no HullBottom slot on the hull")
else:
    # EDGES, not vertices: the transom is one face painted by its centre, and
    # every vertex of its outline below the waterline also touches a bottom
    # face - a vertex test took the whole stern outline for "the waterline"
    # and reported a metre of error that was not there. The waterline is the
    # run of edges between the two paints that runs FORE AND AFT.
    faces_of = {}
    for p in me.polygons:
        for ek in p.edge_keys:
            faces_of.setdefault(ek, []).append(p.material_index == bottom)
    ring_v = set()
    for (a, b), fl in faces_of.items():
        if len(fl) == 2 and fl[0] != fl[1]:
            d = me.vertices[b].co - me.vertices[a].co
            if d.length > 1e-6 and abs(d.x) > 0.7 * d.length:
                ring_v.update((a, b))
    ring = [me.vertices[vi].co.copy() for vi in ring_v]
    # The OUTER skin per station and side: the solidify copy is 22 cm inboard,
    # and boundary edges also run inboard of the planking. "Outboard" is taken
    # from the WHOLE hull's breadth at the waterline in that station, not from
    # the ring's own widest vertex - a station whose ring held only inboard
    # vertices otherwise called the widest of those the skin (26 of them, a
    # metre inboard, on the first run of this check).
    breadth = {}
    for v in me.vertices:
        if abs(v.co.z) < 0.05:
            k = (round(v.co.x * 4), v.co.y > 0)
            breadth[k] = max(breadth.get(k, 0.0), abs(v.co.y))
    # And the middle 60% of her length only. Towards the stem the solidify
    # offset turns fore and aft, so its inner copy keeps nearly the outer
    # skin's breadth and takes the normal's tilt as height: 14 vertices from
    # 9.4 m forward, 2 to 4 cm off, inside the forefoot, on the runs that
    # tried 100% and 80%. Eighteen metres of parallel body is lever enough:
    # the frame-1 pitch this file exists to remove would put the ends of it
    # 13 cm off, and one centimetre fails.
    half_len = 0.3 * (max(v.co.x for v in me.vertices) - min(v.co.x for v in me.vertices))
    outer = [c for c in ring
             if abs(c.x) <= half_len
             and abs(c.y) >= breadth.get((round(c.x * 4), c.y > 0), 1e9) - 0.05]
    prof = sorted(outer, key=lambda c: c.x)
    if prof:
        step = max(1, len(prof) // 8)
        say("waterline profile (x m, z m): %s"
            % ", ".join("%.1f:%.3f" % (c.x, c.z) for c in prof[::step]))
    n = len(outer)
    if n < 20:
        fail("waterline ring has only %d outer vertices" % n)
    else:
        sx = sum(c.x for c in outer); sz = sum(c.z for c in outer)
        sxx = sum(c.x * c.x for c in outer); sxz = sum(c.x * c.z for c in outer)
        slope = (n * sxz - sx * sz) / max(1e-9, n * sxx - sx * sx)
        height = (sz - slope * sx) / n
        worst = max(abs(c.z) for c in outer)
        say("waterline ring: %d outer vertices, height %.4f m, slope %.5f m/m, worst %.4f m"
            % (n, height, slope, worst))
        off = sorted((c for c in outer if abs(c.z) > 0.01), key=lambda c: c.x)
        if off:
            say("off the waterline: %d - %s" % (len(off), ", ".join(
                "(%.2f, %.2f, %.3f)" % (c.x, c.y, c.z) for c in off[:12])))
        if worst > 0.01:
            fail("waterline ring is not at z=0 (worst %.4f m): the rock is not at rest "
                 "or the axes are wrong" % worst)

# 3. The sides.
ports = [p for n_, p in mz_pos.items() if "_Port_" in n_]
stbds = [p for n_, p in mz_pos.items() if "_Starboard_" in n_]
if not ports or not stbds or len(ports) != len(stbds):
    fail("muzzles: %d port, %d starboard" % (len(ports), len(stbds)))
if any(p.y <= 0 for p in ports) or any(p.y >= 0 for p in stbds):
    fail("a port muzzle is not at +Y or a starboard one not at -Y")
pairs_off = 0.0
for n_, p in mz_pos.items():
    if "_Port_" in n_:
        q = mz_pos.get(n_.replace("_Port_", "_Starboard_"))
        if q is None:
            fail("no starboard twin for %s" % n_)
            continue
        pairs_off = max(pairs_off, abs(p.x - q.x), abs(p.y + q.y), abs(p.z - q.z))
say("muzzles: %d a side, mirror pairs agree to %.4f m" % (len(ports), pairs_off))
if pairs_off > 0.005:
    fail("port and starboard muzzles are not mirror images (%.4f m)" % pairs_off)

rud = [o for o in meshes if o.name.endswith("_Rudder")]
if rud:
    rx = sum((v.co.x for v in rud[0].data.vertices)) / len(rud[0].data.vertices)
    if rx >= 0:
        fail("the rudder is forward of mid-hull (x=%.2f)" % rx)
masts = [o for o in meshes if o.name.endswith("_Masts")]
if masts and max(v.co.z for v in masts[0].data.vertices) < 10.0:
    fail("the masts do not stand up")


def bounds(o):
    vs = [v.co for v in o.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return lo, hi


def ue(v):
    """Blender metres (y = port) -> Unreal centimetres (y = starboard)."""
    return [round(v.x * 100.0, 1), round(-v.y * 100.0, 1), round(v.z * 100.0, 1)]


facts = {"asset": NAME, "source": os.path.basename(SRC), "source_sha1": sha,
         "frame": "Unreal: cm, X bow, Y starboard, Z up, origin waterline mid-hull"}
hlo, hhi = bounds(hull_obj)
facts["hull"] = {"x_min": round(hlo.x * 100, 1), "x_max": round(hhi.x * 100, 1),
                 "half_beam": round(max(-hlo.y, hhi.y) * 100, 1),
                 "keel_z": round(hlo.z * 100, 1), "rail_z": round(hhi.z * 100, 1)}

# Masts, from their sails: the joined "_Masts" mesh does not say where one mast
# ends, but every square sail and gaff is named for its mast (Sail_M1_...).
rig = {}
for o in meshes:
    if "_Sail_M" in o.name:
        k = o.name.split("_Sail_M")[1].split("_")[0]
        lo, hi = bounds(o)
        r = rig.setdefault(k, {"lo": lo.copy(), "hi": hi.copy(), "sails": 0})
        r["lo"] = Vector(map(min, r["lo"], lo)); r["hi"] = Vector(map(max, r["hi"], hi))
        r["sails"] += 1
facts["masts"] = [{"mast": k, "sails": r["sails"],
                   "x_min": round(r["lo"].x * 100, 1), "x_max": round(r["hi"].x * 100, 1),
                   "half_width": round(max(-r["lo"].y, r["hi"].y) * 100, 1),
                   "z_min": round(r["lo"].z * 100, 1), "z_max": round(r["hi"].z * 100, 1)}
                  for k, r in sorted(rig.items())]

# ------------------------------------------- what a cannonball meets (17.10)
# The rail, station by station: the highest point of the hull in each 50 cm
# of her length. The game's rig boxes stand on it (a box that reached below
# the rail would take the hit the bulwark should have taken, because a box is
# wider than the hull), so the gate needs it to check them.
STATION = 0.5
rail = {}
for v in hull_obj.data.vertices:
    k = int(math.floor(v.co.x / STATION))
    rail[k] = max(rail.get(k, -1e9), v.co.z)
facts["hull"]["rail_profile"] = [[round(k * STATION * 100, 1), round((k + 1) * STATION * 100, 1),
                                  round(rail[k] * 100, 1)] for k in sorted(rail)]
mid_rail = [z for x0, x1, z in facts["hull"]["rail_profile"] if -500 <= x0 < 500]
say("rail: %d stations, %.0f..%.0f cm amidships, %.0f cm at most"
    % (len(rail), min(mid_rail), max(mid_rail), max(rail.values()) * 100))


def bounds_of(objs):
    vs = [v.co for o in objs for v in o.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return lo, hi


# Every sail, square or fore-and-aft, and the mast it belongs to. Square sails
# and gaffs are named for their mast (Sail_M1_...); a headsail is set on the
# foremost mast's stays and carries no number.
sails = [o for o in meshes if "_Sail_" in o.name]
mast_x = {m["mast"]: 0.5 * (m["x_min"] + m["x_max"]) for m in facts["masts"]}
foremost = max(mast_x, key=lambda k: mast_x[k]) if mast_x else None
aftmost = min(mast_x, key=lambda k: mast_x[k]) if mast_x else None
if len(mast_x) != 2:
    fail("the game has two rig zones (fore, main); this ship has %d masts" % len(mast_x))
zones = {foremost: "fore", aftmost: "main"}
rigf = {}
for o in sails:
    lo, hi = bounds(o)
    square = 2.0 * max(-lo.y, hi.y) > (hi.x - lo.x)
    k = o.name.split("_Sail_M")[1].split("_")[0] if "_Sail_M" in o.name else foremost
    z = rigf.setdefault(zones.get(k, "?"), {"mast": k, "square": [], "cloth": []})
    z["square" if square else "cloth"].append(o)
    say("sail %-28s %s, mast %s (%s): x %.2f..%.2f, half width %.2f m"
        % (o.name, "square" if square else "fore-and-aft", k, zones.get(k, "?"),
           lo.x, hi.x, max(-lo.y, hi.y)))
if "?" in rigf:
    fail("sails on no mast the game has a zone for: %s" % [o.name for o in rigf["?"]["square"] + rigf["?"]["cloth"]])
facts["rig"] = {}
for zone, z in sorted(rigf.items()):
    if zone == "?":
        continue
    entry = {"mast": z["mast"]}
    if z["square"]:
        lo, hi = bounds_of(z["square"])
        entry["square"] = {"sails": sorted(o.name for o in z["square"]),
                           "x_min": round(lo.x * 100, 1), "x_max": round(hi.x * 100, 1),
                           "half_width": round(max(-lo.y, hi.y) * 100, 1),
                           "z_min": round(lo.z * 100, 1), "z_max": round(hi.z * 100, 1)}
    if z["cloth"]:
        entry["cloth"] = {"sails": sorted(o.name for o in z["cloth"])}
    facts["rig"][zone] = entry


def tri_facts(o):
    """Triangles, the ones with no area (Unreal drops them), and the bounds in
    Unreal's frame - the same three numbers the import checks on every mesh."""
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    tris = len(bm.faces)
    degenerate = sum(1 for f in bm.faces if f.calc_area() < 1e-10)
    boundary = sum(1 for e in bm.edges if e.is_boundary)
    bm.free()
    lo, hi = bounds(o)
    return {"tris": tris, "tris_degenerate": degenerate, "free_edges": boundary,
            "bounds": {"min": ue(Vector((lo.x, hi.y, lo.z))), "max": ue(Vector((hi.x, lo.y, hi.z)))}}


def export_one(o, path):
    for x in bpy.data.objects:
        x.select_set(x is o)
    bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True,
                             apply_unit_scale=True, global_scale=1.0,
                             apply_scale_options="FBX_SCALE_NONE",
                             object_types={"MESH"}, mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_space_transform=False,
                             bake_anim=False, path_mode="STRIP", embed_textures=False)


# The three meshes, built from COPIES so the ship drawn below is untouched, and
# without materials: nothing draws them, and a livery name in a slot nobody
# sees would be one more thing to keep in step.
SHOT_PARTS = ("_Hull", "_Deck", "_DeckProps", "_Rudder")
groups = [("shot", NAME + "_Shot", [o for o in meshes if o.name.endswith(SHOT_PARTS)])]
for zone in ("fore", "main"):
    cloth = rigf.get(zone, {}).get("cloth", [])
    groups.append((zone + "_cloth", NAME + "_" + zone.capitalize() + "Cloth", cloth))
facts["shot"] = {}
for key, asset, parts in groups:
    if not parts:
        fail("%s: nothing to build it from" % asset)
        continue
    copies = []
    for o in parts:
        c = o.copy()
        c.data = o.data.copy()
        c.data.materials.clear()
        bpy.context.scene.collection.objects.link(c)
        copies.append(c)
    for x in bpy.data.objects:
        x.select_set(x in copies)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    shot = bpy.context.view_layer.objects.active
    shot.name = asset
    shot.data.name = asset
    f = tri_facts(shot)
    f["asset"] = asset
    f["parts"] = sorted(o.name for o in parts)
    path = os.path.join(OUT, asset + ".fbx")
    export_one(shot, path)
    f["fbx_sha1"] = hashlib.sha1(open(path, "rb").read()).hexdigest()
    facts["shot"][key] = f
    say("%s: %d tris (%d with no area), %d free edges, from %s"
        % (asset, f["tris"], f["tris_degenerate"], f["free_edges"], f["parts"]))
    me = shot.data
    bpy.data.objects.remove(shot, do_unlink=True)
    bpy.data.meshes.remove(me)
# The hull itself must be closed - a ball that finds a gap in the planking
# flies on to the far side's inner skin and is charged THERE, a metre and a
# half from where it went in. The props are open boxes and the deck a sheet;
# the hull is the one part that must have no free edge.
import bmesh
bm = bmesh.new()
bm.from_mesh(hull_obj.data)
hull_open = sum(1 for e in bm.edges if e.is_boundary)
bm.free()
say("the hull skin has %d free edges" % hull_open)
if hull_open:
    fail("the hull skin is not closed (%d free edges)" % hull_open)

# ------------------------------------------------------ slots, then one mesh
renamed = {}
liveries = set()
for m in bpy.data.materials:
    part, livery = part_of(m.name)
    if part is None:
        fail("material %r is not a known part" % m.name)
        continue
    if livery:
        liveries.add(livery)
    renamed[m.name] = part
for m in list(bpy.data.materials):
    if m.name in renamed:
        m.name = renamed[m.name]
        m.use_nodes = False        # no image reaches the FBX; Unreal paints it
if len(liveries) != 1:
    fail("expected one livery in the file, got %s" % sorted(liveries))
facts["livery"] = sorted(liveries)[0] if liveries else None

for o in bpy.data.objects:
    o.select_set(False)
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = hull_obj
bpy.ops.object.join()
ship = bpy.context.view_layer.objects.active
ship.name = NAME
ship.data.name = NAME

tris_by_slot = {}
for p in ship.data.polygons:
    s = ship.data.materials[p.material_index].name
    tris_by_slot[s] = tris_by_slot.get(s, 0) + len(p.vertices) - 2
facts["tris"] = sum(tris_by_slot.values())
facts["slots"] = tris_by_slot
# Triangles with no area. Unreal's mesh build drops them, so its count is this
# many short of ours - and the import check compares against what SHOULD
# arrive, not against what did.
import bmesh
bm = bmesh.new()
bm.from_mesh(ship.data)
bmesh.ops.triangulate(bm, faces=bm.faces[:])
facts["tris_degenerate"] = sum(1 for f in bm.faces if f.calc_area() < 1e-10)
coincident = sum(1 for f in bm.faces
                 if min((f.verts[i].co - f.verts[(i + 1) % 3].co).length for i in range(3)) < 2e-7)
bm.free()
say("degenerate triangles: %d with no area, %d with two corners in one place"
    % (facts["tris_degenerate"], coincident))
lo, hi = bounds(ship)
facts["bounds"] = {"min": ue(Vector((lo.x, hi.y, lo.z))), "max": ue(Vector((hi.x, lo.y, hi.z)))}
say("joined %s: %d tris, %d slots %s" % (NAME, facts["tris"], len(tris_by_slot),
                                        sorted(tris_by_slot)))
say("bounds x %.2f..%.2f y +-%.2f z %.2f..%.2f m"
    % (lo.x, hi.x, max(-lo.y, hi.y), lo.z, hi.z))

# The muzzles, as sockets on the mesh. Old empties go; new ones are made in the
# final frame, parented to the one mesh, named for Unreal's socket import.
for o in muzzles:
    bpy.data.objects.remove(o, do_unlink=True)
for o in [o for o in bpy.data.objects if o.type == "EMPTY"]:
    bpy.data.objects.remove(o, do_unlink=True)
facts["muzzles"] = []
for n_ in sorted(mz_pos):
    p = mz_pos[n_]
    e = bpy.data.objects.new("SOCKET_" + n_, None)
    bpy.context.scene.collection.objects.link(e)
    e.parent = ship
    e.location = p
    side = "port" if "_Port_" in n_ else "starboard"
    idx = n_.rsplit("_", 1)[1]
    facts["muzzles"].append({"socket": n_, "side": side, "deck": int(idx[0]),
                             "gun": int(idx[1:]), "pos": ue(p)})

# ------------------------------------------------------------------ export
for o in bpy.data.objects:
    o.select_set(o is ship or o.parent is ship)
bpy.context.view_layer.objects.active = ship
fbx = os.path.join(OUT, NAME + ".fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True,
                         apply_unit_scale=True, global_scale=1.0,
                         apply_scale_options="FBX_SCALE_NONE",
                         object_types={"MESH", "EMPTY"}, mesh_smooth_type="FACE",
                         add_leaf_bones=False, bake_space_transform=False,
                         bake_anim=False, path_mode="STRIP", embed_textures=False)
facts["fbx_sha1"] = hashlib.sha1(open(fbx, "rb").read()).hexdigest()

# ------------------------------------------- read back what was written
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx)
back_m = [o for o in bpy.data.objects if o.type == "MESH"]
back_s = [o for o in bpy.data.objects if o.type == "EMPTY" and o.name.startswith("SOCKET_")]
back_tris = sum(len(p.vertices) - 2 for o in back_m for p in o.data.polygons)
say("read back %s: meshes=%d tris=%d sockets=%d slots=%s"
    % (os.path.basename(fbx), len(back_m), back_tris, len(back_s),
       sorted(m.name for m in bpy.data.materials)))
if len(back_m) != 1 or back_tris != facts["tris"] or len(back_s) != len(facts["muzzles"]):
    fail("the written FBX is not what was built")
# And each mesh a ball meets, the same way.
for key, f in sorted(facts["shot"].items()):
    p = os.path.join(OUT, f["asset"] + ".fbx")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=p)
    ms = [o for o in bpy.data.objects if o.type == "MESH"]
    t = sum(len(q.vertices) - 2 for o in ms for q in o.data.polygons)
    say("read back %s: meshes=%d tris=%d" % (os.path.basename(p), len(ms), t))
    if len(ms) != 1 or t != f["tris"]:
        fail("the written %s is not what was built (%d meshes, %d tris against %d)"
             % (os.path.basename(p), len(ms), t, f["tris"]))

with open(os.path.join(OUT, NAME + ".json"), "w", newline="\n") as f:
    json.dump(facts, f, indent=1, sort_keys=True)
    f.write("\n")
if FAIL:
    say("FAILED %d" % len(FAIL))
    sys.exit(1)
say("DONE %s" % fbx)
