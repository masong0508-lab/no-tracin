#!/usr/bin/env python3
"""Builds src/tracks_data.h: the three Virtua-style circuits.

Each course is a closed Catmull-Rom spline through hand-placed control
points (x, z, height, attributes), resampled every ~STEP world units. The
layouts are drawn from memory of Virtua Racing's Big Forest, Bay Bridge and
Acropolis (corner order, hairpins, the bridge, hills and landmarks). The
control points below are on a 1/SCALE plan; build() blows them up (heights
by HSCALE) so that a real-sized car (2 m x 4.4 m) sits on a road and in a
circuit of real proportions. Nothing is read from any ROM.

Also writes the lookup grids the game uses to find the nearest stretch of
road for any point, places the scenery, and writes src/tracks_dims.h with
the dimensions the C code shares with this script.

    python3 tools/mktracks.py            # writes src/tracks_data.h and src/tracks_dims.h
    python3 tools/mktracks.py --preview  # also writes build/track_*.png maps
"""
import math
import random
import sys
import os

SCALE = 2.0        # the plans below are drawn at 1/SCALE (20 world units = 1 m)
HSCALE = 1.4       # heights grow less than the plan, so the grades get gentler
STEP = 144         # resample spacing, world units
CELL_SHIFT = 7     # nearest-road grid: 128-unit cells
CELL = 1 << CELL_SHIFT
GRID = 128         # 128 x 128 cells over the world
WORLD = CELL * GRID   # circuits lie inside 0..WORLD on x and z (16384)
PCELL_SHIFT = 9    # scenery grid: 512-unit cells
PCELL = 1 << PCELL_SHIFT
PGRID = WORLD // PCELL
HALF_W = 150       # road half width: a 15 m track
VERGE = 160        # grass between the road edge and the barrier (u8)
KERB_R = 900 * SCALE   # corners tighter than this get kerbs and tyre walls
STRAIGHT = 4000 * SCALE  # radius recorded for straights (u16)
MAX_H = 510        # Prop.y2 holds ground height / 2 in a u8
# Scenery keeps its real size, but the bigger in-fields want more of it:
# trees, buildings and columns stand DENSITY times as close along the road
# in a band SPREAD times as deep (the valley walls and rocks hug the road).
DENSITY = 1.6
SPREAD = 2.0

# Control-point attributes (apply from this point to the next).
BRIDGE = 1         # no verge, railings at the road edge, side girders
TUNNEL = 2         # walls and a roof close in over the road

# Scenery types (must match track.c).
P_TREE, P_PINE, P_BUILDING, P_STAND, P_FERRIS, P_TENT, P_TOWER, P_ROCK, \
    P_COLUMN, P_TEMPLE, P_CRANE, P_LIGHTHOUSE, P_BALLOON, P_HOUSE, P_SIGN, P_BOAT, \
    P_CLIFF, P_COASTER = range(18)

# Track point flags (must match track.c).
F_KERB_L, F_KERB_R, F_BRIDGE, F_START, F_TUNNEL = 1, 2, 4, 8, 16


# ---------------------------------------------------------------- the courses

