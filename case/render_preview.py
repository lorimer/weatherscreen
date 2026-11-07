#!/usr/bin/env python3
"""Renders shaded preview PNGs of the case from the STL files in ./out."""
import sys
from pathlib import Path
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

OUT = Path(__file__).resolve().parent / "out"

def shade(mesh, color, light=np.array([0.4, -0.7, 0.6])):
    light = light / np.linalg.norm(light)
    n = mesh.face_normals
    k = 0.35 + 0.65 * np.clip(n @ light, 0, 1)
    base = np.array(matplotlib.colors.to_rgb(color))
    return np.clip(base[None, :] * k[:, None], 0, 1)

def view(parts, elev, azim, name, title):
    fig = plt.figure(figsize=(9, 7), dpi=110)
    ax = fig.add_subplot(111, projection="3d")
    # One collection so matplotlib depth-sorts faces across all parts.
    tris = np.vstack([m.triangles for m, _ in parts])
    cols = np.vstack([shade(m, c) for m, c in parts])
    ax.add_collection3d(Poly3DCollection(tris, facecolors=cols, edgecolors="none"))
    v = np.vstack([m.vertices for m, _ in parts])
    c, r = (v.max(0) + v.min(0)) / 2, (v.max(0) - v.min(0)).max() / 2
    ax.set_xlim(c[0] - r, c[0] + r); ax.set_ylim(c[1] - r, c[1] + r); ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(elev=elev, azim=azim); ax.set_axis_off()
    ax.set_title(title)
    fig.savefig(OUT / name, bbox_inches="tight"); plt.close(fig)

tray = trimesh.load(OUT / "tray_placed.stl")
stand = trimesh.load(OUT / "stand_placed.stl")
# Stand-in for the display glass so the front view reads correctly.
glass = trimesh.creation.box((192.9, 1.0, 110.7))
import math
sys.path.insert(0, str(Path(__file__).resolve().parent))
import case
t = math.radians(case.TILT_DEG)
x, y, z = case.local_to_world(0, 0, -0.5)
R = trimesh.transformations.rotation_matrix(-t, [1, 0, 0])
glass.apply_transform(R); glass.apply_translation((x, y, z))
parts = [(tray, "#E8E2D6"), (stand, "#C9C1B0"), (glass, "#1d1d22")]
view(parts, 12, -70, "preview_front.png", "Front three-quarter")
view(parts, 0, 0, "preview_side.png", "Side (18 deg tilt)")
view(parts, 25, 60, "preview_back.png", "Back: service opening, ballast well, sensor pocket")
view([(trimesh.load(OUT / "front_tray.stl"), "#E8E2D6")], -60, -90, "preview_tray_back.png", "Front tray from behind")
print("ok")
