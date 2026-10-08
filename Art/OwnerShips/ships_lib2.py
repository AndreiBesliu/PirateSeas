
import bpy, math, os
from mathutils import Vector
import ships_lib1
from ships_lib1 import *

THEMES = {
    "pirate":   dict(hull=(0.34, 0.23, 0.14), band=(0.07, 0.065, 0.06), bottom=(0.13, 0.10, 0.07), trim=(0.30, 0.20, 0.12),
                     sail=(0.50, 0.46, 0.40), flag="skull", flag_color=(0.0, 0.0, 0.0)),
    "pirate_red": dict(hull=(0.30, 0.20, 0.12), band=(0.42, 0.07, 0.05), bottom=(0.12, 0.09, 0.07), trim=(0.07, 0.06, 0.05),
                     sail=(0.32, 0.29, 0.26), flag="skull", flag_color=(0.0, 0.0, 0.0)),
    "navy":     dict(hull=(0.075, 0.07, 0.065), band=(0.78, 0.58, 0.22), bottom=(0.36, 0.42, 0.33), trim=(0.78, 0.58, 0.22),
                     sail=(0.93, 0.90, 0.82), flag="striped", flag_color=(0.60, 0.06, 0.06)),
    "merchant": dict(hull=(0.48, 0.34, 0.21), band=(0.10, 0.22, 0.14), bottom=(0.13, 0.10, 0.07), trim=(0.40, 0.28, 0.17),
                     sail=(0.80, 0.70, 0.52), flag="striped", flag_color=(0.10, 0.20, 0.50)),
    "galleon":  dict(hull=(0.36, 0.22, 0.12), band=(0.45, 0.07, 0.045), bottom=(0.13, 0.10, 0.07), trim=None,
                     sail=(0.90, 0.84, 0.70), flag="cross", flag_color=(0.70, 0.08, 0.06)),
}

def get_mats(theme):
    T = THEMES[theme]
    M = {}
    M["hull"] = mat_tex(theme + "_HullPlanks", tex_wood(theme + "_HullPlanks", T["hull"], 8, 11), 0.8)
    M["bottom"] = mat_tex(theme + "_HullBottom", tex_wood(theme + "_HullBottom", T["bottom"], 8, 12, dirt=0.9), 0.75)
    M["band"] = mat_tex(theme + "_HullBand", tex_wood(theme + "_HullBand", T["band"], 8, 13), 0.6)
    if T["trim"] is None:
        M["trim"] = mat_solid("Gilded_Trim", (0.62, 0.46, 0.16), 0.35, 0.85)
    else:
        M["trim"] = mat_tex(theme + "_HullTrim", tex_wood(theme + "_HullTrim", T["trim"], 8, 14), 0.6)
    M["sail"] = mat_tex(theme + "_Sail", tex_canvas(theme + "_Sail", T["sail"], seed=len(theme)), 0.95)
    M["flag"] = mat_tex(theme + "_Flag", tex_flag(theme + "_Flag", T["flag"], T["flag_color"]), 0.9)
    M["deck"] = mat_tex("Deck_Planks", tex_wood("Deck_Planks", (0.62, 0.50, 0.36), 10, 21, dirt=0.6), 0.85)
    M["spar"] = mat_tex("Spar_Wood", tex_wood("Spar_Wood", (0.45, 0.32, 0.19), 4, 31, seams=False), 0.75)
    M["rope"] = mat_solid("Rope", (0.11, 0.085, 0.06), 0.9)
    M["iron"] = mat_solid("Iron", (0.05, 0.05, 0.055), 0.45, 0.8)
    M["dark"] = mat_solid("Dark_Interior", (0.02, 0.015, 0.01), 0.9)
    M["glow"] = mat_solid("Window_Glow", (1.0, 0.72, 0.35), 0.4, 0.0, emit=(1.0, 0.62, 0.25))
    M["gold"] = mat_solid("Gilded_Trim", (0.62, 0.46, 0.16), 0.35, 0.85)
    return M

def make_empty(name, coll, loc=(0, 0, 0), parent=None, size=1.0):
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = "PLAIN_AXES"
    e.empty_display_size = size
    e.location = loc
    coll.objects.link(e)
    if parent:
        e.parent = parent
    return e