# (x, z, height, attr) on the 1/SCALE plan (water and landmarks too).
# Clockwise unless noted; heading 0 is +z (north), turning right goes
# toward +x. laps, start_time and cp_time set the arcade race: laps, and
# seconds on the clock at the start and at each checkpoint.
BIG_FOREST = dict(
    name="BIG FOREST", start=1, level="BEGINNER", laps=3, start_time=55, cp_time=28,
    theme="forest",
    points=[
        (2000, 1700, 0, 0),
        (2000, 2600, 0, 0),
        (2000, 3500, 10, 0),
        (2100, 4200, 50, 0),       # long right sweeper, climbing into the hills
        (2500, 4750, 90, 0),
        (3200, 4980, 130, 0),
        (3900, 4980, 150, 0),      # crest
        (4350, 4800, 140, 0),      # chicane
        (4750, 5000, 120, 0),
        (5250, 4900, 90, 0),
        (5800, 4500, 55, 0),       # downhill toward the park
        (6050, 3800, 30, 0),
        (6060, 3000, 20, 0),
        (6000, 2300, 20, 0),
        (5800, 1950, 20, 0),       # amusement park hairpin (right, 180)
        (5500, 1880, 20, 0),
        (5250, 2050, 20, 0),
        (5180, 2500, 20, 0),
        (5150, 3000, 20, 0),
        (4900, 3350, 20, 0),       # left onto the back straight
        (4400, 3400, 30, 0),
        (3900, 3250, 30, BRIDGE),  # over the creek
        (3500, 3050, 30, 0),
        (3250, 2700, 20, 0),       # fast right-hander
        (3300, 2100, 10, 0),
        (3150, 1500, 0, 0),
        (2700, 1150, 0, 0),        # last right onto the straight
        (2200, 1200, 0, 0),
    ],
    water=[(3620, 2200, 3820, 4400)],
    landmarks=[
        (P_FERRIS, 5560, 1450, 0),
        (P_COASTER, 6520, 2950, 0),
        (P_COASTER, 4550, 1500, 0),
        (P_TENT, 6400, 2000, 0),
        (P_TENT, 6450, 2500, 1),
        (P_BALLOON, 4300, 4300, 0),
        (P_STAND, 1790, 2300, 1),
        (P_STAND, 1790, 2700, 1),
        (P_SIGN, 2200, 3300, 1),
        (P_HOUSE, 4700, 2600, 0),
        (P_HOUSE, 4600, 2200, 1),
    ],
    zones=[  # (from point, to point, kind, spacing)
        (0, 28, P_TREE, 120),
        (3, 13, P_PINE, 150),
        (19, 26, P_PINE, 160),
    ],
)

BAY_BRIDGE = dict(
    name="BAY BRIDGE", start=1, level="MEDIUM", laps=3, start_time=48, cp_time=24,
    theme="bay",
    points=[
        (2300, 1500, 0, 0),
        (3200, 1500, 0, 0),
        (3900, 1550, 0, 0),
        (4400, 1800, 0, 0),        # left sweeper toward the bridge
        (4650, 2300, 10, 0),
        (4700, 2800, 50, 0),       # climb
        (4700, 3150, 110, BRIDGE),
        (4700, 4100, 140, BRIDGE),  # bridge deck
        (4700, 5050, 110, 0),
        (4700, 5500, 50, 0),
        (4600, 5950, 10, 0),       # left at the far end
        (4250, 6250, 0, TUNNEL),   # tunnel under the headland
        (3700, 6300, 0, TUNNEL),
        (3200, 6250, 0, 0),        # city chicane
        (2850, 6050, 0, 0),
        (2500, 6250, 0, 0),
        (2100, 6200, 0, 0),
        (1750, 5900, 0, 0),        # left, down the waterfront
        (1650, 5300, 0, 0),
        (1900, 4700, 10, 0),       # S-bends through town
        (1650, 4100, 20, 0),
        (1900, 3500, 20, 0),
        (1700, 2900, 10, 0),
        (1450, 2400, 0, 0),
        (1500, 1850, 0, 0),        # tight left onto the harbour straight
        (1800, 1530, 0, 0),
    ],
    water=[(2600, 3150, 7400, 5000), (900, 300, 7400, 1150)],
    landmarks=[
        (P_LIGHTHOUSE, 6200, 4300, 0),
        (P_BOAT, 3600, 3800, 0),
        (P_BOAT, 5600, 4500, 1),
        (P_BOAT, 3000, 900, 0),
        (P_CRANE, 3000, 1220, 0),
        (P_CRANE, 3600, 1220, 0),
        (P_STAND, 2900, 1720, 2),
        (P_STAND, 3400, 1720, 2),
    ],
    zones=[
        (11, 25, P_BUILDING, 170),
        (0, 4, P_HOUSE, 220),
        (8, 11, P_TREE, 140),
    ],
)

