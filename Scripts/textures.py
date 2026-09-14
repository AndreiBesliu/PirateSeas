"""
Generates the project's textures. Runs in ordinary Python, NOT in Unreal.

The project had no textures at all: every surface was one flat linear colour
imported from Blender, which is exactly what makes it read as plasticine. A
texture here is never downloaded - it is synthesised, so the whole look stays
reproducible from source and nothing in the tree has a licence attached to it.

Everything is made to TILE EXACTLY. The trick is to build the noise in the
frequency domain and transform back: a spectrum on integer frequencies has the
image size as its period by construction, so the seam cannot exist. Tiling by
mirroring or by cross-fading a random field would leave a visible repeat, and a
repeat across a ten-kilometre sea is worse than no detail at all.

Written as 8-bit PNG by hand (zlib + struct) because Pillow is not installed
and one dependency for one file is not worth it.
"""
import io
import math
import os
import struct
import sys
import zlib

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Textures")


# ----------------------------------------------------------------- PNG out
def write_png(path, arr):
    """arr: HxW (grey), HxWx3 (rgb) or HxWx4, uint8."""
    if arr.ndim == 2:
        colour, chans = 0, 1
    elif arr.shape[2] == 3:
        colour, chans = 2, 3
    elif arr.shape[2] == 4:
        colour, chans = 6, 4
    else:
        raise ValueError("channels")
    h, w = arr.shape[0], arr.shape[1]
    raw = arr.reshape(h, w * chans)
    # PNG wants a filter byte per scanline; 0 = None.
    lines = np.concatenate(
        [np.zeros((h, 1), np.uint8), raw.astype(np.uint8)], axis=1)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, colour, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(lines.tobytes(), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return path


# ----------------------------------------------------------------- noise
def periodic_noise(n, rng, falloff=2.0, lo=2, hi=None, aniso=None):
    """A tiling scalar field, from a spectrum with |k|^-falloff amplitude.

    lo/hi bound the frequencies in cycles across the image, so the SIZE of the
    features is set here rather than discovered by trial. aniso, when given as
    (dirx, diry, strength), stretches the spectrum along a direction - which is
    how wind-blown ripples get their grain instead of looking like porridge."""
    hi = hi or n // 2
    ky = np.fft.fftfreq(n, 1.0 / n)[:, None]
    kx = np.fft.rfftfreq(n, 1.0 / n)[None, :]
    k = np.sqrt(kx * kx + ky * ky)
    amp = np.where((k >= lo) & (k <= hi), np.power(np.maximum(k, 1e-6), -falloff), 0.0)
    if aniso:
        dx, dy, strength = aniso
        norm = np.maximum(k, 1e-6)
        along = (kx * dx + ky * dy) / norm
        # Energy kept ACROSS the wind direction: ripples run in lines square to
        # the breeze, so the crests are long the way real cat's paws are.
        amp = amp * (1.0 + strength * (1.0 - np.abs(along)))
    phase = rng.uniform(0.0, 2.0 * math.pi, amp.shape)
    spec = amp * (np.cos(phase) + 1j * np.sin(phase))
    img = np.fft.irfft2(spec, s=(n, n))
    img -= img.mean()
    m = np.abs(img).max()
    return img / m if m > 0 else img


def fbm(n, rng, octaves=5, falloff=2.2, lo=3):
    out = np.zeros((n, n))
    amp, freq = 1.0, lo
    for _ in range(octaves):
        out += amp * periodic_noise(n, rng, falloff, freq, freq * 2)
        amp *= 0.55
        freq *= 2
    out -= out.min()
    return out / max(out.max(), 1e-6)


def normal_from_height(height, strength):
    """Central differences, wrapped, so the normal map tiles as well as the
    height it came from."""
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
    nx, ny = -dx * strength, -dy * strength
    nz = np.ones_like(nx)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    n = np.stack([nx * inv, ny * inv, nz * inv], axis=2)
    return np.clip(n * 0.5 + 0.5, 0, 1)


def must_divide(period, n, what):
    """A structured pattern tiles only if its period divides the tile exactly.

    That is arithmetic, not a matter of degree, so it is asserted here rather
    than measured in the output. Three different statistical seam tests were
    tried on the images and each was wrong in its own direction - one accused
    correct plank textures, one passed a deliberately broken canvas, one
    accused nearly everything. The property is exact; check it exactly."""
    if n % period != 0:
        raise AssertionError(
            "%s: period %s does not divide the %s-pixel tile, so the pattern "
            "cannot wrap" % (what, period, n))


def u8(a):
    return np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)


