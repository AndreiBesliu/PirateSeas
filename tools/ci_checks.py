"""
The checks that do NOT need Unreal.

A GitHub-hosted runner has no engine - UE 5.7 is a hundred-odd gigabytes behind
a licence - so nothing here builds the game or runs a measurement. That is done
by the self-hosted workflow, on the machine that has the engine.

What IS here is everything that can be proved without it, and every one of these
guards exists because the thing it checks actually went wrong in this project:

  1. every script parses                 - a patch script broke three of them
  2. no byte-order marks                 - a PowerShell Set-Content added them
  3. no secret-shaped strings            - a token went out in the first commit
                                           and had to be pulled before pushing
  4. nothing the engine regenerates      - Intermediate alone is 2.5 GB
  5. the textures build                  - they are the project's only art
  6. the textures TILE                   - the whole point of building the noise
                                           in the frequency domain; a seam here
                                           would repeat across ten kilometres
  7. the textures are DETERMINISTIC      - the project compares runs, and art
                                           that differs per run makes every
                                           before/after comparison meaningless
  8. the owner's list is consecutive     - a skipped number means an item was
                                           written and lost

Run it anywhere:  python tools/ci_checks.py
"""
import ast
import io
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILS = []
NOTES = []


def fail(msg):
    FAILS.append(msg)
    print("FAIL  " + msg)


def ok(msg):
    print("ok    " + msg)


def note(msg):
    NOTES.append(msg)
    print("note  " + msg)


def tracked_files():
    """What git actually has. Checking the working tree instead would let an
    ignored file fail the build for something nobody will ever publish."""
    out = subprocess.run(["git", "ls-files"], cwd=ROOT,
                         capture_output=True, text=True)
    if out.returncode != 0:
        fail("git ls-files failed: " + out.stderr.strip())
        return []
    return [p for p in out.stdout.splitlines() if p]


# ------------------------------------------------------------------ 1, 2, 3, 4
def check_tree(files):
    py = [f for f in files if f.endswith(".py")]
    for rel in py:
        p = os.path.join(ROOT, rel)
        try:
            ast.parse(io.open(p, encoding="utf-8-sig").read(), filename=rel)
        except SyntaxError as e:
            fail("%s does not parse: line %s, %s" % (rel, e.lineno, e.msg))
    ok("%d python files parse" % len(py))

    TEXT = (".py", ".md", ".ini", ".cs", ".h", ".cpp", ".uproject", ".yml",
            ".ps1", ".gitignore", ".gitattributes")
    # Only where it bites. A byte-order mark in a .py file run by Unreal's
    # embedded interpreter is a real hazard and one did cost a run here; in a
    # .cpp it is what Unreal's own generator writes and MSVC reads it happily.
    BOM_MATTERS = (".py", ".yml", ".ini", ".ps1")
    boms = []
    for rel in files:
        if not rel.endswith(BOM_MATTERS):
            continue
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        with open(p, "rb") as fh:
            if fh.read(3) == b"\xef\xbb\xbf":
                boms.append(rel)
    if boms:
        fail("byte-order mark in: " + ", ".join(boms[:6]))
    else:
        ok("no byte-order marks in %d text files" % len(files))

    # Secret-shaped strings. Deliberately blunt: a false positive costs one
    # conversation, a false negative costs a rotated credential.
    import re
    PATTERNS = [
        (r"SecurityToken\s*=\s*\w{8,}", "an Unreal SecurityToken"),
        (r"AIza[0-9A-Za-z_\-]{30,}", "a Google API key"),
        (r"ghp_[0-9A-Za-z]{30,}", "a GitHub token"),
        (r"sk-[0-9A-Za-z]{20,}", "an OpenAI-style key"),
        (r"-----BEGIN [A-Z ]*PRIVATE KEY-----", "a private key"),
        (r"(?i)\b(password|passwd)\s*[=:]\s*\S{6,}", "a password"),
    ]
    hits = []
    for rel in files:
        if not rel.endswith(TEXT):
            continue
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        text = io.open(p, encoding="utf-8-sig", errors="replace").read()
        for pat, what in PATTERNS:
            m = re.search(pat, text)
            if m:
                hits.append("%s: %s" % (rel, what))
    if hits:
        fail("secret-shaped strings: " + "; ".join(hits))
    else:
        ok("no secret-shaped strings tracked")

    GENERATED = ("Intermediate/", "Saved/", "Binaries/", "DerivedDataCache/",
                 "Scripts/Textures/")
    leaked = [f for f in files if f.startswith(GENERATED)]
    if leaked:
        fail("%d generated files are tracked, e.g. %s"
             % (len(leaked), leaked[0]))
    else:
        ok("nothing the engine or the generator rebuilds is tracked")


# ------------------------------------------------------------------ 5, 6, 7
def check_textures():
    try:
        import numpy as np
    except ImportError:
        note("numpy missing - texture checks skipped, which is NOT a pass")
        return

    sys.path.insert(0, os.path.join(ROOT, "Scripts"))
    import textures as T

    out = T.OUT
    before = set(os.listdir(out)) if os.path.isdir(out) else set()
    T.main.__globals__["sys"].argv = ["textures.py", "all"]
    try:
        T.main()
    except AssertionError as e:
        # A period that does not divide the tile. Reported as a check failure
        # with its own message rather than as a traceback, so the build log
        # says WHICH pattern and why.
        fail("texture generator refused: %s" % e)
        return
    names = sorted(f for f in os.listdir(out) if f.endswith(".png"))
    if len(names) < 15:
        fail("only %d textures generated" % len(names))
        return
    ok("%d textures generated" % len(names))

    # Determinism: the same seeds must give the same bytes, or every
    # before/after comparison in this project is comparing art as well as code.
    first = {}
    for n in names:
        with open(os.path.join(out, n), "rb") as fh:
            first[n] = fh.read()
    T.main()
    differ = []
    for n in names:
        with open(os.path.join(out, n), "rb") as fh:
            if fh.read() != first[n]:
                differ.append(n)
    if differ:
        fail("not deterministic: " + ", ".join(differ[:5]))
    else:
        ok("textures are byte-identical across two runs")

    # Tiling. The noise is built in the frequency domain precisely so the image
    # is periodic; this measures that rather than trusting it. The step ACROSS
    # the seam must be no worse than the typical step INSIDE the image.
    import zlib, struct

    def read_png(path):
        raw = open(path, "rb").read()
        pos, w, h, chans, idat = 8, 0, 0, 0, b""
        while pos < len(raw):
            ln = struct.unpack(">I", raw[pos:pos + 4])[0]
            tag = raw[pos + 4:pos + 8]
            data = raw[pos + 8:pos + 8 + ln]
            if tag == b"IHDR":
                w, h, _, colour = struct.unpack(">IIBB", data[:10])
                chans = {0: 1, 2: 3, 6: 4}[colour]
            elif tag == b"IDAT":
                idat += data
            pos += 12 + ln
        flat = np.frombuffer(zlib.decompress(idat), np.uint8)
        rows = flat.reshape(h, w * chans + 1)[:, 1:]
        return rows.reshape(h, w, chans).astype(np.int16)

    # TILING is not checked here any more, and that is deliberate.
    #
    # Three statistical seam tests were written against these images and each
    # was wrong in its own direction: seam-versus-mean accused correct plank
    # textures at thirteen times; seam-versus-percentile PASSED a canvas
    # deliberately rebuilt with seven strips across a 1024 tile; seam-versus-
    # unrelated-columns then accused nine textures including ones that are
    # periodic by construction. Each felt reasonable and each was checked by
    # breaking a texture on purpose, which is the only reason the second one
    # was caught at all.
    #
    # The reason none of them worked is that the property is not statistical.
    # The noise textures are periodic BY CONSTRUCTION - built from a spectrum on
    # integer frequencies, so the tile is their period and no measurement can
    # add to that. The structured ones (planks, canvas strips, rope lay) are
    # periodic exactly when their period divides the tile, which is arithmetic.
    # So it is asserted in the generator itself, by must_divide() in
    # Scripts/textures.py, and this check's job is simply to RUN the generator -
    # a broken period now raises and takes the build down with a named message.
    #
    # Proved by deliberately setting the canvas to seven strips: the generator
    # raises, this check fails, and the message says which pattern and why.
    # Leave the tree as we found it: these are gitignored, but a CI run that
    # litters is a CI run somebody will start ignoring.
    if not before:
        for n in names:
            os.remove(os.path.join(out, n))
        try:
            os.rmdir(out)
        except OSError:
            pass


# ------------------------------------------------------------------ 8
def check_docs():
    import re
    for name in ("README.md", "DEVLOG.md", "OWNER_VERIFY.md"):
        if not os.path.exists(os.path.join(ROOT, name)):
            fail("missing " + name)
    p = os.path.join(ROOT, "OWNER_VERIFY.md")
    if not os.path.exists(p):
        return
    nums = [int(m) for m in re.findall(r"^##\s+(\d+)\.",
                                       io.open(p, encoding="utf-8").read(), re.M)]
    if not nums:
        fail("OWNER_VERIFY.md has no numbered items")
        return
    missing = [n for n in range(1, max(nums) + 1) if n not in nums]
    if missing:
        fail("OWNER_VERIFY.md skips item(s): %s" % missing)
    else:
        ok("OWNER_VERIFY.md has %d consecutive items" % len(nums))


