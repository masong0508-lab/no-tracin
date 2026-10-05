#!/usr/bin/env python3
"""Builds src/tracks_data.h: the three Virtua-style circuits.

Each course is a closed Catmull-Rom spline through hand-placed control
points (x, z, height, attributes), resampled every ~96 world units. The
layouts are drawn from memory of Virtua Racing's Big Forest, Bay Bridge and
Acropolis (corner order, hairpins, the bridge, hills and landmarks), scaled
to fit the GBA engine. Nothing is read from any ROM.

Also writes the lookup grids the game uses to find the nearest stretch of
road for any point, and places the scenery.

    python3 tools/mktracks.py            # writes src/tracks_data.h
    python3 tools/mktracks.py --preview  # also writes build/track_*.png maps
"""
import math
import random
import sys
import os

STEP = 96          # resample spacing, world units
CELL = 128         # nearest-road grid
GRID = 64          # 64 x 64 cells over the 8192-unit world
PCELL = 512        # scenery grid
PGRID = 16
HALF_W = 84        # road half width
VERGE = 96         # grass between the road edge and the barrier

# Control-point attributes (apply from this point to the next).
BRIDGE = 1         # no verge, railings at the road edge, side girders
TUNNEL = 2         # (unused)

# Scenery types (must match track.c).
P_TREE, P_PINE, P_BUILDING, P_STAND, P_FERRIS, P_TENT, P_TOWER, P_ROCK, \
    P_COLUMN, P_TEMPLE, P_CRANE, P_LIGHTHOUSE, P_BALLOON, P_HOUSE, P_SIGN, P_BOAT = range(16)

# Track point flags (must match track.c).
F_KERB_L, F_KERB_R, F_BRIDGE, F_START = 1, 2, 4, 8


# ---------------------------------------------------------------- the courses