# ----------------------------------------------------------------- water
def water(n=1024):
    rng = np.random.default_rng(7)
    made = []

    # Two ripple sheets at different scales, panned at different speeds in the
    # material. One sheet alone reads as a repeating pattern the moment the
    # camera moves; two, crossing, do not.
    for i, (lo, hi, strength, seed) in enumerate(
            [(6, 48, 2.6, 7), (24, 160, 1.5, 11)]):
        r = np.random.default_rng(seed)
        h = periodic_noise(n, r, falloff=1.8, lo=lo, hi=hi,
                           aniso=(1.0, 0.35, 1.4))
        made.append(write_png(os.path.join(OUT, "T_WaterRipple%d_N.png" % i),
                              u8(normal_from_height(h, strength))))

    # Foam: bubbles, not cloud. High-frequency noise pushed through a curve
    # that keeps the bright cells and crushes the rest, so it breaks into
    # clumps with holes instead of a grey wash.
    f = fbm(n, np.random.default_rng(23), octaves=6, falloff=2.0, lo=4)
    f = np.clip((f - 0.42) / 0.45, 0, 1) ** 0.75
    made.append(write_png(os.path.join(OUT, "T_Foam_M.png"), u8(f)))
    return made


# ----------------------------------------------------------------- timber
def timber(n=1024):
    """Planking, for hull and deck. Grain plus seams, in one albedo and one
    normal, with roughness carried in the alpha of the albedo so the material
    needs two samples rather than three."""
    rng = np.random.default_rng(3)
    y = np.arange(n)[:, None] * np.ones((1, n))
    x = np.ones((n, 1)) * np.arange(n)[None, :]

    PLANKS = 16
    must_divide(PLANKS, n, "timber planks")
    pw = n / PLANKS
    plank_i = np.floor(y / pw)
    edge = np.minimum(y % pw, pw - (y % pw))
    # The seam: a dark line a couple of pixels wide, and a soft shadow beside it.
    seam = np.clip(edge / 3.0, 0, 1)
    seam_soft = np.clip(edge / 14.0, 0, 1) * 0.35 + 0.65

    # Grain runs ALONG the plank, so the spectrum is stretched hard that way.
    grain = periodic_noise(n, rng, falloff=1.5, lo=3, hi=220,
                           aniso=(1.0, 0.0, 6.0))
    grain = 0.5 + 0.5 * grain

    # Every plank a slightly different timber, and staggered butt joints, so
    # sixteen strakes do not read as one striped sheet.
    shade = (rng.uniform(0.78, 1.18, PLANKS))[plank_i.astype(int) % PLANKS]
    # BUTTS must be an integer number of joints across the tile, or the pattern
    # does not close: `(x/n + phase) % 0.5` walks 0 -> 0.999 across the image
    # and lands half a period away from where it started, which puts a hard
    # line down the seam. The CI tiling check measured it at 8.5 times an
    # ordinary pixel step on the albedo and 13 on the normal - a visible stripe
    # repeating every tile along a thirty-metre hull.
    BUTTS = 2
    butt_phase = rng.uniform(0, 1, PLANKS)[plank_i.astype(int) % PLANKS]
    butt_at = ((x / n) * BUTTS + butt_phase) % 1.0
    butt = np.clip(np.abs(butt_at - 0.5) * 120.0, 0, 1) * 0.4 + 0.6

    tone = (0.55 + 0.45 * grain) * shade * seam_soft
    tone = np.clip(tone, 0, 1.6)

    # Linear albedo, dark oiled oak. Kept dark on purpose: a sunlit hull that
    # is already bright in the texture blows out the moment the sun hits it.
    base = np.stack([tone * 0.150, tone * 0.082, tone * 0.042], axis=2)
    base *= np.clip(seam * 0.75 + 0.25, 0, 1)[:, :, None]
    base *= butt[:, :, None]
    albedo = u8(np.clip(base, 0, 1) ** (1 / 2.2))       # sRGB for the importer
    write_png(os.path.join(OUT, "T_Timber_C.png"), albedo)

    h = grain * 0.35 + seam * 0.8 + butt * 0.25
    write_png(os.path.join(OUT, "T_Timber_N.png"),
              u8(normal_from_height(h, 3.2)))

    # Rough where the grain is open and along the seams, smoother on the
    # varnished face.
    rough = 0.62 + 0.22 * (1.0 - grain) + 0.14 * (1.0 - seam)
    write_png(os.path.join(OUT, "T_Timber_R.png"), u8(np.clip(rough, 0, 1)))


