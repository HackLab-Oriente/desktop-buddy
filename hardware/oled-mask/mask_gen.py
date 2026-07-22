#!/usr/bin/env python3
"""Buddy Zero OLED mask generator — parametric STL, no dependencies.

A slip-over face mask for a GME12864-class 0.96" SSD1306 module: front
plate hides the PCB, a window frames the active pixels, wrap-around walls
friction-fit the board, a notch clears the header pins. Plus cat ears.

MEASURE YOUR MODULE FIRST (clone dimensions drift):
  1. PCB_W x PCB_H            — PCB outline
  2. WIN_CY_FROM_PCB_BOTTOM   — PCB bottom edge to the *center* of the lit area
  3. STACK                    — PCB back to glass front (wall depth must cover it)
Edit the constants, rerun:  python3 mask_gen.py  ->  buddy-zero-oled-mask.stl

Print: window face down on the bed, no supports, 0.2 mm layers, any PLA.
"""
import math
import struct

# --- Measure these three (defaults = common GME12864 clone) ---------------
PCB_W = 27.3
PCB_H = 27.8
WIN_CY_FROM_PCB_BOTTOM = 12.0   # to center of active area
STACK = 4.2                     # PCB back -> glass front

# --- Tunables --------------------------------------------------------------
CLR = 0.30        # friction-fit clearance per side
WALL = 1.6        # side wall thickness
PLATE_T = 2.0     # face plate thickness
WALL_H = 2.0      # how far walls extend past STACK to grip the PCB
WIN_W, WIN_H = 24.0, 13.0   # window (active area 21.7 x 10.9 + margin)
NOTCH_W = 14.0    # header-pin notch in the top wall
EARS = True       # commit to the bit
EAR_W, EAR_H = 12.0, 9.0

tris = []  # (normal, v0, v1, v2)


def quad(a, b, c, d, n):
    tris.append((n, a, b, c))
    tris.append((n, a, c, d))


def box(x0, y0, z0, x1, y1, z1):
    quad((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (0, 0, -1))
    quad((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1), (0, 0, 1))
    quad((x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1), (-1, 0, 0))
    quad((x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0), (1, 0, 0))
    quad((x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (0, -1, 0))
    quad((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1), (0, 1, 0))


def prism(p0, p1, p2, z0, z1):
    """Triangular prism from 3 CCW xy points."""
    (ax, ay), (bx, by), (cx, cy) = p0, p1, p2
    tris.append(((0, 0, -1), (ax, ay, z0), (cx, cy, z0), (bx, by, z0)))
    tris.append(((0, 0, 1), (ax, ay, z1), (bx, by, z1), (cx, cy, z1)))
    for (x0, y0), (x1, y1) in ((p0, p1), (p1, p2), (p2, p0)):
        nx, ny = y1 - y0, -(x1 - x0)
        ln = math.hypot(nx, ny) or 1.0
        quad((x0, y0, z0), (x1, y1, z0), (x1, y1, z1), (x0, y0, z1),
             (nx / ln, ny / ln, 0))


# --- Build -----------------------------------------------------------------
W = PCB_W + 2 * (CLR + WALL)
H = PCB_H + 2 * (CLR + WALL)
DEPTH = STACK + WALL_H

# Face plate = four border boxes around the window
wx0 = (W - WIN_W) / 2
wy0 = WALL + CLR + WIN_CY_FROM_PCB_BOTTOM - WIN_H / 2
wx1, wy1 = wx0 + WIN_W, wy0 + WIN_H
box(0, 0, 0, W, wy0, PLATE_T)                # below window
box(0, wy1, 0, W, H, PLATE_T)                # above window
box(0, wy0, 0, wx0, wy1, PLATE_T)            # left of window
box(wx1, wy0, 0, W, wy1, PLATE_T)            # right of window

# Walls wrap the PCB (z: behind the plate)
z0, z1 = PLATE_T, PLATE_T + DEPTH
box(0, 0, z0, WALL, H, z1)                                   # left
box(W - WALL, 0, z0, W, H, z1)                               # right
box(WALL, 0, z0, W - WALL, WALL, z1)                         # bottom
nx0, nx1 = (W - NOTCH_W) / 2, (W + NOTCH_W) / 2              # top, notched
box(WALL, H - WALL, z0, nx0, H, z1)
box(nx1, H - WALL, z0, W - WALL, H, z1)

if EARS:
    prism((3.0, H), (3.0 + EAR_W, H), (3.0 + EAR_W * 0.35, H + EAR_H), 0, PLATE_T)
    prism((W - 3.0 - EAR_W, H), (W - 3.0, H),
          (W - 3.0 - EAR_W * 0.35, H + EAR_H), 0, PLATE_T)

# --- Write binary STL ------------------------------------------------------
out = "buddy-zero-oled-mask.stl"
with open(out, "wb") as f:
    f.write(b"BuddyZero OLED mask".ljust(80, b"\0"))
    f.write(struct.pack("<I", len(tris)))
    for n, v0, v1, v2 in tris:
        f.write(struct.pack("<12fH", *n, *v0, *v1, *v2, 0))

print(f"{out}: {len(tris)} triangles")
print(f"outer {W:.1f} x {H + (EAR_H if EARS else 0):.1f} x {PLATE_T + DEPTH:.1f} mm")
print(f"window {WIN_W:.1f} x {WIN_H:.1f} at y {wy0:.1f}..{wy1:.1f}")
