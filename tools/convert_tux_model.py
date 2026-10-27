#!/usr/bin/env python3
"""Convert the original Tux Racer tux.tcl scene graph to a small binary model.

This is a deliberately tiny Tcl interpreter for the subset used by tux.tcl:
set, proc, procedure calls, Tcl-style variable substitution, brace/quote words
and the tux_* scene commands.  It preserves the original hierarchy, transforms
and 31 ellipsoid parts of Tux.
"""
from __future__ import annotations

import math
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "upstream" / "tuxracer-data-0.61" / "tux.tcl"
OUT = ROOT / "assets" / "model" / "tux.mdl"

ROLES = {
    ":t1": 1,          # root
    "left_shoulder": 2,
    "right_shoulder": 3,
    "left_hip": 4,
    "right_hip": 5,
    "left_knee": 6,
    "right_knee": 7,
    "left_ankle": 8,
    "right_ankle": 9,
    "tail": 10,
    "neck": 11,
    "head": 12,
}

# Tcl material name -> RGB565 tint used by the software renderer.
MATERIALS = {
    "white_penguin": 0xEF7D,  # warm white
    "black_penguin": 0x18E3,  # near black
    "beak_colour": 0xFC00,    # orange-yellow
    "nostril_colour": 0x81C0,
    "iris_colour": 0x0000,
}
DEFAULT_MAT = 0x18E3


class Tok:
    __slots__ = ("text", "braced")
    def __init__(self, text, braced=False):
        self.text = text
        self.braced = braced


def tokenize(s: str):
    cmds = []
    cur = []
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c in " \t\r":
            i += 1
            continue
        if c == "\n" or c == ";":
            if cur:
                cmds.append(cur)
                cur = []
            i += 1
            continue
        if c == "#":
            if not cur:
                while i < n and s[i] != "\n":
                    i += 1
                continue
            # A '#' inside a word is literal in Tcl; tux.tcl has none.
        if c == "{":
            depth = 1
            j = i + 1
            while j < n and depth:
                if s[j] == "{":
                    depth += 1
                elif s[j] == "}":
                    depth -= 1
                j += 1
            cur.append(Tok(s[i + 1:j - 1], braced=True))
            i = j
            continue
        if c == '"':
            j = i + 1
            out = []
            while j < n and s[j] != '"':
                if s[j] == "\\" and j + 1 < n:
                    out.append(s[j:j+2])
                    j += 2
                else:
                    out.append(s[j])
                    j += 1
            cur.append(Tok("".join(out), braced=False))
            i = j + 1
            continue
        j = i
        while j < n and s[j] not in " \t\r\n;":
            j += 1
        cur.append(Tok(s[i:j], braced=False))
        i = j
    if cur:
        cmds.append(cur)
    return cmds


def mat_identity():
    return [1.0, 0, 0, 0,  0, 1.0, 0, 0,  0, 0, 1.0, 0]


def mat_mul(a, b):
    # 3x4 * 3x4 (implicit last row 0,0,0,1), row-major
    out = [0.0] * 12
    for r in range(3):
        for c in range(4):
            out[r * 4 + c] = (a[r * 4 + 0] * b[0 * 4 + c] +
                              a[r * 4 + 1] * b[1 * 4 + c] +
                              a[r * 4 + 2] * b[2 * 4 + c] +
                              (a[r * 4 + 3] if c == 3 else 0.0))
    return out


def mat_translate(x, y, z):
    m = mat_identity()
    m[3], m[7], m[11] = x, y, z
    return m


def mat_scale(x, y, z):
    m = mat_identity()
    m[0], m[5], m[10] = x, y, z
    return m