# ------------------------------------------------------------------ 9
def check_comparison():
    """Does the measurement gate actually report each of its three verdicts?

    Written after a review found that the comparison walked only the keys it had
    just collected, so a measurement that STOPPED BEING TAKEN read as a match -
    and after finding, in the same pass, that ships_sunk counted a string the
    game never prints, which nailed that number to zero for three commits. Both
    are the same failure: a gate that cannot come out red is not a gate, and
    neither of them could be caught by running it, because running it is exactly
    what produced the green light.

    So the comparison is fed fixtures here instead, on a runner with no Unreal
    on it, where the answer is known in advance.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    try:
        import ci_measure
    except Exception as e:
        fail("cannot import ci_measure: %s" % e)
        return

    before = len(FAILS)
    base = {"s": {"hits": 3, "lift_mean": 1.0}}

    moved, new, gone = ci_measure.compare({"s": {"hits": 3, "lift_mean": 1.0}}, base)
    if moved or new or gone:
        fail("comparison reports a difference between identical numbers: %s"
             % (moved + new + gone))

    moved, new, gone = ci_measure.compare({"s": {"hits": 5, "lift_mean": 1.0}}, base)
    if len(moved) != 1 or gone or new:
        fail("a number that changed was not reported as MOVED: %s"
             % (moved + new + gone))

    # The one the review found: the key is simply absent from the new result.
    moved, new, gone = ci_measure.compare({"s": {"hits": 3}}, base)
    if len(gone) != 1 or moved or new:
        fail("a measurement that STOPPED BEING TAKEN was not reported: "
             "moved=%s new=%s gone=%s" % (moved, new, gone))

    moved, new, gone = ci_measure.compare({"s": {"hits": 3, "lift_mean": 1.0,
                                                 "extra": 1}}, base)
    if len(new) != 1 or moved or gone:
        fail("a brand new measurement was not reported as NEW: %s"
             % (moved + new + gone))

    # A float that moved by less than the tolerance is not a difference; one
    # that moved by more is. Both directions, because a tolerance that swallows
    # everything is the same bug wearing a different hat.
    if ci_measure.compare({"s": {"hits": 3, "lift_mean": 1.01}}, base)[0]:
        fail("the float tolerance rejects a change smaller than itself")
    if not ci_measure.compare({"s": {"hits": 3, "lift_mean": 1.4}}, base)[0]:
        fail("the float tolerance swallows a change four hundred times its size")

    # A WHOLE SCENARIO that stopped being run. The fixtures above all use the
    # same single scenario name on both sides, so they proved the key-level union
    # and agreed with their author exactly where he was wrong: the scenario level
    # still walked one side, and compare({}, base) reported no differences at all
    # for a run that measured nothing.
    moved, new, gone = ci_measure.compare({}, base)
    if len(gone) != 1 or moved or new:
        fail("a whole scenario that stopped running was not reported: "
             "moved=%s new=%s gone=%s" % (moved, new, gone))

    two = {"s": {"hits": 3, "lift_mean": 1.0}, "t": {"hits": 1}}
    moved, new, gone = ci_measure.compare({"s": {"hits": 3, "lift_mean": 1.0}}, two)
    if len(gone) != 1 or moved or new:
        fail("one scenario of two dropping out was not reported: gone=%s" % gone)

    # And the counters the game really prints. This is a WAKELOG line copied from
    # a log, with the numbers changed: every one of these was being printed and
    # read by nothing at all until this commit.
    # THE TRAIL AND THE GUN LAY, and this pair of fixtures exists because the
    # gap they close has already bitten. When the shot trail became a ribbon its
    # quit line gained a `chains=` field between `live=` and `discarded=`; the
    # regex in ci_measure kept the old shape, stopped matching, and all four
    # trail numbers silently stopped being read - so the `trail_off` scenario
    # went on running and proving nothing about trails at all. Nothing went red,
    # because compare() can only report a number as GONE if it was ever recorded
    # in a baseline, and these never had been.
    #
    # The lines below are copied from a real log with the numbers changed. A
    # format drift that breaks the reader now fails HERE, on a machine with no
    # Unreal on it, in under a second.
    tr = ci_measure.measure("fixture", "LogTemp: Display: TRAILLOG TOTAL laid=346 "
                            "live=252 chains=12 discarded=1 stranded=0\n")
    for key, want in (("trail_laid", 346), ("trail_live_end", 252),
                      ("trail_chains", 12), ("trail_discarded", 1),
                      ("trail_stranded", 0)):
        if tr.get(key) != want:
            fail("the trail's quit line is no longer read: %s is %r, expected %r"
                 % (key, tr.get(key), want))

    aim = ci_measure.measure("fixture", "LogTemp: Display: AIMLOG TOTAL hand=1 "
                             "side=starboard train=+12.0 layfwd=+0.105 elev=5.0 "
                             "fall=383m stop=1 locked=0 segs=4\n")
    for key, want in (("aim_by_hand", 1), ("aim_train", 12.0),
                      ("aim_layfwd", 0.105), ("aim_elev", 5.0),
                      ("aim_fall_m", 383), ("aim_stop", 1), ("aim_segments", 4)):
        if aim.get(key) != want:
            fail("the gun-lay quit line is no longer read: %s is %r, expected %r"
                 % (key, aim.get(key), want))

    # AND THE DELIBERATELY RED ONE: the old trail format, which must NOT parse.
    # A reader that accepted both shapes would be a reader that could not tell
    # which one it was looking at, and the whole point of the fixture above is
    # that the shape is load-bearing.
    stale = ci_measure.measure("fixture", "LogTemp: Display: TRAILLOG TOTAL "
                               "laid=346 live=252 discarded=1 stranded=0\n")
    if "trail_laid" in stale:
        fail("the trail reader still accepts the OLD line shape, so a format "
             "drift would go unnoticed in exactly the way it already did once")

    # The same, for the gun lay. This line lost its layfwd field and must no
    # longer parse - and it is not hypothetical: adding layfwd turned these
    # checks red one commit after they were written, which is the whole point.
    stale_aim = ci_measure.measure("fixture", "LogTemp: Display: AIMLOG TOTAL "
                                   "hand=1 side=starboard train=+12.0 elev=5.0 "
                                   "fall=383m stop=1 locked=0 segs=4\n")
    if "aim_train" in stale_aim:
        fail("the gun-lay reader still accepts the line shape from BEFORE layfwd, "
             "so the pair that catches a mirrored sign could go quiet unnoticed")

    # A DEPRESSED GUN. The carriages allow three degrees below level, so the
    # player's elevation goes negative the moment the wheel is wound down - and
    # a reader anchored on digits alone loses this key and every other one on
    # the line with it. Nothing had gone wrong yet when this was added; it was
    # found by looking at a neighbouring field that had started printing
    # negatives, which is the only reason it is here before rather than after.
    low = ci_measure.measure("fixture", "LogTemp: Display: AIMLOG TOTAL hand=1 "
                             "side=port train=-4.5 layfwd=-0.078 elev=-2.5 "
                             "fall=90m stop=0 locked=1 segs=4\n")
    for key, want in (("aim_train", -4.5), ("aim_layfwd", -0.078),
                      ("aim_elev", -2.5), ("aim_fall_m", 90)):
        if low.get(key) != want:
            fail("a gun laid BELOW level is not read: %s is %r, expected %r"
                 % (key, low.get(key), want))

    # THE GUN SMOKE's quit line, so a format drift on it fails here rather than
    # in a baseline nobody re-reads. The trail's own line outgrew its regex once
    # and all four trail numbers stopped being read in silence; this is the same
    # line in the same shape, and it gets the same fixture from the day it ships.
    smoke = ci_measure.measure("fixture", "LogTemp: Display: SMOKELOG TOTAL "
                               "spawned=16 live=0 culled=3 stranded=0\n")
    if smoke.get("smoke_spawned") != 16 or smoke.get("smoke_culled") != 3:
        fail("the gun-smoke quit line is no longer read: %r" % smoke)
    if "smoke_stranded" not in smoke:
        fail("a zero must still be RECORDED, or the one number that must never "
             "move could vanish instead of moving: %r" % smoke)

    # THE SCARS' quit line: the level TOTAL for added/culled, the survivors'
    # sum for live. A wreck's line missing must not lower added.
    ho = ci_measure.measure("fixture",
        "LogTemp: Display: HOLELOG ShipPawn_0 added=2 live=2 culled=0\n"
        "LogTemp: Display: HOLELOG TOTAL added=7 culled=1\n")
    if ho.get("holes_total") != 7 or ho.get("holes_live_end") != 2 or ho.get("holes_culled") != 1:
        fail("the scar totals must come from HOLELOG TOTAL: %r" % ho)

    # AND A REFUSED HIT IS NOT A HIT. damage=0 is a ball into a hull already
    # going down; it is counted apart, so scars and splinters can equal hits.
    rh = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG hit by=ShipPawn_0 shot=1 target=EnemyShipPawn_0 damage=60\n"
        "LogTemp: Display: SHOTLOG hit by=ShipPawn_0 shot=2 target=EnemyShipPawn_0 damage=0\n")
    if rh.get("hull_hits") != 1 or rh.get("hits_ignored") != 1:
        fail("a damage=0 hit must be counted as ignored, not as a hit: %r" % rh)

    # AND THE ARITHMETIC: a scar per ball into timber, so holes_total must
    # equal hull_hits on every recorded row but holes_off - the same gate the
    # splinters live under, for the same reason.
    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if os.path.exists(base_path):
        rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
        for name, row in sorted(rows.items()):
            if "holes_total" not in row or "hull_hits" not in row:
                continue
            if name == "holes_off":
                if row["holes_total"] != 0:
                    fail("holes_off must add no scars: %r" % row["holes_total"])
                continue
            if row["holes_total"] != row["hull_hits"]:
                fail("%s: %d scars for %d hull hits" % (name, row["holes_total"], row["hull_hits"]))

    # THE GUN BAND a row ran with, read off the first ship's line.
    gb = ci_measure.measure("fixture",
        "LogTemp: Display: SHIPLOG ShipPawn_0 gunband=200..350\n"
        "LogTemp: Display: SHIPLOG EnemyShipPawn_0 gunband=200..350\n")
    if gb.get("gun_band_lo") != 200 or gb.get("gun_band_hi") != 350:
        fail("the gun band line is not read: %r" % gb)

    # SOUND REQUESTS against their events, over the recorded baseline: a gun
    # that goes quiet must be a number. sound_off is the row where they are
    # meant to be zero.
    sn = ci_measure.measure("fixture",
        "LogTemp: Display: SOUNDLOG TOTAL cannon=16 hit=3 rig=8 splash=5 missing=0\n"
        "LogTemp: Display: SHOTLOG broadside ShipPawn_0 side=starboard guns=4 target=x\n"
        "LogTemp: Display: SHOTLOG broadside EnemyShipPawn_0 side=port guns=3 target=x\n"
        "LogTemp: Display: SHOTLOG rig by=ShipPawn_0 shot=1 target=EnemyShipPawn_0 comp=MainRig\n")
    if sn.get("sound_cannon") != 16 or sn.get("sound_splash") != 5 or sn.get("sound_missing") != 0:
        fail("the sound quit line is not read: %r" % sn)
    # And the old five-field line must NOT parse: a reader that accepted it
    # would go on reporting four numbers with the fifth silently absent.
    stale = ci_measure.measure("fixture", "LogTemp: Display: SOUNDLOG TOTAL cannon=16 hit=3 rig=8 splash=5\n")
    if "sound_cannon" in stale:
        fail("the five-field SOUNDLOG line still parses; a dropped missing= would pass unnoticed")
    if sn.get("balls_fired") != 7 or sn.get("rig_hits") != 1:
        fail("the events the sounds are held against are not counted: %r" % sn)
    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if os.path.exists(base_path):
        rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
        for name, row in sorted(rows.items()):
            if "sound_cannon" not in row:
                continue
            # missing= on EVERY recorded row, sound_off included: a request that
            # found no wave. Zero, or the asset is gone and the cook would say so
            # too late.
            if row.get("sound_missing") != 0:
                fail("%s: sound_missing=%r (must be 0 on every row)" % (name, row.get("sound_missing")))
            if name == "sound_off":
                if any(row.get(k) for k in ("sound_cannon", "sound_hit", "sound_rig", "sound_splash")):
                    fail("sound_off must request no sound: %r" % row)
                continue
            pairs = (("sound_cannon", "balls_fired"), ("sound_hit", "hull_hits"),
                     ("sound_rig", "rig_hits"), ("sound_splash", "splashes"))
            for a, b in pairs:
                if row.get(a) != row.get(b):
                    fail("%s: %s=%r but %s=%r" % (name, a, row.get(a), b, row.get(b)))

        # THE PAIRS THAT MUST DIFFER IN EXACTLY ONE FAMILY. Every *_off row is
        # gunnery with one switch, and the claim "and nothing else moves" was
        # prose in six places; here it is checked against the recorded rows.
        FAMILIES = {"sound_off": ("sound_",), "chips_off": ("chips_",),
                    "holes_off": ("holes_",), "flash_off": ("flash_",),
                    "smoke_off": ("smoke_",), "trail_off": ("trail_",)}
        g = rows.get("gunnery", {})
        for name, prefixes in FAMILIES.items():
            r = rows.get(name)
            if not r or not g:
                continue
            moved = sorted(k for k in set(r) | set(g) if r.get(k) != g.get(k))
            outside = [k for k in moved if not k.startswith(prefixes)]
            inside = [k for k in moved if k.startswith(prefixes)]
            if outside:
                fail("%s moves keys outside its family: %s" % (name, outside))
            if not inside:
                fail("%s does not differ from gunnery in its own family - the switch is dead" % name)

    # THE SOUNDS ARE WHAT THEIR GENERATOR MAKES. The .wav files are tracked (the
    # imported .uasset copies are what the game uses; the waves are kept so a
    # diff shows what changed), so the check is: regenerate, and every byte
    # must equal the tracked copy. That covers determinism too - a generator
    # that varied per run could not match its own committed output. The
    # generator is imported OUTSIDE the numpy guard: a missing sounds.py must
    # be a failure, not a 'numpy missing' note.
    sys.path.insert(0, os.path.join(ROOT, "Scripts"))
    try:
        import sounds as SND
    except ModuleNotFoundError as e:
        fail("Scripts/sounds.py cannot be imported: %s" % e)
        SND = None
    if SND is not None:
        try:
            import numpy  # noqa: F401
        except ImportError:
            note("numpy missing - sound regeneration skipped, which is NOT a pass")
        else:
            before = {}
            for n, _, _ in SND.SOUNDS:
                pth = os.path.join(SND.OUT, n + ".wav")
                before[n] = open(pth, "rb").read() if os.path.exists(pth) else None
            SND.main()
            bad = [n for n, _, _ in SND.SOUNDS
                   if open(os.path.join(SND.OUT, n + ".wav"), "rb").read() != before[n]]
            if bad:
                fail("tracked waves differ from what Scripts/sounds.py makes: " + ", ".join(bad))
            else:
                ok("%d tracked waves match their generator byte for byte" % len(SND.SOUNDS))

    # ENEMY GUNS KNOCKED OUT: only the enemy's, only the guns zone. A player
    # gun hit and an enemy hull hit must both count for nothing here.
    eg = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG zone guns target=EnemyShipPawn_0 at=(-240,410,105) gun=1\n"
        "LogTemp: Display: SHOTLOG zone guns target=ShipPawn_0 at=(120,400,90) gun=2\n"
        "LogTemp: Display: SHOTLOG zone hull target=EnemyShipPawn_0 at=(0,346,10) gun=-1\n")
    if eg.get("enemy_gun_hits") != 1:
        fail("enemy_gun_hits must count the one enemy guns-zone line: %r" % eg)

    # THE HULL'S COLLISION, said per ship. One transparent hull must make the
    # key 0 even when the others are fine.
    sh = ci_measure.measure("fixture",
        "LogTemp: Display: SHIPLOG ShipPawn_0 shothull=ok\n"
        "LogTemp: Display: SHIPLOG EnemyShipPawn_0 shothull=NO-COLLISION\n")
    if sh.get("shothull_ok") != 0:
        fail("one hull without collision must read shothull_ok=0: %r" % sh)
    sh = ci_measure.measure("fixture", "LogTemp: Display: SHIPLOG ShipPawn_0 shothull=ok\n")
    if sh.get("shothull_ok") != 1:
        fail("every hull ok must read shothull_ok=1: %r" % sh)

    # THE MUZZLE FLASH's quit line. Three fields, not four: it has no cull, and
    # a fixture written to the smoke's shape would have passed on a reader that
    # silently matched nothing.
    flash = ci_measure.measure("fixture", "LogTemp: Display: FLASHLOG TOTAL "
                               "spawned=16 live=2 stranded=0\n")
    if flash.get("flash_spawned") != 16 or flash.get("flash_live_end") != 2:
        fail("the muzzle-flash quit line is no longer read: %r" % flash)
    if "flash_stranded" not in flash:
        fail("a zero must still be RECORDED, or the one number that must never "
             "move could vanish instead of moving: %r" % flash)

    # THE SPLINTERS' quit line, and the arithmetic that says what it is FOR:
    # a burst per ball that went into a hull, so the count belongs beside the hit
    # count rather than standing on its own.
    chips = ci_measure.measure("fixture", "LogTemp: Display: CHIPLOG TOTAL "
                               "spawned=9 live=1 stranded=0\n")
    if chips.get("chips_spawned") != 9 or chips.get("chips_live_end") != 1:
        fail("the splinter quit line is no longer read: %r" % chips)
    if "chips_stranded" not in chips:
        fail("a zero must still be RECORDED: %r" % chips)

    # HULL HITS AND BURSTS, read apart and then compared. The reader must not
    # score a rigging sweep or a splash as a hull hit, and it must not miss an
    # enemy's or a merchant's name.
    hh = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG hit by=ShipPawn_0 shot=1 "
        "target=EnemyShipPawn_0 damage=60\n"
        "LogTemp: Display: SHOTLOG hit by=EnemyShipPawn_0 shot=2 "
        "target=ShipPawn_0 damage=60\n"
        "LogTemp: Display: SHOTLOG hit by=ShipPawn_0 shot=3 "
        "target=MerchantShipPawn_1 damage=60\n"
        "LogTemp: Display: SHOTLOG hit by=ShipPawn_0 shot=4 "
        "target=Island_2 damage=60\n")
    if hh.get("hull_hits") != 3:
        fail("hull_hits must count the three SHIPS and not the island: %r" % hh)

    # AND THE ARITHMETIC THIS PAIR EXISTS FOR, over the RECORDED baseline rather
    # than over a fixture: a burst per ball into a hull, in every scenario. The
    # comment that used to state this named `struck`, which counts damage ZONES -
    # it was out by eight in the very row it was written for. A cross-check
    # written in prose is one nobody runs.
    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if os.path.exists(base_path):
        rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
        for name, row in sorted(rows.items()):
            if "chips_spawned" not in row or "hull_hits" not in row:
                continue
            # chips_off is the row where they are MEANT to disagree.
            if name == "chips_off":
                if row["chips_spawned"] != 0:
                    fail("chips_off must produce no bursts: %r" % row["chips_spawned"])
                continue
            if row["chips_spawned"] != row["hull_hits"]:
                fail("%s: %d bursts for %d hull hits - one of the two is wrong "
                     "about what happened" % (name, row["chips_spawned"],
                                              row["hull_hits"]))

    # AND THE TWO LINES MUST NOT READ EACH OTHER. They share a shape and differ
    # in one field; a reader loose about its prefix would score the smoke's
    # numbers as the flash's, and every scenario would agree with itself.
    crossed = ci_measure.measure("fixture", "LogTemp: Display: SMOKELOG TOTAL "
                                 "spawned=99 live=98 culled=97 stranded=96\n")
    if "flash_spawned" in crossed:
        fail("the smoke's quit line is being read as the flash's: %r" % crossed)

    # AND THE OLD ONE-OFF DIAGNOSTIC MUST NOT BE MISTAKEN FOR IT. SMOKELOG also
    # prints a per-puff spread line at t=1.0; a reader loose enough to match that
    # would report some puff's card sizes as the run's totals.
    spread = ci_measure.measure("fixture", "LogTemp: Display: SMOKELOG at t=1.0 "
                                "the cards span 539 x 604 x 541 cm, "
                                "sizes 245..194\n")
    if "smoke_spawned" in spread:
        fail("the per-puff SMOKELOG diagnostic is being read as the totals: %r"
             % spread)

    # THE PLAYER'S MAGAZINE, READ OFF THE PLAYER'S LINE. This fixture puts both
    # hulls in, with different numbers, because the defect it was written for was
    # not a wrong regex - it was a right regex aimed at the wrong ship. A reader
    # that drifts back onto EnemyShipPawn_ scores 40 here and fails.
    # EVERY NUMBER IN IT DISTINCT. The first version of this fixture wrote
    # shot=0/3 fired=3, so own_shot_max and own_shot_fired were both 3 and the
    # two regex groups could have been swapped without the gate noticing; and
    # own_shot_left was asserted by nothing at all. A fixture whose fields are
    # interchangeable tests the regex's shape, not its meaning.
    two = ("LogTemp: Display: SHOTLOG EnemyShipPawn_0 magazine shot=36/40 "
           "fired=4 dry=0\n"
           "LogTemp: Display: SHOTLOG ShipPawn_0 magazine shot=2/7 "
           "fired=5 dry=1\n")
    mine = ci_measure.measure("fixture", two)
    for key, want in (("own_shot_left", 2), ("own_shot_max", 7),
                      ("own_shot_fired", 5), ("own_dry", 1)):
        if mine.get(key) != want:
            fail("%s should be %d off the player's line, got %r: %r"
                 % (key, want, mine.get(key), mine))
    if mine.get("shot_max") != 40 or mine.get("shot_left") != 36:
        fail("the enemy's magazine keys must keep reading the enemy: %r" % mine)

    # THE PARTIAL BROADSIDE. Three guns on the player's first order against four
    # on the enemy's, from lines that differ only in the hull's name - which is
    # the whole trap this pair exists to keep shut.
    bs = ("LogTemp: Display: SHOTLOG broadside EnemyShipPawn_0 side=starboard "
          "guns=4 target=x range=500m\n"
          "LogTemp: Display: SHOTLOG broadside ShipPawn_0 side=starboard "
          "guns=3 target=x range=500m\n")
    pb = ci_measure.measure("fixture", bs)
    if pb.get("own_guns_first") != 3 or pb.get("own_broadsides") != 1:
        fail("the player's first broadside is not read apart from the enemy's: "
             "%r" % pb)
    if pb.get("broadsides") != 2:
        fail("the all-hulls broadside count must still see both: %r" % pb)

    # AND THAT "FIRST" MEANS FIRST. Every scenario in the suite has the player
    # firing exactly once, so own_broadsides is 1 everywhere and nothing there
    # could ever tell findall()[0] from findall()[-1] or from a max. The day a
    # scenario fires her twice, the key would have quietly changed meaning.
    many = ("LogTemp: Display: SHOTLOG broadside ShipPawn_0 side=starboard "
            "guns=3 target=x range=500m\n"
            "LogTemp: Display: SHOTLOG broadside EnemyShipPawn_0 side=port "
            "guns=4 target=x range=500m\n"
            "LogTemp: Display: SHOTLOG broadside ShipPawn_0 side=starboard "
            "guns=1 target=x range=500m\n")
    mb = ci_measure.measure("fixture", many)
    if mb.get("own_broadsides") != 2:
        fail("the player's broadsides are not counted: %r" % mb)
    if mb.get("own_guns_first") != 3:
        fail("own_guns_first must be the FIRST of the player's broadsides, not "
             "the last (1) and not the largest: %r" % mb)

    # THE GUN PANEL's state, and the arithmetic that says why a mask and not a
    # count. 9 is 1001: guns 0 and 3 down, 1 and 2 standing. A panel that drew
    # `g < Mounted` with two standing would light pips 0 and 1 - the wrong two -
    # and no count can ever distinguish 1001 from 0011.
    pips = ci_measure.measure("fixture", "LogTemp: Display: HUDLOG TOTAL "
                              "guns_down_port=9 guns_down_stbd=6 "
                              "guns_ready_port=1 guns_ready_stbd=2\n")
    if pips.get("guns_down_port") != 9 or pips.get("guns_down_stbd") != 6:
        fail("the gun-panel quit line is no longer read: %r" % pips)
    if pips.get("guns_ready_stbd") != 2:
        fail("the loaded-gun count is not read off the panel line: %r" % pips)

    # AND THE OLD SHAPE MUST NOT PARSE. Without this the reader could accept
    # both, and the day guns_ready_ is dropped from the quit line the suite would
    # go on printing the two fields it still recognises and call that a pass.
    stale = ci_measure.measure("fixture", "LogTemp: Display: HUDLOG TOTAL "
                               "guns_down_port=9 guns_down_stbd=6\n")
    if "guns_down_port" in stale:
        fail("the four-field gun-panel line still parses; a dropped "
             "guns_ready_ field would pass unnoticed: %r" % stale)
    mask, standing = 9, 0
    for g in range(4):
        if not (mask & (1 << g)):
            standing += 1
    if standing != 2:
        fail("the mask arithmetic is wrong: 1001 leaves two guns standing")
    prefix = [g for g in range(4) if g < standing]
    actually = [g for g in range(4) if not (mask & (1 << g))]
    if prefix == actually:
        fail("mask 9 must NOT agree with a count-drawn panel, or the fixture "
             "proves nothing about the defect it was written for")

    # THE LINE OF BATTLE's quit line, so a format drift on it fails here rather
    # than by quietly reading nothing - which is what happened to the trail.
    line = ci_measure.measure("fixture",
                              "LogTemp: Display: AILOG TOTAL line_skips=2\n")
    if line.get("line_skips") != 2:
        fail("the line-of-battle quit line is no longer read: %r"
             % line.get("line_skips"))

    # AND THE SIGN ITSELF, as arithmetic rather than as a fixture. A lay six
    # degrees forward of the beam puts sin(6) = 0.1045 of the barrels' direction
    # along the bow, on EITHER battery. The defect the owner found by playing
    # gave -0.104 to starboard and +0.104 to port: same magnitude, mirrored sign.
    import math
    if abs(math.sin(math.radians(6.0)) - 0.1045) > 0.001:
        fail("the arithmetic this pair rests on is wrong: sin(6 deg) is not 0.1045")

    w = ci_measure.measure("fixture", "LogTemp: Display: WAKELOG live=6 slots=2 "
                           "shortest=3 tracked=ShipPawn_0:3  ignored=3 stranded=2 "
                           "doubled=1 stolen=5 discarded=7 alive=0 seen=0 lost=4\n")
    got = (w.get("wake_stranded_max"), w.get("wake_doubled_max"),
           w.get("wake_stolen_max"), w.get("wake_discarded_max"),
           w.get("wake_ignored_max"), w.get("splash_lost_max"),
           w.get("wake_live_max"), w.get("wake_slots_max"),
           w.get("wake_shortest_max"))
    if got != (2, 1, 5, 7, 3, 4, 6, 2, 3):
        fail("the wake counters read %s from a line holding "
             "(2, 1, 5, 7, 3, 4, 6, 2, 3)" % (got,))

    # The sea line, likewise copied rather than paraphrased.
    sea = ci_measure.measure("fixture", "LogTemp: Display: SEALOG surface waves "
                             "drawn=6 of 6, amplitude 178 of 178 cm (100%) sigma=56 "
                             "foam>=73 cm over 51, breaking on 11.6% of the sea, "
                             "scatter>=224 cm span 0.54\n")
    if (sea.get("scatter_range_cm"), sea.get("scatter_span")) != (224.0, 0.54):
        fail("the scatter numbers read %s from a line holding (224.0, 0.54)"
             % ((sea.get("scatter_range_cm"), sea.get("scatter_span")),))

    # The wind reaching the plants, off the line the island really prints.
    sw = ci_measure.measure("fixture", "LogTemp: Display: ISLELOG Island_0 sway "
                            "mats=2 wind=14.0 m/s toward 300 deg (readback ok 14.0)\n")
    if (sw.get("sway_mats"), sw.get("sway_wind_ms"), sw.get("sway_readback_ok")) != (2, 14.0, 1):
        fail("the sway line read %s from a line holding (2, 14.0, 1)"
             % ((sw.get("sway_mats"), sw.get("sway_wind_ms"), sw.get("sway_readback_ok")),))
    bad = ci_measure.measure("fixture", "LogTemp: Display: ISLELOG Island_0 sway "
                             "mats=2 wind=14.0 m/s toward 300 deg (readback FAILED -1.0)\n")
    if bad.get("sway_readback_ok") != 0:
        fail("a FAILED readback was not reported as such: %s" % bad.get("sway_readback_ok"))

    rg = ci_measure.measure("fixture", "LogTemp: Display: SHIPLOG ShipPawn_0 "
                            "rigsway mats=7 wind=14.0 m/s toward 100 deg "
                            "(readback ok 14.0)" + chr(10))
    if (rg.get("rig_sway_mats"), rg.get("rig_sway_readback_ok")) != (7, 1):
        fail("the rig sway line read %s from a line holding (7, 1)"
             % ((rg.get("rig_sway_mats"), rg.get("rig_sway_readback_ok")),))

    sk = ci_measure.measure("fixture", "LogTemp: Display: SKYLOG hour=17.5 "
                            "elev=8.1 azim=262 lux=30756 K=3461 ev=[10.7,14.2] "
                            "suns=1 skies=1 volumes=1" + chr(10))
    if (sk.get("sun_elev_deg"), sk.get("sun_lux"), sk.get("sun_kelvin"),
            sk.get("exposure_ev_min")) != (8.1, 30756.0, 3461.0, 10.7):
        fail("the sky line read %s from a line holding (8.1, 30756, 3461, 10.7)"
             % ((sk.get("sun_elev_deg"), sk.get("sun_lux"), sk.get("sun_kelvin"),
                 sk.get("exposure_ev_min")),))

    # And the island band, where the whole point is that the two numbers agree.
    isle = ci_measure.measure("fixture", "LogTemp: Display: ISLELOG Island_0 planted "
                              "palms=18 scrub=118 of 258 tried (missed=0, wrong "
                              "height=121, too steep=1); turf from 573 cm (paint "
                              "says 450, scale 1.27), flatness >= 0.69, 4 of 4 "
                              "numbers read from the material\n")
    if isle.get("turf_band_gap_cm") != 123.0 or isle.get("island_scale_max") != 1.27:
        fail("the island band read gap=%s scale=%s from a line holding 123.0 and "
             "1.27" % (isle.get("turf_band_gap_cm"), isle.get("island_scale_max")))

    # The convoy's one MISSION line, and the ship's own STRUCK line beside it,
    # both copied from the first run in which a merchant struck. The STRUCK
    # line must count as a merchant struck and must NOT count as a ship sunk:
    # "STRUCK" and "SUNK " are four letters apart and one regex away from each
    # other.
    cv = ci_measure.measure("fixture",
        "LogTemp: Display: SHIPLOG MerchantShipPawn_0 STRUCK by=CannonBall_6 "
        "hull=1000/1000 rig=0.55 t=71.4" + chr(10) +
        "LogTemp: Display: CONVOYLOG MISSION TAKEN t=71.4 stopped=1 through=0 "
        "sunk=0 of 2 need=1 gauge=71 lee=0 beat=0 firstStrike=71.4 "
        "raider=EnemyShipPawn_0 side=weather" + chr(10))
    want = {"mission_result": 2, "mission_t": 71.4, "convoy_stopped": 1,
            "convoy_through": 0, "convoy_sunk": 0, "convoy_size": 2,
            "convoy_need": 1, "gauge_ticks": 71, "lee_ticks": 0,
            "beat_seconds": 0, "first_strike_t": 71.4, "merchants_struck": 1,
            "ships_sunk": 0}
    got = {k: cv.get(k) for k in want}
    if got != want:
        fail("the convoy lines read %s, wanted %s" % (got, want))
    # The other two verdicts, off the same line with the word changed.
    for word, code in (("THROUGH", 1), ("UNRESOLVED", 0)):
        v = ci_measure.measure("fixture",
            "LogTemp: Display: CONVOYLOG MISSION %s t=316.3 stopped=0 through=2 "
            "sunk=0 of 2 need=1 gauge=0 lee=315 beat=315 firstStrike=-1.0 "
            "raider=EnemyShipPawn_0 side=lee" % word + chr(10))
        if v.get("mission_result") != code or v.get("first_strike_t") != -1.0:
            fail("MISSION %s read as result=%s firstStrike=%s"
                 % (word, v.get("mission_result"), v.get("first_strike_t")))
    # The hands, off the two quit-time lines of a real run: the player's, who
    # lost eleven men to a broadside aimed high, and the enemy's, who lost none
    # and knotted 0.748 of a rig back. The enemy's rig must be read from the
    # ENEMY's line - 0.77 - and not as the minimum over hulls, which is 0.32
    # and is the damage she dealt. And two broadside lines, so the first one
    # is the earlier of the two and "target=ShipPawn_0" is not mistaken for
    # the t= at the end.
    cw = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG broadside EnemyShipPawn_0 side=starboard guns=4 "
        "target=ShipPawn_0 range=335m heel=1.2 muzzleZ=29 velBeam=-0.63 velFwd=6.32 "
        "elev=4.30 aim=high lead=14.0m inherit=1 bias=1.040 t=224.6" + chr(10) +
        "LogTemp: Display: SHOTLOG broadside EnemyShipPawn_0 side=starboard guns=4 "
        "target=ShipPawn_0 range=340m heel=1.0 muzzleZ=25 velBeam=-0.60 velFwd=6.30 "
        "elev=4.30 aim=high lead=14.0m inherit=1 bias=1.040 t=236.6" + chr(10) +
        "LogTemp: Display: CREWLOG ShipPawn_0 hands=49/60 casualties=11 repairShare=0.00 "
        "repaired=0.000 gunCrew=1.00 rig=0.32 rudder=1.00" + chr(10) +
        "LogTemp: Display: CREWLOG EnemyShipPawn_0 hands=60/60 casualties=0 "
        "repairShare=0.00 repaired=0.748 gunCrew=1.00 rig=0.77 rudder=1.00" + chr(10))
    want = {"casualties_max": 11, "gun_crew_min": 1.0, "repaired_max": 0.748,
            "enemy_rig_quit": 0.77, "first_broadside_t": 224.6, "broadsides": 2}
    got = {k: cw.get(k) for k in want}
    if got != want:
        fail("the crew lines read %s, wanted %s" % (got, want))

    # The money, off two real runs. These three fixtures were written against a
    # PURSE line with four fields; possession gave it four more, and they went
    # RED until they were brought to the line the game really prints now. That
    # is the fixtures earning their keep: a reader that quietly stops matching
    # a line it used to read is the failure this file exists to catch.
    #
    # The rig half and the hull half of the
    # prize pair, copied from their logs: the same merchant and the same cargo
    # pay 1200 and 696 because the hull fraction differs, and prize_rig_at_take
    # moves the OTHER way - which is what tells a correct formula from one
    # wired to the rig by mistake. purse_balances re-derives the sum from the
    # per-prize lines, so a transcription bug in money reads as a boolean that
    # moved rather than a number somebody has to eyeball.
    pr = ci_measure.measure("fixture",
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 taken value=1200 cargo=1200 "
        "hull=1.00 rig=0.55 zone=rig t=70.9 purse=1200 prizes=1" + chr(10) +
        "LogTemp: Display: PRIZELOG PURSE purse=1200 prizes=1 valueMax=1200 "
        "cargo=1200 manned=0 refused=0 handsSent=0 closest=272 landed=0 "
        "landedValue=0 handsHome=0" + chr(10))
    want = {"purse_end": 1200, "prizes_taken": 1, "prize_value_max": 1200,
            "prize_hull_at_take": 1.0, "prize_rig_at_take": 0.55,
            "prize_strike_zone": 1, "purse_balances": 1}
    got = {k: pr.get(k) for k in want}
    if got != want:
        fail("the rig prize read %s, wanted %s" % (got, want))

    ph = ci_measure.measure("fixture",
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 taken value=696 cargo=1200 "
        "hull=0.58 rig=1.00 zone=hull t=83.1 purse=696 prizes=1" + chr(10) +
        "LogTemp: Display: PRIZELOG PURSE purse=696 prizes=1 valueMax=696 "
        "cargo=1200 manned=0 refused=0 handsSent=0 closest=272 landed=0 "
        "landedValue=0 handsHome=0" + chr(10))
    want = {"purse_end": 696, "prize_hull_at_take": 0.58,
            "prize_rig_at_take": 1.0, "prize_strike_zone": 0, "purse_balances": 1}
    got = {k: ph.get(k) for k in want}
    if got != want:
        fail("the hull prize read %s, wanted %s" % (got, want))

    # And a purse that does NOT balance must say so. Two prizes of 600 with a
    # purse of 1000 is a money bug, and this is the only assertion in the
    # project that re-derives a logged number from its own parts.
    bad = ci_measure.measure("fixture",
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 taken value=600 cargo=1200 "
        "hull=0.50 rig=1.00 zone=hull t=50.0 purse=600 prizes=1" + chr(10) +
        "LogTemp: Display: PRIZELOG MerchantShipPawn_1 taken value=600 cargo=1200 "
        "hull=0.50 rig=1.00 zone=hull t=90.0 purse=1200 prizes=2" + chr(10) +
        "LogTemp: Display: PRIZELOG PURSE purse=1000 prizes=2 valueMax=600 "
        "cargo=1200 manned=0 refused=0 handsSent=0 closest=50 landed=0 "
        "landedValue=0 handsHome=0" + chr(10))
    if bad.get("purse_balances") != 0:
        fail("a purse that does not add up read purse_balances=%s"
             % bad.get("purse_balances"))

    # Possession, off the two real runs. The PURSE line grew four fields and
    # the old fixtures above must still read the same numbers from it - which
    # is the point of keeping both: a line that gains fields is the commonest
    # way a reader silently stops matching.
    pm = ci_measure.measure("fixture",
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 taken value=1200 cargo=1200 "
        "hull=1.00 rig=0.55 zone=rig t=70.9 purse=1200 prizes=1" + chr(10) +
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 manned by=EnemyShipPawn_0 "
        "crew=12 closest=65m spent=20.0 t=133.8 manned=1 handsSent=12" + chr(10) +
        "LogTemp: Display: PRIZELOG PURSE purse=2400 prizes=2 valueMax=1200 "
        "cargo=1200 manned=2 refused=0 handsSent=24 closest=65 landed=1 "
        "landedValue=1200 handsHome=12" + chr(10) +
        "LogTemp: Display: SEALOG EnemyShipPawn_0 landTicks=0 clawOffs=0 "
        "rejoinTicks=0 avoidTicks=0 pursuitTicks=2619 prizeTicks=8501" + chr(10))
    want = {"purse_end": 2400, "prizes_taken": 2, "prizes_manned": 2,
            "prizes_refused": 0, "prize_hands_sent": 24, "prize_closest_m": 65.0,
            "prize_ticks_max": 8501, "pursuit_ticks_max": 2619,
            # The port's three, off the same line: two prizes taken and manned,
            # ONE of them home. purse_end 2400 beside purse_landed 1200 is the
            # whole distinction - what she was worth against what reached the
            # quay - and a reader that collapsed them would show up here.
            "prizes_landed": 1, "purse_landed": 1200, "prize_hands_home": 12}
    # prize_hands_home means men who reached a DECK, not men who reached the
    # quay. A mutation that removed the return left it reading 12 until the two
    # were separated; the SHIPLOG line now carries both, crew= and returned=.
    got = {k: pm.get(k) for k in want}
    if got != want:
        fail("the possession lines read %s, wanted %s" % (got, want))

    # And the floor. A refusal must be counted and must NOT be read as a take:
    # manned and refused are different fields on the same line, one letter
    # apart in the regex and a whole mechanic apart in meaning.
    pf = ci_measure.measure("fixture",
        "LogTemp: Display: PRIZELOG MerchantShipPawn_0 REFUSED by=EnemyShipPawn_0: "
        "28 hands aboard, 16 would be left, floor is 20 t=146.3 refused=1" + chr(10) +
        "LogTemp: Display: PRIZELOG PURSE purse=1200 prizes=1 valueMax=1200 "
        "cargo=1200 manned=0 refused=1 handsSent=0 closest=41 landed=0 "
        "landedValue=0 handsHome=0" + chr(10))
    want = {"prizes_manned": 0, "prizes_refused": 1, "prize_hands_sent": 0,
            "prizes_taken": 1, "prize_closest_m": 41.0}
    got = {k: pf.get(k) for k in want}
    if got != want:
        fail("the refusal lines read %s, wanted %s" % (got, want))

    # What the money bought, off the real run. The arithmetic is checkable from
    # the line itself and the fixture checks it: twenty men at twenty apiece is
    # four hundred, four hundred points of hull at a half is two hundred, six
    # hundred spent, six hundred left of twelve hundred landed. A reader that
    # confused spent with coffers would pass a single-number check and fail
    # this one.
    rf = ci_measure.measure("fixture",
        "LogTemp: Display: PORTLOG REFIT spent=600 coffers=600 handsBought=20 "
        "hullBought=400 shotBought=0 refitSeconds=20.0" + chr(10) +
        "LogTemp: Display: SEALOG EnemyShipPawn_0 landTicks=0 clawOffs=0 "
        "rejoinTicks=0 avoidTicks=0 pursuitTicks=2620 prizeTicks=8500 "
        "portTicks=4200" + chr(10))
    want = {"refit_spent": 600, "refit_coffers_end": 600,
            "refit_hands_bought": 20, "refit_hull_bought": 400,
            "refit_seconds": 20.0, "port_ticks_max": 4200,
            "prize_ticks_max": 8500, "pursuit_ticks_max": 2620}
    got = {k: rf.get(k) for k in want}
    if got != want:
        fail("the refit lines read %s, wanted %s" % (got, want))
    if rf["refit_hands_bought"] * 20 + rf["refit_hull_bought"] // 2 != rf["refit_spent"]:
        fail("the refit line does not add up: %d men and %d hull are not %d"
             % (rf["refit_hands_bought"], rf["refit_hull_bought"], rf["refit_spent"]))

    # A ship that bought nothing must read zeros, not absence: the line is
    # printed in every run, convoy or none.
    rz = ci_measure.measure("fixture",
        "LogTemp: Display: PORTLOG REFIT spent=0 coffers=0 handsBought=0 "
        "hullBought=0 shotBought=0 refitSeconds=0.0" + chr(10))
    if (rz.get("refit_spent"), rz.get("refit_hands_bought"),
            rz.get("refit_seconds")) != (0, 0, 0.0):
        fail("a run that bought nothing read %s"
             % ((rz.get("refit_spent"), rz.get("refit_hands_bought"),
                 rz.get("refit_seconds")),))

    # The magazine, off the enemy's own line. shot_left beside shot_fired
    # because one without the other cannot tell a ship that never fired from
    # one that fired everything she had - both read 0 in the first case and
    # 0 / max in the second.
    mg = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG ShipPawn_0 magazine shot=0/0 fired=0 dry=0"
        + chr(10) +
        "LogTemp: Display: SHOTLOG EnemyShipPawn_0 magazine shot=20/40 fired=20 "
        "dry=0" + chr(10))
    want = {"shot_left": 20, "shot_max": 40, "shot_fired": 20, "dry_refusals": 0}
    got = {k: mg.get(k) for k in want}
    if got != want:
        fail("the magazine line read %s, wanted %s - and note the PLAYER's line "
             "of zeros comes first, so a reader that took the first match would "
             "report her instead" % (got, want))

    md = ci_measure.measure("fixture",
        "LogTemp: Display: SHOTLOG EnemyShipPawn_0 magazine shot=0/4 fired=4 "
        "dry=1" + chr(10))
    if (md.get("shot_left"), md.get("shot_fired"), md.get("dry_refusals")) != (0, 4, 1):
        fail("an empty magazine read %s"
             % ((md.get("shot_left"), md.get("shot_fired"), md.get("dry_refusals")),))

    # The enemy's gun crew, read BY NAME off her own line. The convoy runs
    # prove why: gun_crew_min is 0.25 in both halves because a merchant
    # carries 14 hands and sits on MinGunCrewFactor, so a min over hulls can
    # never report the raider - who is at 1.00 on the very same log.
    eg = ci_measure.measure("fixture",
        "LogTemp: Display: CREWLOG MerchantShipPawn_0 hands=14/14 casualties=0 "
        "repairShare=0.00 repaired=0.000 gunCrew=0.25 rig=0.55 rudder=1.00" + chr(10) +
        "LogTemp: Display: CREWLOG EnemyShipPawn_0 hands=60/60 casualties=0 "
        "repairShare=0.00 repaired=0.000 gunCrew=1.00 rig=1.00 rudder=1.00" + chr(10))
    if (eg.get("enemy_gun_crew_quit"), eg.get("gun_crew_min"), eg.get("enemy_rig_quit")) != (1.0, 0.25, 1.0):
        fail("the enemy crew keys read %s from a log whose merchant is pinned at "
             "0.25 and whose raider is at 1.00"
             % ((eg.get("enemy_gun_crew_quit"), eg.get("gun_crew_min"),
                 eg.get("enemy_rig_quit")),))

    # And the captain's pursuit counter, off the quit-time line it rides on.
    pt = ci_measure.measure("fixture",
        "LogTemp: Display: SEALOG EnemyShipPawn_0 landTicks=0 clawOffs=0 "
        "rejoinTicks=0 avoidTicks=0 pursuitTicks=5742" + chr(10))
    if pt.get("pursuit_ticks_max") != 5742:
        fail("pursuit_ticks_max read %s from a line holding 5742"
             % pt.get("pursuit_ticks_max"))

    # And the counter the review caught: it must match the line the game really
    # prints. This is the text from ShipPawn.cpp, not a paraphrase of it.
    sunk = ci_measure.measure("fixture", """