def get_collection(name, parent):
    c = bpy.data.collections.get(name)
    if c:
        for ob in list(c.objects):
            data = ob.data
            bpy.data.objects.remove(ob, do_unlink=True)
            if data is not None and getattr(data, "users", 1) == 0:
                if isinstance(data, bpy.types.Mesh):
                    bpy.data.meshes.remove(data)
    else:
        c = bpy.data.collections.new(name)
        parent.children.link(c)
    return c

# ---------------------------------------------------------------- sail shapes
def sq_fn(yb, zt, zb, wt, wb, depth, mode):
    def f(u, v):
        if mode == "furl":
            z = zt - (zt - zb) * 0.05 * (1 - v)
            return Vector(((u - 0.5) * wt * 0.97, yb + 0.15 * (1 - v) + 0.1 * math.sin(math.pi * u), z))
        w = wb + (wt - wb) * v
        x = (u - 0.5) * w
        z = zb + (zt - zb) * v - 0.12 * (zt - zb) * math.sin(math.pi * u) * (1 - v) ** 6
        b = depth * (math.sin(math.pi * u) ** 0.7) * math.sin(math.pi * (1 - v) * 0.85)
        return Vector((x, yb + b, z))
    return f

def gaff_fn(y0, zb, zg, Lb, Lg, rise, depth, mode):
    def f(u, v):
        bot = Vector((0, y0 - u * Lb, zb))
        top = Vector((0, y0 - u * Lg, zg + u * rise))
        if mode == "furl":
            p = bot.lerp(top, v * 0.05)
            p.x = 0.12 * math.sin(math.pi * u)
            return p
        p = bot.lerp(top, v)
        p.x = depth * (math.sin(math.pi * u) ** 0.8) * (math.sin(math.pi * v) ** 0.6)
        return p
    return f

def jib_fn(A, B, C, depth, mode):
    def f(u, v):
        if mode == "furl":
            u = u * 0.04
        p = (A + (B - A) * u) * (1 - v) + C * v
        p.x += depth * (math.sin(math.pi * min(u * 0.85 + 0.0, 1.0)) ** 0.8) * (1 - v) ** 0.6
        return p
    return f

def flag_fn(P, Lf, Hf, phase, amp):
    def f(u, v):
        x = amp * u * math.sin(2 * math.pi * 1.3 * u - phase)
        return Vector((x, P.y + u * Lf, P.z - Hf * (1 - v) - 0.08 * Hf * u * u))
    return f

