#!/usr/bin/env python3
# Copyright 2026 The Aether Engine Contributors
# SPDX-License-Identifier: Apache-2.0
"""Generates samples/levels/physics_playground.alevel.

A deterministic physics scene built from code: a wall of boxes, a ramp with
balls rolling down it, a pyramid, and a heavy ball that knocks the wall over.
Run from the repository root:

    python3 tools/make_sample_levels.py          # write
    python3 tools/make_sample_levels.py --check  # fail if the file is stale
"""

import argparse
import json
import math
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "samples" / "levels" / "physics_playground.alevel"


class Level:
    def __init__(self):
        self.entities = []

    def add(self, name, components, position=(0, 0, 0), rotation=(0, 0, 0, 1), scale=(1, 1, 1)):
        index = len(self.entities) + 1
        entity = {"id": "7c1d0000-0000-4000-8000-%012x" % index,
                  "components": {"Name": {"value": name},
                                 "Transform": {"position": list(position), "rotation": list(rotation),
                                               "scale": list(scale)}}}
        entity["components"].update(components)
        self.entities.append(entity)

    def dump(self):
        lines = ['{', '  "format": "aether.level",', '  "version": 1,',
                 '  "generator": "tools/make_sample_levels.py",', '  "entities": [']
        rows = []
        for e in self.entities:
            comps = list(e["components"].items())
            row = ['    {', '      "id": %s,' % json.dumps(e["id"]), '      "components": {']
            for i, (key, value) in enumerate(comps):
                row.append('        %s: %s%s' % (json.dumps(key), json.dumps(value, separators=(", ", ": ")),
                                               "," if i < len(comps) - 1 else ""))
            row += ['      }', '    }']
            rows.append("\n".join(row))
        return "\n".join(lines) + "\n" + ",\n".join(rows) + "\n  ]\n}\n"


def r(value):
    return round(value, 4)


def cube(color, roughness=0.6, metallic=None):
    mesh = {"mesh": "builtin:cube", "color": list(color), "roughness": roughness}
    if metallic is not None:
        mesh["metallic"] = metallic
    return mesh


def sphere(color, roughness=0.3, metallic=None):
    mesh = {"mesh": "builtin:sphere", "color": list(color), "roughness": roughness}
    if metallic is not None:
        mesh["metallic"] = metallic
    return mesh


BOX = {"shape": "box", "half_extents": [0.5, 0.5, 0.5]}
BALL = {"shape": "sphere", "radius": 0.5}


def body(mass=1.0, restitution=0.1, friction=0.5, velocity=None, kind="dynamic"):
    rb = {"type": kind, "mass": mass, "friction": friction, "restitution": restitution,
          "linear_damping": 0.05, "angular_damping": 0.05, "gravity_scale": 1}
    if velocity:
        rb["linear_velocity"] = list(velocity)
    return rb


def build():
    level = Level()
    level.add("Environment", {"Environment": {"sky": [0.2, 0.36, 0.72], "horizon": [0.62, 0.72, 0.86],
                                              "ground": [0.16, 0.14, 0.12], "intensity": 1, "exposure": 1,
                                              "shadows": True, "sky_visible": True}})
    # Light travelling down and toward -X, -Z.
    level.add("Sun Light", {"Light": {"type": "directional", "color": [1, 0.96, 0.9], "intensity": 3,
                                      "shadows": True}},
              position=(6, 12, 6), rotation=(-0.3535, 0.3535, 0.1464, 0.8536))
    level.add("Ground", {"MeshInstance": cube((0.2, 0.22, 0.25), 0.9), "Collider": BOX},
              position=(0, -0.5, 0), scale=(40, 1, 40))

    # A wall of boxes, 6 wide and 5 high, bricks offset on alternate rows.
    palette = [(0.85, 0.3, 0.25), (0.9, 0.6, 0.2), (0.3, 0.6, 0.85), (0.35, 0.75, 0.4), (0.7, 0.45, 0.85)]
    for row in range(5):
        for col in range(6):
            x = -2.5 + col + (0.5 if row % 2 else 0.0)
            level.add("Brick %d-%d" % (row + 1, col + 1),
                      {"MeshInstance": cube(palette[row]), "Collider": BOX, "RigidBody": body(mass=2.0)},
                      position=(r(x), r(0.5 + row * 1.0), 0))

    # A heavy metal ball thrown at the wall.
    level.add("Wrecking Ball", {"MeshInstance": sphere((0.75, 0.75, 0.8), 0.25, 1), "Collider": BALL,
                                "RigidBody": body(mass=60.0, restitution=0.2, velocity=(0, 3, -14))},
              position=(0, 1.5, 12), scale=(1.6, 1.6, 1.6))

    # A ramp (static, tilted 20 degrees about Z) with balls that roll down and bounce.
    half = math.radians(20) / 2
    level.add("Ramp", {"MeshInstance": cube((0.5, 0.42, 0.3), 0.8), "Collider": BOX},
              position=(-10, 2, -6), rotation=(0, 0, r(-math.sin(half)), r(math.cos(half))), scale=(8, 0.3, 4))
    for i in range(5):
        level.add("Ball %d" % (i + 1),
                  {"MeshInstance": sphere((0.95, 0.5 + 0.08 * i, 0.15), 0.35), "Collider": BALL,
                   "RigidBody": body(mass=1.0, restitution=0.6)},
                  position=(r(-13 + 0.2 * i), r(5.5 + 1.4 * i), r(-7.2 + 0.6 * i)), scale=(0.7, 0.7, 0.7))

    # A pyramid of small crates.
    for layer in range(4):
        count = 4 - layer
        for i in range(count):
            for j in range(count):
                level.add("Crate %d-%d-%d" % (layer + 1, i + 1, j + 1),
                          {"MeshInstance": cube((0.72, 0.52, 0.3), 0.7), "Collider": BOX, "RigidBody": body(mass=1.0)},
                          position=(r(9 + (i - (count - 1) / 2) * 0.82), r(0.4 + layer * 0.8),
                                    r(-6 + (j - (count - 1) / 2) * 0.82)),
                          scale=(0.8, 0.8, 0.8))

    # A lantern for a second light colour.
    level.add("Lantern", {"Light": {"type": "point", "color": [1, 0.6, 0.3], "intensity": 12, "range": 10}},
              position=(9, 4.5, -6))
    return level.dump()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the generated level is out of date")
    args = parser.parse_args()
    text = build()
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print("stale sample level (run tools/make_sample_levels.py): " + OUT.name, file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    print("wrote %s (%d bytes)" % (OUT.relative_to(OUT.parent.parent.parent), len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
