#!/usr/bin/env python3
# Copyright 2026 The Aether Engine Contributors
# SPDX-License-Identifier: Apache-2.0
"""Generates the sample models in samples/content/ (glTF 2.0).

Everything is built from code (geometry and PNG textures), so the output is
deterministic and the content carries the project's licence. Run from the
repository root:

    python3 tools/make_sample_content.py          # write files
    python3 tools/make_sample_content.py --check  # fail if files are stale
"""

import argparse
import base64
import json
import math
import struct
import sys
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "samples" / "content"


# --- PNG ---------------------------------------------------------------------

def png(width, height, pixel):
    """RGBA8 PNG; pixel(x, y) -> (r, g, b, a). Stored (level 0) so the bytes
    do not depend on the zlib version."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 0))
            + chunk(b"IEND", b""))


def crate_texture():
    size = 64

    def pixel(x, y):
        border = x < 5 or y < 5 or x >= size - 5 or y >= size - 5
        diagonal = abs(x - y) < 4 or abs(x - (size - 1 - y)) < 4
        plank = (y // 9) % 2
        grain = ((x * 7 + y * 3) % 11) * 2
        if border or diagonal:
            base = (122, 82, 44)
        else:
            base = (176, 128, 74) if plank else (164, 116, 64)
        r, g, b = (max(0, c - grain) for c in base)
        if (border or diagonal) and (x % 16 == 8 and y % 16 == 8):
            r, g, b = 60, 60, 64  # nail heads
        return (r, g, b, 255)

    return png(size, size, pixel)


def checker_texture():
    size = 32

    def pixel(x, y):
        light = ((x // 4) + (y // 4)) % 2 == 0
        return (236, 236, 240, 255) if light else (52, 120, 210, 255)

    return png(size, size, pixel)


# --- geometry ----------------------------------------------------------------

class Prim:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def vertex(self, p, n, uv=(0.0, 0.0)):
        self.positions.append(p)
        self.normals.append(n)
        self.uvs.append(uv)
        return len(self.positions) - 1

    def quad(self, a, b, c, d):
        self.indices += [a, b, c, a, c, d]


def box(sx, sy, sz, cy=0.0):
    """Box with per-face UVs, centred at (0, cy, 0). Counter-clockwise outside."""
    prim = Prim()
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    faces = [  # normal, u axis, v axis (v points down the image)
        ((0, 0, 1), (1, 0, 0), (0, -1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
        ((1, 0, 0), (0, 0, -1), (0, -1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
    ]
    for n, u, v in faces:
        ids = []
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple(n[i] * (hx, hy, hz)[i] + u[i] * su * (hx, hy, hz)[i] + v[i] * sv * (hx, hy, hz)[i]
                      for i in range(3))
            p = (p[0], p[1] + cy, p[2])
            ids.append(prim.vertex(p, n, ((su + 1) / 2, (sv + 1) / 2)))
        # (-1,-1) -> (1,-1) -> (1,1) runs u then v; v points down, so this is
        # clockwise seen from outside: emit reversed.
        prim.quad(ids[0], ids[3], ids[2], ids[1])
    return prim


def cone(radius, height, base_y, segments):
    """Cone with smooth side normals and a flat base."""
    prim = Prim()
    slope = radius / height
    for i in range(segments):
        a0 = 2 * math.pi * i / segments
        a1 = 2 * math.pi * (i + 1) / segments
        am = (a0 + a1) / 2

        def n(a):
            l = math.sqrt(1 + slope * slope)
            return (math.cos(a) / l, slope / l, math.sin(a) / l)

        p0 = (radius * math.cos(a0), base_y, radius * math.sin(a0))
        p1 = (radius * math.cos(a1), base_y, radius * math.sin(a1))
        tip = (0.0, base_y + height, 0.0)
        i0 = prim.vertex(p0, n(a0))
        i1 = prim.vertex(p1, n(a1))
        it = prim.vertex(tip, n(am))
        prim.indices += [i0, it, i1]  # counter-clockwise from outside
        c = prim.vertex((0.0, base_y, 0.0), (0, -1, 0))
        b0 = prim.vertex(p0, (0, -1, 0))
        b1 = prim.vertex(p1, (0, -1, 0))
        prim.indices += [c, b0, b1]
    return prim


def torus(major, minor, rings, sides):
    prim = Prim()
    for i in range(rings + 1):
        u = i / rings
        a = 2 * math.pi * u
        for j in range(sides + 1):
            v = j / sides
            b = 2 * math.pi * v
            n = (math.cos(a) * math.cos(b), math.sin(b), math.sin(a) * math.cos(b))
            p = ((major + minor * math.cos(b)) * math.cos(a), minor * math.sin(b),
                 (major + minor * math.cos(b)) * math.sin(a))
            prim.vertex(p, n, (u * 4, v))
    for i in range(rings):
        for j in range(sides):
            a = i * (sides + 1) + j
            b = a + sides + 1
            prim.indices += [a, a + 1, b + 1, a, b + 1, b]
    return prim


# --- glTF writer -------------------------------------------------------------

class Gltf:
    def __init__(self):
        self.doc = {"asset": {"version": "2.0", "generator": "Aether tools/make_sample_content.py",
                              "copyright": "2026 The Aether Engine Contributors, Apache-2.0"},
                    "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [],
                    "accessors": [], "bufferViews": [], "buffers": []}
        self.bin = bytearray()

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.bin.extend(data)
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def _accessor(self, values, kind, width):
        # Rounded so libm differences in the last bit cannot change the output.
        flat = [round(c, 6) for v in values for c in v]
        view = self._view(struct.pack("<%df" % len(flat), *flat), 34962)
        acc = {"bufferView": view, "componentType": 5126, "count": len(values), "type": kind}
        if kind == "VEC3":
            acc["min"] = [round(min(v[i] for v in values), 6) for i in range(width)]
            acc["max"] = [round(max(v[i] for v in values), 6) for i in range(width)]
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def material(self, name, color, texture_png=None):
        mat = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": list(color) + [1.0],
                                                      "metallicFactor": 0.0, "roughnessFactor": 0.8}}
        if texture_png is not None:
            image_view = self._view(texture_png)
            self.doc.setdefault("images", []).append({"bufferView": image_view, "mimeType": "image/png"})
            self.doc.setdefault("samplers", [{"magFilter": 9729, "minFilter": 9987}])
            self.doc.setdefault("textures", []).append({"source": len(self.doc["images"]) - 1, "sampler": 0})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(self.doc["textures"]) - 1}
        self.doc["materials"].append(mat)
        return len(self.doc["materials"]) - 1

    def mesh(self, name, prims):
        out = []
        for prim, material in prims:
            idx = self._view(struct.pack("<%dH" % len(prim.indices), *prim.indices), 34963)
            self.doc["accessors"].append({"bufferView": idx, "componentType": 5123, "count": len(prim.indices),
                                          "type": "SCALAR"})
            index_accessor = len(self.doc["accessors"]) - 1
            out.append({"attributes": {"POSITION": self._accessor(prim.positions, "VEC3", 3),
                                       "NORMAL": self._accessor(prim.normals, "VEC3", 3),
                                       "TEXCOORD_0": self._accessor(prim.uvs, "VEC2", 2)},
                        "indices": index_accessor, "material": material})
        self.doc["meshes"].append({"name": name, "primitives": out})
        return len(self.doc["meshes"]) - 1

    def node(self, name, mesh=None, children=None, root=False, **trs):
        node = {"name": name}
        if mesh is not None:
            node["mesh"] = mesh
        if children:
            node["children"] = children
        node.update(trs)
        self.doc["nodes"].append(node)
        index = len(self.doc["nodes"]) - 1
        if root:
            self.doc["scenes"][0]["nodes"].append(index)
        return index

    def _clean(self):
        for key in ("materials", "meshes"):
            if not self.doc[key]:
                del self.doc[key]

    def glb(self):
        self._clean()
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.doc, separators=(",", ":")).encode()
        js += b" " * ((4 - len(js) % 4) % 4)
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(js), 0x4E4F534A) + js
                + struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))

    def gltf(self, bin_name):
        """Text glTF + external .bin (images stay in the buffer)."""
        self._clean()
        self.doc["buffers"] = [{"byteLength": len(self.bin), "uri": bin_name}]
        return (json.dumps(self.doc, indent=1) + "\n").encode(), bytes(self.bin)


# --- models ------------------------------------------------------------------

def make_crate():
    g = Gltf()
    mat = g.material("Crate", (1.0, 1.0, 1.0), crate_texture())
    mesh = g.mesh("Crate", [(box(1, 1, 1), mat)])
    g.node("Crate", mesh, root=True)
    return g.glb()


def make_tree():
    g = Gltf()
    bark = g.material("Bark", (0.32, 0.19, 0.09))
    leaves = g.material("Leaves", (0.13, 0.42, 0.14))
    leaves_light = g.material("Leaves Light", (0.24, 0.58, 0.2))
    trunk = g.mesh("Trunk", [(box(0.3, 1.2, 0.3, cy=0.6), bark)])
    canopy = g.mesh("Canopy", [(cone(1.0, 1.4, 0.0, 16), leaves), (cone(0.7, 1.1, 0.8, 16), leaves_light)])
    top = g.node("Canopy", canopy, translation=[0.0, 1.0, 0.0])
    g.node("Tree", trunk, children=[top], root=True)
    return g.gltf("tree.bin")


def make_torus():
    g = Gltf()
    mat = g.material("Checker", (1.0, 1.0, 1.0), checker_texture())
    mesh = g.mesh("Torus", [(torus(0.7, 0.25, 48, 16), mat)])
    # Stands upright: a quarter turn about X.
    s = math.sqrt(0.5)
    g.node("Torus", mesh, root=True, rotation=[s, 0.0, 0.0, s], translation=[0.0, 0.95, 0.0])
    return g.glb()


def outputs():
    tree_json, tree_bin = make_tree()
    return {
        "models/crate.glb": make_crate(),
        "models/torus.glb": make_torus(),
        "models/tree/tree.gltf": tree_json,
        "models/tree/tree.bin": tree_bin,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated files are out of date")
    args = parser.parse_args()
    stale = []
    for rel, data in outputs().items():
        path = OUT / rel
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(rel)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(OUT.parent.parent)} ({len(data)} bytes)")
    if stale:
        print("stale sample content (run tools/make_sample_content.py): " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
