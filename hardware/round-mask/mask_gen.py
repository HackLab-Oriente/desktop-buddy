#!/usr/bin/env python3
"""Buddy round-face mask generator — parametric STL, no dependencies.

A slip-over bezel for a 1.28" round GC9A01 module (240x240): a front ring
frames the active circle and hides the PCB edge, a cylindrical wall friction-
fits the round board, a notch at the bottom clears the pin header/wires, and
two ears up top carry the buddy's identity.

MEASURE YOUR MODULE FIRST (round-module clones vary a lot):
  1. PCB_D     — diameter of the round PCB
  2. ACTIVE_D  — diameter of the lit circle (power it on and measure the glow)
  3. STACK     — PCB back to glass front (wall must be deeper than this)
Edit the constants, rerun:  python3 mask_gen.py  ->  buddy-round-mask.stl

Print (STAND=True): base flat on the bed. The face leans back only ~12deg
(near vertical) so it prints with little support; enable supports for the ear
undersides and the top arc of the round window, or set "supports on build
plate only". 0.2 mm layers, any PLA.
Print (STAND=False): the flat bezel, window face down, no supports.
"""
import math
import struct

# --- Measure these (defaults = common generic 1.28" round GC9A01) ---------
PCB_D    = 37.5   # round PCB diameter
ACTIVE_D = 32.4   # lit circle diameter (1.28")
STACK    = 4.5    # PCB back -> glass front

# --- Tunables --------------------------------------------------------------
WIN_D    = ACTIVE_D + 1.2   # window opening (a hair larger than the lit area)
CLR      = 0.40   # friction-fit radial clearance
WALL     = 1.8    # side-wall thickness
PLATE_T  = 2.0    # front bezel thickness
WALL_H   = 2.2    # wall depth past STACK to grip the PCB
NOTCH_W  = 16.0   # header/wire notch chord width at the bottom
SEG      = 120    # circle resolution
EARS     = True
EAR_H    = 11.0   # ear height above the rim

# Stand: tilt the face back so it "looks up", and grow a base to stand on.
STAND    = True
TILT     = 12.0   # degrees the face leans back (0 = straight up)
BASE_H   = 12.0   # how far the base swallows the disc bottom
BASE_W   = 46.0   # base footprint width (X)
BASE_FWD = 10.0   # base depth in front of the contact
BASE_BACK = 22.0  # base depth behind the contact (anti-tip bias)

tris = []


def _n(a, b, c):
    ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
    vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
    return (uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx)


def addt(a, b, c, want):
    n = _n(a, b, c)
    if n[0]*want[0] + n[1]*want[1] + n[2]*want[2] < 0:
        b, c = c, b
        n = _n(a, b, c)
    m = math.sqrt(n[0]**2 + n[1]**2 + n[2]**2) or 1.0
    tris.append(((n[0]/m, n[1]/m, n[2]/m), a, b, c))


def addq(a, b, c, d, want):
    addt(a, b, c, want)
    addt(a, c, d, want)


def P(r, ang, z):
    return (r*math.cos(ang), r*math.sin(ang), z)


# Kept arc of the ring = full circle minus a wedge at the bottom (-Y).
gap = 2 * math.asin(min(0.99, (NOTCH_W/2) / (PCB_D/2 + CLR)))  # notch half-angle*2
a_lo = -math.pi/2 + gap/2      # start of kept material
a_hi = -math.pi/2 + 2*math.pi - gap/2

R_out = PCB_D/2 + CLR + WALL
R_in  = PCB_D/2 + CLR          # wall inner (grips PCB)
R_win = WIN_D/2                # window
DEPTH = PLATE_T + STACK + WALL_H


def ring_solid(ri, ro, z0, z1, a0, a1, closed):
    """Annular arc solid ri..ro, z0..z1, over angles a0..a1.
    closed=True adds the two radial end caps (for the notched wall)."""
    step = (a1 - a0) / SEG
    for i in range(SEG):
        t0, t1 = a0 + i*step, a0 + (i+1)*step
        tm = (t0 + t1) / 2
        out = (math.cos(tm), math.sin(tm), 0)
        inw = (-out[0], -out[1], 0)
        # outer wall
        addq(P(ro,t0,z0), P(ro,t1,z0), P(ro,t1,z1), P(ro,t0,z1), out)
        # inner wall
        addq(P(ri,t0,z0), P(ri,t1,z0), P(ri,t1,z1), P(ri,t0,z1), inw)
        # top ring
        addq(P(ri,t0,z1), P(ro,t0,z1), P(ro,t1,z1), P(ri,t1,z1), (0,0,1))
        # bottom ring
        addq(P(ri,t0,z0), P(ro,t0,z0), P(ro,t1,z0), P(ri,t1,z0), (0,0,-1))
    if closed:
        for t, want in ((a0, (math.sin(a0), -math.cos(a0), 0)),
                        (a1, (-math.sin(a1), math.cos(a1), 0))):
            addq(P(ri,t,z0), P(ro,t,z0), P(ro,t,z1), P(ri,t,z1), want)


