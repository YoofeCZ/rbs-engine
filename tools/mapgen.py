"""RBS Engine - tiny helper library for building maps from Python.

The editor is the normal way to build levels; this module exists so the demo maps
can be generated reproducibly (and as an example of the map JSON format).

Coordinates: Y is up. 1 unit ~ 1 inch (player is 56 units tall).
"""
import json
import math


def r4(v):
    return round(v, 4)


class Brush:
    def __init__(self, faces):
        self.faces = faces  # list of dicts {n, d, tex, off?, scale?, rot?}

    def to_json(self):
        out = []
        for f in self.faces:
            j = {"n": [r4(f["n"][0]), r4(f["n"][1]), r4(f["n"][2])], "d": r4(f["d"]), "tex": f["tex"]}
            if f.get("off"):
                j["off"] = [r4(f["off"][0]), r4(f["off"][1])]
            if f.get("scale"):
                j["scale"] = [r4(f["scale"][0]), r4(f["scale"][1])]
            if f.get("rot"):
                j["rot"] = f["rot"]
            out.append(j)
        return {"faces": out}

    def face(self, side):
        """side: '+x' '-x' '+y' '-y' '+z' '-z' (axial faces of box brushes)"""
        axis = {"x": 0, "y": 1, "z": 2}[side[1]]
        sign = 1 if side[0] == "+" else -1
        for f in self.faces:
            if abs(f["n"][axis] - sign) < 1e-6:
                return f
        raise KeyError(side)

    def tex(self, side, tex, **kw):
        f = self.face(side)
        f["tex"] = tex
        f.update(kw)
        return self


def box(mn, mx, tex="concrete", **sides):
    """Axis aligned box. Keyword args override textures per side: px=, nx=, py=, ny=, pz=, nz="""
    mn = [min(mn[i], mx[i]) for i in range(3)]
    mx2 = [max(mn[i], mx[i]) for i in range(3)]
    mx = mx2
    faces = [
        {"n": (1, 0, 0), "d": mx[0], "tex": sides.get("px", tex)},
        {"n": (-1, 0, 0), "d": -mn[0], "tex": sides.get("nx", tex)},
        {"n": (0, 1, 0), "d": mx[1], "tex": sides.get("py", tex)},
        {"n": (0, -1, 0), "d": -mn[1], "tex": sides.get("ny", tex)},
        {"n": (0, 0, 1), "d": mx[2], "tex": sides.get("pz", tex)},
        {"n": (0, 0, -1), "d": -mn[2], "tex": sides.get("nz", tex)},
    ]
    return Brush(faces)


def norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def plane_brush(planes, tex="concrete"):
    """planes: list of (normal, point_on_plane[, tex])"""
    faces = []
    for p in planes:
        n = norm(p[0])
        d = sum(n[i] * p[1][i] for i in range(3))
        faces.append({"n": n, "d": d, "tex": p[2] if len(p) > 2 else tex})
    return Brush(faces)


def wedge_ceiling(x0, x1, z0, z1, y_low, y_high, rising_toward, tex="concrete", thickness=16):
    """A sloped slab between y_low and y_high. rising_toward: '+x' '-x' '+z' '-z'."""
    axis = 0 if rising_toward[1] == "x" else 2
    sign = 1 if rising_toward[0] == "+" else -1
    a0, a1 = (x0, x1) if axis == 0 else (z0, z1)
    run = a1 - a0
    rise = y_high - y_low
    # bottom plane normal points down-ish
    if axis == 0:
        nb = (-rise * sign if False else (rise if sign > 0 else -rise), -run, 0)
    else:
        nb = (0, -run, (rise if sign > 0 else -rise))
    p_low = [0, y_low, 0]
    if axis == 0:
        p_low[0] = a0 if sign > 0 else a1
    else:
        p_low[2] = a0 if sign > 0 else a1
    nb = norm(nb)
    nt = tuple(-c for c in nb)
    p_top = [p_low[0] - nb[0] * thickness, p_low[1] - nb[1] * thickness, p_low[2] - nb[2] * thickness]
    planes = [(nb, p_low), (nt, p_top),
              ((1, 0, 0), (x1, 0, 0)), ((-1, 0, 0), (x0, 0, 0)), ((0, 0, 1), (0, 0, z1)), ((0, 0, -1), (0, 0, z0)),
              ((0, 1, 0), (0, y_high + thickness * 2, 0))]
    return plane_brush(planes, tex)


