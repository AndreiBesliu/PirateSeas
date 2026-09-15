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
    # An island at a size NOBODY ELSE BUILDS. Every other scenario runs at scale
    # 1.00, where the height the plants start at and the height the paint turns
    # to turf are the same number whether they agree by construction or by
    # accident - so a disagreement between them was invisible in the whole
    # suite. These are the flags OWNER_VERIFY item 14 already ships.
    # A matched PAIR, differing in exactly one flag: does the ball carry the
    # ship's way off the muzzle. The lead solver used to subtract our own
    # velocity whatever the ball was given, so the two used to print the same
    # lead; they must differ now.
    #
    # Both carry -ShipRudderTest so she has WAY ON when she fires. Without it
    # the ship is in irons at the moment of the broadside, her own velocity is
    # zero, the two branches agree trivially and the measurement is void - which
    # is exactly what the first version of this pair measured: 4.0 m both times.
    # The guard is velFwd on the same log line; if it ever reads ~0 the pair has
    # stopped testing anything.
    "carried_shot": ["-WindBearing=120", "-WindSpeed=11", "-EnemyX=9000",
                     "-EnemyY=1500", "-ShipRudderTest=2", "-ShipFireTest=20",
                     "-ShipQuitAfter=30", "-ShipLead=1", "-ShipInheritVel=1"],
    "loose_shot": ["-WindBearing=120", "-WindSpeed=11", "-EnemyX=9000",
                   "-EnemyY=1500", "-ShipRudderTest=2", "-ShipFireTest=20",
                   "-ShipQuitAfter=30", "-ShipLead=1", "-ShipInheritVel=0"],
    # An hour of the day other than the one the level was authored at. The sun,
    # its colour and the exposure band all move with it, and none of that is in
    # any other scenario - without this the whole feature could be deleted and
    # every number here would still match.
    "dusk": ["-WindBearing=280", "-WindSpeed=12", "-Islands=1", "-Hour=17.5",
             "-ShipRudderTest=2", "-ShipQuitAfter=30"],
    "lee_shore": ["-WindBearing=0", "-WindSpeed=12", "-Islands=3",
                  "-IsleX=-35000", "-IsleY=0", "-IsleRadius=14000",
                  "-EnemyX=-75000", "-EnemyY=0", "-ShipQuitAfter=40"],
    # The convoy, and the first pair whose two members must come out with
    # DIFFERENT RESULTS: identical but for which side of the wind the raider
    # starts on. Two laden merchants run 1200 m across the wind for a
    # landfall, and stopping one of them takes the convoy; an ordinary enemy
    # hull with the ordinary captain is placed 600 m to windward of them in
    # one run and 600 m to leeward in the other, and the player's hull sits
    # idle at the origin as it does in lee_shore. To windward she runs down
    # on them and one strikes; to leeward she beats the whole run and never
    # gets a shot off. If these two ever agree on mission_result, the wind
    # has stopped mattering and the objective has lost the point of its
    # existence.
    #
    # Sized by measurement, not by taste: at 800 m and three merchants the
    # weather raider's first broadside came at about 170 s and the convoy
    # was in port at 244, so "taken" was unreachable from EITHER side and
    # the pair agreed.
    "convoy_weather": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                       "-ConvoyX=120000", "-ConvoyY=150000",
                       "-ConvoyWindAngle=90", "-ConvoyRangeM=1200",
                       "-EnemyCount=1", "-RaiderSide=weather",
                       "-RaiderOffingM=600", "-ShipQuitAfter=400"],
    "convoy_lee": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                   "-ConvoyX=120000", "-ConvoyY=150000",
                   "-ConvoyWindAngle=90", "-ConvoyRangeM=1200",
                   "-EnemyCount=1", "-RaiderSide=lee",
                   "-RaiderOffingM=600", "-ShipQuitAfter=400"],
    # The hands. A second pair that must come out different, this time in
    # one flag on the CAPTAIN: an enemy starts 1.5 km off with her rig shot
    # down to 0.40 and has to close on the idle player. With repairs at sea
    # she sends half her men aloft on the way and arrives under a jury rig;
    # without, she arrives as she left. first_broadside_t and repaired_max
    # must differ. Casualties are exercised by the gunnery family, where the
    # player's test broadside kills men on the enemy and gun_crew_min reads
    # below 1.
    "crew_repair": ["-WindBearing=120", "-WindSpeed=12", "-EnemyCount=1",
                    "-EnemyX=150000", "-EnemyY=0", "-EnemyRigDamage=0.4",
                    "-AIRepair=1", "-ShipQuitAfter=360"],
    "crew_fight": ["-WindBearing=120", "-WindSpeed=12", "-EnemyCount=1",
                   "-EnemyX=150000", "-EnemyY=0", "-EnemyRigDamage=0.4",
                   "-AIRepair=0", "-ShipQuitAfter=360"],
    # The money, and the third pair that must come out DIFFERENT in one flag -
    # this time a flag that flips a switch the game already flips for itself,
    # and that a player holds under left Shift. Two laden merchants, a raider
    # to windward, and the only difference is where she aims: into the rigging
    # or into the hull. A prize is worth her cargo scaled by how much of her
    # hull is still sound, so the same merchant, stopped two ways, pays two
    # different sums. prize_value_max must differ AND in the stated direction -
    # a formula wired to the rig by mistake would also make them differ, the
    # other way round - and prize_rig_at_take is the decoy that must move
    # opposite. GUARD: prizes_taken must read 1 in BOTH halves; if either is 0
    # the comparison is void, the way fire_velfwd_max guards carried/loose.
    "prize_rig": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                  "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                  "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                  "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                  "-ShipQuitAfter=400"],
    "prize_hull": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                   "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                   "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                   "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=0",
                   "-ShipQuitAfter=400"],
    # POSSESSION. The pair for this one is prize_rig ABOVE, which is this
    # command line without -AIPrize=1 - so no redundant scenario is added and
    # the two really are one flag apart. With the doctrine off she leaves a
    # ship that has struck where she lies and goes hunting the next one; with
    # it on she goes alongside, lies to, and sends twelve men across. Keys that
    # must differ: prizes_manned, prize_hands_out, enemy_gun_crew_quit and
    # prize_ticks_max.
    #
    # The COST is not manufactured by a flag. HandsMax is 60 and FullGunCrew
    # 48, so the first prize is free at the guns and the second is not: she
    # takes both here, twenty-four men go away, and her reload drops to 36/48.
    # That is the mechanic biting at its own default, which is the difference
    # between measuring a mechanic and measuring a clamp.
    "prize_manned": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                     "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                     "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                     "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                     "-AIPrize=1", "-ShipQuitAfter=400"],
    # And the floor, because prizes_refused is otherwise a counter that reads
    # zero in every scenario, which is indistinguishable from a broken one -
    # the ships_sunk defect, which this project has already paid for once.
    #
    # A raider who sails with twenty-eight hands cannot afford even her FIRST
    # prize: twelve away would leave sixteen, under the floor of twenty. So
    # the refusal is deterministic and lands at the same moment the take would
    # have. Note what this does NOT do: it does not stretch the run until the
    # counter happens to fire. That was tried - PrizeCrew=25 at 400 s and then
    # at 500 s - and it failed for a reason worth keeping: sending
    # twenty-five men away slows her guns (35/48), so the SECOND merchant
    # struck at 352 s instead of 177, and by then she lay 350 m to leeward of
    # her, which this project's own polar prices at hundreds of seconds. The
    # counter was not slow; the geometry was wrong.
    "prize_shorthanded": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                          "-ConvoyX=120000", "-ConvoyY=150000",
                          "-ConvoyWindAngle=90", "-ConvoyRangeM=1200",
                          "-EnemyCount=1", "-RaiderSide=weather",
                          "-RaiderOffingM=600", "-ConvoyCargo=1200",
                          "-AIAimHigh=1", "-AIPrize=1", "-EnemyHands=28",
                          "-ShipQuitAfter=300"],
    # THE PORT. A prize with twelve men aboard runs for a roadstead laid
    # downwind, and the money is not the raider's until she is in it. The two
    # halves are one flag apart, -Port=, and both run 500 s because that is
    # what the trip takes: manned at 134 s, alongside the quay at 356. They
    # are a separate PAIR rather than a variation on prize_manned, which stops
    # at 400 s - stretching that scenario instead would have made the quit
    # time a second difference between the halves, and then a landing could be
    # credited to the extra hundred seconds rather than to the port.
    #
    # Keys that must differ: prizes_landed, purse_landed, prize_hands_home.
    # purse_end must NOT differ: what she is worth is settled when she strikes,
    # and a port that changed that would mean the value was never banked where
    # the last commit says it was.
    "prize_home": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                   "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                   "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                   "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                   "-AIPrize=1", "-Port=1", "-ShipQuitAfter=500"],
    "prize_noport": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                     "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                     "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                     "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                     "-AIPrize=1", "-ShipQuitAfter=500"],
    # THE PURSE BUYING SOMETHING. A raider who sails hurt (hull 600 of 1000)
    # and twenty hands short of her complement, with a port to spend in. One
    # flag apart: -AIRefit=. With the doctrine on she hunts, sends two prizes
    # home, and only THEN - money in the coffers - bears away and buys back
    # men and timber, the two things the sea will not return. With it off she
    # earns exactly the same money and spends none of it.
    #
    # Keys that must differ: refit_spent, refit_hands_bought,
    # refit_hull_bought, refit_coffers_end, port_ticks_max. Keys that must NOT:
    # purse_end and purse_landed - she earns the same either way, and a refit
    # that changed what a prize was worth would mean the money was never banked
    # where the commits before this one say it was.
    #
    # And the arithmetic is on the line so it can be read: twenty men at twenty
    # apiece is four hundred, four hundred points of hull at a half is two
    # hundred, six hundred spent out of twelve hundred landed, six hundred
    # left.
    #
    # EIGHT HUNDRED SECONDS, and the number comes from a timeline that was
    # read rather than guessed. Measured once end to end: prize manned at 138,
    # home at 359, the captain bears away at 359, gives up a second prize she
    # cannot reach at 539, reaches the roadstead and begins to refit at 764,
    # and is done at 776. Anything shorter and the run ends with her still on
    # her way, which would read as "the refit does nothing".
    #
    # GUARD: prizes_landed must be 1 in BOTH halves. The whole point is that
    # she can only spend what she has actually landed, so a half where nothing
    # came home is not a comparison, it is a ship with an empty purse.
    "refit_on": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                 "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                 "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                 "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                 "-AIPrize=1", "-Port=1", "-EnemyHull=600", "-EnemyHands=40",
                 "-AIRefit=1", "-ShipQuitAfter=800"],
    "refit_off": ["-WindBearing=0", "-WindSpeed=12", "-Convoy=2",
                  "-ConvoyX=120000", "-ConvoyY=150000", "-ConvoyWindAngle=90",
                  "-ConvoyRangeM=1200", "-EnemyCount=1", "-RaiderSide=weather",
                  "-RaiderOffingM=600", "-ConvoyCargo=1200", "-AIAimHigh=1",
                  "-AIPrize=1", "-Port=1", "-EnemyHull=600", "-EnemyHands=40",
                  "-AIRefit=0", "-ShipQuitAfter=800"],
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
    done = subprocess.run(args, cwd=ROOT, capture_output=True)
    # THE EXIT CODE IS WORTH SOMETHING AGAIN. For most of this project's life
    # the editor exited 1 on every single run, because the Water plugin's
    # collision profile was missing from DefaultEngine.ini - a plugin adds it
    # itself only if you enable it from the editor's UI, and nothing here has
    # ever opened that UI. The complaint became scenery, this comment used to
    # say the exit code proved nothing, and it was right.
    #
    # It was found by trying to PACKAGE the game: a cook counts errors and
    # refuses to build. With that fixed and a handful of constructor physics
    # calls moved onto BodyInstance, a run's log carries zero errors of any
    # kind and the editor exits 0 - so this can be a gate now. The log is still
    # the verdict about the GAME; this is the verdict about the RUN.
    if done.returncode != 0:
        raise RuntimeError(
            "%s: the editor exited %d. That used to be true of every run and "
            "meant nothing; since the water collision profile was added it "
            "means something went wrong. Read Saved/Logs/PirateSeas.log for "
            "lines containing ': Error: '." % (name, done.returncode))
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

    # How far ahead of her mark each broadside aimed, and whether the ball was
    # given the ship's way. The two are one question: the lead must allow for
    # exactly the motion the shot does NOT carry.
    leads = [float(v) for v in re.findall(r"SHOTLOG broadside .*?lead=(-?[0-9.]+)m", text)]
    if leads:
        m["lead_max_m"] = round(max(leads), 1)
    # The pair above is only a measurement while the ship has way on.
    vel = [abs(float(v)) for v in re.findall(r"velFwd=(-?[0-9.]+)", text)]
    if vel:
        m["fire_velfwd_max"] = round(max(vel), 2)
    inh = re.search(r"SHOTLOG \S+ ballistics inherit=(\d+) lead=(\d+)", text)
    if inh:
        m["ballistics_inherit"] = int(inh.group(1))
        m["ballistics_lead"] = int(inh.group(2))
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

    # The colour ramp, and the two halves of it that must behave OPPOSITELY:
    # the centimetres follow the wind (so gunnery and gale must differ), and the
    # share of the ramp the sea travels is what that keying holds still (so they
    # must agree). One key alone could not tell a fix from a freeze.
    sc = re.search(r"scatter>=([0-9.]+) cm span ([0-9.]+)", text)
    if sc:
        m["scatter_range_cm"] = float(sc.group(1))
        m["scatter_span"] = float(sc.group(2))

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
    # wake_live_max is recorded but is NOT the detector: it is bounded by
    # WakePointCount and already sits on that ceiling in two scenarios. slots and
    # shortest are the ones that move the right way - a frozen or mis-bound trail
    # collapses the shortest occupied trail while `live` stays pinned at 24.
    live = [int(v) for v in re.findall(r"WAKELOG live=(\d+)", text)]
    if live:
        m["wake_live_max"] = max(live)
    slots = [int(v) for v in re.findall(r"WAKELOG [^\n]*?\bslots=(\d+)", text)]
    if slots:
        m["wake_slots_max"] = max(slots)
    short = [int(v) for v in re.findall(r"WAKELOG [^\n]*?\bshortest=(\d+)", text)]
    if short:
        m["wake_shortest_max"] = max(short)
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

    # Did the world build what it was asked for - and did the SEA hear about it?
    # Two ends of one wire: the game mode spawns them, the surface has to push
    # them into the material, and the second used to give up for good if it ran
    # once before the first.
    isles = re.search(r"SEALOG islands built=(\d+) of (\d+)", text)
    if isles:
        m["islands_built"] = int(isles.group(1))
    pushed = re.search(r"SEALOG surf against (\d+) islands \(dropped=(\d+)", text)
    if pushed:
        m["islands_pushed"] = int(pushed.group(1))
        m["islands_dropped"] = int(pushed.group(2))

    # The plants against the paint. Island.cpp prints the height it plants from
    # and the height the MATERIAL says the sand ends at; they are the same
    # number only while nothing scales one of them. Recorded as the gap, because
    # a gap of zero is the whole claim and it reads at a glance.
    # Did the plants get the wind? The island makes one dynamic material per
    # plant component and reads the pushed value back; a run where that stops
    # happening leaves the hillside rigid, which is the state this started from
    # and is invisible in every other number.
    sway = re.search(r"ISLELOG \S+ sway mats=(\d+) wind=([0-9.]+) m/s "
                     r"toward [0-9.]+ deg \(readback (\w+)", text)
    if sway:
        m["sway_mats"] = int(sway.group(1))
        m["sway_wind_ms"] = float(sway.group(2))
        m["sway_readback_ok"] = 1 if sway.group(3) == "ok" else 0

    # The hour of the day, when one was asked for. lux and the exposure band
    # move together on purpose: a band that stopped following the light would
    # show up here as one moving without the other.
    sky = re.search(r"SKYLOG hour=([0-9.]+) elev=(-?[0-9.]+) azim=([0-9.]+) "
                    r"lux=([0-9.]+) K=([0-9.]+) ev=\[(-?[0-9.]+),", text)
    if sky:
        m["sun_elev_deg"] = float(sky.group(2))
        m["sun_lux"] = float(sky.group(4))
        m["sun_kelvin"] = float(sky.group(5))
        m["exposure_ev_min"] = float(sky.group(6))

    # And the SHIP's half of the same wire. The island's plants are only in two
    # scenarios; the rig is in all nine, so this is the one that would catch the
    # push going away.
    rig = re.search(r"SHIPLOG \S+ rigsway mats=(\d+) wind=([0-9.]+) m/s "
                    r"toward [0-9.]+ deg \(readback (\w+)", text)
    if rig:
        m["rig_sway_mats"] = int(rig.group(1))
        m["rig_sway_readback_ok"] = 1 if rig.group(3) == "ok" else 0

    # The convoy. One MISSION line per run, printed from every exit including
    # the quit timer, so a run that ended neither way still reports its
    # counters. mission_result: 2 taken, 1 got through, 0 unresolved. The
    # gauge/lee/beat trio is sampled once a second into latched counters;
    # merchants_struck is the SHIP's count of the same event the game mode
    # counts as stopped - two ends of one wire.
    ms = re.search(r"CONVOYLOG MISSION (\w+) t=([0-9.]+) stopped=(\d+) "
                   r"through=(\d+) sunk=(\d+) of (\d+) need=(\d+) gauge=(\d+) "
                   r"lee=(\d+) beat=(\d+) firstStrike=(-?[0-9.]+)", text)
    if ms:
        m["mission_result"] = {"TAKEN": 2, "THROUGH": 1}.get(ms.group(1), 0)
        m["mission_t"] = float(ms.group(2))
        m["convoy_stopped"] = int(ms.group(3))
        m["convoy_through"] = int(ms.group(4))
        m["convoy_sunk"] = int(ms.group(5))
        m["convoy_size"] = int(ms.group(6))
        m["convoy_need"] = int(ms.group(7))
        m["gauge_ticks"] = int(ms.group(8))
        m["lee_ticks"] = int(ms.group(9))
        m["beat_seconds"] = int(ms.group(10))
        m["first_strike_t"] = float(ms.group(11))
        m["merchants_struck"] = len(re.findall(r"SHIPLOG \S+ STRUCK ", text))

    # The hands, off the one line per hull printed at quit. casualties_max is
    # the counter that must be non-zero wherever shot lands on a hull;
    # gun_crew_min is what those casualties cost at the guns (1.00 until the
    # spare dozen are gone); repaired_max is what the carpenter gave back.
    crew = re.findall(r"CREWLOG \S+ hands=(\d+)/(\d+) casualties=(\d+) "
                      r"repairShare=([0-9.]+) repaired=([0-9.]+) gunCrew=([0-9.]+) "
                      r"rig=([0-9.]+) rudder=([0-9.]+)", text)
    if crew:
        m["casualties_max"] = max(int(c[2]) for c in crew)
        m["gun_crew_min"] = min(float(c[5]) for c in crew)
        m["repaired_max"] = round(max(float(c[4]) for c in crew), 3)
    # The ENEMY's rig and gun crew at quit, read BY NAME off her own line.
    # Not a minimum over hulls: the player's rig is what the enemy has been
    # firing at, so a minimum would read the damage she dealt and call it the
    # state she arrived in - and gun_crew_min is worse than useless in a convoy
    # run, where it reads 0.25 in both halves because a merchant carries 14
    # hands and sits on MinGunCrewFactor from the first tick. A key that is
    # pinned to a floor by a hull nobody is asking about cannot report the one
    # that matters.
    enemy_crew = re.search(r"CREWLOG EnemyShipPawn_\d+ hands=\d+/\d+ casualties=\d+ "
                           r"repairShare=[0-9.]+ repaired=[0-9.]+ gunCrew=([0-9.]+) "
                           r"rig=([0-9.]+)", text)
    if enemy_crew:
        m["enemy_gun_crew_quit"] = float(enemy_crew.group(1))
        m["enemy_rig_quit"] = float(enemy_crew.group(2))

    # The money. purse_end/prizes_taken/prize_value_max come off the PURSE line
    # printed at quit in EVERY run, convoy or not, so they are counted zeros
    # rather than absences. The per-prize line carries the ingredients of the
    # value beside the value, and purse_balances re-derives the sum: a
    # transcription bug in money then reads as a boolean that MOVED rather than
    # as a number somebody has to eyeball.
    purse = re.search(r"PRIZELOG PURSE purse=(\d+) prizes=(\d+) valueMax=(\d+) "
                      r"cargo=(\d+) manned=(\d+) refused=(\d+) handsSent=(\d+) "
                      r"closest=(-?[0-9.]+) landed=(\d+) landedValue=(\d+) "
                      r"handsHome=(\d+)", text)
    if purse:
        m["purse_end"] = int(purse.group(1))
        m["prizes_taken"] = int(purse.group(2))
        m["prize_value_max"] = int(purse.group(3))
        # Possession. prize_closest_m is written whether the doctrine is on or
        # off, on purpose: it is how the hailing distance was chosen, and it is
        # how a take that never happens explains itself instead of just
        # reading zero.
        m["prizes_manned"] = int(purse.group(5))
        m["prizes_refused"] = int(purse.group(6))
        # RENAMED from prize_hands_out, which read as "men currently away" and
        # meant "men ever sent". With the port, prizes come home and the two
        # stopped being the same number; a field with two meanings is the
        # defect this project keeps paying for. Both halves are cumulative and
        # monotonic, and "away now" is the difference.
        m["prize_hands_sent"] = int(purse.group(7))
        m["prize_closest_m"] = round(float(purse.group(8)), 0)
        # The port. purse_end is what the prizes were WORTH; purse_landed is
        # what reached the quay. They are equal only when every prize got
        # home, and the gap is the whole point of having a port at all.
        m["prizes_landed"] = int(purse.group(9))
        m["purse_landed"] = int(purse.group(10))
        m["prize_hands_home"] = int(purse.group(11))
    taken = re.findall(r"PRIZELOG \S+ taken value=(\d+) cargo=(\d+) hull=([0-9.]+) "
                       r"rig=([0-9.]+) zone=(\w+) t=[0-9.]+ purse=(\d+) prizes=(\d+)", text)
    if taken:
        m["prize_hull_at_take"] = float(taken[0][2])
        m["prize_rig_at_take"] = float(taken[0][3])
        m["prize_strike_zone"] = 1 if taken[0][4] == "rig" else 0
        if purse:
            m["purse_balances"] = 1 if sum(int(t[0]) for t in taken) == int(purse.group(1)) else 0
    # When the first broadside of the run was fired, whoever fired it. The
    # broadside line carries t= at its end since the crew slice.
    times = [float(v) for v in re.findall(r"SHOTLOG broadside .*? t=([0-9.]+)", text)]
    if times:
        m["first_broadside_t"] = min(times)

    # Ticks a captain spent running down a chase instead of laying her guns.
    # Zero in every fight against a target that does not make off, which is
    # every scenario but the convoy pair - and a non-zero here in any of them
    # says the pursuit rule has started firing where it was not meant to.
    pursuit = [int(v) for v in re.findall(r"SEALOG \S+ landTicks=.*?pursuitTicks=(\d+)", text)]
    if pursuit:
        m["pursuit_ticks_max"] = max(pursuit)
    # What the money BOUGHT. Printed at every quit whether there is a port or
    # not, so these are counted zeros rather than absences - and spent beside
    # coffers, because either one alone cannot be told from a ship that had
    # nothing to spend in the first place.
    refit = re.search(r"PORTLOG REFIT spent=(\d+) coffers=(-?\d+) handsBought=(\d+) "
                      r"hullBought=(\d+) refitSeconds=([0-9.]+)", text)
    if refit:
        m["refit_spent"] = int(refit.group(1))
        m["refit_coffers_end"] = int(refit.group(2))
        m["refit_hands_bought"] = int(refit.group(3))
        m["refit_hull_bought"] = int(refit.group(4))
        m["refit_seconds"] = round(float(refit.group(5)), 1)
    # Ticks a captain spent making for the port instead of hunting.
    port_ticks = [int(v) for v in
                  re.findall(r"SEALOG \S+ landTicks=.*?portTicks=(\d+)", text)]
    if port_ticks:
        m["port_ticks_max"] = max(port_ticks)

    # Ticks a captain spent standing by a prize instead of fighting. Zero in
    # every scenario that does not turn the doctrine on, which is all but two.
    prize_ticks = [int(v) for v in
                   re.findall(r"SEALOG \S+ landTicks=.*?prizeTicks=(\d+)", text)]
    if prize_ticks:
        m["prize_ticks_max"] = max(prize_ticks)

    band = re.findall(r"turf from ([0-9.]+) cm \(paint says ([0-9.]+), scale ([0-9.]+)\)", text)
    if band:
        m["turf_band_gap_cm"] = round(max(abs(float(a) - float(b)) for a, b, _ in band), 1)
        m["island_scale_max"] = round(max(float(c) for _, _, c in band), 2)
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