ACROPOLIS = dict(
    name="ACROPOLIS", start=1, level="EXPERT", laps=3, start_time=56, cp_time=30,
    theme="acropolis",
    points=[
        (1800, 1800, 0, 0),
        (1800, 2600, 10, 0),
        (1800, 3300, 40, 0),
        (1650, 3900, 80, 0),       # fast left kink, climbing the valley
        (1900, 4500, 120, 0),      # esses
        (1700, 5100, 160, 0),
        (1950, 5650, 200, 0),
        (2400, 6000, 230, 0),
        (2900, 6250, 250, 0),      # temple hairpin (right, 180)
        (3250, 6150, 250, 0),
        (3300, 5800, 235, 0),
        (2950, 5450, 210, 0),
        (2850, 5000, 180, 0),
        (3200, 4500, 140, 0),      # long downhill
        (3900, 4300, 90, 0),
        (4600, 4450, 70, 0),       # double-apex right
        (5300, 4300, 60, 0),
        (5650, 3800, 50, 0),
        (5600, 3200, 40, 0),
        (5900, 2700, 30, 0),       # fast left
        (5750, 2150, 20, 0),
        (5250, 1900, 20, 0),
        (4600, 2050, 20, 0),
        (4050, 2400, 30, 0),       # left, then the switchback
        (3500, 2550, 30, 0),
        (3050, 2350, 20, 0),
        (2950, 1900, 10, 0),
        (2750, 1350, 0, 0),
        (2350, 1100, 0, 0),        # last hairpin onto the straight
        (1950, 1250, 0, 0),
    ],
    water=[],
    landmarks=[
        (P_TEMPLE, 3050, 6650, 0),
        (P_TEMPLE, 2350, 5600, 1),
        (P_STAND, 1590, 2300, 1),
        (P_SIGN, 2100, 3700, 1),
        (P_TOWER, 4300, 5000, 0),
    ],
    zones=[
        (2, 8, P_CLIFF, 120),      # the valley walls
        (16, 20, P_CLIFF, 140),
        (2, 14, P_ROCK, 130),
        (13, 23, P_COLUMN, 260),
        (0, 30, P_PINE, 210),
    ],
)

COURSES = [BIG_FOREST, BAY_BRIDGE, ACROPOLIS]

# Footprint radius of each landmark as track.c draws it: keeps the scenery
# and the road clear of it (the game culls with the same sizes).
LANDMARK_R = {P_STAND: 330, P_FERRIS: 260, P_TENT: 100, P_TOWER: 40, P_TEMPLE: 300,
              P_CRANE: 335, P_LIGHTHOUSE: 60, P_BALLOON: 100, P_HOUSE: 80, P_SIGN: 120,
              P_BOAT: 100, P_COASTER: 545}
# Landmarks the game draws from afar, from a list of their own (the rest of
# the scenery goes in the grid and is only drawn close by).
BIG = {P_STAND, P_FERRIS, P_TOWER, P_TEMPLE, P_CRANE, P_LIGHTHOUSE, P_BALLOON, P_COASTER}
# Grandstands stand back from their spot and signs run along the road,
# facing it, and the cranes' jibs reach back over the water: only this
# much of one reaches toward the road.
LANDMARK_FRONT = {P_STAND: 20, P_SIGN: 10, P_CRANE: 130}


def scaled(course):
    """The course blown up from its plan to the world: control points, water
    and landmarks by SCALE, heights by HSCALE."""
    c = dict(course)
    c["points"] = [(x * SCALE, z * SCALE, y * HSCALE, a) for x, z, y, a in course["points"]]
    c["water"] = [tuple(round(v * SCALE) for v in w) for w in course["water"]]
    c["landmarks"] = [(t, round(x * SCALE), round(z * SCALE), rot) for t, x, z, rot in course["landmarks"]]
    return c


# ---------------------------------------------------------------- geometry

def catmull(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                  (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def resample(points, start):
    n = len(points)
    points = points[start:] + points[:start]   # the start line is on the first point
    dense = []   # (x, z, y, attr)
    for i in range(n):
        p0, p1, p2, p3 = (points[(i + k) % n] for k in (-1, 0, 1, 2))
        subs = int(64 * SCALE)
        for s in range(subs):
            t = s / subs
            x = catmull(p0[0], p1[0], p2[0], p3[0], t)
            z = catmull(p0[1], p1[1], p2[1], p3[1], t)
            y = catmull(p0[2], p1[2], p2[2], p3[2], t)
            dense.append((x, z, y, p1[3], (i + start) % n))
    # Arc length.
    cum = [0.0]
    for i in range(1, len(dense) + 1):
        a, b = dense[i - 1], dense[i % len(dense)]
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]
    count = max(8, round(total / STEP))
    out = []
    j = 0
    for k in range(count):
        d = total * k / count
        while cum[j + 1] < d:
            j += 1
        a, b = dense[j], dense[(j + 1) % len(dense)]
        f = (d - cum[j]) / max(1e-9, cum[j + 1] - cum[j])
        out.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f,
                    max(0.0, a[2] + (b[2] - a[2]) * f), a[3], a[4]))
    return out, total