def prism(center, radius, y0, y1, sides=8, tex="rust", rot=0.0):
    """Vertical n-sided prism (pillars, pipes)."""
    planes = [((0, 1, 0), (0, y1, 0)), ((0, -1, 0), (0, y0, 0))]
    for i in range(sides):
        a = 2 * math.pi * i / sides + rot
        n = (math.cos(a), 0, math.sin(a))
        p = (center[0] + n[0] * radius, 0, center[1] + n[2] * radius)
        planes.append((n, p))
    return plane_brush(planes, tex)


def hprism(axis, center, radius, a0, a1, sides=8, tex="rust", rot=math.pi / 8):
    """Horizontal prism along 'x' or 'z' (pipes, tunnel ribs). center = (other_horizontal, y)."""
    planes = []
    for i in range(sides):
        a = 2 * math.pi * i / sides + rot
        if axis == "x":
            n = (0, math.sin(a), math.cos(a))
            p = (0, center[1] + n[1] * radius, center[0] + n[2] * radius)
        else:
            n = (math.cos(a), math.sin(a), 0)
            p = (center[0] + n[0] * radius, center[1] + n[1] * radius, 0)
        planes.append((n, p))
    if axis == "x":
        planes += [((1, 0, 0), (a1, 0, 0)), ((-1, 0, 0), (a0, 0, 0))]
    else:
        planes += [((0, 0, 1), (0, 0, a1)), ((0, 0, -1), (0, 0, a0))]
    return plane_brush(planes, tex)


def fit_tex(brush, side, tex, tex_w, tex_h, rect_u0, rect_v0, rect_w, rect_h):
    """Stretch a texture exactly over a rectangle on an axial face.
    rect is given in the face's (s, t) projection: walls: s along the wall, t = -y."""
    su = rect_w / tex_w
    sv = rect_h / tex_h
    brush.tex(side, tex, scale=(su, sv), off=(-rect_u0 / su, -rect_v0 / sv))
    return brush


class MapBuilder:
    def __init__(self, title):
        self.settings = {"title": title}
        self.brushes = []
        self.entities = []

    def add(self, *brushes):
        for b in brushes:
            self.brushes.append(b)

    def ent(self, cls, origin, angle=0, brushes=None, components=None, **props):
        e = {"class": cls, "origin": [r4(origin[0]), r4(origin[1]), r4(origin[2])]}
        if angle:
            e["angle"] = angle
        if props:
            e["props"] = props
        if components:
            e["components"] = components
        if brushes:
            e["brushes"] = [b.to_json() for b in brushes]
        self.entities.append(e)
        return e

    def light(self, origin, color=(255, 220, 180), intensity=1.0, radius=300, **kw):
        return self.ent("light", origin, color=list(color), intensity=intensity, radius=radius, **kw)

    def room(self, mn, mx, wall="concrete", floor="concrete", ceil="concrete", t=16, skip=()):
        """Hollow room: brushes are built OUTSIDE the given interior bounds."""
        x0, y0, z0 = mn
        x1, y1, z1 = mx
        if "floor" not in skip:
            self.add(box((x0 - t, y0 - t, z0 - t), (x1 + t, y0, z1 + t), floor))
        if "ceil" not in skip:
            self.add(box((x0 - t, y1, z0 - t), (x1 + t, y1 + t, z1 + t), ceil))
        if "-x" not in skip:
            self.add(box((x0 - t, y0, z0 - t), (x0, y1, z1 + t), wall))
        if "+x" not in skip:
            self.add(box((x1, y0, z0 - t), (x1 + t, y1, z1 + t), wall))
        if "-z" not in skip:
            self.add(box((x0, y0, z0 - t), (x1, y1, z0), wall))
        if "+z" not in skip:
            self.add(box((x0, y0, z1), (x1, y1, z1 + t), wall))

    def save(self, path):
        data = {"format": "rbs-map", "version": 1, "settings": self.settings,
                "brushes": [b.to_json() for b in self.brushes], "entities": self.entities}
        with open(path, "w") as f:
            json.dump(data, f, indent=1)
        print("wrote", path, len(self.brushes), "brushes,", len(self.entities), "entities")
