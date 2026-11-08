#!/usr/bin/env python3
"""
Old Man Weather - 3D-printed tabletop case for the
Waveshare ESP32-S3-Touch-LCD-7B (7", 1024x600).

Two printed parts:
  1. front_tray  - holds the display with its glass flush to the front. Four M3 x 20
                   screws go through the back into the display frame's threaded tabs.
  2. stand       - sloped tabletop stand (18 deg tilt) with a ballast well and a vented
                   pocket for the BME280, away from the electronics' heat.

All display geometry comes from Waveshare's STEP model (hardware/dimensions in
github.com/waveshareteam/ESP32-S3-Touch-LCD-7B) and its dimension drawing.

Coordinates (tray "local"): X to the viewer's right, Y up the screen, Z toward the
viewer; Z = 0 is the front face of the glass, the glass is centred on X = Y = 0.

Usage:
    pip install cadquery
    python3 case.py                         # writes STL + STEP into ./out
    python3 case.py --check board.stp       # also checks clearances against the model
Change parameters below (tilt, wall, screw length...) and re-run.
"""
import argparse
import math
from pathlib import Path

import cadquery as cq

# ----------------------------------------------------------------------------- display
GLASS_W, GLASS_H, GLASS_R, GLASS_T = 192.96, 110.76, 7.88, 1.0
# Rear of the display behind the glass border (LCD module incl. frame), local coords
MODULE = dict(x0=-84.4, x1=81.1, y0=-47.5, y1=50.8)
# Threaded M3 tabs on the display frame (back face 8.5 mm behind the glass front)
TABS = [(-64.59, -33.81), (-64.59, 31.85), (61.61, -33.81), (61.61, 31.85)]
TAB_Z = -8.5
DEEPEST_PART_Z = -18.4          # tallest connectors on the board
PCB = dict(x0=-50.9, x1=55.1, y0=-47.2, y1=25.8)
USB_PORTS = [(55.0, -11.5), (55.0, 3.6)]   # right edge of the board, opening faces +X
SD_SLOT = dict(x0=28.2, x1=44.3, y_top=25.7)  # card inserts downward from the top edge
HEADERS_X = (-50.4, -45.4)       # HY2.0 sensor/I2C headers on the left

# ----------------------------------------------------------------------------- case
CLEAR = 0.35        # gap around the glass
WALL = 4.0          # side wall
LEDGE = 3.0         # glass rests on this ledge (inside the black border)
LEDGE_Z = -(GLASS_T + 0.05)
INNER_DEPTH = 28.0  # glass front to inside of back wall
BACK_T = 3.0
EDGE_CHAMFER = 1.0

SCREW_LEN = 20.0            # M3 x 20 into the display tabs
TAB_ENGAGE = 1.7            # thread length in the tab
STANDOFF_GAP = 0.3          # standoff stops short so screws pull the glass onto the ledge
STANDOFF_D = 8.0
STANDOFF_TIP_D = 6.0        # narrower last 3 mm near the display frame
SCREW_CLEAR_D = 3.4
HEAD_D = 6.4

STAND_BOSSES = [(-70, -42), (-70, 42), (70, -42), (70, 42)]
BOSS_D, BOSS_H, PILOT_D = 9.0, 8.0, 2.7   # M3 x 12 self-tapping into PLA/PETG

TILT_DEG = 18.0             # screen leans back this much from vertical
STAND_W = 160.0
STAND_PLATE_T = 4.0
BASE_T = 5.0
BASE_DEPTH = 115.0
CHEEK_T = 5.0

OUT = Path(__file__).resolve().parent / "out"

OUTER_W = GLASS_W + 2 * (CLEAR + WALL)
OUTER_H = GLASS_H + 2 * (CLEAR + WALL)
OUTER_R = GLASS_R + CLEAR + WALL
BACK_Z = -(INNER_DEPTH + BACK_T)


def rrect(w, h, r, z0, z1, x=0.0, y=0.0):
    """Rounded rectangle prism from z0 to z1 (z0 < z1)."""
    return (cq.Workplane("XY").workplane(offset=z0).center(x, y)
            .rect(w, h).extrude(z1 - z0).edges("|Z").fillet(r))


