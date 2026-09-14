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


def main():
    print("PirateSeas checks - the ones that do not need Unreal\n")
    files = tracked_files()
    check_tree(files)
    check_textures()
    check_docs()
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
