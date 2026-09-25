"""
Where did the ball actually meet the ship - and which zone SHOULD it have hurt?

    python tools/probe_hull_hits.py [scenario,scenario,...] [--band=LO,HI]

--band is the gun band (cm, hull-local Z) used for the third column; the default
200,350 is "from just under the deck to the top of the rail". It runs the engine
either way, so a different band costs minutes, not seconds.

The ship's collision is a BOX, half extents (1550, 520, 350) cm about the hull
origin, which is the waterline. The hull inside it is what Scripts/ship.py
lofts: 30 m long, 8.4 m at its widest, a few percent of that at the stem, a
sheered deck at about 2.1 m and a rail 1.15 m above it. A ball stops on the
box's face; ShipPawn classifies the damage from that point.

For every non-rig hit this reads `SHOTLOG zone ... at=(..) dir=(..)` (both in
the struck hull's frame, dir being the ball's INCOMING direction - see
ACannonBall::LastFlightVel), continues the ball's line from the box face until
it enters the hull's real sections, and classifies the damage again at that
point. It prints three columns: the zone the game gave, the zone the same rules
give at the timber, and the zone a band about the guns' actual height gives.

Written 25.09 to answer one question before building anything: does the box
matter for more than where the splinters appear? Measured over the seven
distinct fights in the suite (41 hull-zone hits):

  - 0 of 41 were false: every ball's line would have met timber;
  - the timber was 1.7 m (guns zone) to 3.4 m (hull zone) past the box face on
    average, and up to 7 m;
  - 12 of 41 dismounted a gun, and ALL TWELVE met the hull below the deck, 0.6
    to 1.9 m under the gun they took out. The gun band is [0, 240] cm, set
    when the ports were at 120; the ports have been at 280 since 18.09.

The section formulas are a COPY of ship.py's, because that file imports bpy.
If the hull is ever re-lofted, this copy has to follow - and the anchor values
checked in anchor() below will say so by failing.

A probe, not a gate: it runs the engine, takes minutes, and answers a design
question. The numbers it prints are not recorded anywhere automatically.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# ---- ship.py, the formulas only (metres) -----------------------------------
LENGTH, BEAM, DRAFT, FREEBOARD, SHEER, BULWARK = 30.0, 8.4, 2.6, 2.1, 1.5, 1.15
BALL_R = 0.075           # a 15 kg iron shot is about 15 cm across
PORTS_CM = [-600.0, -240.0, 120.0, 480.0]    # ShipPawn.cpp GGunPortsX


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def half_beam(t):
    if t < 0.45:
        f = 0.55 + 0.45 * smoothstep(0.0, 1.0, t / 0.45)
    else:
        f = 1.0 - 0.945 * smoothstep(0.0, 1.0, (t - 0.45) / 0.55) ** 1.35
    return 0.5 * BEAM * f


def draft(t):
    if t <= 0.58:
        return DRAFT * (0.86 + 0.14 * (t / 0.58))
    return DRAFT * (1.0 - 0.72 * smoothstep(0.0, 1.0, (t - 0.58) / 0.42) ** 1.2)


def deck_z(t):
    s = (2.0 * t - 1.0) ** 2
    return FREEBOARD + SHEER * s + 0.55 * smoothstep(0.55, 1.0, t)


def half_width_at(t, z):
    """Half width (m) at station t and height z (m), or None outside the hull.
    Keel to deck is ship.py's section inverted; deck to rail is the bulwark,
    which stands at the deck edge. The open deck between the bulwarks counts
    as solid - a ball crossing it below the rail meets the far bulwark anyway,
    so whether it HITS is right and only its depth is understated."""
    d, fz = draft(t), deck_z(t)
    hb = half_beam(t)
    if z < -d or z > fz + BULWARK:
        return None
    if z >= fz:
        return hb
    c = max(0.0, min(1.0, ((fz - z) / (d + fz)) ** (1.0 / 1.25)))
    return hb * math.sin(math.acos(c)) ** 1.12


def inside(x, y, z):
    t = x / LENGTH + 0.5
    if t < 0.0 or t > 1.0:
        return False
    w = half_width_at(t, z)
    return w is not None and abs(y) <= w + BALL_R


def march(p, d, step=0.05, reach=32.0):
    n = math.sqrt(sum(c * c for c in d)) or 1.0
    d = [c / n for c in d]
    s = 0.0
    while s <= reach:
        if inside(*[p[i] + d[i] * s for i in range(3)]):
            return s
        s += step
    return None


def zone(x, y, z, lo_cm, hi_cm, win_cm=100.0):
    """ShipPawn::ClassifyHit, the parts a box hit can reach (the rig is decided
    by WHICH COMPONENT was struck, never by position)."""
    if x <= -1350 and abs(y) <= 180 and -270 <= z <= 90:
        return "rudder"
    if abs(y) >= 380 and lo_cm <= z <= hi_cm:
        for px in PORTS_CM:
            if abs(x - px) <= win_cm:
                return "guns"
    return "hull"


def anchor():
    """Values worked on paper before this file was trusted. If ship.py's hull
    changes and this copy does not follow, these fail first."""
    checks = [
        ("half width amidships at the waterline", half_width_at(0.5, 0.0), 3.46, 0.03),
        ("half width amidships at the deck", half_width_at(0.5, 2.0999), 4.16, 0.03),
        ("beam shot at the waterline, box face to timber",
         march([0, 5.2, 0], [0, -1, 0]), 1.67, 0.06),
    ]
    ok = True
    for name, got, want, tol in checks:
        good = got is not None and abs(got - want) <= tol
        ok &= good
        print("anchor %-48s %.2f (paper %.2f) %s" % (name, got or -1, want,
                                                     "ok" if good else "FAIL"))
    if march([15.5, 4, 1], [0.3, -1, 0]) is not None:
        print("anchor a ball raking forward past the stem must miss: FAIL")
        ok = False
    return ok


ZONE_RE = re.compile(r"SHOTLOG zone (\w+) target=(\S+) at=\((-?\d+),(-?\d+),(-?\d+)\)"
                     r".*? dir=\((-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)\)")


def main():
    if not anchor():
        print("\nthe hull copy no longer matches paper; nothing below would mean anything")
        return 1
    import ci_measure
    args = [a for a in sys.argv[1:] if not a.startswith("--band=")]
    band = [a for a in sys.argv[1:] if a.startswith("--band=")]
    lo, hi = (200.0, 350.0)
    if band:
        lo, hi = [float(v) for v in band[0].split("=", 1)[1].split(",")]
    names = (args[0].split(",") if args else
             ["gunnery", "gale", "shot_short", "guns_split", "prize_hull",
              "crew_fight", "crew_repair"])
    rows = []
    for name in names:
        text = ci_measure.run(name, ci_measure.SCENARIOS[name])
        for m in ZONE_RE.finditer(text):
            if m.group(1).lower() in ("forerig", "mainrig"):
                continue
            at = [int(m.group(i)) / 100.0 for i in (3, 4, 5)]
            dr = [float(m.group(i)) for i in (6, 7, 8)]
            rows.append((name, m.group(1).lower(), at, dr))
        print("%-14s done" % name)

    outward = sum(1 for _, _, at, dr in rows if abs(abs(at[1]) - 5.2) < 0.25
                  and at[1] * dr[1] > 0)
    if outward:
        print("\n%d side hits have a direction pointing OUT of the ship: the "
              "instrument is reading the bounce again, and every answer is void"
              % outward)
        return 1

    false_hits, depths, flips, flips_band, mism = 0, [], 0, 0, 0
    print("\nscenario       game    timber  timber+gun-band   box z  timber z")
    for name, logged, at, dr in rows:
        s = march(at, dr)
        bx, by, bz = [c * 100 for c in at]
        if s is None:
            false_hits += 1
            continue
        depths.append(s)
        tx, ty, tz = [(at[i] + dr[i] * s) * 100 for i in range(3)]
        now = zone(bx, by, bz, 0, 240)
        mism += (now != logged)
        pos = zone(tx, ty, tz, 0, 240)
        both = zone(tx, ty, tz, lo, hi)
        flips += (pos != now)
        flips_band += (both != now)
        if now != pos or now != both:
            print("%-14s %-7s %-7s %-15s %6.0f %8.0f" % (name, now, pos, both, bz, tz))
    depths.sort()
    print()
    print("hull-zone hits: %d" % len(rows))
    print("this file's classifier disagrees with the game's log: %d (must be 0)" % mism)
    print("false (the line never meets timber): %d" % false_hits)
    if depths:
        print("box face to timber: median %.2f m, max %.2f m"
              % (depths[len(depths) // 2], depths[-1]))
    print("zone changes at the timber, band as now:            %d" % flips)
    print("zone changes at the timber, band %.0f..%.0f cm:      %d" % (lo, hi, flips_band))
    return 0 if mism == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