def cyl(d, z0, z1, x, y):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2).extrude(z1 - z0)


def front_tray():
    body = rrect(OUTER_W, OUTER_H, OUTER_R, BACK_Z, 0)
    body = body.faces(">Z").edges().chamfer(EDGE_CHAMFER)
    # Glass pocket (flush front) and the cavity behind the ledge.
    body = body.cut(rrect(GLASS_W + 2 * CLEAR, GLASS_H + 2 * CLEAR, GLASS_R + CLEAR, LEDGE_Z, 1))
    body = body.cut(rrect(GLASS_W - 2 * LEDGE, GLASS_H - 2 * LEDGE, GLASS_R - LEDGE + 1.5,
                          -INNER_DEPTH, LEDGE_Z + 0.01))

    # Service opening over the board (cables, SD card), plus a slot for a 90-degree
    # USB-C plug on the board's right edge.
    opening = rrect(PCB["x1"] - PCB["x0"] + 4, 80, 6, BACK_Z - 1, -INNER_DEPTH + 0.01, x=(PCB["x0"] + PCB["x1"]) / 2, y=-10)
    usb_slot = rrect(20, 26, 4, BACK_Z - 1, -INNER_DEPTH + 0.01, x=64, y=-4)
    body = body.cut(opening).cut(usb_slot)

    # Standoffs to the display's M3 tabs.
    top = TAB_Z - STANDOFF_GAP
    head_seat = TAB_Z + TAB_ENGAGE - SCREW_LEN          # screw tip ends at the far face of the tab
    for x, y in TABS:
        s = cyl(STANDOFF_D, -INNER_DEPTH - 0.01, top - 3, x, y).union(cyl(STANDOFF_TIP_D, top - 3.01, top, x, y))
        body = body.union(s)
        body = body.cut(cyl(SCREW_CLEAR_D, BACK_Z - 1, top + 1, x, y))
        body = body.cut(cyl(HEAD_D, BACK_Z - 1, head_seat, x, y))

    # Bosses the stand screws into.
    for x, y in STAND_BOSSES:
        body = body.union(cyl(BOSS_D, -INNER_DEPTH - 0.01, -INNER_DEPTH + BOSS_H, x, y))
        body = body.cut(cyl(PILOT_D, BACK_Z - 1, -INNER_DEPTH + BOSS_H - 1.5, x, y))
    return body


# ----------------------------------------------------------------------------- placement
def tray_to_world():
    """Location that tilts the tray back by TILT_DEG and sets it on the stand base.

    World frame: x right, y toward the back, z up.  Local +Z (toward viewer) maps to
    (0, -cos t, sin t); local +Y (screen up) maps to (0, sin t, cos t).
    """
    t = math.radians(TILT_DEG)
    # Lowest point of the tilted tray is its bottom-back edge.
    y_loc, z_loc = -OUTER_H / 2, BACK_Z
    lowest_z = y_loc * math.cos(t) + z_loc * math.sin(t)
    front_bottom_y = -(0 * math.cos(t)) + y_loc * math.sin(t)
    dz = BASE_T - lowest_z
    dy = 12.0 - front_bottom_y          # front-bottom edge 12 mm behind the base's front edge
    return t, dy, dz


def place(wp):
    """Rotate/translate a tray-local Workplane into the world frame."""
    t, dy, dz = tray_to_world()
    # Local->world: (X, Y, Z) -> (X, Y sin t - Z cos t, Y cos t + Z sin t).
    # As a rotation: first map local (X,Y,Z) -> (X, -Z, Y) [rotate -90 about X... done below],
    # then tilt back by t about X.
    s = wp.rotate((0, 0, 0), (1, 0, 0), 90)       # (X, Y, Z) -> (X, -Z, Y)
    s = s.rotate((0, 0, 0), (1, 0, 0), -TILT_DEG)  # lean the top back (toward +y)
    return s.translate((0, dy, dz))


