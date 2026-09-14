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
}


def run(name, flags):
    args = [EDITOR, UPROJECT] + PINNED + flags
    subprocess.run(args, cwd=ROOT, capture_output=True)
    # The editor exits 1 in this project regardless, for an unrelated water
    # collision-profile complaint, so the exit code proves nothing. The LOG is
    # the verdict.
    if not os.path.exists(LOG):
        raise RuntimeError("%s produced no log at all" % name)
    return io.open(LOG, encoding="utf-8", errors="replace").read()


def measure(name, text):
    m = {}
    m["broadsides"] = len(re.findall(r"SHOTLOG broadside", text))
    m["struck"] = len(re.findall(r"SHOTLOG (?:hit |rig )", text))
    m["splashes"] = len(re.findall(r"SHOTLOG splash", text))
    m["groundings"] = len(re.findall(r"GROUNDLOG", text))
    m["ships_sunk"] = len(re.findall(r"sink=sinking", text))

    # Buoyancy: what the hull actually floats on. A number near 1.00 means
    # buoyancy carries the weight; well under means something else is.
    lifts = [float(v) for v in re.findall(r"lift=([0-9.]+)", text)]
    if lifts:
        m["lift_mean"] = round(sum(lifts) / len(lifts), 3)

    # The wake: the largest number of live breadcrumbs seen at once.
    live = [int(v) for v in re.findall(r"WAKELOG live=(\d+)", text)]
    if live:
        m["wake_live_max"] = max(live)

    # Did the world build what it was asked for?
    isles = re.search(r"SEALOG islands built=(\d+) of (\d+)", text)
    if isles:
        m["islands_built"] = int(isles.group(1))
    return m


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
    moved, new = [], []
    for name, got in results.items():
        if name not in base:
            new.append(name)
            continue
        for key, value in sorted(got.items()):
            if key not in base[name]:
                new.append("%s.%s" % (name, key))
                continue
            was = base[name][key]
            same = (abs(value - was) <= 0.02) if isinstance(value, float) \
                else (value == was)
            if not same:
                moved.append("%s.%s: %s -> %s" % (name, key, was, value))

    print("")
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
    if new:
        return 1
    print("every measurement matches the baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