# Front bezel: full washer, window..outer, thin (PLATE_T). Overlaps the wall.
ring_solid(R_win, R_out, 0.0, PLATE_T, 0.0, 2*math.pi, closed=False)
# Wall: thick rim R_in..R_out, full depth, notched at the bottom.
ring_solid(R_in, R_out, 0.0, DEPTH, a_lo, a_hi, closed=True)


def prism(pts, z0, z1):
    a, b, c = pts
    addt((a[0],a[1],z1), (b[0],b[1],z1), (c[0],c[1],z1), (0,0,1))
    addt((a[0],a[1],z0), (b[0],b[1],z0), (c[0],c[1],z0), (0,0,-1))
    for (x0,y0), (x1,y1) in ((a,b), (b,c), (c,a)):
        want = (y1-y0, -(x1-x0), 0)
        addq((x0,y0,z0), (x1,y1,z0), (x1,y1,z1), (x0,y0,z1), want)


if EARS:
    ry = R_out + EAR_H
    prism([(-15, 10), (-3, 10), (-12, ry)], 0.0, PLATE_T)
    prism([(3, 10), (15, 10), (12, ry)], 0.0, PLATE_T)


def box(x0, x1, y0, y1, z0, z1):
    addq((x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0), (0,0,-1))
    addq((x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1), (0,0,1))
    addq((x0,y0,z0),(x0,y1,z0),(x0,y1,z1),(x0,y0,z1), (-1,0,0))
    addq((x1,y0,z0),(x1,y1,z0),(x1,y1,z1),(x1,y0,z1), (1,0,0))
    addq((x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1), (0,-1,0))
    addq((x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1), (0,1,0))


if STAND:
    # Rotate the whole mask about X so the ears point up and the face leans
    # back by TILT (looking up), then drop it onto z=0.
    A = math.radians(90 + TILT)
    ca, sa = math.cos(A), math.sin(A)
    rot = lambda p: (p[0], p[1]*ca - p[2]*sa, p[1]*sa + p[2]*ca)
    tris = [(rot(n), rot(a), rot(b), rot(c)) for (n, a, b, c) in tris]
    zmin = min(min(a[2], b[2], c[2]) for _, a, b, c in tris)
    dz = -zmin  # drop the disc's lowest point onto z=0 (base swallows it)
    tris = [(n, (a[0],a[1],a[2]+dz), (b[0],b[1],b[2]+dz), (c[0],c[1],c[2]+dz))
            for (n, a, b, c) in tris]
    # Contact point (lowest vertex) sets where the base sits.
    low = min((v for _, a, b, c in tris for v in (a, b, c)), key=lambda v: v[2])
    cy = low[1]
    # A base slab that swallows the disc's lower BASE_H and spreads a stable
    # footprint, biased backward against the lean. Overlaps the disc → unions.
    box(-BASE_W/2, BASE_W/2, cy - BASE_BACK, cy + BASE_FWD, 0.0, BASE_H)

# --- Write binary STL ------------------------------------------------------
out = "buddy-round-mask.stl"
with open(out, "wb") as f:
    f.write(b"BuddyZero round mask".ljust(80, b"\0"))
    f.write(struct.pack("<I", len(tris)))
    for n, v0, v1, v2 in tris:
        f.write(struct.pack("<12fH", *n, *v0, *v1, *v2, 0))

print(f"{out}: {len(tris)} triangles")
print(f"outer diameter {2*R_out:.1f} mm  (+ ears {EAR_H:.0f} mm up top)")
print(f"window diameter {2*R_win:.1f} mm  ·  bezel depth {DEPTH:.1f} mm")
print(f"notch {NOTCH_W:.0f} mm at bottom for the header/wires")
if STAND:
    print(f"stand: leans back {TILT:.0f}deg on a {BASE_W:.0f} mm base")