def local_to_world(X, Y, Z):
    t, dy, dz = tray_to_world()
    y = -Z
    z = Y
    # rotate by -t about x: y' = y cos(-t) - z sin(-t), z' = y sin(-t) + z cos(-t)
    return (X, y * math.cos(t) + z * math.sin(t) + dy, -y * math.sin(t) + z * math.cos(t) + dz)


def stand(tray_world):
    # Sloped plate directly behind the tray back (built in tray-local coordinates).
    plate_y0, plate_y1 = -OUTER_H / 2 + 2, 50.0
    plate = (cq.Workplane("XY").workplane(offset=BACK_Z - STAND_PLATE_T)
             .center(0, (plate_y0 + plate_y1) / 2).rect(STAND_W, plate_y1 - plate_y0).extrude(STAND_PLATE_T))
    # Openings matching the tray's service opening and USB slot (cables + SD access).
    # The service opening runs out through the top edge so the plate prints without a
    # long bridge.
    plate = plate.cut(rrect(PCB["x1"] - PCB["x0"] + 4, 200, 6, BACK_Z - 10, BACK_Z + 1,
                            x=(PCB["x0"] + PCB["x1"]) / 2, y=-50 + 100))
    plate = plate.cut(rrect(20, 26, 4, BACK_Z - 10, BACK_Z + 1, x=64, y=-4))
    for x, y in STAND_BOSSES:
        plate = plate.cut(cyl(3.4, BACK_Z - 10, BACK_Z + 1, x, y))
        plate = plate.cut(cyl(6.4, BACK_Z - 10, BACK_Z - 2.0, x, y))   # screw head recess
    plate_w = place(plate)

    # Base plate.
    base = cq.Workplane("XY").box(STAND_W + 2 * CHEEK_T, BASE_DEPTH, BASE_T, centered=(True, False, False))

    # Side cheeks: triangles joining the back of the sloped plate to the base.
    top_back = local_to_world(0, plate_y1, BACK_Z - STAND_PLATE_T)
    bot_back = local_to_world(0, plate_y0, BACK_Z - STAND_PLATE_T)
    cheeks = None
    for sx in (-1, 1):
        x0 = sx * (STAND_W / 2 + CHEEK_T / 2)
        pts = [(bot_back[1] - 8, BASE_T - 0.01), (top_back[1], top_back[2]),
               (top_back[1] + 6, top_back[2] - 4), (BASE_DEPTH - 2, BASE_T - 0.01)]
        prof = (cq.Workplane("YZ").workplane(offset=x0 - CHEEK_T / 2).polyline(pts).close().extrude(CHEEK_T))
        cheeks = prof if cheeks is None else cheeks.union(prof)
    # Short wall joining the cheeks under the sloped plate (stiffness).
    brace = cq.Workplane("XY").box(STAND_W, 4, 22, centered=(True, False, False)).translate((0, BASE_DEPTH - 4, BASE_T - 0.01))

    # Ballast well in the rear of the base: fill with steel washers, coins or fishing
    # weights and seal with tape or a little glue.
    well_outer = cq.Workplane("XY").box(110, 44, 14, centered=(True, False, False)).translate((10, BASE_DEPTH - 52, BASE_T - 0.01))
    well_inner = cq.Workplane("XY").box(106, 40, 14, centered=(True, False, False)).translate((10, BASE_DEPTH - 50, BASE_T + 0.5))

    # Vented BME280 pocket at the rear-left corner, open to room air through slots and
    # closed to the case interior except for a cable hole.
    px0, py0 = -STAND_W / 2 - CHEEK_T, BASE_DEPTH - 32
    pocket_outer = cq.Workplane("XY").box(34, 32, 20, centered=False).translate((px0, py0, BASE_T - 0.01))
    pocket_inner = cq.Workplane("XY").box(26, 26, 30, centered=False).translate((px0 + 3, py0 + 3, BASE_T))  # open top
    vents = None
    for i in range(5):
        v = cq.Workplane("XY").box(10, 2.2, 11, centered=False).translate((px0 - 1, py0 + 5 + i * 4.6, BASE_T + 4))
        vents = v if vents is None else vents.union(v)
    for i in range(5):
        v = cq.Workplane("XY").box(2.2, 10, 11, centered=False).translate((px0 + 5 + i * 5, py0 + 26, BASE_T + 4))
        vents = vents.union(v)

    # Rear notch in the brace for the USB cable.
    notch = cq.Workplane("XY").box(16, 10, 12, centered=(True, False, False)).translate((60, BASE_DEPTH - 6, BASE_T - 0.02))

    s = (base.union(plate_w).union(cheeks).union(brace).union(well_outer).union(pocket_outer)
         .cut(well_inner).cut(pocket_inner).cut(vents).cut(notch))
    s = s.cut(cq.Workplane("XY").box(8, 7, 7).translate((px0 + 34, py0 + 13, BASE_T + 9)))  # cable hole
    # Never intrude on the tray itself.
    s = s.cut(tray_world)
    # Felt/rubber feet recesses (10 mm bumpers) under the four corners.
    for fx in (-STAND_W / 2 + 6, STAND_W / 2 - 6):
        for fy in (10, BASE_DEPTH - 10):
            s = s.cut(cyl(10.6, -0.01, 0.8, fx, fy))
    return s