# ----------------------------------------------------------------- canvas
def canvas(n=1024):
    """Sailcloth: a woven grid, panel seams across the cloth, and blotching
    from salt and weather. Flat cream is what made the sails read as card."""
    rng = np.random.default_rng(5)
    y = np.arange(n)[:, None] * np.ones((1, n))
    x = np.ones((n, 1)) * np.arange(n)[None, :]

    # The weave, fine enough to be felt rather than seen.
    WEAVE = 128
    must_divide(WEAVE, n, "canvas weave")
    weave = (np.sin(x * math.pi * 2 * WEAVE / n) * np.sin(y * math.pi * 2 * WEAVE / n))
    weave = 0.5 + 0.5 * weave

    # Cloths are sewn from strips about a yard wide; the seam is a raised
    # double-stitched band, and it is the detail that says "sail" fastest.
    # Eight, not seven: the strip width has to divide the tile exactly or the
    # seams do not meet across the wrap. 1024/7 is 146.28..., and the CI check
    # caught the 4.4x step it left down the edge.
    STRIPS = 8
    must_divide(STRIPS, n, "canvas strips")
    sw = n / STRIPS
    d = np.minimum(x % sw, sw - (x % sw))
    seam = np.clip(d / 6.0, 0, 1)
    STITCH = 64          # was 90, which does not divide 1024
    must_divide(STITCH, n, "canvas stitching")
    stitch = (np.abs(np.sin(y * math.pi * 2 * STITCH / n)) > 0.55) & (d < 9)

    # Weather, but fine-grained. The first pass used low frequencies and put a
    # single dark bruise the size of a cloth on every sail; in the capture it
    # read as a stain on plastic, not as canvas. High frequencies only, and
    # half the amplitude.
    weather = fbm(n, rng, octaves=4, falloff=2.0, lo=14)
    tone = 0.84 + 0.16 * weave - 0.075 * weather - 0.14 * (1.0 - seam)
    tone = np.clip(tone, 0.35, 1.0)

    base = np.stack([tone * 0.62, tone * 0.565, tone * 0.455], axis=2)
    write_png(os.path.join(OUT, "T_Canvas_C.png"),
              u8(np.clip(base, 0, 1) ** (1 / 2.2)))

    # Seam and stitch carry the relief; the weave is felt, not seen.
    h = weave * 0.14 + (1.0 - seam) * 1.5 + stitch * 0.9 + weather * 0.10
    write_png(os.path.join(OUT, "T_Canvas_N.png"),
              u8(normal_from_height(h, 4.0)))