def heading_of(dx, dz):
    return math.atan2(dx, dz)   # 0 = +z, positive turns toward +x


def build(course, seed):
    course = scaled(course)
    pts, total = resample(course["points"], course.get("start", 0))
    n = len(pts)
    rec = []
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        dx, dz = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dz)
        rec.append(dict(x=a[0], z=a[1], y=a[2], attr=a[3], cp=a[4],
                        ux=dx / ln, uz=dz / ln, len=ln, head=heading_of(dx, dz)))
    # Curvature from the heading change over neighbouring samples.
    for i in range(n):
        h0, h1 = rec[i - 1]["head"], rec[(i + 1) % n]["head"]
        dh = (h1 - h0 + math.pi) % (2 * math.pi) - math.pi
        arc = rec[i - 1]["len"] + rec[i]["len"]
        rec[i]["radius"] = min(STRAIGHT, abs(arc / dh) if abs(dh) > 1e-6 else STRAIGHT)
        rec[i]["turn"] = dh
    # Smooth the radius a little (kerbs on corners, speeds for the CPU cars).
    rad = [r["radius"] for r in rec]
    for i in range(n):
        rec[i]["radius"] = min(rad[i - 1], rad[i], rad[(i + 1) % n])
    for i, r in enumerate(rec):
        f = 0
        if r["radius"] < KERB_R:
            f |= F_KERB_L | F_KERB_R
        if r["attr"] & BRIDGE:
            f |= F_BRIDGE
        if r["attr"] & TUNNEL:
            f |= F_TUNNEL
        if i == 0:
            f |= F_START
        r["flags"] = f
        r["verge"] = 0 if f & F_BRIDGE else 14 if f & F_TUNNEL else VERGE
    check_clearance(course["name"], rec)
    props = place_scenery(course, rec, seed)
    return rec, total, props


def seg_dist(px, pz, r):
    vx, vz = px - r["x"], pz - r["z"]
    t = max(0.0, min(r["len"], vx * r["ux"] + vz * r["uz"]))
    cx, cz = r["x"] + r["ux"] * t, r["z"] + r["uz"] * t
    return math.hypot(px - cx, pz - cz)


def nearest(px, pz, rec):
    best, bi = 1e18, 0
    for i, r in enumerate(rec):
        d = seg_dist(px, pz, r)
        if d < best:
            best, bi = d, i
    return best, bi


def check_clearance(name, rec):
    """Stretches of road that aren't neighbours along the track must be far
    enough apart that their barriers don't meet."""
    n = len(rec)
    near = 8 * 96 * SCALE       # along the road, closer than this is the same bend
    worst = 1e9
    for i, r in enumerate(rec):
        for j in range(n):
            gap = min(abs(i - j), n - abs(i - j))
            if gap * STEP < near:
                continue
            d = math.hypot(r["x"] - rec[j]["x"], r["z"] - rec[j]["z"])
            worst = min(worst, d)
    need = 2 * (HALF_W + VERGE) + 40
    status = "ok" if worst >= need else "TOO CLOSE"
    print(f"  {name}: closest approach {worst:.0f} (need {need}) {status}", file=sys.stderr)
    if worst < need:
        sys.exit(1)


def barrier_dist(r):
    return HALF_W + r["verge"]