# ----------------------------------------------------------------------------- checks
def check_against_model(step_path, tray):
    """Collision/clearance report against Waveshare's STEP model."""
    model = cq.importers.importStep(str(step_path))
    # model (x, y, z) -> local (X, Y, Z) = (-x, z - 1.02, y - 0.8): a proper rotation.
    # Rotate +90 about X: (x,y,z)->(x,-z,y); then 180 about Z: ->(-x,z,y). Then shift so
    # the glass front is at Z=0 and the glass is centred.
    m = model.rotate((0, 0, 0), (1, 0, 0), 90).rotate((0, 0, 0), (0, 0, 1), 180).translate((0, -1.02, -0.8))
    bb = m.val().BoundingBox()
    print(f"model in tray coords: X {bb.xmin:.2f}..{bb.xmax:.2f}  Y {bb.ymin:.2f}..{bb.ymax:.2f}  Z {bb.zmin:.2f}..{bb.zmax:.2f}")
    inter = tray.val().intersect(m.val())
    vol = inter.Volume() if inter.isValid() else float("nan")
    print(f"tray/display overlap volume: {vol:.3f} mm^3 (0 = no collision)")
    return vol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", help="path to ESP32-S3-Touch-LCD-7B.stp for a clearance check")
    args = ap.parse_args()
    OUT.mkdir(exist_ok=True)

    tray = front_tray()
    tray_w = place(tray)
    st = stand(tray_w)

    for name, part, orient in (("front_tray", tray, None), ("stand", st, None)):
        cq.exporters.export(part, str(OUT / f"{name}.stl"), tolerance=0.05, angularTolerance=0.1)
        cq.exporters.export(part, str(OUT / f"{name}.step"))
        bb = part.val().BoundingBox()
        print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, volume {part.val().Volume() / 1000:.0f} cm^3")
    asm = tray_w.union(st)
    cq.exporters.export(asm, str(OUT / "assembly_preview.step"))
    cq.exporters.export(tray_w, str(OUT / "tray_placed.stl"), tolerance=0.1)
    cq.exporters.export(st, str(OUT / "stand_placed.stl"), tolerance=0.1)

    top = TAB_Z - STANDOFF_GAP
    head_seat = TAB_Z + TAB_ENGAGE - SCREW_LEN
    print(f"standoff top Z {top:.2f}, screw head seat Z {head_seat:.2f}, back face Z {BACK_Z:.2f}"
          f" -> counterbore depth {head_seat - BACK_Z:.2f} mm")
    print(f"clearance behind tallest parts: {DEEPEST_PART_Z - (-INNER_DEPTH):.1f} mm")
    if args.check:
        check_against_model(args.check, tray)


if __name__ == "__main__":
    main()
