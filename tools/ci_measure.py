"""
Runs the game headlessly and compares the numbers against a recorded baseline.

This is the check that needs the engine. It plays three scenarios with every
hazard pinned, reads the counts out of the log, and fails when they move.

THREE RULES, each of which this project learned the hard way:

1. PIN EVERYTHING. -UseFixedTimeStep -FPS=60 -ShipSeed=N and the wind, on every
   run. Without all four the runs do not repeat: two runs with identical flags
   and identical code once gave 5 and then 8 hits in the rigging, because gun
   scatter was the one unpinned hazard in the project. A comparison of unpinned
   runs reads scatter and reports it as a regression.

2. A MISSING BASELINE IS NOT A DIFFERENCE. An earlier comparison script in this
   project reported "DIFFERS" for two scenarios whose baseline files simply did
   not exist - diff failed on absence and the shell read failure as difference.
   Here, absent means absent, and it is reported as NEW, not as broken.

3. SAY THE NUMBERS. A red light that does not print what moved sends whoever
   reads it back to reproduce the run by hand.

4. A MEASUREMENT THAT STOPPED BEING TAKEN IS NOT A MATCH - AT EITHER LEVEL. The
   first version of this rule was implemented one level too low: the keys inside
   a scenario were compared as a union while the SCENARIOS themselves were still
   walked from the new result alone, so deleting an entry from SCENARIOS - most
   damagingly `sinking`, which exists only to keep ships_sunk non-zero - printed
   "every measurement matches the baseline" and exited 0. compare({}, baseline)
   returned no differences at all. Both levels walk the union now, and
   ci_checks.py feeds that exact case as a fixture. Several of the numbers
   below are recorded only when the log carries them, so a measurement can
   vanish rather than move - and a comparison that walks only the keys it just
   collected would never reach the missing one. This one walks the UNION of both
   sides, and absence is its own verdict, separate from MOVED and from NEW.

    python tools/ci_measure.py            compare against the baseline
    python tools/ci_measure.py --record   write the current numbers as the
                                          baseline (do this deliberately, and
                                          say so in the commit)
"""
import io
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UPROJECT = os.path.join(ROOT, "PirateSeas.uproject")
LOG = os.path.join(ROOT, "Saved", "Logs", "PirateSeas.log")
BASELINE = os.path.join(ROOT, "tools", "measurement_baseline.json")
LAST = os.path.join(ROOT, "tools", "last_measurement.json")