# (x, z, height, attr). Clockwise unless noted; heading 0 is +z (north),
# turning right goes toward +x.
BIG_FOREST = dict(
    name="BIG FOREST", start=1, level="BEGINNER", laps=4, start_time=45, cp_time=20,
    theme="forest",
    points=[
        (2000, 1700, 0, 0),
        (2000, 2600, 0, 0),
        (2000, 3500, 10, 0),
        (2100, 4200, 30, 0),       # long right sweeper
        (2500, 4750, 50, 0),
        (3200, 4980, 70, 0),
        (3900, 4980, 80, 0),
        (4350, 4800, 80, 0),       # chicane
        (4750, 5000, 80, 0),
        (5250, 4900, 70, 0),
        (5800, 4500, 50, 0),       # downhill toward the park
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
    name="BAY BRIDGE", start=1, level="MEDIUM", laps=4, start_time=50, cp_time=22,
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
        (4250, 6250, 0, 0),
        (3700, 6300, 0, 0),
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
    name="ACROPOLIS", start=1, level="EXPERT", laps=4, start_time=55, cp_time=24,
    theme="acropolis",
    points=[
        (1800, 1800, 0, 0),
        (1800, 2600, 10, 0),
        (1800, 3300, 30, 0),
        (1650, 3900, 60, 0),       # fast left kink, climbing
        (1900, 4500, 90, 0),       # esses
        (1700, 5100, 120, 0),
        (1950, 5650, 150, 0),
        (2400, 6000, 180, 0),
        (2900, 6250, 200, 0),      # temple hairpin (right, 180)
        (3250, 6150, 200, 0),
        (3300, 5800, 190, 0),
        (2950, 5450, 170, 0),
        (2850, 5000, 150, 0),
        (3200, 4500, 120, 0),      # long downhill
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
        (2, 14, P_ROCK, 130),
        (13, 23, P_COLUMN, 260),
        (0, 30, P_PINE, 210),
    ],
)

COURSES = [BIG_FOREST, BAY_BRIDGE, ACROPOLIS]


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
        for s in range(64):
            t = s / 64
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
        rec[i]["radius"] = min(4000, abs(arc / dh) if abs(dh) > 1e-6 else 4000)
        rec[i]["turn"] = dh
    # Smooth the radius a little (kerbs on corners, speeds for the CPU cars).
    rad = [r["radius"] for r in rec]
    for i in range(n):
        rec[i]["radius"] = min(rad[i - 1], rad[i], rad[(i + 1) % n])
    for i, r in enumerate(rec):
        f = 0
        if r["radius"] < 900:
            f |= F_KERB_L | F_KERB_R
        if r["attr"] & BRIDGE:
            f |= F_BRIDGE
        if i == 0:
            f |= F_START
        r["flags"] = f
        r["verge"] = 0 if f & F_BRIDGE else VERGE
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
    worst = 1e9
    for i, r in enumerate(rec):
        for j in range(n):
            gap = min(abs(i - j), n - abs(i - j))
            if gap < 8:
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
    occupied = [(p[1], p[2], 260) for p in props]

    def free(x, z, r):
        return all(math.hypot(x - ox, z - oz) > r + orr for ox, oz, orr in occupied)

    pts = course["points"]
    n = len(rec)
    for c0, c1, kind, spacing in course["zones"]:
        idx = [i for i, r in enumerate(rec) if c0 <= r["cp"] < c1]
        dist = 0.0
        for i in idx:
            r = rec[i]
            dist += r["len"]
            while dist > spacing:
                dist -= spacing
                for side in (-1, 1):
                    if rnd.random() < 0.35:
                        continue
                    off = barrier_dist(r) + 60 + rnd.random() * (260 if kind != P_ROCK else 40)
                    along = rnd.random() * r["len"]
                    px = r["x"] + r["ux"] * along + r["uz"] * off * side
                    pz = r["z"] + r["uz"] * along - r["ux"] * off * side
                    radius = {P_BUILDING: 110, P_HOUSE: 70, P_ROCK: 60, P_COLUMN: 30}.get(kind, 40)
                    if not (300 < px < 7900 and 300 < pz < 7900):
                        continue
                    if not clear(px, pz, radius) or not free(px, pz, radius):
                        continue
                    rot = int((math.degrees(r["head"]) % 360) / 90 + 0.5) & 3
                    var = rnd.randrange(256)
                    props.append((kind, px, pz, rot, var))
                    occupied.append((px, pz, radius))
    return props


# ---------------------------------------------------------------- output

def grid_lists(rec):
    """For each 256-unit cell, the road segments within reach of it."""
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
    return int(min(510, max(0, h)))


def c_array(ctype, name, values, per_line=16):
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def emit(courses_built):
    out = ["// Generated by tools/mktracks.py. Do not edit by hand.",
           "// Hand-made circuits in the style of Virtua Racing's three courses.", ""]
    prefixes = ["bf", "bb", "ac"]
    for (course, rec, total, props), pre in zip(courses_built, prefixes):
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
        out.append(c_array("u16", f"{pre}_cell_first", first))
        out.append(c_array("u8", f"{pre}_cell_count", count, 32))
        assert len(rec) < 256, "cell lists hold u8 point numbers"
        out.append(c_array("u8", f"{pre}_cell_list", flat, 24))
        sprops, pfirst, pcount = prop_lists(props)
        out.append(f"static const Prop {pre}_props[{len(sprops)}] = {{")
        for t, x, z, rot, var in sprops:
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
        out.append(f"#define {pre.upper()}_WATER {len(w)}")
        out.append("")
    return "\n".join(out) + "\n"


def preview(course, rec, props, path):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (820, 820), (40, 90, 40))
    d = ImageDraw.Draw(img)
    s = 0.1
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
        built.append((c, rec, total, props))
        if "--preview" in sys.argv:
            os.makedirs(os.path.join(root, "build"), exist_ok=True)
            preview(c, rec, props, os.path.join(root, "build", f"track_{k}.png"))
    with open(os.path.join(root, "src", "tracks_data.h"), "w") as f:
        f.write(emit(built))


if __name__ == "__main__":
    main()