# ----------------------------------------------------------------- smoke
def smoke(n=512):
    """Billowing noise for gun smoke, in two channels.

    R: soft low-frequency billows, the shape of the cloud.
    G: sharper high-frequency curd, the froth that breaks the rim up.

    Named _D for DATA, not _M for mask: the importer maps _M to TC_GRAYSCALE,
    which keeps one channel and throws the other away. A two-channel texture
    under that suffix loses half of itself silently, and the symptom is not
    "the green channel is missing" - it is smoke that renders as flat slabs,
    because the erosion mask it was supposed to drive has no variation left.

    Both are used as an EROSION mask, not as an opacity: the card's alpha is
    (noise - threshold) rescaled, and the threshold climbs with age. Fading a
    card's alpha to zero gives a ghost that goes see-through; eroding it against
    noise makes the rim break into holes and wisps and die ragged, which is what
    smoke actually does."""
    billow = fbm(n, np.random.default_rng(67), octaves=5, falloff=2.4, lo=2)
    curd = fbm(n, np.random.default_rng(71), octaves=6, falloff=1.9, lo=6)
    # Stretched a little, so the curd has a grain instead of being porridge.
    grain = 0.5 + 0.5 * periodic_noise(n, np.random.default_rng(73),
                                       falloff=1.6, lo=14, hi=120,
                                       aniso=(1.0, 0.4, 1.8))
    b = np.clip(billow * 1.15, 0, 1)
    c = np.clip(curd * 0.75 + grain * 0.35, 0, 1)
    write_png(os.path.join(OUT, "T_Smoke_D.png"),
              np.stack([u8(b), u8(c), u8(b * c)], axis=2))


# ----------------------------------------------------------------- macro
def macro(n=512):
    """Large-scale blotching, tiled at tens of metres, to be multiplied over
    ground albedo.

    Without it an island seen from two hundred metres is one flat green: the
    turf texture tiles every seven metres, which at that range is well under a
    pixel, so it averages out to its own mean colour. Detail that vanishes with
    distance has to be paired with detail that does not."""
    f = fbm(n, np.random.default_rng(61), octaves=5, falloff=2.3, lo=2)
    f = 0.5 + 0.5 * (f - f.mean()) / max(f.std(), 1e-6) * 0.45
    write_png(os.path.join(OUT, "T_Macro_M.png"), u8(np.clip(f, 0, 1)))


# ----------------------------------------------------------------- rope
def rope(n=512):
    """Tarred hemp: three strands laid up in a right-hand spiral.

    A rope is 3-7 cm thick and will never be more than a few pixels across, so
    what matters is not the detail but that the light CATCHES the lay - a
    perfectly smooth dark cylinder reads as a wire, and a ship rigged with
    wires looks wrong in a way nobody can name."""
    rng = np.random.default_rng(53)
    y = np.arange(n)[:, None] * np.ones((1, n))
    x = np.ones((n, 1)) * np.arange(n)[None, :]

    # The lay: three strands, wound so the pattern repeats an integer number of
    # times across the tile in BOTH axes, or the seam shows as a kink.
    STRANDS, TURNS = 4, 8
    must_divide(STRANDS, n, "rope strands")
    must_divide(TURNS, n, "rope lay")
    phase = (x / n) * STRANDS + (y / n) * TURNS
    lay = np.abs(((phase % 1.0) - 0.5) * 2.0)          # 0 at strand centre
    round_off = np.cos(lay * math.pi * 0.5) ** 0.6      # each strand rounded

    fibre = periodic_noise(n, rng, falloff=1.4, lo=60, hi=300,
                           aniso=(1.0, 2.0, 2.5))
    tone = 0.55 + 0.45 * round_off + 0.10 * fibre
    tone = np.clip(tone, 0.2, 1.2)

    base = np.stack([tone * 0.055, tone * 0.043, tone * 0.032], axis=2)
    write_png(os.path.join(OUT, "T_Rope_C.png"),
              u8(np.clip(base, 0, 1) ** (1 / 2.2)))
    write_png(os.path.join(OUT, "T_Rope_N.png"),
              u8(normal_from_height(round_off * 0.9 + fibre * 0.2, 3.0)))