UE = os.environ.get("UE", r"C:\Program Files\Epic Games\UE_5.7")
EDITOR = os.path.join(UE, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")

# Pinned on every scenario. All four, always.
PINNED = ["-game", "-NullRHI", "-unattended", "-nosound",
          "-UseFixedTimeStep", "-FPS=60", "-ShipSeed=1"]

SCENARIOS = {
    # A broadside at a known range: the gunnery numbers.
    "gunnery": ["-WindBearing=120", "-WindSpeed=11", "-EnemyX=9000",
                "-EnemyY=1500", "-ShipFireTest=8", "-ShipQuitAfter=45"],
    # Driven onto a beach: the grounding forces and the claw-off.
    "grounding": ["-WindBearing=120", "-WindSpeed=12", "-Islands=1",
                  "-ShipRunAground=2", "-ShipQuitAfter=40"],
    # Sailing free, under helm: buoyancy, the polar, and the wake.
    "sailing": ["-WindBearing=120", "-WindSpeed=12", "-ShipRudderTest=2",
                "-ShipQuitAfter=40"],
    # A ship is scuttled on purpose. This scenario buys nothing about gunnery;
    # it exists so that ships_sunk is non-zero SOMEWHERE. A counter that reads
    # zero on every scenario is indistinguishable from a counter that is broken,
    # and this one was broken for three commits without a light going red.
    "sinking": ["-WindBearing=120", "-WindSpeed=11", "-EnemyX=9000",
                "-EnemyY=1500", "-EnemySinkTest=6", "-ShipQuitAfter=40"],
    # Six hulls and three wake slots. Same reasoning as `sinking`: the wake
    # counters below are all zero in a world with fewer ships than slots, and a
    # counter that is zero everywhere cannot be told from a broken one. Here
    # `wake_ignored_max` is 3 and the rudder test keeps ships crossing in and out
    # of the nearest-three set, which is the traffic that exercises the binding
    # and the slot steal.
    "crowded": ["-WindBearing=120", "-WindSpeed=12", "-EnemyCount=5",
                "-ShipRudderTest=2", "-ShipQuitAfter=40"],
    # The gunnery flags at a different wind. The swell is built from the wind
    # now, and until it was, a sweep of -WindSpeed changed the sailing and left
    # the water identical - so "no change across the range" read as "the fix
    # holds at both extremes" when it meant "the input never moved". These two
    # scenarios differ in exactly one flag, so the sea numbers below have to
    # differ too; if they ever match again, the coupling is gone.
    "gale": ["-WindBearing=120", "-WindSpeed=18", "-EnemyX=9000",
             "-EnemyY=1500", "-ShipFireTest=8", "-ShipQuitAfter=45"],
}


def run(name, flags):
    # DELETE THE LOG FIRST. Every scenario writes the same
    # Saved/Logs/PirateSeas.log, so an editor that fails to start - a bad flag,
    # a missing DLL, a build half-written - leaves the PREVIOUS scenario's log
    # sitting there, and this reads it and reports its numbers under the new
    # scenario's name. Nothing in the numbers would look wrong; they would be
    # somebody else's numbers.
    if os.path.exists(LOG):
        os.remove(LOG)

    args = [EDITOR, UPROJECT] + PINNED + flags
    subprocess.run(args, cwd=ROOT, capture_output=True)
    # The editor exits 1 in this project regardless, for an unrelated water
    # collision-profile complaint, so the exit code proves nothing. The LOG is
    # the verdict.
    if not os.path.exists(LOG):
        raise RuntimeError("%s produced no log at all - the editor did not "
                           "start, or wrote nowhere this script can see" % name)
    text = io.open(LOG, encoding="utf-8", errors="replace").read()

    # And that it is a log of THIS scenario: the game prints its own command
    # line, so the flags that were asked for have to appear in it. A run that
    # ended before it got that far is not a measurement of anything.
    line = re.search(r"LogInit: Command Line:(.*)", text)
    if not line:
        raise RuntimeError("%s: the log carries no command line, so there is no "
                           "proof it came from this run" % name)
    for flag in flags:
        if flag not in line.group(1):
            raise RuntimeError("%s: the log's command line does not carry %s - "
                               "this is a log of some other run" % (name, flag))
    return text


def measure(name, text):
    m = {}
    m["broadsides"] = len(re.findall(r"SHOTLOG broadside", text))
    m["struck"] = len(re.findall(r"SHOTLOG (?:hit |rig )", text))
    m["splashes"] = len(re.findall(r"SHOTLOG splash", text))
    # Grounding BLOWS, not grounding log LINES. The damage path prints two lines
    # per blow - "damage taken=" and "zone" - so this counted every blow twice
    # and the baseline's "groundings: 2" was one grounding, reported as two. A
    # number with no unit is not a measurement.
    m["groundings"] = len(re.findall(r"GROUNDLOG damage", text))
    # Balls destroyed at the muzzle because the gun port was under the local
    # surface. Zero in every shipped scenario today; it exists because it was
    # NOT zero before the guard, and the four splashes it produced were counted
    # as shot falling in the sea.
    m["awash"] = len(re.findall(r"SHOTLOG awash", text))
    # The terminal event, not a phase word. This counted "sink=sinking" for
    # three commits. The only line that prints sink= prints one of afloat,
    # flooding, foundering, plunging or wreck - never "sinking" - so the number
    # was nailed to zero and the gate could not come out non-zero whatever the
    # game did. The `sinking` scenario below exists to keep it honest: if this
    # pattern ever stops matching, that scenario falls from 1 to 0 and says so.
    m["ships_sunk"] = len(re.findall(r"SHIPLOG \S+ SUNK ", text))

    # Buoyancy: what the hull actually floats on. A number near 1.00 means
    # buoyancy carries the weight; well under means something else is.
    lifts = [float(v) for v in re.findall(r"lift=([0-9.]+)", text)]
    if lifts:
        m["lift_mean"] = round(sum(lifts) / len(lifts), 3)

    # The sea itself. wave_amplitude_cm is the sum of the drawn amplitudes and
    # moves with the wind; foam_start_cm is where the white water begins, keyed
    # to the crest's standard deviation; breaking_pct is the share of the sea
    # that actually breaks - the number that would have said 0.3% while the
    # comment said "the top fifth".
    w = re.search(r"SEALOG surface waves drawn=\d+ of \d+, amplitude ([0-9.]+) of", text)
    if w:
        m["wave_amplitude_cm"] = float(w.group(1))
    f = re.search(r"foam>=([0-9.]+) cm over", text)
    if f:
        m["foam_start_cm"] = float(f.group(1))
    b = re.search(r"breaking on ([0-9.]+)% of the sea", text)
    if b:
        m["foam_breaking_pct"] = float(b.group(1))

    # Did any shot die against a sea with a wave in it? surfZ is the flat-plane
    # detector: before the wave-aware query it was ~0 on all 25 splashes ever
    # logged, on a sea a metre high.
    surf = [float(v) for v in re.findall(r"SHOTLOG splash .*?surfZ=(-?[0-9.]+)", text)]
    if surf:
        m["splash_surfz_max"] = round(max(surf), 1)

    # The wake. NOTE which of these is the regression detector and which is not:
    # wake_live_max is bounded by WakePointCount and already SITS on that ceiling
    # (24) in the sailing and grounding scenarios, so a wake that goes wrong can
    # only push it into a limit it has already reached. It is recorded because it
    # is cheap, not because it can fail.
    #
    # These four are the ones that carry the load. Each is a thing the wake says
    # must never happen, and each was printed for a whole session without being
    # read by anything: a trail still being drawn with nobody advancing its clock
    # (stranded), one ship holding two slots (doubled), a followed ship that
    # could not be given a slot at all (discarded), and a fading trail taken to
    # make room (stolen, which is legitimate but should not move silently).
    live = [int(v) for v in re.findall(r"WAKELOG live=(\d+)", text)]
    if live:
        m["wake_live_max"] = max(live)
    for tag in ("stranded", "doubled", "stolen", "discarded", "ignored"):
        vals = [int(v) for v in
                re.findall(r"WAKELOG [^\n]*?\b%s=(\d+)" % tag, text)]
        if vals:
            m["wake_%s_max" % tag] = max(vals)

    # And the splash slot the ring buffer had to overwrite: the counter added
    # when the splash was built, never read until now.
    lost = [int(v) for v in re.findall(r"WAKELOG [^\n]*?\blost=(\d+)", text)]
    if lost:
        m["splash_lost_max"] = max(lost)

    # Did the world build what it was asked for?
    isles = re.search(r"SEALOG islands built=(\d+) of (\d+)", text)
    if isles:
        m["islands_built"] = int(isles.group(1))
    return m


def compare(results, base):
    """(results, baseline) -> (moved, new, gone), each a list of sentences.

    Kept out of main() and free of files, the engine and the clock so that
    ci_checks.py can feed it fixtures on a machine with no Unreal on it. A
    comparison nobody can test is the thing this project keeps being bitten by:
    the bug that made this function necessary was a loop that could not report a
    missing measurement, and it sat in a green pipeline for three commits.
    """
    moved, new, gone = [], [], []
    # The union AT THE SCENARIO LEVEL TOO. Walking results.items() alone meant a
    # scenario that stopped running was never visited: compare({}, baseline)
    # reported nothing at all, and deleting one entry from SCENARIOS left the run
    # printing "every measurement matches the baseline" while a quarter of the
    # gate no longer ran.
    for name in sorted(set(results) | set(base)):
        if name not in base:
            new.append(name)
            continue
        if name not in results:
            gone.append("%s (the whole scenario - %d numbers no longer taken)"
                        % (name, len(base[name])))
            continue
        got = results[name]
        # The UNION of both sides. Walking only the keys just collected is how a
        # measurement disappears quietly: lift_mean, wake_live_max and
        # islands_built are each recorded only when the log carries the line
        # they are read from, so a run that stopped printing lift= would offer
        # no lift_mean at all, the loop would never reach it, and the comparison
        # would report a clean match for a game that had stopped floating.
        for key in sorted(set(got) | set(base[name])):
            if key not in base[name]:
                new.append("%s.%s" % (name, key))
                continue
            if key not in got:
                gone.append("%s.%s (was %s)" % (name, key, base[name][key]))
                continue
            value, was = got[key], base[name][key]
            same = (abs(value - was) <= 0.02) if isinstance(value, float) \
                else (value == was)
            if not same:
                moved.append("%s.%s: %s -> %s" % (name, key, was, value))
    return moved, new, gone


def main():
    record = "--record" in sys.argv
    if not os.path.exists(EDITOR):
        print("FAIL  no Unreal at %s - this check needs the engine and there is "
              "none here. That is a missing prerequisite, not a pass." % EDITOR)
        return 1

    results = {}
    for name, flags in SCENARIOS.items():
        print("running %s ..." % name)
        results[name] = measure(name, run(name, flags))
        print("  " + json.dumps(results[name], sort_keys=True))

    io.open(LAST, "w", encoding="utf-8").write(
        json.dumps(results, indent=2, sort_keys=True))

    if record:
        io.open(BASELINE, "w", encoding="utf-8").write(
            json.dumps(results, indent=2, sort_keys=True))
        print("\nbaseline recorded at tools/measurement_baseline.json")
        return 0

    if not os.path.exists(BASELINE):
        print("\nNO BASELINE to compare against. That is NOT a pass and it is "
              "NOT a failure - it is a comparison that could not be made.\n"
              "Record one deliberately:  python tools/ci_measure.py --record")
        return 1

    # utf-8-SIG. A byte-order mark in front of the JSON is not hypothetical:
    # PowerShell's Set-Content -Encoding utf8 puts one there, and it did, and
    # json.loads refused the file with a decode error that looks nothing like
    # "the baseline moved".
    base = json.loads(io.open(BASELINE, encoding="utf-8-sig").read())
    moved, new, gone = compare(results, base)

    print("")
    if gone:
        # Distinct from MOVED deliberately: this is not a number that changed,
        # it is a number nobody took. Different cause, different fix.
        print("%d measurement(s) STOPPED BEING MEASURED - the log no longer "
              "carries the line they are read from:" % len(gone))
        for g in gone:
            print("  " + g)
    if new:
        print("%d measurement(s) have no baseline (NEW, not broken): %s"
              % (len(new), ", ".join(new)))
    if moved:
        print("%d measurement(s) MOVED:" % len(moved))
        for m in moved:
            print("  " + m)
        print("\nIf the change was intended, re-record the baseline in the same "
              "commit that causes it, so the diff shows both.")
        return 1
    if new or gone:
        return 1
    print("every measurement matches the baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
