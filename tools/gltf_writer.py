"""Minimal glTF 2.0 (.glb) writer with rigid skinning and joint rotation/translation
animations - enough to produce low-poly animated characters for the RBS Engine.

Models made in Blender (File > Export > glTF 2.0) work the same way in the engine;
this module only exists so the base game assets can be generated from code.
"""
import json
import math
import struct

FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125


def quat_axis_angle(axis, deg):
    a = math.radians(deg) * 0.5
    l = math.sqrt(sum(c * c for c in axis)) or 1.0
    s = math.sin(a) / l
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(a))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def euler(x=0.0, y=0.0, z=0.0):
    """degrees, applied X then Y then Z (intrinsic)"""
    q = (0.0, 0.0, 0.0, 1.0)
    if z:
        q = quat_mul(q, quat_axis_angle((0, 0, 1), z))
    if y:
        q = quat_mul(q, quat_axis_angle((0, 1, 0), y))
    if x:
        q = quat_mul(q, quat_axis_angle((1, 0, 0), x))
    return q


class GlbWriter:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        self.bin += data
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v["target"] = target
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, values, comp, typ, target=None, minmax=False, normalized=False):
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[typ]
        fmt = {FLOAT: "f", UBYTE: "B", USHORT: "H", UINT: "I"}[comp]
        flat = []
        for v in values:
            if n == 1:
                flat.append(v)
            else:
                flat.extend(v)
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        view = self._view(data, target)
        acc = {"bufferView": view, "componentType": comp, "count": len(values), "type": typ}
        if normalized:
            acc["normalized"] = True
        if minmax:
            if n == 1:
                acc["min"], acc["max"] = [min(values)], [max(values)]
            else:
                acc["min"] = [min(v[i] for v in values) for i in range(n)]
                acc["max"] = [max(v[i] for v in values) for i in range(n)]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def write_glb(path, mesh, image_uri, bones=None, animations=None, double_sided=False):
    """mesh: dict(pos=[(x,y,z)], nrm=[...], uv=[...], joint=[int], idx=[int])
    bones: [(name, parent_index, world_rest_pos)] - parents must come first
    animations: [(name, duration, {bone_index: [(time, quat)]}, {bone_index: [(time, translation_local)]})]"""
    w = GlbWriter()
    pos_acc = w.accessor(mesh["pos"], FLOAT, "VEC3", 34962, minmax=True)
    nrm_acc = w.accessor(mesh["nrm"], FLOAT, "VEC3", 34962)
    uv_acc = w.accessor(mesh["uv"], FLOAT, "VEC2", 34962)
    idx_comp = USHORT if len(mesh["pos"]) < 65535 else UINT
    idx_acc = w.accessor(mesh["idx"], idx_comp, "SCALAR", 34963)
    attrs = {"POSITION": pos_acc, "NORMAL": nrm_acc, "TEXCOORD_0": uv_acc}
    nodes = []
    scene_nodes = []
    skins = []
    gl_anims = []
    if bones:
        joints = [(j, 0, 0, 0) for j in mesh["joint"]]
        weights = [(1.0, 0.0, 0.0, 0.0) for _ in mesh["joint"]]
        attrs["JOINTS_0"] = w.accessor(joints, UBYTE, "VEC4", 34962)
        attrs["WEIGHTS_0"] = w.accessor(weights, FLOAT, "VEC4", 34962)
        # joint nodes with local translations
        children = {i: [] for i in range(len(bones))}
        for i, (name, parent, wpos) in enumerate(bones):
            if parent >= 0:
                children[parent].append(i)
        for i, (name, parent, wpos) in enumerate(bones):
            ppos = bones[parent][2] if parent >= 0 else (0.0, 0.0, 0.0)
            local = [wpos[k] - ppos[k] for k in range(3)]
            node = {"name": name, "translation": local}
            if children[i]:
                node["children"] = children[i]
            nodes.append(node)
        ibm = []
        for (name, parent, wpos) in bones:
            # column-major inverse of a pure translation
            ibm.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -wpos[0], -wpos[1], -wpos[2], 1))
        ibm_acc = w.accessor(ibm, FLOAT, "MAT4")
        skins.append({"joints": list(range(len(bones))), "inverseBindMatrices": ibm_acc, "skeleton": 0})
        roots = [i for i, b in enumerate(bones) if b[1] < 0]
        scene_nodes.extend(roots)
        for (aname, duration, rot_tracks, trans_tracks) in (animations or []):
            channels, samplers = [], []
            for bone, keys in rot_tracks.items():
                t_acc = w.accessor([k[0] for k in keys], FLOAT, "SCALAR", minmax=True)
                v_acc = w.accessor([k[1] for k in keys], FLOAT, "VEC4")
                samplers.append({"input": t_acc, "output": v_acc, "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": bone, "path": "rotation"}})
            for bone, keys in (trans_tracks or {}).items():
                t_acc = w.accessor([k[0] for k in keys], FLOAT, "SCALAR", minmax=True)
                v_acc = w.accessor([k[1] for k in keys], FLOAT, "VEC3")
                samplers.append({"input": t_acc, "output": v_acc, "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": bone, "path": "translation"}})
            gl_anims.append({"name": aname, "channels": channels, "samplers": samplers})
    mesh_node = {"name": "mesh", "mesh": 0}
    if bones:
        mesh_node["skin"] = 0
    nodes.append(mesh_node)
    scene_nodes.append(len(nodes) - 1)
    material = {"name": "atlas", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 1.0}}
    if double_sided:
        material["doubleSided"] = True
    gltf = {
        "asset": {"version": "2.0", "generator": "RBS Engine tools/gen_models.py"},
        "scene": 0,
        "scenes": [{"nodes": scene_nodes}],
        "nodes": nodes,
        "meshes": [{"primitives": [{"attributes": attrs, "indices": idx_acc, "material": 0}]}],
        "materials": [material],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": image_uri}],
        "buffers": [{"byteLength": len(w.bin)}],
        "bufferViews": w.views,
        "accessors": w.accessors,
    }
    if skins:
        gltf["skins"] = skins
    if gl_anims:
        gltf["animations"] = gl_anims
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(js) % 4:
        js += b" "
    binary = bytes(w.bin)
    while len(binary) % 4:
        binary += b"\0"
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack("<II", len(binary), 0x004E4942))
        f.write(binary)
