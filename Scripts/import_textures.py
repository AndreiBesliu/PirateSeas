"""
Imports Scripts/Textures/*.png into /Game/Textures with the right compression.

Separate from the material scripts on purpose: AssetTools.import_asset_tasks
takes the commandlet down when it tries to sync the Content Browser and there
is no Slate - AFTER the asset is written. So this is idempotent and safe to run
twice; the second run finds everything present and exits quietly.

Getting the settings wrong here is invisible until it is ugly: a normal map
imported as sRGB colour is washed through a gamma curve and the surface it
describes goes flat, which looks exactly like "the detail did not work" rather
than "the texture was tagged wrong".
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Textures")
DEST = "/Game/Textures"


def L(m):
    unreal.log("TEXIMP " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s=%s: %s" % (n, v, str(e)[:80]))
        return False


def kind(name):
    """The suffix is the contract: _N normal, _M/_R single-channel data, _C
    colour."""
    if name.endswith("_N"):
        return ("normal", unreal.TextureCompressionSettings.TC_NORMALMAP, False)
    if name.endswith("_M") or name.endswith("_R"):
        return ("mask", unreal.TextureCompressionSettings.TC_GRAYSCALE, False)
    if name.endswith("_D"):
        # DATA: several channels that are numbers, not colour. Full RGB
        # compression, no gamma. _M would have kept one channel and dropped the
        # rest without a word.
        return ("data", unreal.TextureCompressionSettings.TC_DEFAULT, False)
    return ("colour", unreal.TextureCompressionSettings.TC_DEFAULT, True)


def main():
    if not os.path.isdir(SRC):
        L("no source folder %s - run Scripts/textures.py first" % SRC)
        return

    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".png"))
    # PS_TEX_FORCE=T_Canvas re-imports everything whose name starts with that,
    # for when the generator changed but the asset already exists. Without it
    # a regenerated PNG is silently ignored and the old texture stays in the
    # game, which looks exactly like "the change had no effect".
    force = os.environ.get("PS_TEX_FORCE", "")
    todo = []
    for f in files:
        name = os.path.splitext(f)[0]
        stale = force and name.startswith(force)
        if EAL.does_asset_exist(DEST + "/" + name) and not stale:
            continue
        if stale:
            L("forcing re-import of %s" % name)
        todo.append((name, os.path.join(SRC, f)))

    L("%d png on disk, %d to import" % (len(files), len(todo)))

    for name, path in todo:
        task = unreal.AssetImportTask()
        sp(task, "filename", path)
        sp(task, "destination_path", DEST)
        sp(task, "destination_name", name)
        sp(task, "automated", True)
        sp(task, "replace_existing", True)
        sp(task, "save", True)
        sp(task, "factory", unreal.TextureFactory())
        AT.import_asset_tasks([task])
        L("imported %s" % name)

    # Settings are applied in a pass of their own, so a run that crashed during
    # import still fixes up everything that landed.
    fixed = 0
    for f in files:
        name = os.path.splitext(f)[0]
        p = DEST + "/" + name
        if not EAL.does_asset_exist(p):
            L("MISSING after import: %s" % name)
            continue
        tex = EAL.load_asset(p)
        what, comp, srgb = kind(name)
        sp(tex, "compression_settings", comp)
        sp(tex, "srgb", srgb)
        # Water and terrain tile across kilometres; without this the mip chain
        # clamps and a seam appears exactly where the tiling was supposed to
        # hide one.
        sp(tex, "address_x", unreal.TextureAddress.TA_WRAP)
        sp(tex, "address_y", unreal.TextureAddress.TA_WRAP)
        if what == "data":
            # NEVER STREAM. A streamed texture that has not arrived yet returns
            # its smallest mip - which is its AVERAGE COLOUR, a flat grey. The
            # gun smoke rendered as flat grey slabs for three iterations and the
            # emissive value measured 0.25 linear, exactly the mean of the noise
            # it was supposed to be sampling. The symptom of "not loaded yet" and
            # of "the maths is wrong" are the same picture.
            sp(tex, "never_stream", True)
        EAL.save_asset(p)
        fixed += 1
        L("%-22s %s srgb=%s" % (name, what, srgb))

    L("DONE settings applied to %d/%d" % (fixed, len(files)))


main()