def place_scenery(course, rec, seed):
    rnd = random.Random(seed)
    props = []
    water = course["water"]

    def in_water(x, z, m=0):
        return any(x0 - m <= x <= x1 + m and z0 - m <= z <= z1 + m for x0, z0, x1, z1 in water)

    def clear(x, z, radius):
        d, i = nearest(x, z, rec)
        return d > barrier_dist(rec[i]) + radius + 30 and not in_water(x, z, radius)

    for t, x, z, rot in course["landmarks"]:
        props.append((t, x, z, rot, 0))
        # Landmarks are placed by hand: make sure none stands on the road.
        d, i = nearest(x, z, rec)
        room = d - barrier_dist(rec[i]) - LANDMARK_FRONT.get(t, LANDMARK_R[t])
        if room < 0:
            print(f"  {course['name']}: landmark {t} at ({x}, {z}) is {-room:.0f} onto the road", file=sys.stderr)
            sys.exit(1)
    occupied = [(p[1], p[2], LANDMARK_R[p[0]] + 40) for p in props]

    def free(x, z, r):
        return all(math.hypot(x - ox, z - oz) > r + orr for ox, oz, orr in occupied)

    pts = course["points"]
    n = len(rec)
    for c0, c1, kind, spacing in course["zones"]:
        hug = kind in (P_CLIFF, P_ROCK)
        if not hug:
            spacing /= DENSITY
        idx = [i for i, r in enumerate(rec) if c0 <= r["cp"] < c1]
        dist = 0.0
        for i in idx:
            r = rec[i]
            dist += r["len"]
            while dist > spacing:
                dist -= spacing
                for side in (-1, 1):
                    if rnd.random() < (0.08 if kind == P_CLIFF else 0.35):
                        continue
                    if kind == P_CLIFF:
                        off = barrier_dist(r) + 75 + rnd.random() * 30
                    else:
                        off = barrier_dist(r) + 60 + rnd.random() * (40 if hug else 260 * SPREAD)
                    along = rnd.random() * r["len"]
                    px = r["x"] + r["ux"] * along + r["uz"] * off * side
                    pz = r["z"] + r["uz"] * along - r["ux"] * off * side
                    radius = {P_BUILDING: 110, P_HOUSE: 70, P_ROCK: 60, P_COLUMN: 30, P_CLIFF: 20}.get(kind, 40)
                    if not (300 < px < WORLD - 300 and 300 < pz < WORLD - 300):
                        continue
                    if not clear(px, pz, radius) or not free(px, pz, radius):
                        continue
                    rot = int((math.degrees(r["head"]) % 360) / 90 + 0.5) & 3
                    if kind == P_CLIFF:      # full heading, so the face lines up with the road
                        rot = int((r["head"] % (2 * math.pi)) / (2 * math.pi) * 256) & 255
                    var = rnd.randrange(256)
                    props.append((kind, px, pz, rot, var))
                    occupied.append((px, pz, radius))
    return props


# ---------------------------------------------------------------- output