# ----------------------------------------------------------------- iron
def iron(n=512):
    rng = np.random.default_rng(13)
    pit = fbm(n, rng, octaves=6, falloff=1.9, lo=6)
    rust = fbm(n, np.random.default_rng(17), octaves=4, falloff=2.6, lo=2)
    rust = np.clip((rust - 0.55) * 3.0, 0, 1)

    dark = 0.030 + 0.020 * pit
    base = np.stack([dark, dark * 0.97, dark * 0.95], axis=2)
    # Rust is not metal: it goes brown AND it kills the reflection, which the
    # material handles by pulling metallic down wherever this mask is high.
    base[:, :, 0] = base[:, :, 0] * (1 - rust) + 0.115 * rust
    base[:, :, 1] = base[:, :, 1] * (1 - rust) + 0.048 * rust
    base[:, :, 2] = base[:, :, 2] * (1 - rust) + 0.022 * rust
    write_png(os.path.join(OUT, "T_Iron_C.png"),
              u8(np.clip(base, 0, 1) ** (1 / 2.2)))
    write_png(os.path.join(OUT, "T_Iron_N.png"),
              u8(normal_from_height(pit * 0.5 + rust * 0.3, 1.8)))
    write_png(os.path.join(OUT, "T_Iron_M.png"), u8(rust))


# ----------------------------------------------------------------- shore
def shore(n=1024):
    """Sand, rock and turf for the island, each tiling, plus the mask that
    says which is which is computed in the material from slope and height -
    not painted here, because the island is generated and has no UV story."""
    rng = np.random.default_rng(29)

    grains = periodic_noise(n, rng, falloff=1.2, lo=40, hi=380)
    ripple = periodic_noise(n, np.random.default_rng(31), falloff=1.9,
                            lo=5, hi=26, aniso=(0.3, 1.0, 2.2))
    sand_h = grains * 0.35 + ripple * 0.65
    tone = 0.80 + 0.20 * (0.5 + 0.5 * sand_h)
    write_png(os.path.join(OUT, "T_Sand_C.png"),
              u8(np.clip(np.stack([tone * 0.62, tone * 0.52, tone * 0.375], 2), 0, 1) ** (1 / 2.2)))
    write_png(os.path.join(OUT, "T_Sand_N.png"),
              u8(normal_from_height(sand_h, 2.0)))

    crack = fbm(n, np.random.default_rng(37), octaves=6, falloff=1.6, lo=3)
    strata = 0.5 + 0.5 * periodic_noise(n, np.random.default_rng(41),
                                        falloff=2.2, lo=4, hi=40,
                                        aniso=(0.0, 1.0, 5.0))
    rock_t = 0.55 + 0.45 * crack * strata
    write_png(os.path.join(OUT, "T_Rock_C.png"),
              u8(np.clip(np.stack([rock_t * 0.115, rock_t * 0.105, rock_t * 0.098], 2), 0, 1) ** (1 / 2.2)))
    write_png(os.path.join(OUT, "T_Rock_N.png"),
              u8(normal_from_height(crack * 0.8 + strata * 0.5, 4.0)))

    clump = fbm(n, np.random.default_rng(43), octaves=6, falloff=2.0, lo=5)
    blade = periodic_noise(n, np.random.default_rng(47), falloff=1.3,
                           lo=60, hi=300, aniso=(0.25, 1.0, 3.0))
    gt = 0.55 + 0.45 * clump + 0.14 * blade
    write_png(os.path.join(OUT, "T_Turf_C.png"),
              u8(np.clip(np.stack([gt * 0.062, gt * 0.105, gt * 0.038], 2), 0, 1) ** (1 / 2.2)))
    write_png(os.path.join(OUT, "T_Turf_N.png"),
              u8(normal_from_height(clump * 0.7 + blade * 0.4, 3.0)))


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    jobs = {"water": water, "timber": timber, "canvas": canvas,
            "iron": iron, "shore": shore, "rope": rope, "macro": macro, "smoke": smoke}
    for name, fn in jobs.items():
        if which in ("all", name):
            fn()
            print("TEX built %s" % name)
    for f in sorted(os.listdir(OUT)):
        p = os.path.join(OUT, f)
        print("TEX %-22s %8d bytes" % (f, os.path.getsize(p)))


if __name__ == "__main__":
    main()
