
import bpy, bmesh, math, os
import numpy as np
from mathutils import Vector, Matrix

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
TEX_DIR = os.path.join(OUT_DIR, "textures")
os.makedirs(TEX_DIR, exist_ok=True)

def sstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)

def clamp(x, a=0.0, b=1.0):
    return min(max(x, a), b)

# ---------------------------------------------------------------- textures
def _noise(h, w, gh, gw, rng):
    g = rng.random((gh + 1, gw + 1)).astype(np.float32)
    g[gh, :] = g[0, :]
    g[:, gw] = g[:, 0]
    ys = np.linspace(0, gh, h, endpoint=False)
    xs = np.linspace(0, gw, w, endpoint=False)
    y0 = ys.astype(int); x0 = xs.astype(int)
    fy = (ys - y0)[:, None]; fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    a = g[y0][:, x0]; b = g[y0][:, x0 + 1]; c = g[y0 + 1][:, x0]; d = g[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy).astype(np.float32)

def _fbm(size, gh, gw, rng, octaves=4):
    out = np.zeros((size, size), np.float32); amp = 1.0; tot = 0.0
    for _ in range(octaves):
        out += amp * _noise(size, size, gh, gw, rng)
        tot += amp; amp *= 0.5; gh *= 2; gw *= 2
    return out / tot

def _to_img(name, rgb):
    h, w, _ = rgb.shape
    rgba = np.ones((h, w, 4), np.float32)
    rgba[..., :3] = np.clip(rgb, 0.0, 1.0)
    rgba = np.ascontiguousarray(np.flipud(rgba))
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old)
    img = bpy.data.images.new(name, w, h, alpha=False)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = os.path.join(TEX_DIR, name + ".png")
    img.file_format = "PNG"
    img.save()
    return img

