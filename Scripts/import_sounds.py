"""
Imports Scripts/Sounds/S_*.wav as USoundWave assets under /Game/Sounds, and
builds ATT_Sea, the one attenuation every gunnery sound shares - then READS
BACK durations and the attenuation's radius, because an import that silently
produced a 0.0 s wave would play nothing and log a play request all the same.

Run through Scripts/run_py.ps1 -Script import_sounds.py -Tag SNDIMP. Like the
other importers it may die inside import_asset_tasks on the first run; the
second run's "before" lines are the disk truth. Run it twice.
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "Sounds")
DEST = "/Game/Sounds"
NAMES = ["S_Cannon", "S_Hit", "S_Rig", "S_Splash"]
# Sea air carries a gun a long way; a ball into oak or water is a near thing.
# Metres, turned into centimetres below. "Natural" falloff is closest to an
# inverse-square drop without the sudden silence a linear curve has.
ATT = {"S_Cannon": (60.0, 2500.0), "S_Hit": (20.0, 700.0),
       "S_Rig": (20.0, 500.0), "S_Splash": (15.0, 500.0)}


def L(m):
    unreal.log("SNDIMP " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:80]))
        return False


def report(tag):
    for n in NAMES:
        path = DEST + "/" + n
        w = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if w is None:
            L("%s %s MISSING" % (tag, n))
            continue
        try:
            dur = w.get_editor_property("duration")
        except Exception:
            dur = -1.0
        L("%s %s duration=%.3f s" % (tag, n, dur))
    for n in NAMES:
        path = DEST + "/ATT_" + n
        a = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if a is None:
            L("%s ATT_%s MISSING" % (tag, n))
        else:
            s = a.get_editor_property("attenuation")
            L("%s ATT_%s falloff=%.0f cm radius=%.0f cm" % (
                tag, n, s.get_editor_property("falloff_distance"),
                s.get_editor_property("attenuation_shape_extents").x))


report("before")

# ---- the attenuations first: they survive the commandlet, the import may not
for n in NAMES:
    path = DEST + "/ATT_" + n
    inner_m, falloff_m = ATT[n]
    if not EAL.does_asset_exist(path):
        a = AT.create_asset("ATT_" + n, DEST, unreal.SoundAttenuation,
                            unreal.SoundAttenuationFactory())
    else:
        a = EAL.load_asset(path)
    s = a.get_editor_property("attenuation")
    sp(s, "attenuation_shape", unreal.AttenuationShape.SPHERE)
    sp(s, "attenuation_shape_extents", unreal.Vector(inner_m * 100.0, 0.0, 0.0))
    sp(s, "falloff_distance", falloff_m * 100.0)
    sp(s, "distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    sp(s, "spatialize", True)
    sp(a, "attenuation", s)
    EAL.save_asset(path)

# ---- the waves
tasks = []
for n in NAMES:
    src = os.path.join(SRC, n + ".wav")
    if not os.path.exists(src):
        L("no %s - run Scripts/sounds.py first" % src)
        continue
    task = unreal.AssetImportTask()
    sp(task, "filename", src)
    sp(task, "destination_path", DEST)
    sp(task, "destination_name", n)
    sp(task, "automated", True)
    sp(task, "replace_existing", True)
    sp(task, "save", True)
    tasks.append(task)
L("importing %d waves" % len(tasks))
AT.import_asset_tasks(tasks)
# Nothing runs past here on a first import; the second run reports.
report("after")
