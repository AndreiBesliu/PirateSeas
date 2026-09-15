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
           "and reads the convoy, crew and pursuit lines")


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