def tex_wood(name, color, planks=8, seed=1, size=1024, dirt=0.5, seams=True):
    if name in bpy.data.images:
        return bpy.data.images[name]
    rng = np.random.default_rng(seed)
    y = np.arange(size)[:, None]; x = np.arange(size)[None, :]
    ph = size // planks
    pid = np.minimum(y // ph, planks - 1)[:, 0]
    tint = (0.78 + 0.4 * rng.random(planks)).astype(np.float32)
    jx = rng.integers(0, size, planks)
    t_col = tint[pid][:, None]
    grain = _fbm(size, max(planks, 4) * 10, 3, rng, 3)
    fine = _noise(size, size, max(planks, 4) * 50, 5, rng)
    v = t_col * (0.62 + 0.76 * grain) * (0.86 + 0.28 * fine)
    if seams:
        seam = ((y % ph) < 3)
        joint = (np.abs(x - jx[pid][:, None]) < 2)
        v = np.where(seam | joint, v * 0.3, v)
    d = _fbm(size, 4, 4, rng, 4)
    v = v * (1.0 - dirt * 0.45 * d)
    rgb = v[..., None] * np.array(color, np.float32)[None, None, :]
    return _to_img(name, rgb)

def tex_canvas(name, color, seed=3, size=1024, dirt=0.5):
    if name in bpy.data.images:
        return bpy.data.images[name]
    rng = np.random.default_rng(seed)
    y = np.arange(size)[:, None]; x = np.arange(size)[None, :]
    weave = 0.5 + 0.5 * np.sin(x * 2 * np.pi / 8) * np.sin(y * 2 * np.pi / 8)
    cloud = _fbm(size, 6, 6, rng, 5)
    v = (0.80 + 0.35 * cloud) * (0.93 + 0.07 * weave)
    pw = size // 8
    seam = ((x % pw) < 3)
    stitch = (((x % pw) >= 8) & ((x % pw) < 10) & (((y // 8) % 2) == 0))
    v = np.where(seam, v * 0.72, v)
    v = np.where(stitch, v * 0.82, v)
    streak = _fbm(size, 2, 14, rng, 3)
    v = v * (1.0 - dirt * 0.35 * streak)
    rgb = v[..., None] * np.array(color, np.float32)[None, None, :]
    return _to_img(name, rgb)

def tex_flag(name, kind="skull", color=(0.65, 0.05, 0.05)):
    if name in bpy.data.images:
        return bpy.data.images[name]
    W, H = 512, 320
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    noise = _fbm(512, 4, 4, rng, 4)[:H, :W]
    if kind == "skull":
        img = np.zeros((H, W, 3), np.float32) + 0.035
        def seg(p, q, r):
            dx, dy = q[0] - p[0], q[1] - p[1]
            l2 = dx * dx + dy * dy
            t = np.clip(((xx - p[0]) * dx + (yy - p[1]) * dy) / l2, 0, 1)
            return np.hypot(xx - (p[0] + t * dx), yy - (p[1] + t * dy)) < r
        white = seg((150, 235), (362, 105), 12) | seg((150, 105), (362, 235), 12)
        for (px, py) in [(140, 243), (160, 227), (372, 97), (352, 113), (140, 97), (160, 113), (372, 243), (352, 227)]:
            white |= ((xx - px) ** 2 + (yy - py) ** 2) < 17 ** 2
        cx, cy = 256, 125
        skull = (((xx - cx) ** 2 + (yy - cy) ** 2) < 62 ** 2) | ((np.abs(xx - cx) < 36) & (yy > cy + 25) & (yy < cy + 88))
        eyes = (((xx - (cx - 26)) ** 2 + (yy - (cy - 2)) ** 2) < 17 ** 2) | (((xx - (cx + 26)) ** 2 + (yy - (cy - 2)) ** 2) < 17 ** 2)
        nose = (np.abs(xx - cx) < (yy - (cy + 16)) * 0.45) & (yy > cy + 16) & (yy < cy + 44)
        teeth = (((xx - cx + 100) % 12) < 2) & (yy > cy + 58) & (yy < cy + 88) & (np.abs(xx - cx) < 34)
        white = (white | skull) & ~eyes & ~nose & ~teeth
        img[white] = 0.9
    elif kind == "cross":
        img = np.zeros((H, W, 3), np.float32) + np.array((0.9, 0.87, 0.78), np.float32)
        band = (np.abs(xx - W * 0.38) < 34) | (np.abs(yy - H * 0.5) < 34)
        img[band] = color
    else:
        img = np.zeros((H, W, 3), np.float32) + np.array(color, np.float32)
        img[(yy > 40) & (yy < 70)] = (0.9, 0.85, 0.7)
        img[(yy > H - 70) & (yy < H - 40)] = (0.9, 0.85, 0.7)
    img *= (0.82 + 0.3 * noise)[..., None]
    return _to_img(name, img)

# ---------------------------------------------------------------- materials
def _new_mat(name):
    m = bpy.data.materials.new(name)
    try:
        m.use_nodes = True
    except Exception:
        pass
    nt = m.node_tree
    b = None
    for n in nt.nodes:
        if n.type == "BSDF_PRINCIPLED":
            b = n
    if b is None:
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        b = nt.nodes.new("ShaderNodeBsdfPrincipled")
        nt.links.new(b.outputs[0], out.inputs[0])
    return m, nt, b

def mat_tex(name, img, rough=0.85, metal=0.0):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m, nt, b = _new_mat(name)
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    t.location = (-400, 200)
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    m.use_backface_culling = False
    return m

def mat_solid(name, rgb, rough=0.6, metal=0.0, emit=None):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m, nt, b = _new_mat(name)
    b.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    m.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    if emit:
        for key in ("Emission Color", "Emission"):
            if key in b.inputs:
                b.inputs[key].default_value = (emit[0], emit[1], emit[2], 1.0)
                break
        if "Emission Strength" in b.inputs:
            b.inputs["Emission Strength"].default_value = 2.0
    return m

# ---------------------------------------------------------------- mesh helpers
def mesh_to_object(name, bm, mats, coll, sharp=35):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    if sharp:
        try:
            me.set_sharp_from_angle(angle=math.radians(sharp))
        except Exception:
            pass
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob

class MB:
    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")

    def _face(self, verts, smooth, mat):
        try:
            f = self.bm.faces.new(verts)
        except ValueError:
            return None
        f.smooth = smooth
        f.material_index = mat
        return f

    def cyl(self, p0, p1, r0, r1=None, seg=10, caps=True, smooth=True, mat=0):
        p0 = Vector(p0); p1 = Vector(p1)
        r1 = r0 if r1 is None else r1
        d = p1 - p0
        if d.length < 1e-6:
            return
        d.normalize()
        a = d.cross(Vector((0, 0, 1)))
        if a.length < 1e-4:
            a = d.cross(Vector((1, 0, 0)))
        a.normalize()
        b = d.cross(a).normalized()
        ring0 = []; ring1 = []
        for i in range(seg):
            ang = 2 * math.pi * i / seg
            o = a * math.cos(ang) + b * math.sin(ang)
            ring0.append(self.bm.verts.new(p0 + o * r0))
            ring1.append(self.bm.verts.new(p1 + o * r1))
        for i in range(seg):
            j = (i + 1) % seg
            self._face([ring0[i], ring0[j], ring1[j], ring1[i]], smooth, mat)
        if caps:
            self._face(ring1, False, mat)
            self._face(ring0[::-1], False, mat)

    def box(self, c, s, rot_z=0.0, mat=0, smooth=False, rot_x=0.0):
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        pts = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
               (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
        M = Matrix.Translation(Vector(c)) @ Matrix.Rotation(rot_z, 4, "Z") @ Matrix.Rotation(rot_x, 4, "X")
        vs = [self.bm.verts.new(M @ Vector(p)) for p in pts]
        for q in [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]:
            self._face([vs[i] for i in q], smooth, mat)

    def prism_x(self, pts_yz, x0, x1, mat=0):
        A = [self.bm.verts.new((x0, p[0], p[1])) for p in pts_yz]
        B = [self.bm.verts.new((x1, p[0], p[1])) for p in pts_yz]
        n = len(pts_yz)
        self._face(A, False, mat)
        self._face(B[::-1], False, mat)
        for i in range(n):
            j = (i + 1) % n
            self._face([A[i], B[i], B[j], A[j]], False, mat)

    def torus(self, c, axis, R, r, segs=24, rsegs=8, mat=0):
        c = Vector(c); ax = Vector(axis).normalized()
        a = ax.cross(Vector((0, 0, 1)))
        if a.length < 1e-4:
            a = ax.cross(Vector((1, 0, 0)))
        a.normalize()
        b = ax.cross(a).normalized()
        grid = []
        for i in range(segs):
            ang = 2 * math.pi * i / segs
            rad = a * math.cos(ang) + b * math.sin(ang)
            row = []
            for j in range(rsegs):
                ph = 2 * math.pi * j / rsegs
                row.append(self.bm.verts.new(c + rad * R + rad * (r * math.cos(ph)) + ax * (r * math.sin(ph))))
            grid.append(row)
        for i in range(segs):
            for j in range(rsegs):
                i2 = (i + 1) % segs; j2 = (j + 1) % rsegs
                self._face([grid[i][j], grid[i2][j], grid[i2][j2], grid[i][j2]], True, mat)

    def autouv(self, tile=2.5, swap=False):
        for f in self.bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                p = l.vert.co
                if ax == 0:
                    uv = (p.y, p.z)
                elif ax == 1:
                    uv = (p.x, p.z)
                else:
                    uv = (p.x, p.y)
                if swap and ax != 2:
                    uv = (uv[1], uv[0])
                l[self.uv].uv = (uv[0] / tile, uv[1] / tile)

    def finish(self, name, mats, coll, origin=(0, 0, 0), recalc=True, uv=True, swap=False, tile=2.5):
        bm = self.bm
        if len(bm.verts) == 0:
            bm.free()
            return None
        if recalc:
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.normal_update()
        if uv:
            self.autouv(tile, swap)
        o = Vector(origin)
        if o.length > 0:
            bmesh.ops.translate(bm, vec=-o, verts=bm.verts)
        ob = mesh_to_object(name, bm, mats, coll)
        ob.location = o
        return ob

# ---------------------------------------------------------------- cloth (double layer + shape keys)
def cloth(name, fns, nu, nv, uvs, mat, coll, thick=0.04):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")

    def pts_for(f, sign):
        out = []
        last = Vector((0, 1, 0))
        for j in range(nv + 1):
            v = j / nv
            for i in range(nu + 1):
                u = i / nu
                p = f(u, v)
                du = f(min(u + 1e-3, 1.0), v) - f(max(u - 1e-3, 0.0), v)
                dv = f(u, min(v + 1e-3, 1.0)) - f(u, max(v - 1e-3, 0.0))
                n = du.cross(dv)
                if n.length < 1e-9:
                    n = last
                else:
                    n = n.normalized(); last = n
                out.append(p + n * (sign * thick * 0.5))
        return out

    f0 = fns[0][1]
    A = [bm.verts.new(p) for p in pts_for(f0, 1)]
    B = [bm.verts.new(p) for p in pts_for(f0, -1)]
    W = nu + 1
    for j in range(nv):
        for i in range(nu):
            qa = [A[j * W + i], A[j * W + i + 1], A[(j + 1) * W + i + 1], A[(j + 1) * W + i]]
            qb = [B[j * W + i], B[(j + 1) * W + i], B[(j + 1) * W + i + 1], B[j * W + i + 1]]
            fa = bm.faces.new(qa); fb = bm.faces.new(qb)
            fa.smooth = True; fb.smooth = True
            uvsA = [(i / nu, j / nv), ((i + 1) / nu, j / nv), ((i + 1) / nu, (j + 1) / nv), (i / nu, (j + 1) / nv)]
            uvsB = [(i / nu, j / nv), (i / nu, (j + 1) / nv), ((i + 1) / nu, (j + 1) / nv), ((i + 1) / nu, j / nv)]
            for l, (uu, vv) in zip(fa.loops, uvsA):
                l[uvl].uv = (uu * uvs[0], vv * uvs[1])
            for l, (uu, vv) in zip(fb.loops, uvsB):
                l[uvl].uv = (uu * uvs[0], vv * uvs[1])
    ob = mesh_to_object(name, bm, [mat], coll, sharp=0)
    if len(fns) > 1:
        ob.shape_key_add(name="Basis", from_mix=False)
        for key, f in fns[1:]:
            kb = ob.shape_key_add(name=key, from_mix=False)
            kb.value = 0.0
            kb.slider_min = 0.0
            kb.slider_max = 1.0
            P = pts_for(f, 1) + pts_for(f, -1)
            for i, p in enumerate(P):
                kb.data[i].co = p
    return ob

# ---------------------------------------------------------------- hull
class HullShape:
    def __init__(s, L, B, D, H, bulwark=1.0, stern_step=0.0, t0=0.13, t1=0.17, fore_step=0.0, f0=0.84, f1=0.87,
                 bow_rise=0.8, stern_w=0.7, bow_over=0.0, rake=0.0, flare=0.10, tumble=0.12):
        s.L = L; s.W = B / 2.0; s.D = D; s.H = H; s.bulwark = bulwark
        s.stern_step = stern_step; s.t0 = t0; s.t1 = t1
        s.fore_step = fore_step; s.f0 = f0; s.f1 = f1
        s.bow_rise = bow_rise; s.stern_w = stern_w
        s.bow_over = bow_over; s.rake = rake; s.flare = flare; s.tumble = tumble

    def w(s, t):
        if t < 0.45:
            k = (0.45 - t) / 0.45
            f = 1 - (1 - s.stern_w) * k * k
        else:
            k = (t - 0.45) / 0.55
            f = 1 - k ** 2.2
        return s.W * max(f, 0.0)

    def d(s, t):
        if t < 0.35:
            f = 1 - 0.45 * ((0.35 - t) / 0.35) ** 2
        else:
            f = 1 - ((t - 0.35) / 0.65) ** 1.6
        return s.D * max(f, 0.06)

    def h(s, t):
        return (s.H + s.bow_rise * t ** 3 + s.stern_step * (1 - sstep(s.t0, s.t1, t))
                + s.fore_step * sstep(s.f0, s.f1, t))

    def fl(s, t):
        return s.flare * sstep(0.45, 1.0, t) - s.tumble * (1 - sstep(0.0, 0.6, t))

    def deckz(s, t):
        return s.h(t) - s.bulwark

    def pt(s, t, theta):
        w = s.w(t); d = s.d(t); h = s.h(t)
        z = h - (d + h) * math.cos(theta)
        zf = 1 - math.cos(theta)
        x = w * math.sin(theta) * (1 + s.fl(t) * zf * zf)
        y = t * s.L + s.bow_over * s.L * sstep(0.72, 1.0, t) * zf ** 2 - s.rake * (1 - sstep(0.0, 0.3, t)) * zf
        return x, y, z

    def theta_z(s, t, z):
        d = s.d(t); h = s.h(t)
        return math.acos(clamp((h - z) / (d + h), -1.0, 1.0))

    def at_z(s, t, z):
        return s.pt(t, s.theta_z(t, z))

    def rimx(s, t):
        return s.pt(t, math.pi / 2)[0]

def _ring_thetas(HS, t, levels_fn, counts=None):
    d = HS.d(t); h = HS.h(t)
    zs = [-d]
    for z in sorted(levels_fn(t)):
        z = clamp(z, -d + 0.05, h - 0.03)
        if z <= zs[-1] + 0.02:
            z = zs[-1] + 0.02
        zs.append(z)
    zs.append(max(h, zs[-1] + 0.02))
    th = [HS.theta_z(t, z) for z in zs]
    th[-1] = math.pi / 2
    for i in range(1, len(th)):
        if th[i] < th[i - 1] + 1e-4:
            th[i] = th[i - 1] + 1e-4
    th[-1] = max(th[-1], math.pi / 2)
    return th

def build_hull(name, HS, mats, coll, levels_fn, NS=60, N=16, tile=3.0, thick=0.22):
    th_mid = _ring_thetas(HS, 0.5, levels_fn)
    counts = [max(1, round((th_mid[k + 1] - th_mid[k]) / (math.pi / 2) * N)) for k in range(len(th_mid) - 1)]
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    rings = []; uvs = {}
    for i in range(NS + 1):
        t = i / NS
        th = _ring_thetas(HS, t, levels_fn)
        thetas = []
        for k, n in enumerate(counts):
            for s_ in range(n):
                thetas.append(th[k] + (th[k + 1] - th[k]) * s_ / n)
        thetas.append(th[-1])
        left = [HS.pt(t, a) for a in reversed(thetas)]
        left = [(-p[0], p[1], p[2]) for p in left]
        right = [HS.pt(t, a) for a in thetas[1:]]
        pts = left + right
        arc = 0.0; ring = []
        for j, p in enumerate(pts):
            if j > 0:
                arc += math.dist(p, pts[j - 1])
            v = bm.verts.new(p)
            ring.append(v)
            uvs[v] = (p[1] / tile, arc / tile)
        rings.append(ring)
    M = len(rings[0])
    for i in range(NS):
        for j in range(M - 1):
            a = rings[i][j]; b = rings[i][j + 1]; c = rings[i + 1][j + 1]; d = rings[i + 1][j]
            f = bm.faces.new([a, d, c, b])
            f.smooth = True
            for l in f.loops:
                l[uvl].uv = uvs[l.vert]
    tf = bm.faces.new(rings[0])
    tf.smooth = False
    for l in tf.loops:
        l[uvl].uv = (l.vert.co.x / tile, l.vert.co.z / tile)
    bm.normal_update()
    cen = Vector((0, HS.L * 0.5, 0.0))
    side = [f for f in bm.faces if f is not tf]
    s = sum(f.normal.dot(f.calc_center_median() - cen) for f in side)
    if s < 0:
        for f in side:
            f.normal_flip()
    if tf.normal.y > 0:
        tf.normal_flip()
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.normal_update()
    ob = mesh_to_object(name, bm, mats, coll, sharp=0)
    md = ob.modifiers.new("Solid", "SOLIDIFY")
    md.thickness = thick
    md.offset = -1.0
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    nm = bpy.data.meshes.new_from_object(ev)
    old = ob.data
    ob.modifiers.clear()
    ob.data = nm
    bpy.data.meshes.remove(old)
    nm.name = name
    try:
        nm.set_sharp_from_angle(angle=math.radians(50))
    except Exception:
        pass
    return ob

def build_deck(name, HS, mat, coll, NS=60, NX=7, tile=3.0, inset=0.24):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    rows = []
    for i in range(NS + 1):
        t = i / NS
        x, y, z = HS.at_z(t, HS.deckz(t))
        xd = max(x - inset, 0.02)
        row = []
        for k in range(NX):
            s = -1 + 2 * k / (NX - 1)
            row.append(bm.verts.new((s * xd, y, z + 0.06 * (1 - s * s))))
        rows.append(row)
    for i in range(NS):
        for k in range(NX - 1):
            f = bm.faces.new([rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]])
            f.smooth = False
            for l in f.loops:
                l[uvl].uv = (l.vert.co.y / tile, l.vert.co.x / tile)
    bm.normal_update()
    if sum(f.normal.z for f in bm.faces) < 0:
        for f in bm.faces:
            f.normal_flip()
    return mesh_to_object(name, bm, [mat], coll, sharp=0)