# ---------------------------------------------------------------- ship
def build_ship(S, export=True):
    name = S["name"]
    M = get_mats(S["theme"])
    top = bpy.data.collections.get("PirateSeas_Ships")
    if top is None:
        top = bpy.data.collections.new("PirateSeas_Ships")
        bpy.context.scene.collection.children.link(top)
    coll = get_collection(name, top)
    root = make_empty(name, coll, size=3)
    rock = make_empty(name + "_Rock", coll, parent=root, size=2)
    HS = HullShape(**S["hull"])
    L = HS.L; sc = L / 30.0
    bands = S.get("bands", [(0.0, 99.0)])
    parts = []

    def levels(t):
        dz = HS.deckz(t); tp = HS.h(t) - 0.2
        lv = [0.0, tp]
        for lo, hi in bands:
            lv += [min(dz + lo, tp), min(dz + hi, tp)]
        return lv

    hull = build_hull(name + "_Hull", HS, [M["hull"], M["bottom"], M["band"], M["trim"]], coll, levels)
    for p in hull.data.polygons:
        c = p.center
        t = clamp(c.y / L)
        tp = HS.h(t) - 0.2; dz = HS.deckz(t)
        if c.z < 0.0:
            mi = 1
        elif c.z > tp:
            mi = 3
        elif any(dz + lo < c.z < min(dz + hi, tp) for lo, hi in bands):
            mi = 2
        else:
            mi = 0
        p.material_index = mi
    parts.append(hull)
    parts.append(build_deck(name + "_Deck", HS, M["deck"], coll))

    # ---- props (indices): 0 deck wood,1 spar,2 iron,3 dark,4 glow,5 trim,6 band
    PM = [M["deck"], M["spar"], M["iron"], M["dark"], M["glow"], M["trim"], M["band"]]
    props = MB()
    masts_spec = sorted(S["masts"], key=lambda m: m["t"])
    mast_ts = [m["t"] for m in masts_spec]

    def free_t(t, gap=0.04):
        return all(abs(t - mt) > gap for mt in mast_ts)

    # quarterdeck / forecastle bulkheads
    if HS.stern_step > 0.3:
        ya, yb = HS.t0 * L, HS.t1 * L
        z0, z1 = HS.deckz(HS.t1) - 0.05, HS.deckz(HS.t0) + 0.03
        xw = HS.at_z(HS.t1, HS.deckz(HS.t1))[0] - 0.22
        props.box((0, (ya + yb) / 2, (z0 + z1) / 2), (2 * xw, yb - ya + 0.05, z1 - z0), mat=6)
        hd = min(1.9, (z1 - z0) - 0.25)
        for sx in (-0.35, 0.35):
            props.box((sx * xw, yb + 0.02, z0 + hd / 2 + 0.05), (0.9, 0.08, hd), mat=3)
        props.box((0, yb + 0.03, z1 - 0.05), (2 * xw, 0.12, 0.12), mat=5)
    if HS.fore_step > 0.3:
        ya, yb = HS.f0 * L, HS.f1 * L
        z0, z1 = HS.deckz(HS.f0) - 0.05, HS.deckz(HS.f1) + 0.03
        xw = HS.at_z(HS.f0, HS.deckz(HS.f0))[0] - 0.22
        props.box((0, (ya + yb) / 2, (z0 + z1) / 2), (2 * xw, yb - ya + 0.05, z1 - z0), mat=6)
        hd = min(1.9, (z1 - z0) - 0.25)
        for sx in (-0.35, 0.35):
            props.box((sx * xw, ya - 0.02, z0 + hd / 2 + 0.05), (0.9, 0.08, hd), mat=3)
        props.box((0, ya - 0.03, z1 - 0.05), (2 * xw, 0.12, 0.12), mat=5)

    # hatches, capstan, barrels
    for t in S.get("hatches", [0.32, 0.5, 0.6]):
        if free_t(t) and HS.t1 + 0.02 < t < HS.f0 - 0.02:
            y = t * L; z = HS.deckz(t)
            props.box((0, y, z + 0.2), (1.5 * sc + 0.4, 1.7 * sc + 0.4, 0.4), mat=0)
            props.box((0, y, z + 0.41), (1.3 * sc + 0.3, 1.5 * sc + 0.3, 0.04), mat=3)
    ct = S.get("capstan", 0.42)
    if free_t(ct, 0.03):
        y = ct * L; z = HS.deckz(ct)
        props.cyl((0, y, z - 0.05), (0, y, z + 0.9), 0.42 * sc + 0.15, 0.36 * sc + 0.12, seg=12, mat=1)
        props.cyl((0, y, z + 0.9), (0, y, z + 1.05), 0.48 * sc + 0.18, None, seg=12, mat=1)
        for k in range(6):
            a = k * math.pi / 3
            props.cyl((0, y, z + 0.8), (math.cos(a) * 1.3 * sc, y + math.sin(a) * 1.3 * sc, z + 0.8), 0.04, None, seg=5, mat=1)
    ft = mast_ts[-1]
    yf = ft * L; zf = HS.deckz(ft)
    for (bx, by) in ((0.9, -1.0), (-0.9, -1.0), (0.0, -1.6), (1.4, -1.6)):
        props.cyl((bx * sc, yf + by * sc, zf), (bx * sc, yf + by * sc, zf + 0.85), 0.3, 0.3, seg=10, mat=1)
        props.cyl((bx * sc, yf + by * sc, zf + 0.25), (bx * sc, yf + by * sc, zf + 0.3), 0.32, None, seg=10, mat=2)
        props.cyl((bx * sc, yf + by * sc, zf + 0.6), (bx * sc, yf + by * sc, zf + 0.65), 0.32, None, seg=10, mat=2)

    # wheel
    tw = HS.t0 * 0.55 if HS.stern_step > 0.3 else 0.09
    yw = tw * L; zw = HS.deckz(tw) + 1.05
    props.box((0, yw + 0.35, zw - 0.5), (0.3, 0.3, 1.05), mat=1)
    props.box((0, yw + 1.3, HS.deckz(tw) + 0.55), (0.55, 0.55, 1.1), mat=1)
    wb = MB()
    R = 0.62
    wb.torus((0, yw, zw), (0, 1, 0), R, 0.035, segs=24, rsegs=6, mat=0)
    wb.cyl((0, yw - 0.12, zw), (0, yw + 0.3, zw), 0.1, None, seg=10, mat=0)
    for k in range(8):
        a = k * math.pi / 4
        d = Vector((math.cos(a), 0, math.sin(a)))
        wb.cyl(Vector((0, yw, zw)) + d * 0.08, Vector((0, yw, zw)) + d * (R + 0.2), 0.025, 0.02, seg=6, mat=0)
    wheel = wb.finish(name + "_Wheel", [M["spar"]], coll, origin=(0, yw, zw))
    parts.append(wheel)

    # rudder (pivot at top of hinge)
    d0 = HS.d(0.0); h0 = HS.h(0.0)
    def yh(z):
        return -HS.rake * (z + d0) / (d0 + h0) - 0.06
    c = 0.045 * L + 0.4
    zt_r = HS.deckz(0.0) - 0.3
    zb_r = -d0 - 0.05
    prof = [(yh(zb_r), zb_r), (yh(zb_r) - c, zb_r + 0.25), (yh(0.3) - 0.85 * c, 0.3),
            (yh(zt_r) - 0.3 * c, zt_r - 0.4), (yh(zt_r), zt_r)]
    th = 0.12 * sc + 0.06
    rb = MB()
    rb.prism_x(prof, -th, th, mat=0)
    rb.cyl((0, yh(-d0 * 0.4) - c * 0.45, -d0 * 0.4 - 0.2), (0, yh(-d0 * 0.4) - c * 0.45, -d0 * 0.4 + 0.2), th + 0.02, None, seg=8, mat=1)
    rudder = rb.finish(name + "_Rudder", [M["hull"], M["iron"]], coll, origin=(0, yh(zt_r), zt_r))
    parts.append(rudder)

    # stern windows + lanterns
    rows = S.get("stern_rows", 1)
    for k in range(rows):
        z = HS.deckz(0.0) - 1.1 + (HS.stern_step * 0.55 if HS.stern_step > 0.3 else 0.0) - k * 2.1
        if z < 1.0:
            break
        xh = HS.at_z(0.0, z)[0]
        n = max(2, int(1.5 * xh / 1.25))
        for i in range(n):
            x = -0.72 * xh + 1.44 * xh * (i + 0.5) / n
            y = yh(z) + 0.02
            props.box((x, y, z), (0.62, 0.22, 0.95), mat=5)
            props.box((x, y - 0.07, z), (0.46, 0.2, 0.8), mat=4)
        props.box((0, yh(z + 0.75) + 0.02, z + 0.65), (1.6 * xh, 0.3, 0.16), mat=5)
    nl = 3 if L > 34 else 1
    zl = HS.h(0.0)
    for i in range(nl):
        x = 0 if nl == 1 else (i - 1) * 1.3 * sc
        y = yh(zl) - 0.1
        props.cyl((x, y + 0.3, zl - 0.1), (x, y - 0.25, zl + 0.6), 0.05, None, seg=6, mat=2)
        props.cyl((x, y - 0.25, zl + 0.6), (x, y - 0.25, zl + 1.25), 0.24, 0.2, seg=8, mat=4)
        props.cyl((x, y - 0.25, zl + 1.25), (x, y - 0.25, zl + 1.5), 0.26, 0.04, seg=8, mat=2)

    # anchors
    ta = 0.88
    for s_ in (-1, 1):
        y = ta * L; xa = s_ * (HS.rimx(ta) + 0.15); z_top = HS.h(ta) - 0.35
        k = 0.8 * sc + 0.3
        zb = z_top - 2.0 * k
        props.cyl((xa, y, z_top), (xa, y, zb), 0.07 * k, None, seg=6, mat=2)
        props.cyl((xa, y - 0.8 * k, z_top - 0.2 * k), (xa, y + 0.8 * k, z_top - 0.2 * k), 0.06 * k, None, seg=6, mat=1)
        props.cyl((xa, y, zb), (xa, y - 0.7 * k, zb + 0.6 * k), 0.06 * k, 0.03 * k, seg=6, mat=2)
        props.cyl((xa, y, zb), (xa, y + 0.7 * k, zb + 0.6 * k), 0.06 * k, 0.03 * k, seg=6, mat=2)
        props.torus((xa, y, z_top + 0.15 * k), (0, 1, 0), 0.15 * k, 0.03 * k, segs=10, rsegs=4, mat=2)

    # ---- guns + ports
    for gi, G in enumerate(S.get("guns", [])):
        size = G.get("size", 1.0)
        drop = G.get("drop", 0.0)
        carriage = drop == 0.0
        n = G["n"]
        for s_, side in ((-1, "Port"), (1, "Starboard")):
            cb = MB()
            for i in range(n):
                t = G["t0"] + (G["t1"] - G["t0"]) * (i / (n - 1) if n > 1 else 0.5)
                dz = HS.deckz(t) - drop
                zc = dz + 0.55 * size
                xp, yp, _ = HS.at_z(t, zc)
                xp = s_ * xp
                props.box((xp, yp, zc), (0.26, 0.82 * size, 0.72 * size), mat=6 if drop > 0 else 5)
                props.box((xp + s_ * 0.025, yp, zc), (0.26, 0.6 * size, 0.52 * size), mat=3)
                mx = xp + s_ * 0.5 * size
                bx = mx - s_ * 2.3 * size
                cb.cyl((bx, yp, zc), (mx, yp, zc), 0.17 * size, 0.12 * size, seg=12, mat=0)
                cb.cyl((mx - s_ * 0.1 * size, yp, zc), (mx, yp, zc), 0.155 * size, 0.155 * size, seg=12, mat=0)
                cb.cyl((bx - s_ * 0.14 * size, yp, zc), (bx, yp, zc), 0.06 * size, 0.13 * size, seg=8, mat=0)
                tx = bx + s_ * 0.85 * size
                cb.cyl((tx, yp - 0.26 * size, zc), (tx, yp + 0.26 * size, zc), 0.05 * size, None, seg=6, mat=0)
                if carriage:
                    cx = bx + s_ * 0.75 * size
                    cb.box((cx, yp, dz + 0.2 * size), (1.3 * size, 0.62 * size, 0.18 * size), mat=1)
                    for sy in (-1, 1):
                        cb.box((cx - s_ * 0.05, yp + sy * 0.25 * size, dz + 0.36 * size), (1.15 * size, 0.08 * size, 0.42 * size), mat=1)
                        for sx in (-1, 1):
                            cb.cyl((cx + sx * 0.42 * size, yp + sy * 0.36 * size, dz + 0.13 * size),
                                   (cx + sx * 0.42 * size, yp + sy * 0.28 * size, dz + 0.13 * size), 0.13 * size, None, seg=10, mat=1)
                mz = make_empty("Muzzle_%s_%d%02d" % (side, gi, i + 1), coll, loc=(mx + s_ * 0.05, yp, zc), parent=rock, size=0.4)
                mz.rotation_euler = (0, 0, 0 if s_ > 0 else math.pi)
            ob = cb.finish("%s_Cannons_%s_%d" % (name, side, gi), [M["iron"], M["spar"]], coll)
            parts.append(ob)

    # ---- masts, yards, sails
    mastsB = MB(); yardsB = MB(); rope = MB()
    rr = 0.028 * sc + 0.012
    def line(p, q, r=rr):
        rope.cyl(p, q, r, r, seg=4, caps=False, smooth=False)
    infos = []
    sail_objs = []
    for mi_, MS in enumerate(masts_spec):
        t = MS["t"]; y = t * L; zd = HS.deckz(t); Hm = MS["h"]
        r0 = MS.get("r", 0.011 * Hm + 0.08)
        I = dict(y=y, zd=zd, r0=r0, Hm=Hm, rig=MS["rig"], y2=y)
        mname = "%s_Mast%d" % (name, mi_ + 1)
        if MS["rig"] == "square":
            z1 = zd + 0.56 * Hm; z2 = zd + 0.83 * Hm; z3 = zd + Hm
            mastsB.cyl((0, y, zd - 1.0), (0, y, z1 + 0.9), r0, r0 * 0.78, seg=12)
            pw = max(1.4, 0.15 * Hm * MS.get("yard", 1.0))
            yardsB.box((0, y + r0 * 0.6, z1 - 0.05), (pw, pw * 0.62, 0.14))
            y2 = y + r0 * 1.4
            mastsB.cyl((0, y2, z1 - 1.2), (0, y2, z2 + 0.7), r0 * 0.6, r0 * 0.45, seg=10)
            yardsB.box((0, y2, z2 - 0.1), (pw * 0.55, 0.3, 0.1))
            mastsB.cyl((0, y2, z2 - 0.8), (0, y2, z3 + 0.3), r0 * 0.36, r0 * 0.18, seg=8)
            Wc = MS.get("yard", 1.0) * HS.W * 2 * 1.5
            Zs = [z1 - 0.05 * Hm, z1 + 0.8 * (z2 - z1), z2 + 0.7 * (z3 - z2)]
            Ws = [Wc, Wc * 0.78, Wc * 0.56]
            Ys = [y + r0 + 0.25, y2 + r0 * 0.6 + 0.2, y2 + r0 * 0.4 + 0.15]
            nlev = MS.get("levels", 3)
            zb_course = zd + max(1.8, 0.1 * Hm)
            for k in range(nlev):
                yr = (0.32 - 0.07 * k) * r0 * 1.6 + 0.06
                yardsB.cyl((-Ws[k] / 2, Ys[k], Zs[k]), (0, Ys[k], Zs[k]), yr * 0.45, yr, seg=8)
                yardsB.cyl((0, Ys[k], Zs[k]), (Ws[k] / 2, Ys[k], Zs[k]), yr, yr * 0.45, seg=8)
                zt = Zs[k] - 0.12
                zb = zb_course if k == 0 else Zs[k - 1] + 0.12
                wt = Ws[k] * 0.93
                wb_ = Ws[0] * 1.0 if k == 0 else Ws[k - 1] * 0.93
                hgt = zt - zb
                yb = Ys[k] + 0.12
                fns = [("Basis", sq_fn(yb, zt, zb, wt, wb_, 0.10 * hgt, "base")),
                       ("Billow", sq_fn(yb, zt, zb, wt, wb_, 0.22 * hgt, "base")),
                       ("Furled", sq_fn(yb, zt, zb, wt, wb_, 0.0, "furl"))]
                so = cloth("%s_Sail_M%d_%s" % (name, mi_ + 1, ["Course", "Topsail", "Topgallant"][k]), fns, 14, 10, (1, 1), M["sail"], coll)
                sail_objs.append(so)
            I.update(z1=z1, z2=z2, z3=z3, pw=pw, y2=y2, Zs=Zs, Ws=Ws, Ys=Ys, nlev=nlev)
        else:
            z3 = zd + Hm; z1 = zd + 0.72 * Hm; z2 = z3 - 0.3
            mastsB.cyl((0, y, zd - 1.0), (0, y, z3), r0, r0 * 0.45, seg=10)
            yardsB.box((0, y, z1 + 0.1), (1.2 * sc + 0.4, 0.25, 0.1))
            I.update(z1=z1, z2=z2, z3=z3, pw=1.2 * sc + 0.4)
        if MS["rig"] == "gaff" or MS.get("spanker"):
            sq = MS["rig"] == "square"
            zb = zd + (2.4 if sq else 1.7)
            zg = (I["Zs"][0] - 0.6) if sq else zd + 0.7 * Hm
            Lb = min(MS.get("boom", 0.32 * L), y + 0.07 * L)
            Lg = Lb * (0.6 if sq else 0.65); rise = 0.42 * Lg
            y0 = y - r0 - 0.1
            yardsB.cyl((0, y0, zb), (0, y - Lb, zb), 0.5 * r0, 0.3 * r0, seg=8)
            yardsB.cyl((0, y0, zg), (0, y0 - Lg, zg + rise), 0.42 * r0, 0.25 * r0, seg=8)
            hg = zg - zb
            fns = [("Basis", gaff_fn(y0 - 0.1, zb + 0.15, zg - 0.1, Lb - 0.3, Lg - 0.2, rise, 0.06 * hg, "base")),
                   ("Billow", gaff_fn(y0 - 0.1, zb + 0.15, zg - 0.1, Lb - 0.3, Lg - 0.2, rise, 0.13 * hg, "base")),
                   ("Furled", gaff_fn(y0 - 0.1, zb + 0.15, zg - 0.1, Lb - 0.3, Lg - 0.2, rise, 0.0, "furl"))]
            sail_objs.append(cloth("%s_Sail_M%d_Gaff" % (name, mi_ + 1), fns, 12, 12, (1, 1), M["sail"], coll))
            line((0, y - Lb, zb), (0, y0 - Lg, zg + rise), rr * 0.7)
        infos.append(I)

    # bowsprit + jibs
    fm = infos[-1]
    bow_y = L * (1 + HS.bow_over) - 0.2; bow_z = HS.h(1.0) - 0.35
    ang = math.radians(S.get("bowsprit_angle", 20))
    dirv = Vector((0, math.cos(ang), math.sin(ang)))
    base = Vector((0, bow_y, bow_z))
    Lbs = S.get("bowsprit", 0.33) * L
    tip = base + dirv * Lbs
    yardsB.cyl(base - dirv * 3.0, tip, fm["r0"] * 0.75, fm["r0"] * 0.3, seg=10)
    line(tip, (0, L * (1 + HS.bow_over * 0.3), 0.4), rr * 1.2)
    nj = S.get("jibs", 2)
    for j in range(nj):
        fa = 0.45 + 0.52 * (j / (nj - 1) if nj > 1 else 0.5)
        A = base + dirv * (Lbs * fa)
        if fm["rig"] == "square":
            Cz = fm["z1"] - 0.3 if j == 0 else fm["z2"] - 0.2 * j
        else:
            Cz = fm["zd"] + (0.88 - 0.05 * (nj - 1 - j)) * fm["Hm"]
        C = Vector((0, fm["y2"] + 0.2, Cz))
        Bc = Vector((0, fm["y"] + 0.3 * (A.y - fm["y"]) + 0.8, max(A.z, fm["zd"] + 2.4) + 0.35 * j))
        line(A, C)
        dep = 0.06 * (C - A).length
        fns = [("Basis", jib_fn(A, Bc, C, dep, "base")), ("Billow", jib_fn(A, Bc, C, dep * 2.0, "base")),
               ("Furled", jib_fn(A, Bc, C, 0.0, "furl"))]
        sail_objs.append(cloth("%s_Sail_Jib%d" % (name, j + 1), fns, 10, 12, (1, 1), M["sail"], coll))
    line((0, fm["y2"], fm["z1"] - 0.2), base + dirv * 0.5, rr * 1.3)

    # shrouds, ratlines, stays
    for idx, I in enumerate(infos):
        y = I["y"]; topz = I["z1"] - 0.2
        n = 3 + (I["Hm"] > 22) + (I["Hm"] > 34)
        sp = 0.75 * sc + 0.3
        for s_ in (-1, 1):
            mp = Vector((s_ * I["r0"] * 0.9, y, topz))
            pts = []
            for i in range(n):
                yi = y + 0.3 * sp - i * sp
                ti = clamp(yi / L)
                dp = Vector((s_ * (HS.rimx(ti) + 0.22 * sc + 0.1), yi, HS.h(ti) - 0.35))
                line(mp, dp, rr * 1.15)
                pts.append(dp)
                props.box((dp.x - s_ * 0.05, yi, dp.z + 0.05), (0.25, 0.9 * sp, 0.12), mat=1)
            step = 0.42 * sc + 0.18
            z = max(p.z for p in pts) + 0.6
            while z < topz - 1.2:
                for a, b in zip(pts[:-1], pts[1:]):
                    pa = a.lerp(mp, (z - a.z) / (mp.z - a.z))
                    pb = b.lerp(mp, (z - b.z) / (mp.z - b.z))
                    line(pa, pb, rr * 0.45)
                z += step
            if I["rig"] == "square":
                for k in range(3):
                    line((s_ * I["r0"] * 0.5, I["y2"], I["z2"] - 0.2), (s_ * I["pw"] / 2, y + I["r0"] * 0.6 + (k - 1) * I["pw"] * 0.25, I["z1"]), rr * 0.8)
                line((s_ * 0.1, I["y2"], I["z3"] - 0.3), (s_ * I["pw"] * 0.27, I["y2"], I["z2"] - 0.1), rr * 0.6)
                tb = clamp((y - n * sp - 1.5 * sc) / L)
                line((s_ * I["r0"] * 0.5, I["y2"], I["z2"]), (s_ * (HS.rimx(tb) + 0.2), tb * L, HS.h(tb) - 0.35), rr)
                tb2 = clamp(tb - 0.04)
                line((s_ * 0.1, I["y2"], I["z3"] - 0.2), (s_ * (HS.rimx(tb2) + 0.2), tb2 * L, HS.h(tb2) - 0.35), rr * 0.8)
            else:
                tb = clamp((y - n * sp - 1.5 * sc) / L)
                line((s_ * 0.1, y, I["z3"] - 0.3), (s_ * (HS.rimx(tb) + 0.2), tb * L, HS.h(tb) - 0.35), rr)
        if idx + 1 < len(infos):
            F = infos[idx + 1]
            line((0, I["y2"], I["z1"] - 0.3), (0, F["y"] + 0.8, F["zd"] + 1.2), rr * 1.2)
            if I["rig"] == "square" and F["rig"] == "square":
                line((0, I["y2"], I["z2"] - 0.1), (0, F["y2"], F["z1"] + 0.1), rr)
                line((0, I["y2"], I["z3"] - 0.3), (0, F["y2"], F["z2"]), rr * 0.8)
            else:
                line((0, I["y2"], I["z3"] - 0.4), (0, F["y2"], F["z3"] - 0.6), rr * 0.8)

    # flag on main (tallest) mast
    MI = max(infos, key=lambda i: i["Hm"])
    Lf = clamp(0.1 * L, 2.0, 5.5); Hf = 0.62 * Lf
    yardsB.cyl((0, MI["y2"], MI["z3"]), (0, MI["y2"], MI["z3"] + Hf + 0.5), 0.07, 0.05, seg=6)
    P = Vector((0, MI["y2"] + 0.1, MI["z3"] + Hf + 0.4))
    ffns = [("Basis", flag_fn(P, Lf, Hf, 0, 0.0))]
    for k in range(4):
        ffns.append(("Wave_%d" % (k * 90), flag_fn(P, Lf, Hf, k * math.pi / 2, 0.16 * Lf)))
    flag = cloth(name + "_Flag", ffns, 16, 6, (1, 1), M["flag"], coll, thick=0.015)
    for uvl in flag.data.uv_layers:
        pass

    parts.append(mastsB.finish(name + "_Masts", [M["spar"]], coll, swap=True, tile=2.0))
    parts.append(yardsB.finish(name + "_Yards", [M["spar"]], coll, tile=2.0))
    parts.append(rope.finish(name + "_Rigging", [M["rope"]], coll, recalc=False, uv=False))
    parts.append(props.finish(name + "_DeckProps", PM, coll))
    parts += sail_objs
    parts.append(flag)
    for p in parts:
        if p is not None and p.parent is None:
            p.parent = rock

    # ---- animation (240 frames @ 24fps, seamless loop)
    sce = bpy.context.scene
    sce.render.fps = 24; sce.frame_start = 1; sce.frame_end = 240
    k_ = (25.0 / L) ** 0.5
    for f in range(1, 242, 4):
        ph = 2 * math.pi * (f - 1) / 240.0
        rock.rotation_euler = (math.radians(1.4 * k_) * math.sin(3 * ph + 0.7), math.radians(3.2 * k_) * math.sin(2 * ph), 0.0)
        rock.location = (0, 0, 0.18 * (L / 25.0) ** 0.3 * math.sin(4 * ph + 1.3))
        rock.keyframe_insert("rotation_euler", frame=f)
        rock.location.z
        rock.keyframe_insert("location", frame=f)
    for si, so in enumerate(sail_objs):
        kb = so.data.shape_keys.key_blocks["Billow"]
        for f in range(1, 242, 8):
            ph = 2 * math.pi * (f - 1) / 240.0
            kb.value = 0.72 + 0.28 * math.sin(3 * ph + si * 0.9)
            kb.keyframe_insert("value", frame=f)
    kbs = flag.data.shape_keys.key_blocks
    for f in range(1, 242, 2):
        ph = 2 * math.pi * (f - 1) / 20.0
        c_, s2 = math.cos(ph), math.sin(ph)
        vals = {"Wave_0": max(0, c_), "Wave_90": max(0, s2), "Wave_180": max(0, -c_), "Wave_270": max(0, -s2)}
        for kname, val in vals.items():
            kbs[kname].value = val
            kbs[kname].keyframe_insert("value", frame=f)
    sce.frame_set(1)

    # stats
    dg = bpy.context.evaluated_depsgraph_get()
    tris = 0
    for ob in coll.objects:
        if ob.type == "MESH":
            tris += sum(len(p.vertices) - 2 for p in ob.data.polygons)
    res = {"name": name, "objects": len(coll.objects), "tris": tris, "sails": len(sail_objs)}
    if export:
        res["fbx"] = export_fbx(root)
    return root, res

def export_fbx(root):
    import addon_utils
    try:
        addon_utils.enable("io_scene_fbx", default_set=True)
    except Exception:
        pass
    fdir = os.path.join(OUT_DIR, "FBX")
    os.makedirs(fdir, exist_ok=True)
    path = os.path.join(fdir, root.name + ".fbx")
    old = root.location.copy()
    root.location = (0, 0, 0)
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    root.select_set(True)
    for o in root.children_recursive:
        o.select_set(True)
    bpy.context.view_layer.objects.active = root
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"EMPTY", "MESH"},
                             apply_scale_options="FBX_SCALE_ALL", axis_forward="Z", axis_up="Y",
                             bake_space_transform=True, use_mesh_modifiers=True, mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=False,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.5,
                             path_mode="COPY", embed_textures=True)
    root.location = old
    return path
