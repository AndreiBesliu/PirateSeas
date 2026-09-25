"""
Generates the project's sounds. Runs in ordinary Python, NOT in Unreal - the
same rule as textures.py, for the same reason: nothing is downloaded, nothing
carries a licence, and every byte is reproducible from source.

    python Scripts/sounds.py          writes Scripts/Sounds/S_*.wav

The project had NO sound at all until 25.09 - "nu exista niciun sunet" appears
in the handoff, the devlog and the README - so a broadside was a flash, a puff
and a number. Four sounds cover what the guns do:

  S_Cannon   the gun going off: a sub-bass thump under a broadband report,
             both decaying; the long tail is what says "big gun, far away"
  S_Hit      a ball into oak: a short crack with a woody ring
  S_Rig      a ball through rigging: a snap, higher and shorter than the hit
  S_Splash   a ball into the sea: a swell of filtered noise, no attack

44.1 kHz, 16-bit, mono, written by hand (struct) - no dependency for one file.
Every sound is seeded, so the same source gives the same bytes: the suite
compares runs, and art that differs per run would make that comparison compare
art as well as code. tools/ci_checks.py builds them twice and compares.
"""
import math
import os
import struct
import sys

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Sounds")
RATE = 44100


def write_wav(path, samples):
    """samples: float array in [-1, 1]. Peak-normalised to 0.9 so nothing clips
    and every sound sits at the same headroom."""
    peak = float(np.max(np.abs(samples))) or 1.0
    s = np.clip(samples / peak * 0.9, -1.0, 1.0)
    pcm = (s * 32767.0).astype("<i2").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def t_axis(seconds):
    return np.arange(int(RATE * seconds)) / RATE


def lowpass(x, cutoff_hz):
    """One-pole lowpass, run twice for a gentler knee. Enough for noise
    shaping; nothing here needs a real filter design."""
    a = math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    y = np.empty_like(x)
    for _ in range(2):
        acc = 0.0   # per pass; the first version carried it over, so pass two
        # started from pass one's last sample instead of from silence
        for i in range(len(x)):
            acc = a * acc + (1.0 - a) * x[i]
            y[i] = acc
        x = y.copy()
    return y


def cannon(rng):
    t = t_axis(1.6)
    # The report: broadband noise with a fast decay, band-limited so it does
    # not read as a hiss; the sub thump: a tone starting at 60 Hz and falling
    # towards 22 as it decays, the way a big pressure wave does.
    report = lowpass(rng.standard_normal(len(t)), 2200.0) * np.exp(-t * 6.0)
    f = 38.0 * np.exp(-t * 1.2) + 22.0
    thump = np.sin(2.0 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 3.5)
    # A little low rumble that outlasts both: distance.
    rumble = lowpass(rng.standard_normal(len(t)), 180.0) * np.exp(-t * 1.6) * 0.6
    return report * 0.7 + thump * 1.0 + rumble


def hit(rng):
    t = t_axis(0.35)
    crack = lowpass(rng.standard_normal(len(t)), 5000.0) * np.exp(-t * 40.0)
    # The ring of the plank: two damped modes.
    ring = (np.sin(2.0 * np.pi * 410.0 * t) * np.exp(-t * 18.0)
            + 0.5 * np.sin(2.0 * np.pi * 640.0 * t) * np.exp(-t * 26.0))
    return crack * 1.0 + ring * 0.35


def rig(rng):
    t = t_axis(0.25)
    snap = lowpass(rng.standard_normal(len(t)), 9000.0) * np.exp(-t * 55.0)
    # A sweep is the INTEGRAL of its frequency law, as cannon() does it; the
    # first version wrote sin(2*pi*f(t)*t), whose instantaneous pitch is
    # f + t*f'(t), not f.
    f = 1900.0 * np.exp(-t * 12.0) + 300.0
    whip = np.sin(2.0 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 30.0)
    return snap * 0.8 + whip * 0.3


def splash(rng):
    t = t_axis(0.9)
    # No attack: the column of water rises, then the spray falls back.
    env = np.minimum(t / 0.08, 1.0) * np.exp(-(t - 0.08).clip(0.0) * 4.5)
    body = lowpass(rng.standard_normal(len(t)), 1400.0)
    spray = lowpass(rng.standard_normal(len(t)), 6000.0) * np.exp(-t * 7.0) * 0.4
    return (body + spray) * env


SOUNDS = [("S_Cannon", cannon, 11), ("S_Hit", hit, 13),
          ("S_Rig", rig, 17), ("S_Splash", splash, 19)]


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn, seed in SOUNDS:
        path = os.path.join(OUT, name + ".wav")
        write_wav(path, fn(np.random.default_rng(seed)))
        print("SND %-10s %7d bytes" % (name, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
