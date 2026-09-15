"""
Compares a RECTANGLE of two captures, in numbers.

    python tools/png_diff.py a.png b.png x0 y0 x1 y1

Written because "the palms move in the wind" is exactly the kind of claim this
project keeps getting wrong by looking at it. Two captures a second apart, a box
drawn round the hillside and nothing else, and the answer is a percentage rather
than an impression: if the plants are still, the box is identical to the byte.

No Pillow, no downloads - the same rule the texture generator follows. PNG is
zlib plus five filter types, and reading one is thirty lines.
"""
import io
import struct
import sys
import zlib

import numpy as np


def read_png(path):
    """Returns an (h, w, channels) uint8 array. Handles the 8-bit RGB/RGBA,
    non-interlaced files Unreal's screenshot writer produces, and refuses
    anything else out loud rather than returning something plausible."""
    raw = io.open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos, idat, w, h, bits, colour = 8, [], 0, 0, 0, 0
    while pos < len(raw):
        (length,) = struct.unpack(">I", raw[pos:pos + 4])
        kind = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, bits, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bits != 8 or colour not in (2, 6) or interlace:
                raise ValueError("%s: %d-bit colour type %d interlace %d is not "
                                 "what this reads" % (path, bits, colour, interlace))
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length

    data = zlib.decompress(b"".join(idat))
    ch = 3 if colour == 2 else 4
    stride = w * ch
    out = np.zeros((h, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.uint8)
    at = 0
    for y in range(h):
        f = data[at]
        line = np.frombuffer(data[at + 1:at + 1 + stride], dtype=np.uint8).astype(np.int32)
        at += 1 + stride
        cur = np.zeros(stride, dtype=np.int32)
        for x in range(stride):
            a = cur[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 0:
                v = line[x]
            elif f == 1:
                v = line[x] + a
            elif f == 2:
                v = line[x] + b
            elif f == 3:
                v = line[x] + (a + b) // 2
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                v = line[x] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else c))
            else:
                raise ValueError("%s: filter %d on row %d" % (path, f, y))
            cur[x] = v & 0xFF
        out[y] = cur.astype(np.uint8)
        prev = cur.astype(np.uint8)
    return out.reshape(h, w, ch)


def main():
    if len(sys.argv) != 7:
        print(__doc__)
        return 2
    a = read_png(sys.argv[1]).astype(np.int32)
    b = read_png(sys.argv[2]).astype(np.int32)
    if a.shape != b.shape:
        print("FAIL the two captures are different sizes: %s vs %s"
              % (a.shape, b.shape))
        return 1
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7])
    ca, cb = a[y0:y1, x0:x1, :3], b[y0:y1, x0:x1, :3]
    if ca.size == 0:
        print("FAIL the box is empty")
        return 1
    d = np.abs(ca - cb)
    moved = float((d.max(axis=2) > 6).mean()) * 100.0
    print("PNGDIFF box=(%d,%d)-(%d,%d) pixels=%d changed=%.2f%% mean=%.2f max=%d"
          % (x0, y0, x1, y1, ca.shape[0] * ca.shape[1], moved,
             float(d.mean()), int(d.max())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