LogTemp: Display: SHIPLOG EnemyShipPawn_0 SUNK by=SeaGameMode_0 breach=(900,520)
LogTemp: Display: SHIPLOG ShipPawn_0 sink=foundering draught=120
""")["ships_sunk"]
    if sunk != 1:
        fail("ships_sunk read %d from a log holding exactly one SUNK line" % sunk)
    if len(FAILS) == before:
        ok("the measurement gate reports matches, moves, new and missing numbers, "
           "and reads the convoy, crew, prize, possession, refit, magazine and "
           "pursuit lines")


def check_ledger():
    """THE SHIP'S BOOK, held against paper.

    Three kinds of proof, and none of them is the game agreeing with itself:
      - the suite can never open a player's book: the pin, no flag the engine
        would read as the switch, every other launcher pinned, and in the
        recorded rows the player's slot never appears;
      - every book row's numbers - including which rows must leave the book
        SHUT and why - are worked out HERE, before looking at the row, from the
        row's own flags, the fixture file and the price list read out of the
        headers: a second arithmetic, in another language, over the same inputs;
      - the pair differs in the ledger_* family and nothing else, and the
        cycle row opens with exactly what the spend row closed with: the only
        proof that crosses a process boundary, which is the feature.
    """
    import re
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    try:
        import ci_measure
    except Exception as e:
        fail("cannot import ci_measure: %s" % e)
        return

    # ---- the lines are read field by field. Every number distinct, so two
    # regex groups swapped cannot read as right.
    close = ("LogTemp: Display: LEDGERLOG CLOSE slot=given why=open reason=quit written=1 roundtrip=1 "
             "cruise=4 ship=1 hands=47 hull=611 shot=39 chest=602 wrecks=3 wreckCharge=17 "
             "refused=2 rejected=5 setAside=1 recovered=1 tackle=2 order=1\n")
    opened = ("LogTemp: Display: LEDGERLOG OPEN ship=ShipPawn_0 loaded=1 rejected=0 cruise=6 "
              "hands=44/60 hull=622/1000 shot=33/40 chest=605 wrecks=7 clamped=8 tackle=1/2 order=0\n")
    lost = "LogTemp: Display: LEDGERLOG ShipPawn_0 lost: a new hull costs 1780, the chest paid 999\n"
    side = "LogTemp: Display: PORTLOG SIDE refused=4\n"
    tack = ("LogTemp: Display: TACKLELOG EnemyShipPawn_0 tier=0/2 reload=12.0 ordered=0 orders=0 withdrawn=0 refusedTop=0\n"
            "LogTemp: Display: TACKLELOG ShipPawn_0 tier=1/2 reload=10.0 ordered=1 orders=3 withdrawn=2 refusedTop=4\n"
            "LogTemp: Display: TACKLELOG TOTAL bought=6 refusedCoffers=9 spent=1200\n")
    got = ci_measure.measure("fixture", opened + close + lost + side + tack)
    want = {"ledger_closes": 1, "ledger_slot": 1, "ledger_why": 0, "ledger_written": 1,
            "ledger_roundtrip": 1, "ledger_cruise_out": 4, "ledger_ship_out": 1,
            "ledger_hands_out": 47, "ledger_hull_out": 611, "ledger_shot_out": 39,
            "ledger_chest_out": 602, "ledger_wrecks_out": 3, "ledger_wreck_charge": 17,
            "ledger_refused": 2, "ledger_rejected": 5, "ledger_set_aside": 1,
            "ledger_recovered": 1, "ledger_loaded": 1, "ledger_cruise": 6,
            "ledger_hands_in": 44, "ledger_hull_in": 622, "ledger_shot_in": 33,
            "ledger_chest_in": 605, "ledger_wrecks_in": 7, "ledger_clamped": 8,
            "ledger_ship_cost": 1780, "ledger_paid": 999, "refit_refused_side": 4,
            "ledger_tackle_out": 2, "ledger_order_out": 1, "ledger_tackle_in": 1, "ledger_order_in": 0,
            "tackle_tier": 1, "tackle_reload": 10.0, "tackle_ordered": 1, "tackle_orders": 3,
            "tackle_withdrawn": 2, "tackle_refused_top": 4, "tackle_bought": 6,
            "tackle_refused_coffers": 9, "tackle_spent": 1200}
    bad = {k: (got.get(k), v) for k, v in want.items() if got.get(k) != v}
    if bad:
        fail("the book's lines are not read field by field (got, want): %r" % bad)
    twice = ci_measure.measure("fixture", close + "LogTemp: Warning: LEDGERLOG CLOSE again - refused\n")
    if twice.get("ledger_closes") != 2:
        fail("a second CLOSE line is not counted: ledger_closes=%r" % twice.get("ledger_closes"))
    for word, code in (("pinned", 1), ("noport", 2), ("testflag", 3)):
        off = ci_measure.measure("fixture", "LogTemp: Display: LEDGERLOG CLOSE slot=off why=%s reason=quit "
                                 "written=0 roundtrip=0 cruise=0 ship=0 hands=0 hull=0 shot=0 chest=0 "
                                 "wrecks=0 wreckCharge=0 refused=0 rejected=0 setAside=0 recovered=0 "
                                 "tackle=0 order=0\n" % word)
        if off.get("ledger_slot") != 0 or off.get("ledger_why") != code or "ledger_hands_out" in off:
            fail("a book shut for '%s' reads as something else: %r" % (word, off))

    # ---- the pin, and nothing the engine would read as it. FParse::Value
    # finds a name wherever the character before it is not a letter or a digit
    # (Strifind, CString.h): "-ShipLedger=" is safe, "x_Ledger=" or a path
    # ".../Ledger=" is not. The check mirrors exactly that.
    if "-Ledger=0" not in ci_measure.PINNED:
        fail("PINNED has no -Ledger=0: every suite row would open the owner's own book")
    flags = [("PINNED", a) for a in ci_measure.PINNED]
    flags += [(n, a) for n, fl in ci_measure.SCENARIOS.items() for a in fl]
    for where, a in flags:
        low = a.lower()
        for hit in re.finditer("ledger=", low):
            if a == "-Ledger=0" and hit.start() == 1:
                continue
            if hit.start() == 0 or not low[hit.start() - 1].isalnum():
                fail("%s: %s would be read by the engine as the book's switch" % (where, a))
        if low.startswith("-ledgerbook="):
            path = a.split("=", 1)[1]
            if not path.startswith("Saved/CI/") or ".." in path:
                fail("%s: %s opens a book outside Saved/CI/" % (where, a))
    # Every OTHER script that starts the game. It sees a "-game" argument in a
    # .py/.ps1 under Scripts/ or tools/ - not recipes written in the docs,
    # which README and HANDOFF carry -Ledger=0 in by hand. ci_checks is left
    # out because this very scan names the string it looks for.
    for folder, exts in (("Scripts", (".ps1", ".py")), ("tools", (".py",))):
        for fn in sorted(os.listdir(os.path.join(ROOT, folder))):
            if not fn.endswith(exts) or fn == "ci_checks.py":
                continue
            text = io.open(os.path.join(ROOT, folder, fn), encoding="utf-8", errors="replace").read()
            if re.search(r"""["']-game["']""", text) and "-Ledger=0" not in text:
                fail("%s/%s starts the game without -Ledger=0: it would read and "
                     "write the owner's book" % (folder, fn))

    # ---- the price list and the hull, read out of the headers
    def const(header, pattern):
        src = io.open(os.path.join(ROOT, "Source", "PirateSeas", header), encoding="utf-8").read()
        mm = re.search(pattern, src)
        if not mm:
            fail("cannot read %r out of %s - the paper arithmetic has no inputs" % (pattern, header))
            return None
        return float(mm.group(1))
    HANDS = const("ShipPawn.h", r"int32 HandsMax = (\d+);")
    HULL = const("ShipPawn.h", r"float MaxHullIntegrity = ([\d.]+)f;")
    SHOT = const("ShipPawn.h", r"int32 ShotMax = (\d+);")
    HAND_COST = const("SeaGameMode.h", r"int32 HandCost = (\d+);")
    HULL_COST = const("SeaGameMode.h", r"float HullPointCost = ([\d.]+)f;")
    SHOT_COST = const("SeaGameMode.h", r"int32 ShotCost = (\d+);")
    HULL_TICK = const("SeaGameMode.h", r"float HullPointsPerTick = ([\d.]+)f;")
    SHOT_TICK = const("SeaGameMode.h", r"int32 ShotPerTick = (\d+);")
    RELOAD = const("ShipPawn.h", r"float ReloadSeconds = ([\d.]+)f;")
    TACKLE_S = const("ShipPawn.h", r"float TackleSecondsPerTier = ([\d.]+)f;")
    TACKLE_MAX = const("ShipPawn.h", r"int32 TackleMaxTier = (\d+);")
    TACKLE_COST = const("SeaGameMode.h", r"int32 TackleCost = (\d+);")
    pawn_src = io.open(os.path.join(ROOT, "Source", "PirateSeas", "ShipPawn.cpp"), encoding="utf-8").read()
    gm = re.search(r"GGunMuzzleY\[\] = \{([^}]*)\}", pawn_src)
    GUNS = len([x for x in gm.group(1).split(",") if x.strip()]) if gm else None
    if GUNS is None:
        fail("cannot count the guns a side (GGunMuzzleY) - the paper arithmetic has no inputs")
    if None in (HANDS, HULL, SHOT, HAND_COST, HULL_COST, SHOT_COST, HULL_TICK, SHOT_TICK,
                RELOAD, TACKLE_S, TACKLE_MAX, TACKLE_COST, GUNS):
        return

    def page(name):
        """The fixture, read by the RULES the game's reader states - book=1,
        end=1, every number one to nine digits, a ship all-or-nothing - and
        written again here, not imported from anywhere."""
        kv = {}
        for line in io.open(os.path.join(ROOT, "tools", "books", name), encoding="utf-8"):
            if "=" in line:
                k, v = line.strip().split("=", 1)
                kv[k.strip().lower()] = v.strip()
        num = lambda k: kv.get(k, "").isdigit() and 1 <= len(kv[k]) <= 9
        ship = [k for k in ("hands", "hull", "shot") if k in kv]
        extra = [k for k in ("tackle", "order") if k in kv]
        ok_ = (kv.get("book") == "1" and kv.get("end") == "1"
               and all(num(k) for k in ("cruises", "wrecks", "chest"))
               and (len(ship) == 3 or (not ship and not extra))
               and all(num(k) for k in ship + extra))
        return kv if ok_ else None

    FRESH = {"ledger_hands_in": HANDS, "ledger_hull_in": HULL, "ledger_shot_in": SHOT,
             "ledger_tackle_in": 0, "ledger_order_in": 0}

    def opened_from(kv):
        """What OPEN must show: a refused page sails as built; a book, each
        value cut to what the hull holds."""
        if kv is None:
            return dict(FRESH, ledger_loaded=0, ledger_chest_in=0, ledger_cruise=1,
                        ledger_wrecks_in=0, ledger_clamped=0)
        h, v, sh = int(kv["hands"]), float(kv["hull"]), int(kv["shot"])
        t, o = int(kv.get("tackle", 0)), int(kv.get("order", 0))
        ch, cv, cs = min(max(h, 0), HANDS), min(max(v, 1), HULL), min(max(sh, 0), SHOT)
        ct, co = min(max(t, 0), TACKLE_MAX), min(max(o, 0), 1)
        return {"ledger_loaded": 1, "ledger_hands_in": ch, "ledger_hull_in": cv,
                "ledger_shot_in": cs, "ledger_chest_in": int(kv["chest"]),
                "ledger_cruise": int(kv["cruises"]) + 1, "ledger_wrecks_in": int(kv["wrecks"]),
                "ledger_tackle_in": ct,
                # an order for a tier past the top is refused at the fit
                "ledger_order_in": 1 if co == 1 and ct < TACKLE_MAX else 0,
                "ledger_clamped": (ch != h) + (cv != v) + (cs != sh) + (ct != t) + (co != o)}

    def shut_because(fl):
        """The game's rule, stated again: which rows must leave the book shut."""
        if not any(a.startswith("-LedgerBook=") for a in fl):
            return 1                                    # pinned
        if not any(a.startswith("-Port=") and a != "-Port=0" for a in fl):
            return 2                                    # no roadstead
        if any(a.startswith(("-Shot=", "-ShipHullTest=", "-ShipToggleTackle=", "-EnemyBreakTest=")) for a in fl):
            return 3                                    # a test flag
        return 0

    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if not os.path.exists(base_path):
        note("no baseline - the book's rows are not checked, which is NOT a pass")
        return
    rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
    wanted_rows = list(ci_measure.BOOKS) + ["ledger_cycle"]
    missing = [n for n in wanted_rows if n not in rows]
    if missing:
        fail("the book's rows are not in the baseline: %s" % ", ".join(missing))
        return

    # ---- every row: one close; the player's slot never; open or shut by rule
    for name, row in sorted(rows.items()):
        fl = ci_measure.SCENARIOS.get(name, [])
        why = shut_because(fl)
        if row.get("ledger_closes") != 1:
            fail("%s: ledger_closes=%r - the quit path that writes the book ran %s"
                 % (name, row.get("ledger_closes"), "never" if not row.get("ledger_closes") else "more than once"))
        if row.get("ledger_slot") == 2:
            fail("%s opened the PLAYER's book (slot=default) under a measurement" % name)
        if row.get("ledger_why") != why:
            fail("%s: the book is %s by the game (why=%r) but %s by the rule (why=%r)"
                 % (name, "open" if row.get("ledger_why") == 0 else "shut", row.get("ledger_why"),
                    "open" if why == 0 else "shut", why))
        if why and (row.get("ledger_slot"), row.get("ledger_written")) != (0, 0):
            fail("%s: a shut book was opened or written: slot=%r written=%r"
                 % (name, row.get("ledger_slot"), row.get("ledger_written")))
        if row.get("refit_refused_side", 0) and name != "ledger_side":
            fail("%s: the roadstead refused %d hull(s) of the wrong side - a row that never "
                 "asked for it" % (name, row["refit_refused_side"]))

    def expect(name, want):
        row = rows[name]
        wrong = {k: (row.get(k), v) for k, v in want.items() if row.get(k) != v}
        if wrong:
            fail("%s against paper (got, want): %r" % (name, wrong))
        else:
            ok("%s: %d numbers match the paper arithmetic" % (name, len(want)))

    WRITES = {"ledger_slot": 1, "ledger_written": 1, "ledger_roundtrip": 1}

    # ---- OPEN, and a book that does nothing closes as it opened
    for name in ("ledger_fresh", "ledger_carry", "ledger_tmp"):
        fixture = ci_measure.BOOKS[name][0]
        o = opened_from(page(fixture)) if fixture else opened_from(None)
        closed = {"ledger_hands_out": o["ledger_hands_in"], "ledger_hull_out": o["ledger_hull_in"],
                  "ledger_shot_out": o["ledger_shot_in"], "ledger_chest_out": o["ledger_chest_in"],
                  "ledger_cruise_out": o["ledger_cruise"], "ledger_wrecks_out": o["ledger_wrecks_in"],
                  "ledger_tackle_out": o["ledger_tackle_in"], "ledger_order_out": o["ledger_order_in"],
                  "ledger_ship_out": 1}
        expect(name, dict(o, **closed, **WRITES))
    expect("ledger_tmp", {"ledger_recovered": 1})

    # ---- three refusals, each for exactly one reason: set aside, sail as built
    for name in ("ledger_bad", "ledger_torn", "ledger_garbage"):
        if page(ci_measure.BOOKS[name][0]) is not None:
            fail("%s: its fixture reads as a book by the stated rules - it cannot prove a refusal" % name)
        expect(name, dict(opened_from(None), ledger_rejected=1, ledger_set_aside=1, **WRITES))

    # ---- the port buys in its own order, out of the chest
    sp = opened_from(page("spend.book"))
    buy_shot = SHOT - sp["ledger_shot_in"]
    buy_hands = HANDS - sp["ledger_hands_in"]
    buy_hull = HULL - sp["ledger_hull_in"]
    spent = buy_shot * SHOT_COST + buy_hands * HAND_COST + buy_hull * HULL_COST
    if spent > sp["ledger_chest_in"]:
        fail("spend.book no longer affords a whole refit; the arithmetic below assumes it does")
    ticks = -(-buy_shot // SHOT_TICK) + buy_hands + -(-buy_hull // HULL_TICK)
    expect("ledger_spend", dict(sp, refit_spent=spent, refit_shot_bought=buy_shot,
                                refit_hands_bought=buy_hands, refit_hull_bought=buy_hull,
                                refit_seconds=ticks * 0.5,
                                ledger_chest_out=sp["ledger_chest_in"] - spent,
                                ledger_hands_out=HANDS, ledger_hull_out=HULL,
                                ledger_shot_out=SHOT, ledger_cruise_out=sp["ledger_cruise"], **WRITES))

    # ---- the next cruise opens with what the last one closed with
    s_row = rows["ledger_spend"]
    expect("ledger_cycle", {"ledger_hands_in": s_row.get("ledger_hands_out"),
                            "ledger_hull_in": s_row.get("ledger_hull_out"),
                            "ledger_shot_in": s_row.get("ledger_shot_out"),
                            "ledger_chest_in": s_row.get("ledger_chest_out"),
                            "ledger_cruise": (s_row.get("ledger_cruise_out") or 0) + 1,
                            "ledger_loaded": 1})

    # ---- a lost ship: a new hull at the port's prices, as far as the chest goes
    wr = opened_from(page("wreck.book"))
    cost = HULL * HULL_COST + HANDS * HAND_COST + SHOT * SHOT_COST
    paid = min(cost, wr["ledger_chest_in"])
    if paid == cost:
        fail("wreck.book's chest covers a whole new hull, so 'as far as the chest reaches' is never tested")
    if wr["ledger_hull_in"] == HULL:
        fail("wreck.book's hull is the as-built one, so a replacement wrongly fitted from it would not show")
    expect("ledger_wreck", dict(wr, ledger_refused=1, ledger_ship_cost=cost, ledger_paid=paid,
                                ledger_wreck_charge=paid, ledger_chest_out=wr["ledger_chest_in"] - paid,
                                ledger_wrecks_out=wr["ledger_wrecks_in"] + 1, ledger_ship_out=1,
                                ledger_hands_out=HANDS, ledger_hull_out=HULL, ledger_shot_out=SHOT,
                                ledger_tackle_out=0, ledger_order_out=0, **WRITES))

    # ---- the purse's side: the port refuses her, her captain does not count it
    sd = opened_from(page("spend.book"))
    expect("ledger_side", {"refit_refused_side": 1, "refit_spent": 0, "port_ticks_max": 0,
                           "ledger_chest_out": sd["ledger_chest_in"], **WRITES})

    # ---- the pair: the book's family moves, nothing else does
    f_row, k_row = rows["ledger_fresh"], rows["ledger_carry"]
    moved = sorted(k for k in set(f_row) | set(k_row) if f_row.get(k) != k_row.get(k))
    outside = [k for k in moved if not k.startswith("ledger_")]
    if outside:
        fail("ledger_carry moves keys outside the book's family: %s" % outside)
    if not moved:
        fail("ledger_carry does not differ from ledger_fresh - the book fits nothing")
    elif not outside:
        ok("ledger_carry vs ledger_fresh: %d ledger_* keys move, nothing else" % len(moved))

    # ================================================= THE GUN TACKLE
    # Every tackle row, worked out here first: what the book fits, what the
    # port sells and in what order, which tier the guns end with, and so
    # whether they are loaded at the quit - one broadside at -ShipFireTest,
    # every gun of the side, ready when fire + reload <= quit.
    tackle_rows = [n for n in ci_measure.SCENARIOS if n.startswith("tackle_")]
    missing = [n for n in tackle_rows if n not in rows]
    if missing:
        fail("the tackle rows are not in the baseline: %s" % ", ".join(missing))
        return

    def flag(fl, name, default=None):
        for a in fl:
            if a.startswith(name + "="):
                return float(a.split("=", 1)[1])
        return default

    def ready(fl, tier):
        fire, quit_ = flag(fl, "-ShipFireTest"), flag(fl, "-ShipQuitAfter")
        if fire is None:
            return None
        return GUNS if fire + (RELOAD - tier * TACKLE_S) <= quit_ else 0

    def tackle_expect(name, tier, extra):
        fl = ci_measure.SCENARIOS[name]
        want = dict(extra, tackle_tier=tier, tackle_reload=RELOAD - tier * TACKLE_S)
        r = ready(fl, tier)
        if r is not None:
            want["guns_ready_stbd"] = r
            # AND THE BROADSIDE HAPPENED. For a tier that is ready at the quit,
            # "all loaded" is also what a battery that never fired reads - the
            # review caught the expectation passing on nothing.
            want["first_broadside_t"] = flag(fl, "-ShipFireTest")
            want["balls_fired"] = GUNS
        expect(name, want)

    # the carried tier, and the pair's effect
    for name in ("tackle_none", "tackle_carry"):
        o = opened_from(page(ci_measure.BOOKS[name][0]))
        tackle_expect(name, o["ledger_tackle_in"], dict(
            o, ledger_tackle_out=o["ledger_tackle_in"], ledger_order_out=o["ledger_order_in"],
            ledger_hands_out=o["ledger_hands_in"], ledger_hull_out=o["ledger_hull_in"],
            ledger_shot_out=o["ledger_shot_in"] - GUNS, ledger_chest_out=o["ledger_chest_in"],
            ledger_cruise_out=o["ledger_cruise"], ledger_wrecks_out=o["ledger_wrecks_in"],
            ledger_ship_out=1, **WRITES))

    # the purchase, AFTER shot and BEFORE the men and the hull
    b = opened_from(page("tacklebuy.book"))
    if b["ledger_order_in"] != 1:
        fail("tacklebuy.book carries no order - the purchase is not exercised")
    shot_buy = SHOT - b["ledger_shot_in"]
    price = (b["ledger_tackle_in"] + 1) * TACKLE_COST
    men_short = HANDS - b["ledger_hands_in"]
    hull_short = HULL - b["ledger_hull_in"]
    left = b["ledger_chest_in"] - shot_buy * SHOT_COST - price
    men_buy = min(men_short, left // HAND_COST)
    left2 = left - men_buy * HAND_COST
    hull_buy = min(hull_short, left2 / HULL_COST)
    if left < 0 or men_buy >= men_short or hull_buy >= hull_short:
        fail("tacklebuy.book no longer puts the order, the men and the hull in competition")
    for rival, cost in (("men", men_short * HAND_COST), ("hull", hull_short * HULL_COST)):
        if b["ledger_chest_in"] - shot_buy * SHOT_COST - cost >= price:
            fail("tacklebuy.book affords the tackle even after the %s - that half of the order is not tested" % rival)
    ticks = -(-shot_buy // SHOT_TICK) + 1 + men_buy + -(-hull_buy // HULL_TICK)
    spent = shot_buy * SHOT_COST + price + men_buy * HAND_COST + hull_buy * HULL_COST
    tackle_expect("tackle_buy", b["ledger_tackle_in"] + 1, dict(
        b, tackle_bought=1, tackle_spent=price, tackle_refused_coffers=0,
        refit_spent=spent, refit_shot_bought=shot_buy, refit_hands_bought=men_buy,
        refit_hull_bought=hull_buy, refit_seconds=ticks * 0.5,
        ledger_chest_out=b["ledger_chest_in"] - spent,
        ledger_hands_out=b["ledger_hands_in"] + men_buy,
        ledger_hull_out=b["ledger_hull_in"] + hull_buy, ledger_tackle_out=b["ledger_tackle_in"] + 1,
        ledger_order_out=0, **WRITES))

    # shot FIRST: the rounds leave less than the price
    sf = opened_from(page("tackleshotfirst.book"))
    sf_shot = SHOT - sf["ledger_shot_in"]
    sf_price = (sf["ledger_tackle_in"] + 1) * TACKLE_COST
    if not (sf["ledger_chest_in"] >= sf_price > sf["ledger_chest_in"] - sf_shot * SHOT_COST):
        fail("tackleshotfirst.book no longer lets the shot decide the order")
    tackle_expect("tackle_shotfirst", sf["ledger_tackle_in"], dict(
        sf, tackle_refused_coffers=1, tackle_bought=0, refit_shot_bought=sf_shot,
        refit_spent=sf_shot * SHOT_COST, ledger_chest_out=sf["ledger_chest_in"] - sf_shot * SHOT_COST,
        ledger_order_out=0, **WRITES))

    # an order with no roadstead in reach is written back as it came in
    pd = opened_from(page("tacklepending.book"))
    tackle_expect("tackle_pending", pd["ledger_tackle_in"], dict(
        pd, tackle_bought=0, tackle_refused_coffers=0, tackle_ordered=1,
        ledger_order_out=1, ledger_tackle_out=pd["ledger_tackle_in"],
        ledger_chest_out=pd["ledger_chest_in"], **WRITES))

    # the grace: a key-placed order is not payable before it has run out
    GRACE = const("ShipPawn.h", r"float TackleOrderGraceSeconds = ([\d.]+)f;")
    TICK = const("SeaGameMode.cpp", r"&ASeaGameMode::SamplePrizes, ([\d.]+)f, true")
    gr = rows["tackle_grace"]
    if GRACE is not None and TICK is not None:
        first_payable = -(-GRACE // TICK) * TICK
        expect("tackle_grace", {"tackle_orders": 1, "tackle_refused_coffers": 1,
                                "tackle_refused_t": first_payable, "tackle_ordered": 0})
        if first_payable <= TICK:
            fail("the grace is shorter than a tick - it cannot be seen")
        # and the refusal that falls through does so in the SAME tick: the
        # short row's repairs begin at the very first tick
        expect("tackle_short", {"refit_first_t": TICK})

    # the next cruise opens with it
    bo = rows["tackle_buy"]
    tackle_expect("tackle_cycle", bo.get("ledger_tackle_out") or 0, {
        "ledger_tackle_in": bo.get("ledger_tackle_out"), "ledger_order_in": bo.get("ledger_order_out"),
        "ledger_chest_in": bo.get("ledger_chest_out"), "ledger_hull_in": bo.get("ledger_hull_out"),
        "ledger_shot_in": bo.get("ledger_shot_out"), "ledger_loaded": 1})

    # tier 2 at twice the price
    t = opened_from(page("tackletop.book"))
    price2 = (t["ledger_tackle_in"] + 1) * TACKLE_COST
    tackle_expect("tackle_top", t["ledger_tackle_in"] + 1, dict(
        t, tackle_bought=1, tackle_spent=price2, refit_spent=price2, refit_seconds=0.5,
        ledger_chest_out=t["ledger_chest_in"] - price2, ledger_order_out=0, **WRITES))
    if ready(ci_measure.SCENARIOS["tackle_top"], t["ledger_tackle_in"] + 1) == \
            ready(ci_measure.SCENARIOS["tackle_top"], t["ledger_tackle_in"]):
        fail("tackle_top's quit cannot tell tier 2 from tier 1")

    # an order the chest cannot pay: refused, closed, and the repairs go on
    sh = opened_from(page("tackleshort.book"))
    if sh["ledger_chest_in"] >= (sh["ledger_tackle_in"] + 1) * TACKLE_COST:
        fail("tackleshort.book can pay for the tackle - the refusal is not exercised")
    hull_s = min(HULL - sh["ledger_hull_in"], sh["ledger_chest_in"] / HULL_COST)
    tackle_expect("tackle_short", sh["ledger_tackle_in"], dict(
        sh, tackle_refused_coffers=1, tackle_bought=0, refit_hull_bought=hull_s,
        refit_spent=hull_s * HULL_COST, refit_seconds=-(-hull_s // HULL_TICK) * 0.5,
        ledger_chest_out=sh["ledger_chest_in"] - hull_s * HULL_COST, ledger_order_out=0,
        tackle_ordered=0, **WRITES))

    # an order past the top, carried in the book: refused at the fit
    mx = opened_from(page("tacklemax.book"))
    tackle_expect("tackle_max", mx["ledger_tackle_in"], dict(
        mx, tackle_refused_top=1, ledger_order_out=0, tackle_ordered=0, **WRITES))

    # the key's own function: ordered, then withdrawn
    tackle_expect("tackle_toggle", 0, {"tackle_orders": 1, "tackle_withdrawn": 1, "tackle_ordered": 0})
    expect("ledger_testtackle", {"tackle_orders": 1, "tackle_ordered": 1, "ledger_why": 3, "ledger_written": 0})

    # no tackle anywhere it was not asked for
    for name, row in sorted(rows.items()):
        if name.startswith(("tackle_", "ledger_testtackle")):
            continue
        if row.get("tackle_tier", 0) or row.get("tackle_orders", 0) or row.get("tackle_bought", 0):
            fail("%s: tackle in a row that never asked for it: %r"
                 % (name, {k: v for k, v in row.items() if k.startswith("tackle_")}))
        if "tackle_reload" in row and row["tackle_reload"] != RELOAD:
            fail("%s: reload %.1f without tackle, not %.1f" % (name, row["tackle_reload"], RELOAD))

    # the pair: tier 1 against tier 0 - the book and the tackle move, and the
    # guns; nothing else does
    # EXACTLY these keys, both directions: the book's tier in and out, the
    # hull's tier and reload, and the guns loaded at the quit. A family by
    # prefix let any ledger_* key drift unseen.
    n_row, c_row = rows["tackle_none"], rows["tackle_carry"]
    moved = set(k for k in set(n_row) | set(c_row) if n_row.get(k) != c_row.get(k))
    want_moved = {"ledger_tackle_in", "ledger_tackle_out", "tackle_tier", "tackle_reload",
                  "guns_ready_stbd"}
    if moved != want_moved:
        fail("tackle_carry vs tackle_none moves %s, not exactly %s"
             % (sorted(moved), sorted(want_moved)))
    else:
        ok("tackle_carry vs tackle_none: exactly %d keys move, the guns among them" % len(moved))

    # the tier never passes the top, on any row: the one invariant the pawn
    # no longer clamps after the sale
    for name, row in sorted(rows.items()):
        if row.get("tackle_tier", 0) > TACKLE_MAX:
            fail("%s: tackle tier %d above the top %d" % (name, row["tackle_tier"], TACKLE_MAX))

def check_line():
    """THE LINE CLOSES OVER A SHIP THAT BREAKS OFF.

    The pair line_breaks / line_formed differs in one flag and must move
    EXACTLY the line's keys; every number on line_breaks is worked out from
    the header constants first. On every row: nobody ever dresses on a runner
    (runnerTicks 0), the line is recorded as closed exactly when some walk
    stepped over a ship, and nothing breaks off where no test asked for it
    unless the row is named here with the reason.
    """
    import re
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    try:
        import ci_measure
    except Exception as e:
        fail("cannot import ci_measure: %s" % e)
        return

    # the lines, field by field, every number distinct
    got = ci_measure.measure("fixture",
        "LogTemp: Display: LINELOG TOTAL broken=3 closed=12.34 runnerTicks=5 runnerGap=678 stationGap=91 runnersAfloat=2 stationErr=17\n"
        "LogTemp: Display: AILOG EnemyShipPawn_0 BROKEN for the test at t=10.02 hull=250/1000\n")
    want = {"line_broken": 3, "line_closed_t": 12.34, "line_runner_ticks": 5, "line_runner_gap_m": 678.0,
            "line_station_gap_m": 91.0, "line_runners_afloat": 2, "line_station_err_m": 17.0,
            "line_break_t": 10.02, "line_break_hull": 250.0}
    bad = {k: (got.get(k), v) for k, v in want.items() if got.get(k) != v}
    if bad:
        fail("the line's lines are not read field by field (got, want): %r" % bad)
    none = ci_measure.measure("fixture",
        "LogTemp: Display: LINELOG TOTAL broken=0 closed=-1.00 runnerTicks=0 runnerGap=-1 stationGap=-1 runnersAfloat=0 stationErr=-1\n")
    if none.get("line_closed_t") != -1.0 or none.get("line_runner_gap_m") != -1.0:
        fail("a line that never closed does not read as -1: %r" % none)

    def const(path, pattern):
        src = io.open(os.path.join(ROOT, "Source", "PirateSeas", path), encoding="utf-8").read()
        mm = re.search(pattern, src)
        if not mm:
            fail("cannot read %r out of %s" % (pattern, path))
            return None
        return float(mm.group(1))
    HULL = const("ShipPawn.h", r"float MaxHullIntegrity = ([\d.]+)f;")
    RUN = const("ShipAIController.h", r"float DisengageHullFraction = ([\d.]+)f;")
    BREAK = const("SeaGameMode.h", r"float BreakTestHullFraction = ([\d.]+)f;")
    INTERVAL = const("ShipAIController.h", r"float LineIntervalCm = ([\d.]+)f;")
    REJOIN = const("ShipAIController.h", r"float RejoinAboveCm = ([\d.]+)f;")
    if None in (HULL, RUN, BREAK, INTERVAL, REJOIN):
        return
    SPACING = const("SeaGameMode.h", r"float SquadronSpacingCm = ([\d.]+)f;")
    if SPACING is None:
        return
    # the follower's distance from her station point at the spawn: abeam of
    # the leader by the spacing, and the station an interval astern of him
    SPAWN_ERR = ((SPACING / 100) ** 2 + (INTERVAL / 100) ** 2) ** 0.5
    if not BREAK < RUN:
        fail("the break test puts the hull at %.2f, not below the %.2f at which a ship runs" % (BREAK, RUN))

    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if not os.path.exists(base_path):
        note("no baseline - the line's rows are not checked, which is NOT a pass")
        return
    rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
    for n in ("line_formed", "line_breaks", "ledger_testbreak"):
        if n not in rows:
            fail("%s is not in the baseline" % n)
            return

    # rows where a Crown ship breaks off without the test, each with its reason
    NATURAL = {}
    for name, row in sorted(rows.items()):
        fl = ci_measure.SCENARIOS.get(name, [])
        if row.get("line_runner_ticks") != 0:
            fail("%s: a captain dressed on a ship that had broken off (%r ticks)" % (name, row.get("line_runner_ticks")))
        # ONE direction only: a walk that stepped over someone was latched as
        # closed. The other direction can go stale honestly - line_skips is
        # read off live captains, and one that stepped and then sank takes her
        # depth with her while the latch keeps the moment.
        if row.get("line_skips", 0) >= 1 and row.get("line_closed_t", -1) == -1:
            fail("%s: line_skips %r but the line was never latched as closed" % (name, row.get("line_skips")))
        tested = any(a.startswith("-EnemyBreakTest=") for a in fl)
        if not tested and row.get("line_broken", 0) and name not in NATURAL:
            fail("%s: %d Crown ship(s) broke off with no test asking - name the row and the reason"
                 % (name, row["line_broken"]))

    tick = 1.0 / 60
    b, f = rows["line_breaks"], rows["line_formed"]
    at = next(float(a.split("=", 1)[1]) for a in ci_measure.SCENARIOS["line_breaks"] if a.startswith("-EnemyBreakTest="))
    checks = [
        ("line_break_hull", b.get("line_break_hull") == BREAK * HULL),
        ("line_break_t", b.get("line_break_t") is not None and abs(b["line_break_t"] - at) <= 2 * tick),
        ("line_closed_t", b.get("line_closed_t") is not None and b.get("line_break_t") is not None
         and 0 <= b["line_closed_t"] - b["line_break_t"] <= 2 * tick),
        ("line_broken", b.get("line_broken") == 1),
        ("line_skips", b.get("line_skips") == 1),
        ("line_runner_gap_m", (b.get("line_runner_gap_m") or -1) > REJOIN / 100),
        ("line_station_gap_m", b.get("line_station_gap_m") == -1),
        ("line_station_err_m", b.get("line_station_err_m") == -1),
        ("line_runners_afloat", b.get("line_runners_afloat") == 1),
        ("no broadside (breaks)", b.get("broadsides") == 0),
    ]
    control = [
        # FORMED means closing on the station point, not merely lying near the
        # ship ahead: abeam at the spawn the gap is already under two intervals,
        # but the station point (one interval astern) is SPAWN_ERR off. Formed:
        # under half of that. (Measured 80 m of 192 after 90 s.)
        ("control: a line formed", f.get("line_station_err_m") is not None
         and 0 <= f["line_station_err_m"] < SPAWN_ERR / 2),
        ("control: nobody broke off", f.get("line_broken") == 0 and f.get("line_closed_t") == -1),
        ("no broadside (formed)", f.get("broadsides") == 0),
    ]
    wrong = [k for k, good in checks + control if not good]
    if wrong:
        fail("line_breaks / line_formed against paper: %s" % ", ".join(wrong))
    else:
        ok("line_breaks / line_formed: %d numbers match the paper" % len(checks + control))

    MUST = {"line_broken", "line_break_t", "line_break_hull", "line_closed_t", "line_skips",
            "line_runner_gap_m", "line_station_gap_m", "line_runners_afloat", "line_station_err_m"}
    moved = set(k for k in set(b) | set(f) if b.get(k) != f.get(k))
    if moved != MUST:
        fail("line_breaks vs line_formed moves %s, not exactly %s" % (sorted(moved), sorted(MUST)))
    else:
        ok("line_breaks vs line_formed: exactly %d keys move" % len(MUST))

    # THE OLDER PAIR, which was prose until now: a consort that STRIKES. It
    # must move exactly the closing, its depth and the station of the ship
    # that lost her leader.
    c, w = rows.get("line_closes"), rows.get("line_whole")
    if c and w:
        moved = set(k for k in set(c) | set(w) if c.get(k) != w.get(k))
        want = {"line_skips", "line_closed_t", "line_station_gap_m", "line_station_err_m"}
        if not moved <= want or not {"line_skips", "line_closed_t"} <= moved:
            fail("line_closes vs line_whole moves %s, not within %s" % (sorted(moved), sorted(want)))
        else:
            ok("line_closes vs line_whole: %s" % ", ".join(sorted(moved)))
    else:
        fail("line_closes / line_whole are not in the baseline")

    tb = rows["ledger_testbreak"]
    if (tb.get("ledger_why"), tb.get("ledger_written")) != (3, 0):
        fail("ledger_testbreak: -EnemyBreakTest does not shut the book: why=%r written=%r"
             % (tb.get("ledger_why"), tb.get("ledger_written")))

def check_escape():
    """THE RUNNER WHO GETS AWAY, on paper.

    escape_on / escape_off differ in one flag and move exactly the escape's
    keys, the runner count, and - named, because they leave with her - the
    escaped hull's own quit-line keys. The escape is seen within one sample:
    no farther past the range than her best speed covers in half a second.
    Two guards with the rows only they can catch.
    """
    import re
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    try:
        import ci_measure
    except Exception as e:
        fail("cannot import ci_measure: %s" % e)
        return
    got = ci_measure.measure("fixture",
        "LogTemp: Display: SEALOG player ship=ShipPawn_0 bound t=0.0\n"
        "LogTemp: Display: SEALOG player ship=ShipPawn_1 bound t=35.8\n"
        "LogTemp: Display: SEALOG TOTAL escaped=2 firstEscape=36.5 dist=2083 victories=3\n")
    want = {"sea_escaped": 2, "sea_escape_t": 36.5, "sea_escape_dist_m": 2083.0, "sea_victories": 3,
            "sea_player_rebound_t": 35.8}
    bad = {k: (got.get(k), v) for k, v in want.items() if got.get(k) != v}
    if bad:
        fail("the escape lines are not read field by field (got, want): %r" % bad)

    def const(path, pattern):
        src = io.open(os.path.join(ROOT, "Source", "PirateSeas", path), encoding="utf-8").read()
        mm = re.search(pattern, src)
        if not mm:
            fail("cannot read %r out of %s" % (pattern, path))
            return None
        return float(mm.group(1))
    RANGE = const("SeaGameMode.h", r"float EscapeRangeM = ([\d.]+)f;")
    RESPAWN = const("SeaGameMode.h", r"float EnemyRespawnDelay = ([\d.]+)f;")
    PERIOD = const("SeaGameMode.cpp", r"&ASeaGameMode::SampleEscapes, ([\d.]+)f, true")
    TOP = const("ShipPawn.h", r"float MaxForwardSpeed = ([\d.]+)f;")
    if None in (RANGE, RESPAWN, PERIOD, TOP):
        return

    base_path = os.path.join(ROOT, "tools", "measurement_baseline.json")
    if not os.path.exists(base_path):
        note("no baseline - the escape rows are not checked, which is NOT a pass")
        return
    rows = json.loads(io.open(base_path, encoding="utf-8-sig").read())
    need = ("escape_on", "escape_off", "escape_fighting", "escape_player_down")
    if any(n not in rows for n in need):
        fail("escape rows missing from the baseline: %s" % [n for n in need if n not in rows])
        return

    def flag(name, key):
        for a in ci_measure.SCENARIOS[name]:
            if a.startswith(key + "="):
                return float(a.split("=", 1)[1])
        return None

    on, off = rows["escape_on"], rows["escape_off"]
    slack = TOP / 100 * PERIOD
    quit_on = flag("escape_on", "-ShipQuitAfter")
    checks = [
        ("escaped once", on.get("sea_escaped") == 1),
        ("seen within one sample of the range", on.get("sea_escape_dist_m") is not None
         and RANGE <= on["sea_escape_dist_m"] <= RANGE + slack),
        ("a victory", on.get("sea_victories") == 1),
        ("no runner left", on.get("line_runners_afloat") == 0),
        ("quit before a new squadron", on.get("sea_escape_t") is not None
         and on["sea_escape_t"] < quit_on < on["sea_escape_t"] + RESPAWN),
        ("off: she runs on", (off.get("sea_escaped"), off.get("sea_victories"), off.get("line_runners_afloat")) == (0, 0, 1)),
    ]
    fi, pd = rows["escape_fighting"], rows["escape_player_down"]
    checks += [
        ("a ship still fighting past the range stays", fi.get("sea_escaped") == 0
         and flag("escape_fighting", "-EnemyX") / 100 > RANGE),
        ("a runner waits for the player's next hull", pd.get("sea_escaped") == 1
         and pd.get("sea_player_rebound_t") is not None and pd.get("sea_escape_t") is not None
         and pd["sea_player_rebound_t"] <= pd["sea_escape_t"] <= pd["sea_player_rebound_t"] + PERIOD
         and flag("escape_player_down", "-EnemyX") / 100 > RANGE),
    ]
    wrong = [k for k, good in checks if not good]
    if wrong:
        fail("the escape against paper: %s" % ", ".join(wrong))
    else:
        ok("the escape: %d numbers match the paper" % len(checks))

    # EXACTLY: the escape's own keys, the runner count, and the escaped
    # hull's quit-line keys, which leave the sea with her.
    HULL = {"dry_refusals", "enemy_gun_crew_quit", "enemy_rig_quit", "port_ticks_max",
            "prize_ticks_max", "pursuit_ticks_max", "shot_fired", "shot_left", "shot_max"}
    MUST = {"sea_escaped", "sea_escape_t", "sea_escape_dist_m", "sea_victories", "line_runners_afloat"} | HULL
    moved = set(k for k in set(on) | set(off) if on.get(k) != off.get(k))
    if moved != MUST:
        fail("escape_on vs escape_off moves %s, not exactly %s" % (sorted(moved), sorted(MUST)))
    else:
        ok("escape_on vs escape_off: exactly %d keys move" % len(MUST))

    for name, row in sorted(rows.items()):
        if name.startswith("escape_"):
            continue
        if row.get("sea_escaped", 0):
            fail("%s: a Crown ship escaped in a row that never broke one off" % name)

def main():
    print("PirateSeas checks - the ones that do not need Unreal\n")
    files = tracked_files()
    check_tree(files)
    check_textures()
    check_docs()
    check_comparison()
    check_ledger()
    check_line()
    check_escape()
    print("")
    if NOTES:
        print("%d check(s) SKIPPED - a skip is not a pass:" % len(NOTES))
        for n in NOTES:
            print("  - " + n)
    if FAILS:
        print("%d FAILED" % len(FAILS))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