def mat_rotate(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    if axis == "x":
        return [1, 0, 0, 0,  0, c, -s, 0,  0, s, c, 0]
    if axis == "y":
        return [c, 0, s, 0,  0, 1, 0, 0,  -s, 0, c, 0]
    return [c, -s, 0, 0,  s, c, 0, 0,  0, 0, 1, 0]


class Node:
    def __init__(self, name, parent, sphere=False, radius=1.0):
        self.name = name
        self.parent = parent
        self.sphere = sphere
        self.radius = radius
        self.mat = mat_identity()
        self.material = DEFAULT_MAT
        self.role = 0


def convert():
    text = SRC.read_text(errors="replace")
    cmds = tokenize(text)
    procs = {}
    nodes = []
    by_name = {}
    role_nodes = {}
    scopes = [{}]

    def lookup(name):
        for sc in reversed(scopes):
            if name in sc:
                return sc[name]
        return ""

    def subst(word: Tok):
        if word.braced:
            return word.text
        s = word.text
        return re.sub(r"\$([A-Za-z_][A-Za-z0-9_]*)", lambda m: lookup(m.group(1)), s)

    def node_name(parent, child):
        return ":" + child if parent == ":" else parent + ":" + child

    def find(name):
        return by_name.get(name)

    def eval_cmds(cmds):
        for words in cmds:
            if not words:
                continue
            cmd = subst(words[0])
            args = [subst(w) for w in words[1:]]

            if cmd == "proc":
                if len(args) < 3:
                    continue
                pname = args[0]
                params = args[1].split()
                body = words[3].text if len(words) >= 4 else ""
                procs[pname] = (params, body)
                continue

            if cmd == "set":
                if len(args) >= 2:
                    scopes[-1][args[0]] = args[1]
                continue

            if cmd in procs:
                params, body = procs[cmd]
                bind = {}
                for i, p in enumerate(params):
                    bind[p] = args[i] if i < len(args) else ""
                scopes.append(bind)
                eval_cmds(tokenize(body))
                scopes.pop()
                continue

            if cmd == "tux_transform":
                if len(args) < 2:
                    continue
                parent, child = args[0], args[1]
                nm = node_name(parent, child)
                p = find(parent)
                nd = Node(nm, p)
                by_name[nm] = nd
                nodes.append(nd)
                continue

            if cmd == "tux_sphere":
                if len(args) < 3:
                    continue
                parent, child, rad = args[0], args[1], args[2]
                nm = node_name(parent, child)
                p = find(parent)
                nd = Node(nm, p, sphere=True, radius=float(rad))
                by_name[nm] = nd
                nodes.append(nd)
                continue

            if cmd == "tux_translate":
                if len(args) < 2:
                    continue
                nd = find(args[0])
                if nd:
                    v = [float(x) for x in args[1].split()[:3]]
                    nd.mat = mat_mul(nd.mat, mat_translate(*v))
                continue

            if cmd == "tux_rotate":
                if len(args) < 3:
                    continue
                nd = find(args[0])
                if nd:
                    nd.mat = mat_mul(nd.mat, mat_rotate(args[1], float(args[2])))
                continue

            if cmd == "tux_scale":
                if len(args) < 3:
                    continue
                nd = find(args[0])
                if nd:
                    c = [float(x) for x in args[1].split()[:3]]
                    f = [float(x) for x in args[2].split()[:3]]
                    m = mat_translate(-c[0], -c[1], -c[2])
                    m = mat_mul(m, mat_scale(*f))
                    m = mat_mul(m, mat_translate(*c))
                    nd.mat = mat_mul(nd.mat, m)
                continue

            if cmd == "tux_surfaceproperty":
                if len(args) >= 2:
                    nd = find(args[0])
                    if nd:
                        nd.material = MATERIALS.get(args[1], DEFAULT_MAT)
                continue

            if cmd in ("tux_shadow", "tux_eye"):
                continue

            if cmd in ("tux_root_node", "tux_neck", "tux_head",
                       "tux_left_shoulder", "tux_right_shoulder",
                       "tux_left_hip", "tux_right_hip",
                       "tux_left_knee", "tux_right_knee",
                       "tux_left_ankle", "tux_right_ankle", "tux_tail"):
                if args:
                    role_nodes[cmd] = args[0]
                continue

            if cmd == "tux_material":
                continue
            # ignore unknown robustly
        return

    eval_cmds(cmds)

    # assign roles
    role_map = {
        "tux_root_node": ":t1",
        "tux_neck": 11, "tux_head": 12,
        "tux_left_shoulder": 2, "tux_right_shoulder": 3,
        "tux_left_hip": 4, "tux_right_hip": 5,
        "tux_left_knee": 6, "tux_right_knee": 7,
        "tux_left_ankle": 8, "tux_right_ankle": 9,
        "tux_tail": 10,
    }
    for cmd, rid in role_map.items():
        nm = role_nodes.get(cmd)
        if nm and nm in by_name:
            by_name[nm].role = rid

    # root is the :t1 node
    root = by_name.get(":t1")
    if root:
        root.role = 1

    # Model-space bounding box of sphere centers +/- radii (rest pose)
    mn = [1e9, 1e9, 1e9]
    mx = [-1e9, -1e9, -1e9]
    world = {}
    def world_of(nd):
        if nd.name in world:
            return world[nd.name]
        if nd.parent is None:
            m = nd.mat
        else:
            m = mat_mul(world_of(nd.parent), nd.mat)
        world[nd.name] = m
        return m
    for nd in nodes:
        if not nd.sphere:
            continue
        m = world_of(nd)
        cx, cy, cz = m[3], m[7], m[11]
        r = abs(nd.radius) * max(abs(m[0]), abs(m[5]), abs(m[10]))
        mn = [min(mn[0], cx-r), min(mn[1], cy-r), min(mn[2], cz-r)]
        mx = [max(mx[0], cx+r), max(mx[1], cy+r), max(mx[2], cz+r)]

    print(f"nodes={len(nodes)} spheres={sum(1 for n in nodes if n.sphere)}")
    print("bbox", [round(v,3) for v in mn], [round(v,3) for v in mx])
    for nm, nd in by_name.items():
        if nd.role:
            print("role", nd.role, nm, nd.sphere)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(b"TMOD")
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<I", len(nodes)))
        for i, nd in enumerate(nodes):
            parent = -1 if nd.parent is None else nodes.index(nd.parent)
            f.write(struct.pack("<iBBBB", parent, 1 if nd.sphere else 0, nd.role, 0, 0))
            f.write(struct.pack("<I", nd.material))
            f.write(struct.pack("<i", int(round(nd.radius * 65536.0))))
            for v in nd.mat:
                f.write(struct.pack("<i", int(round(v * 65536.0))))
    print("wrote", OUT, OUT.stat().st_size, "bytes")
    bake(nodes, by_name, role_nodes, OUT.parent / "tux_mesh.bin")


# ---------------------------------------------------------------------------
# Baked render meshes (rigid poses, original sphere hierarchy merged)
# ---------------------------------------------------------------------------

def rot_x(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [1,0,0,0, 0,c,-s,0, 0,s,c,0]
def rot_y(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [c,0,s,0, 0,1,0,0, -s,0,c,0]
def rot_z(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [c,-s,0,0, s,c,0,0, 0,0,1,0]


def sphere_mesh(lon, lat):
    verts = []
    for j in range(lat + 1):
        theta = math.pi / 2 - j * math.pi / lat
        st, ct = math.sin(theta), math.cos(theta)
        for i in range(lon + 1):
            phi = 2 * math.pi * i / lon
            verts.append((ct * math.cos(phi), st, ct * math.sin(phi)))
    faces = []
    for j in range(lat):
        for i in range(lon):
            a = j * (lon + 1) + i
            b = a + 1
            c = a + lon + 1
            d = c + 1
            faces.append((a, b, d))
            faces.append((a, d, c))
    return verts, faces


MESH_CACHE = {}

def get_mesh(lod):
    if lod not in MESH_CACHE:
        dims = [(8,5), (6,4), (4,3), (3,2)][lod]
        MESH_CACHE[lod] = sphere_mesh(*dims)
    return MESH_CACHE[lod]


def pose_angles(turn, braking, airborne):
    hip = -20
    knee = -10
    ankle = -20
    shoulder_z = 0
    shoulder_y = 0
    neck = -50
    head = -30
    head_y = 0
    tail = 0
    if braking:
        shoulder_z += 28
    if turn:
        shoulder_z += 12
        hip += turn * 10
        knee += turn * 10
        head_y = -turn * 20
        tail = turn * 20
    if airborne:
        shoulder_z += 35
        hip = -38
        knee = -32
        ankle = -8
    return hip, knee, ankle, shoulder_z, shoulder_y, neck, head, head_y, tail


def joint_matrix(role, turn, braking, airborne):
    hip, knee, ankle, sz, sy, neck, head, hy, tail = pose_angles(turn, braking, airborne)
    if role in (2, 3):
        rz = rot_z(sz)
        ry = rot_y(-sy if role == 2 else sy)
        return mat_mul(rz, ry)
    if role in (4, 5):
        return rot_z(hip)
    if role in (6, 7):
        return rot_z(knee)
    if role in (8, 9):
        return rot_z(ankle)
    if role == 10:
        return rot_z(tail)
    if role == 11:
        return rot_z(neck)
    if role == 12:
        return mat_mul(rot_z(head), rot_y(hy))
    return mat_identity()


def bake(nodes, by_name, role_nodes, out_path):
    poses = [(0, 0, 0), (-1, 0, 0), (1, 0, 0), (0, 0, 1)]  # straight, left, right, air
    with out_path.open("wb") as f:
        f.write(b"TMH1")
        f.write(struct.pack("<I", len(poses)))
        for turn, braking, airborne in poses:
            world = {}
            def W(nd):
                if nd.name in world:
                    return world[nd.name]
                if nd.role in range(2, 13):
                    lm = joint_matrix(nd.role, turn, braking, airborne)
                else:
                    lm = nd.mat
                if nd.parent is None:
                    w = lm
                else:
                    w = mat_mul(W(nd.parent), lm)
                world[nd.name] = w
                return w

            verts = []
            faces = []
            for nd in nodes:
                if not nd.sphere:
                    continue
                w = W(nd)
                # screen/model-space radius
                sx = math.sqrt(sum(w[k] * w[k] for k in (0, 4, 8)))
                sy = math.sqrt(sum(w[k] * w[k] for k in (1, 5, 9)))
                sz = math.sqrt(sum(w[k] * w[k] for k in (2, 6, 10)))
                wr = nd.radius * max(sx, sy, sz)
                if wr > 0.16:
                    lod = 0
                elif wr > 0.07:
                    lod = 1
                elif wr > 0.025:
                    lod = 2
                else:
                    lod = 3
                mesh_v, mesh_f = get_mesh(lod)
                base = len(verts)
                for (x, y, z) in mesh_v:
                    verts.append((
                        w[0]*x + w[1]*y + w[2]*z + w[3],
                        w[4]*x + w[5]*y + w[6]*z + w[7],
                        w[8]*x + w[9]*y + w[10]*z + w[11],
                    ))
                for (a, b, c) in mesh_f:
                    va = verts[base+a]; vb = verts[base+b]; vc = verts[base+c]
                    ab = (vb[0]-va[0], vb[1]-va[1], vb[2]-va[2])
                    ac = (vc[0]-va[0], vc[1]-va[1], vc[2]-va[2])
                    nx = ab[1]*ac[2] - ab[2]*ac[1]
                    ny = ab[2]*ac[0] - ab[0]*ac[2]
                    nz = ab[0]*ac[1] - ab[1]*ac[0]
                    ln = math.sqrt(nx*nx + ny*ny + nz*nz)
                    if ln < 1e-9:
                        continue
                    nx, ny, nz = nx/ln, ny/ln, nz/ln
                    # ensure outward relative to the sphere center
                    cx = (va[0]+vb[0]+vc[0]) / 3 - w[3]
                    cy = (va[1]+vb[1]+vc[1]) / 3 - w[7]
                    cz = (va[2]+vb[2]+vc[2]) / 3 - w[11]
                    if nx*cx + ny*cy + nz*cz < 0:
                        b, c = c, b
                        nx, ny, nz = -nx, -ny, -nz
                    faces.append((base+a, base+b, base+c, nd.material, nx, ny, nz))

            f.write(struct.pack("<II", len(verts), len(faces)))
            for (x, y, z) in verts:
                f.write(struct.pack("<iii", int(round(x*65536)), int(round(y*65536)), int(round(z*65536))))
            for (a, b, c, col, nx, ny, nz) in faces:
                f.write(struct.pack("<HHH", a, b, c))
                f.write(struct.pack("<H", col))
                f.write(struct.pack("<hhh", int(round(nx*32767)), int(round(ny*32767)), int(round(nz*32767))))
            print(f"pose {turn},{braking},{airborne}: verts={len(verts)} faces={len(faces)}")
    print("wrote", out_path, out_path.stat().st_size, "bytes")


if __name__ == "__main__":
    convert()