def grid_lists(rec):
    """For each CELL-sized cell, the road segments within reach of it."""
    reach = HALF_W + VERGE + 80     # the barriers, plus the car and camera radius
    cells = [[] for _ in range(GRID * GRID)]
    for i, r in enumerate(rec):
        x0 = min(r["x"], r["x"] + r["ux"] * r["len"]) - reach
        x1 = max(r["x"], r["x"] + r["ux"] * r["len"]) + reach
        z0 = min(r["z"], r["z"] + r["uz"] * r["len"]) - reach
        z1 = max(r["z"], r["z"] + r["uz"] * r["len"]) + reach
        for cz in range(max(0, int(z0 // CELL)), min(GRID, int(z1 // CELL) + 1)):
            for cx in range(max(0, int(x0 // CELL)), min(GRID, int(x1 // CELL) + 1)):
                mx, mz = (cx + 0.5) * CELL, (cz + 0.5) * CELL
                if seg_dist(mx, mz, r) < reach + CELL * 0.71:
                    cells[cz * GRID + cx].append(i)
    first, count, flat = [], [], []
    for c in cells:
        first.append(len(flat))
        count.append(len(c))
        flat.extend(c)
    return first, count, flat


def prop_lists(props):
    cells = [[] for _ in range(PGRID * PGRID)]
    for k, p in enumerate(props):
        cx, cz = int(p[1] // PCELL), int(p[2] // PCELL)
        cells[cz * PGRID + cx].append(k)
    order, first, count = [], [], []
    for c in cells:
        first.append(len(order))
        count.append(len(c))
        order.extend(c)
    return [props[k] for k in order], first, count


def ground_height(rec, x, z):
    """Height of the ground under scenery: the road's height, falling away
    down the embankment beyond the barrier (as track.c draws it)."""
    d, i = nearest(x, z, rec)
    r, q = rec[i], rec[(i + 1) % len(rec)]
    t = max(0.0, min(r["len"], (x - r["x"]) * r["ux"] + (z - r["z"]) * r["uz"]))
    h = r["y"] + (q["y"] - r["y"]) * t / r["len"]
    edge = barrier_dist(r)
    if d > edge:
        h = 0 if r["flags"] & F_BRIDGE else max(0.0, h - (d - edge))
    return int(min(MAX_H, max(0, h)))


def c_array(ctype, name, values, per_line=16):
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def emit_dims():
    """The dimensions track.h (and so car.c, race.c, main.c) shares with
    this script, so the two can't disagree."""
    assert VERGE <= 255, "TrackPt.verge is a u8"
    assert WORLD <= 32768, "coordinates are s16"
    assert PGRID * PCELL == WORLD
    return "\n".join([
        "// Generated by tools/mktracks.py. Do not edit by hand.",
        "// Circuit dimensions shared by the generator and the game.",
        "#ifndef TRACKS_DIMS_H",
        "#define TRACKS_DIMS_H",
        "",
        f"#define TRACK_HALF_W      {HALF_W}      // half the road width",
        f"#define TRACK_WORLD       {WORLD}    // circuits lie inside 0..TRACK_WORLD on x and z",
        f"#define TRACK_CELL_SHIFT  {CELL_SHIFT}        // nearest-road grid: cells of 1 << shift units,",
        f"#define TRACK_GRID        {GRID}      // TRACK_GRID across",
        f"#define TRACK_PCELL_SHIFT {PCELL_SHIFT}        // scenery grid: cells of 1 << shift units,",
        f"#define TRACK_PGRID       {PGRID}       // TRACK_PGRID across",
        "",
        "#endif",
        ""])


def emit(courses_built):
    out = ["// Generated by tools/mktracks.py. Do not edit by hand.",
           "// Hand-made circuits in the style of Virtua Racing's three courses.", ""]
    prefixes = ["bf", "bb", "ac"]
    for (course, rec, total, props), pre in zip(courses_built, prefixes):
        assert len(rec) < 256, "point numbers are u8 (cell lists, MAX_POINTS)"
        assert total < 65536, "lap and dist are u16"
        assert max(r["y"] for r in rec) <= MAX_H, "ground heights must fit Prop.y2"
        assert all(0 <= p[1] < WORLD and 0 <= p[2] < WORLD for p in props), "scenery outside the world"
        assert all(0 <= r["x"] < WORLD and 0 <= r["z"] < WORLD for r in rec), "road outside the world"
        assert all(0 <= v <= WORLD for w in course["water"] for v in w), "water outside the world"
        out.append(f"// {course['name']}: {len(rec)} points, lap {total:.0f} units, {len(props)} props")
        out.append(f"static const TrackPt {pre}_pts[{len(rec)}] = {{")
        dist = 0.0
        for r in rec:
            out.append("    {{ {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {} }},".format(
                round(r["x"]), round(r["z"]), round(r["y"]),
                round(r["ux"] * 16384), round(r["uz"] * 16384), round(r["len"]),
                round(dist), round(r["head"] / (2 * math.pi) * 65536) & 0xFFFF,
                round(r["radius"]), r["flags"], r["verge"]))
            dist += r["len"]
        out.append("};")
        first, count, flat = grid_lists(rec)
        assert len(flat) < 65536 and max(count) < 256, "cell_first is u16, cell_count u8"
        out.append(c_array("u16", f"{pre}_cell_first", first))
        out.append(c_array("u8", f"{pre}_cell_count", count, 32))
        out.append(c_array("u8", f"{pre}_cell_list", flat, 24))
        marks = [p for p in props if p[0] in BIG]
        sprops, pfirst, pcount = prop_lists([p for p in props if p[0] not in BIG])
        assert len(sprops) < 65536 and max(pcount) < 256, "prop_first is u16, prop_count u8"
        assert len(marks) < 256, "mark_count is u8"
        for name, plist in ((f"{pre}_props", sprops), (f"{pre}_marks", marks)):
            out.append(f"static const Prop {name}[{len(plist)}] = {{")
            for t, x, z, rot, var in plist:
                out.append(f"    {{ {t}, {rot}, {var}, {ground_height(rec, x, z) // 2}, {round(x)}, {round(z)} }},")
            out.append("};")
        out.append(c_array("u16", f"{pre}_prop_first", pfirst))
        out.append(c_array("u8", f"{pre}_prop_count", pcount, 32))
        w = course["water"]
        flatw = [v for rect in w for v in rect] or [0, 0, 0, 0]
        out.append(c_array("s16", f"{pre}_water", flatw))
        out.append(f"#define {pre.upper()}_POINTS {len(rec)}")
        out.append(f"#define {pre.upper()}_LAP {round(total)}")
        out.append(f"#define {pre.upper()}_PROPS {len(sprops)}")
        out.append(f"#define {pre.upper()}_MARKS {len(marks)}")
        out.append(f"#define {pre.upper()}_WATER {len(w)}")
        out.append(f"#define {pre.upper()}_LAPS {course['laps']}")
        out.append(f"#define {pre.upper()}_START_TIME {course['start_time']}")
        out.append(f"#define {pre.upper()}_CP_TIME {course['cp_time']}")
        out.append("")
    return "\n".join(out) + "\n"


def preview(course, rec, props, path):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (820, 820), (40, 90, 40))
    d = ImageDraw.Draw(img)
    s = 820 / WORLD
    for x0, z0, x1, z1 in course["water"]:
        d.rectangle([x0 * s, 820 - z1 * s, x1 * s, 820 - z0 * s], fill=(40, 80, 180))
    n = len(rec)
    for i, r in enumerate(rec):
        b = rec[(i + 1) % n]
        col = (200, 200, 200) if not r["flags"] & F_BRIDGE else (230, 160, 60)
        w = 2 * HALF_W * s
        d.line([r["x"] * s, 820 - r["z"] * s, b["x"] * s, 820 - b["z"] * s], fill=col, width=max(1, int(w)))
        if r["flags"] & F_KERB_L:
            d.point([r["x"] * s, 820 - r["z"] * s], fill=(255, 0, 0))
    d.ellipse([rec[0]["x"] * s - 5, 820 - rec[0]["z"] * s - 5, rec[0]["x"] * s + 5, 820 - rec[0]["z"] * s + 5], fill=(255, 255, 0))
    colors = {P_TREE: (20, 60, 20), P_PINE: (10, 50, 30), P_BUILDING: (150, 150, 170), P_ROCK: (120, 100, 80),
              P_COLUMN: (240, 240, 230), P_HOUSE: (200, 120, 90)}
    for t, x, z, rot, var in props:
        c = colors.get(t, (255, 0, 255))
        rr = 6 if t not in colors else 2
        d.ellipse([x * s - rr, 820 - z * s - rr, x * s + rr, 820 - z * s + rr], fill=c)
    for k, p in enumerate(course["points"]):
        d.text((p[0] * s + 4, 820 - p[1] * s), str(k), fill=(255, 255, 255))
    d.text((10, 10), f"{course['name']}  {len(rec)} pts", fill=(255, 255, 255))
    img.save(path)


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    built = []
    for k, c in enumerate(COURSES):
        rec, total, props = build(c, 1000 + k)
        print(f"{c['name']}: {len(rec)} points, lap {total:.0f}, {len(props)} props", file=sys.stderr)
        c = scaled(c)
        built.append((c, rec, total, props))
        if "--preview" in sys.argv:
            os.makedirs(os.path.join(root, "build"), exist_ok=True)
            preview(c, rec, props, os.path.join(root, "build", f"track_{k}.png"))
    with open(os.path.join(root, "src", "tracks_data.h"), "w") as f:
        f.write(emit(built))
    with open(os.path.join(root, "src", "tracks_dims.h"), "w") as f:
        f.write(emit_dims())


if __name__ == "__main__":
    main()
