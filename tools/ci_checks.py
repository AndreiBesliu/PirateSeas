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


def main():
    print("PirateSeas checks - the ones that do not need Unreal\n")
    files = tracked_files()
    check_tree(files)
    check_textures()
    check_docs()
    check_comparison()
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
