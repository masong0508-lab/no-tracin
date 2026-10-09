// Race circuits: lookup of the nearest stretch of road, the height field
// and barriers built from it, and drawing the road, verges, embankments,
// bridges and scenery.
#include "track.h"
#include "world.h"

#include "tracks_data.h"

// Laps and the race clock come from the course definitions in mktracks.py.
const TrackDef g_tracks[TRACK_COUNT] = {
    { "BIG FOREST", "BEGINNER", bf_pts, BF_POINTS, BF_LAP, bf_cell_first, bf_cell_count, bf_cell_list,
      bf_props, bf_prop_first, bf_prop_count, BF_PROPS, bf_marks, BF_MARKS, bf_water, BF_WATER,
      BF_LAPS, BF_START_TIME, BF_CP_TIME, SCENE_FOREST },
    { "BAY BRIDGE", "MEDIUM", bb_pts, BB_POINTS, BB_LAP, bb_cell_first, bb_cell_count, bb_cell_list,
      bb_props, bb_prop_first, bb_prop_count, BB_PROPS, bb_marks, BB_MARKS, bb_water, BB_WATER,
      BB_LAPS, BB_START_TIME, BB_CP_TIME, SCENE_BAY },
    { "ACROPOLIS", "EXPERT", ac_pts, AC_POINTS, AC_LAP, ac_cell_first, ac_cell_count, ac_cell_list,
      ac_props, ac_prop_first, ac_prop_count, AC_PROPS, ac_marks, AC_MARKS, ac_water, AC_WATER,
      AC_LAPS, AC_START_TIME, AC_CP_TIME, SCENE_MOUNTAINS },
};

const TrackDef *g_track;

#define MAX_POINTS 256
#define RAIL_H     16
#define BANK       2        // embankments fall 1 unit for every 1 outward
#define TOWER_SPAN 1600     // bridges this long get suspension towers,
#define TOWER_H    360      // this tall above the deck,
#define CABLE_REACH 700     // with cables reaching this far along it
#define PIER_SHIFT 9        // piers under a bridge every 512 units

static s16 nrx[MAX_POINTS] EWRAM_BSS, nrz[MAX_POINTS] EWRAM_BSS;   // right-pointing normal at each point, Q14
static u8  tower_at[MAX_POINTS] EWRAM_BSS;     // bridge towers: points their cables reach, 0 = none
static u16 inv_len[MAX_POINTS] EWRAM_BSS;      // 65536 / length of each stretch
s16 track_pitch[MAX_POINTS] EWRAM_BSS;         // slope of each stretch, 1024-unit angle
// Bounding circles of runs of 8 points, to skip whole stretches off screen.
#define BLOCK_PTS 8
static s16 blk_x[MAX_POINTS / BLOCK_PTS] EWRAM_BSS, blk_z[MAX_POINTS / BLOCK_PTS] EWRAM_BSS, blk_r[MAX_POINTS / BLOCK_PTS] EWRAM_BSS;

enum {
    P_TREE, P_PINE, P_BUILDING, P_STAND, P_FERRIS, P_TENT, P_TOWER, P_ROCK,
    P_COLUMN, P_TEMPLE, P_CRANE, P_LIGHTHOUSE, P_BALLOON, P_HOUSE, P_SIGN, P_BOAT,
    P_CLIFF, P_COASTER, P_COUNT
};

// Trees, rocks and the like are drawn out to this deep (about as many are
// in view as on the old, smaller courses); landmarks as far as anything.
#define SMALL_FAR 900
// How far each landmark reaches from its spot, for culling (mktracks.py
// keeps the road and the other scenery clear by the same sizes).
static const u16 prop_reach[P_COUNT] = {
    [P_STAND] = 330, [P_FERRIS] = 260, [P_TOWER] = 40, [P_TEMPLE] = 300, [P_CRANE] = 335,
    [P_LIGHTHOUSE] = 60, [P_BALLOON] = 100, [P_COASTER] = 545,
};

// ---------------------------------------------------------------- queries

static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }

static inline s32 barrier(const TrackPt *p) { return TRACK_HALF_W + p->verge; }

s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    const TrackDef *t = g_track;
    if (x < 0 || z < 0 || x >= TRACK_WORLD || z >= TRACK_WORLD) return 0;
    s32 cell = (z >> TRACK_CELL_SHIFT) * TRACK_GRID + (x >> TRACK_CELL_SHIFT);
    s32 n = t->cell_count[cell];
    if (!n) return 0;
    const u8 *list = &t->cell_list[t->cell_first[cell]];
    s32 best = 0x7FFFFFFF, bi = 0, bt = 0, bcx = 0, bcz = 0;
    for (s32 k = 0; k < n; k++) {
        const TrackPt *p = &t->pts[list[k]];
        s32 vx = x - p->x, vz = z - p->z;
        s32 a = (vx * p->ux + vz * p->uz) >> 14;
        if (a < 0) a = 0;
        if (a > p->len) a = p->len;
        s32 cx = vx - ((p->ux * a) >> 14), cz = vz - ((p->uz * a) >> 14);
        s32 d = cx * cx + cz * cz;
        if (d < best) { best = d; bi = list[k]; bt = a; bcx = cx; bcz = cz; }
    }
    const TrackPt *p = &t->pts[bi], *q = &t->pts[bi + 1 == t->count ? 0 : bi + 1];
    hit->seg = bi;
    hit->along = bt;
    hit->dist = isqrt(best);
    hit->lat = (bcx * p->uz - bcz * p->ux) >= 0 ? hit->dist : -hit->dist;
    hit->h = (p->y << 8) + (((q->y - p->y) << 8) * bt) / (p->len ? p->len : 1);
    (void)bcz;
    return 1;
}

s32 track_height(s32 x, s32 z)
{
    TrackHit h;
    if (!track_find(x, z, &h)) return 0;
    const TrackPt *p = &g_track->pts[h.seg];
    s32 edge = barrier(p);
    if (h.dist <= edge) return h.h;
    if (p->flags & TF_BRIDGE) return 0;
    s32 drop = ((h.dist - edge) * 2 / BANK) << 8;    // the embankment
    return h.h > drop ? h.h - drop : 0;
}

void track_gradient(s32 x, s32 z, s32 *gx, s32 *gz)
{
    TrackHit h;
    *gx = *gz = 0;
    if (!track_find(x, z, &h)) return;
    const TrackPt *p = &g_track->pts[h.seg], *q = &g_track->pts[h.seg + 1 == g_track->count ? 0 : h.seg + 1];
    if (h.dist > barrier(p)) return;
    s32 g = ((q->y - p->y) << 8) / (p->len ? p->len : 1);   // rise per unit, Q8
    *gx = (g * p->ux) >> 14;
    *gz = (g * p->uz) >> 14;
}

s32 track_ground_plane(s32 x, s32 z, s32 r, s32 *h0, s32 *gx, s32 *gz)
{
    TrackHit h;
    *h0 = 0; *gx = 0; *gz = 0;
    if (!track_find(x, z, &h)) return 0;
    const TrackPt *p = &g_track->pts[h.seg], *q = &g_track->pts[h.seg + 1 == g_track->count ? 0 : h.seg + 1];
    if (h.dist + r > barrier(p)) return 0;
    s32 g = ((q->y - p->y) << 8) / (p->len ? p->len : 1);
    *h0 = h.h;
    *gx = (g * p->ux) >> 14;
    *gz = (g * p->uz) >> 14;
    return 1;
}

// Road height near stretch seg (the car's own, say): only that stretch and
// its neighbours are tried, which is much cheaper than a full search.
s32 track_height_near(s32 seg, s32 x, s32 z)
{
    const TrackDef *t = g_track;
    s32 best = 0x7FFFFFFF, bi = seg, ba = 0;
    for (s32 k = -1; k <= 1; k++) {
        s32 i = seg + k;
        if (i < 0) i += t->count;
        if (i >= t->count) i -= t->count;
        const TrackPt *p = &t->pts[i];
        s32 vx = x - p->x, vz = z - p->z;
        s32 a = (vx * p->ux + vz * p->uz) >> 14;
        if (a < 0) a = 0;
        if (a > p->len) a = p->len;
        s32 cx = vx - ((p->ux * a) >> 14), cz = vz - ((p->uz * a) >> 14);
        s32 d = cx * cx + cz * cz;
        if (d < best) { best = d; bi = i; ba = a; }
    }
    const TrackPt *p = &t->pts[bi], *q = &t->pts[bi + 1 == t->count ? 0 : bi + 1];
    s32 edge = barrier(p);
    if (best > edge * edge) return track_height(x, z);
    return (p->y << 8) + (((q->y - p->y) * ba * inv_len[bi]) >> 8);
}

s32 track_surface(s32 x, s32 z)
{
    TrackHit h;
    if (!track_find(x, z, &h)) return SURF_GRASS;
    return h.dist < TRACK_HALF_W ? SURF_ROAD : SURF_GRASS;
}

s32 track_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{
    TrackHit h;
    *nx = 0; *nz = 0;
    if (!track_find(x, z, &h)) return 0;
    const TrackPt *p = &g_track->pts[h.seg];
    s32 edge = barrier(p) - 4;
    s32 pen = h.dist + radius - edge;
    if (pen <= 0 || !h.dist) return 0;
    // Push back toward the centreline.
    s32 cx = x - (p->x + ((p->ux * h.along) >> 14));
    s32 cz = z - (p->z + ((p->uz * h.along) >> 14));
    *nx = -(cx << 14) / h.dist;
    *nz = -(cz << 14) / h.dist;
    return pen;
}

void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint)
{
    const TrackDef *t = g_track;
    while (d < 0) d += t->lap;
    while (d >= t->lap) d -= t->lap;
    s32 i = hint ? *hint : 0;
    if (i < 0 || i >= t->count) i = 0;
    // Walk forward from the hint (cars move a little each step).
    for (s32 guard = 0; guard < t->count; guard++) {
        const TrackPt *p = &t->pts[i];
        if (d >= p->dist && d < p->dist + p->len) break;
        i = i + 1 == t->count ? 0 : i + 1;
    }
    if (hint) *hint = i;
    s32 ni = i + 1 == t->count ? 0 : i + 1;
    const TrackPt *p = &t->pts[i], *q = &t->pts[ni];
    s32 a = d - p->dist;
    s32 f = a * inv_len[i];                       // Q16 fraction along the stretch
    // Blend the normal across the stretch so cars off the centreline
    // follow the curve smoothly.
    s32 rx = nrx[i] + (((nrx[ni] - nrx[i]) * f) >> 16);
    s32 rz = nrz[i] + (((nrz[ni] - nrz[i]) * f) >> 16);
    *x = p->x + ((p->ux * a) >> 14) + ((rx * lat) >> 14);
    *z = p->z + ((p->uz * a) >> 14) + ((rz * lat) >> 14);
    *y = (p->y << 8) + (((q->y - p->y) * f) >> 8);
    s32 dh = (s16)(q->heading - p->heading);
    *heading = (u16)(p->heading + ((dh * (f >> 4)) >> 12));
}

void track_load(s32 index)
{
    if (index < 0) {
        g_track = 0;
        r_set_scene(SCENE_CITY);
        return;
    }
    const TrackDef *t = &g_tracks[index];
    g_track = t;
    for (s32 i = 0; i < t->count; i++) {
        const TrackPt *a = &t->pts[i ? i - 1 : t->count - 1], *b = &t->pts[i];
        s32 x = a->uz + b->uz, z = -a->ux - b->ux;
        s32 l = isqrt(x * x + z * z);
        if (!l) { x = b->uz; z = -b->ux; l = 16384; }
        nrx[i] = (x << 14) / l;
        nrz[i] = (z << 14) / l;
        tower_at[i] = 0;
        inv_len[i] = 65535 / (b->len ? b->len : 1);
        const TrackPt *c = &t->pts[i + 1 == t->count ? 0 : i + 1];
        track_pitch[i] = iatan2(c->y - b->y, b->len ? b->len : 1);
    }
    // Bridge towers a quarter and three quarters of the way across long
    // spans, their cables reaching CABLE_REACH units each way.
    for (s32 i = 0; i < t->count; i++) {
        if (!(t->pts[i].flags & TF_BRIDGE) || (t->pts[i ? i - 1 : t->count - 1].flags & TF_BRIDGE)) continue;
        s32 n = 0, span = 0;
        while (n < t->count && (t->pts[(i + n) % t->count].flags & TF_BRIDGE)) span += t->pts[(i + n++) % t->count].len;
        if (span >= TOWER_SPAN) {
            s32 k = CABLE_REACH / (span / n);
            if (k > n / 4) k = n / 4;
            if (k < 1) k = 1;
            tower_at[(i + n / 4) % t->count] = k;
            tower_at[(i + n * 3 / 4) % t->count] = k;
        }
    }
    for (s32 b = 0; b * BLOCK_PTS < t->count; b++) {
        s32 i0 = b * BLOCK_PTS, i1 = i0 + BLOCK_PTS > t->count ? t->count : i0 + BLOCK_PTS;
        s32 sx = 0, sz = 0, r = 0;
        for (s32 i = i0; i <= i1; i++) { sx += t->pts[i % t->count].x; sz += t->pts[i % t->count].z; }
        sx /= i1 - i0 + 1; sz /= i1 - i0 + 1;
        for (s32 i = i0; i <= i1; i++) {
            const TrackPt *p = &t->pts[i % t->count];
            s32 dx = p->x - sx, dz = p->z - sz;
            s32 d = isqrt(dx * dx + dz * dz) + barrier(p) + p->y * BANK / 2 + 16;
            if (d > r) r = d;
        }
        blk_x[b] = sx; blk_z[b] = sz; blk_r[b] = r;
    }
    r_set_scene(t->scene);
}

// ---------------------------------------------------------------- drawing helpers

static inline void ground4(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1,
                           s32 x2, s32 y2, s32 z2, s32 x3, s32 y3, s32 z3, u8 color)
{
    Vec3 q[4] = { { x0, y0, z0 }, { x1, y1, z1 }, { x2, y2, z2 }, { x3, y3, z3 } };
    r_ground(q, 4, color);
}

static inline void face4(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1,
                         s32 x2, s32 y2, s32 z2, s32 x3, s32 y3, s32 z3, u8 color, u32 flags)
{
    Vec3 q[4] = { { x0, y0, z0 }, { x1, y1, z1 }, { x2, y2, z2 }, { x3, y3, z3 } };
    r_face(q, 4, color, flags);
}

// A square pyramid (or a cone, near enough): apex over (x, z).
static void pyramid(s32 x, s32 y, s32 z, s32 half, s32 h, s32 mat)
{
    Vec3 q[3];
    s32 ty = y + h;
    q[0] = (Vec3){ x, ty, z };
    q[1] = (Vec3){ x + half, y, z - half }; q[2] = (Vec3){ x - half, y, z - half };
    r_face(q, 3, COLOR(mat, 1), 0);
    q[1] = (Vec3){ x + half, y, z + half }; q[2] = (Vec3){ x + half, y, z - half };
    r_face(q, 3, COLOR(mat, 3), 0);
    q[1] = (Vec3){ x - half, y, z + half }; q[2] = (Vec3){ x + half, y, z + half };
    r_face(q, 3, COLOR(mat, 2), 0);
    q[1] = (Vec3){ x - half, y, z - half }; q[2] = (Vec3){ x - half, y, z + half };
    r_face(q, 3, COLOR(mat, 0), 0);
}

// Pitched roof over the box x0..x1, z0..z1 at height y, ridge along z
// (or along x with `along_x`).
static void roof(s32 x0, s32 z0, s32 x1, s32 z1, s32 y, s32 h, s32 along_x, s32 mat)
{
    s32 ry = y + h;
    if (!along_x) {
        s32 mx = (x0 + x1) / 2;
        face4(x0, y, z0, mx, ry, z0, mx, ry, z1, x0, y, z1, COLOR(mat, 0), RF_TWO_SIDED);
        face4(mx, ry, z0, x1, y, z0, x1, y, z1, mx, ry, z1, COLOR(mat, 2), RF_TWO_SIDED);
        Vec3 g0[3] = { { mx, ry, z0 }, { x1, y, z0 }, { x0, y, z0 } };
        Vec3 g1[3] = { { mx, ry, z1 }, { x0, y, z1 }, { x1, y, z1 } };
        r_face(g0, 3, COLOR(mat, 1), 0);
        r_face(g1, 3, COLOR(mat, 1), 0);
    } else {
        s32 mz = (z0 + z1) / 2;
        face4(x0, y, z0, x0, ry, mz, x1, ry, mz, x1, y, z0, COLOR(mat, 1), RF_TWO_SIDED);
        face4(x0, ry, mz, x0, y, z1, x1, y, z1, x1, ry, mz, COLOR(mat, 3), RF_TWO_SIDED);
        Vec3 g0[3] = { { x1, ry, mz }, { x1, y, z1 }, { x1, y, z0 } };
        Vec3 g1[3] = { { x0, ry, mz }, { x0, y, z0 }, { x0, y, z1 } };
        r_face(g0, 3, COLOR(mat, 2), 0);
        r_face(g1, 3, COLOR(mat, 0), 0);
    }
}

// ---------------------------------------------------------------- scenery

static void draw_ferris(s32 x, s32 y, s32 z, s32 frame)
{
    const s32 R = 255, cy = y + R + 60, spokes = 12;
    // A-frame legs.
    face4(x - 105, y, z - 21, x - 6, cy, z - 21, x + 6, cy, z - 21, x + 105, y, z - 21, COLOR(M_STUNT_WHITE, 2), RF_TWO_SIDED);
    face4(x - 105, y, z + 21, x - 6, cy, z + 21, x + 6, cy, z + 21, x + 105, y, z + 21, COLOR(M_STUNT_WHITE, 2), RF_TWO_SIDED);
    s32 spin = frame * 2;
    s32 px = 0, py = 0;
    for (s32 i = 0; i <= spokes; i++) {
        s32 a = spin + (i * 1024) / spokes;
        s32 ex = (isin(a) * R) >> 14, ey = (icos(a) * R) >> 14;
        if (i > 0) {
            // Rim segment and a spoke.
            face4(x + px, cy + py, z, x + ex, cy + ey, z, x + ex - (ex >> 4), cy + ey - (ey >> 4), z,
                  x + px - (px >> 4), cy + py - (py >> 4), z, COLOR(i & 1 ? M_STUNT_RED : M_LINE, 0), RF_TWO_SIDED);
            if (i & 1)
                face4(x - 3, cy, z, x + 3, cy, z, x + ex + 3, cy + ey, z, x + ex - 3, cy + ey, z,
                      COLOR(M_STUNT_WHITE, 1), RF_TWO_SIDED);
            else
                r_box(x + ex - 13, cy + ey - 33, z - 13, x + ex + 13, cy + ey - 6, z + 13, i & 2 ? M_BLD2 : M_BLD4);
        }
        px = ex; py = ey;
    }
}

static void draw_tent(s32 x, s32 y, s32 z, s32 var)
{
    const s32 r = 90, wall = 40, peak = 130;
    s32 px = r, pz = 0;
    for (s32 i = 1; i <= 8; i++) {
        s32 a = i * 128;
        s32 ex = (icos(a) * r) >> 14, ez = (isin(a) * r) >> 14;
        u8 c = COLOR(i & 1 ? M_STUNT_RED : M_STUNT_WHITE, i & 2 ? 0 : 1);
        face4(x + px, y + wall, z + pz, x + ex, y + wall, z + ez, x + ex, y, z + ez, x + px, y, z + pz, c, RF_TWO_SIDED);
        Vec3 q[3] = { { x, y + peak, z }, { x + ex, y + wall, z + ez }, { x + px, y + wall, z + pz } };
        r_face(q, 3, COLOR(i & 1 ? M_STUNT_RED : M_STUNT_WHITE, (i & 3) == 1 ? 1 : 0), RF_TWO_SIDED);
        px = ex; pz = ez;
    }
    r_box(x - 2, y + peak, z - 2, x + 2, y + peak + 30, z + 2, var & 1 ? M_LINE : M_STUNT_RED);
}

static void draw_balloon(s32 x, s32 y, s32 z, s32 frame)
{
    s32 cy = y + 570 + ((isin(frame * 3) * 45) >> 14);
    const s32 r = 90;
    s32 px = r, pz = 0;
    for (s32 i = 1; i <= 8; i++) {
        s32 a = i * 128;
        s32 ex = (icos(a) * r) >> 14, ez = (isin(a) * r) >> 14;
        s32 m = i & 1 ? M_LINE : M_STUNT_RED;
        Vec3 top[3] = { { x, cy + 105, z }, { x + ex, cy, z + ez }, { x + px, cy, z + pz } };
        Vec3 bot[3] = { { x, cy - 120, z }, { x + px, cy, z + pz }, { x + ex, cy, z + ez } };
        r_face(top, 3, COLOR(m, 0), 0);
        r_face(bot, 3, COLOR(m, 2), 0);
        px = ex; pz = ez;
    }
    r_box(x - 15, cy - 165, z - 15, x + 15, cy - 141, z + 15, M_BLD1);
}

// Grandstand facing direction rot (0 +z, 1 +x, 2 -z, 3 -x), centred on (x, z).
static void draw_stand(s32 x, s32 y, s32 z, s32 rot)
{
    const s32 half = 255;
    for (s32 k = 0; k < 4; k++) {
        s32 d0 = -k * 45, d1 = -k * 45 - 45, h = 30 + k * 33;   // steps rise away from the road
        s32 m = k == 3 ? M_STUNT_WHITE : (k & 1) ? M_BLD2 : M_BLD3;
        switch (rot) {
        case 0: r_box(x - half, y, z + d1, x + half, y + h, z + d0, m); break;
        case 1: r_box(x + d1, y, z - half, x + d0, y + h, z + half, m); break;
        case 2: r_box(x - half, y, z - d0, x + half, y + h, z - d1, m); break;
        default: r_box(x - d0, y, z - half, x - d1, y + h, z + half, m); break;
        }
    }
    // Roof on posts.
    switch (rot) {
    case 0: r_box(x - half, y + 180, z - 195, x + half, y + 192, z + 15, M_STUNT_RED); break;
    case 1: r_box(x - 195, y + 180, z - half, x + 15, y + 192, z + half, M_STUNT_RED); break;
    case 2: r_box(x - half, y + 180, z - 15, x + half, y + 192, z + 195, M_STUNT_RED); break;
    default: r_box(x - 15, y + 180, z - half, x + 195, y + 192, z + half, M_STUNT_RED); break;
    }
}

static void draw_temple(s32 x, s32 y, s32 z, s32 depth)
{
    const s32 w = 240, d = 135, ch = 135;
    r_box(x - w - 18, y, z - d - 18, x + w + 18, y + 24, z + d + 18, M_BLD3);
    s32 step = depth > 900 ? 2 : 1;
    for (s32 i = 0; i < 6; i += step) {
        s32 cx = x - w + 12 + i * ((2 * w - 24) / 5);
        r_box(cx - 12, y + 24, z - d, cx + 12, y + 24 + ch, z - d + 24, M_BLD3);
        r_box(cx - 12, y + 24, z + d - 24, cx + 12, y + 24 + ch, z + d, M_BLD3);
    }
    r_box(x - w - 9, y + 24 + ch, z - d - 9, x + w + 9, y + 45 + ch, z + d + 9, M_BLD3);
    roof(x - w - 9, z - d - 9, x + w + 9, z + d + 9, y + 45 + ch, 51, 1, M_BLD3);
}

// A slab of rock lined up with the road (rot = heading, 256 per turn): the
// walls of the Acropolis valley.
static void draw_cliff(s32 x, s32 y, s32 z, s32 rot, s32 v, s32 depth)
{
    s32 h = rot << 2, len = 78, back = 70, top = y + 150 + (v & 127);
    s32 ax = (isin(h) * len) >> 14, az = (icos(h) * len) >> 14;
    s32 bx = (icos(h) * back) >> 14, bz = (-isin(h) * back) >> 14;
    s32 mat = (v & 64) ? M_BLD5 : M_BLD3;
    // Faces toward and away from the road, leaning back a little, then the top.
    face4(x - ax - bx, y - 20, z - az - bz, x + ax - bx, y - 20, z + az - bz,
          x + ax - bx / 2, top, z + az - bz / 2, x - ax - bx / 2, top, z - az - bz / 2, COLOR(mat, 1), RF_TWO_SIDED);
    if (depth > 420) return;     // further off the road-facing side is all that shows
    face4(x - ax + bx, y - 20, z - az + bz, x + ax + bx, y - 20, z + az + bz,
          x + ax + bx / 2, top, z + az + bz / 2, x - ax + bx / 2, top, z - az + bz / 2, COLOR(mat, 2), RF_TWO_SIDED);
    face4(x - ax - bx / 2, top, z - az - bz / 2, x + ax - bx / 2, top, z + az - bz / 2,
          x + ax + bx / 2, top, z + az + bz / 2, x - ax + bx / 2, top, z - az + bz / 2, COLOR(mat, 3), RF_TWO_SIDED);
}

// Roller coaster in the amusement park, running north-south: trestles, a
// humped track and a train running along it.
static void draw_coaster(s32 x, s32 y, s32 z, s32 depth, s32 frame)
{
    static const s16 hp[9] = { 75, 195, 345, 405, 315, 180, 120, 240, 90 };
    const s32 gap = 135, z0 = z - 4 * gap;
    for (s32 i = 0; i < 9; i++) {
        s32 pz = z0 + i * gap;
        if (depth < 900 || !(i & 1)) r_box(x - 7, y, pz - 7, x + 7, y + hp[i], pz + 7, M_STUNT_WHITE);
        if (i < 8)
            face4(x - 21, y + hp[i], pz, x - 21, y + hp[i + 1], pz + gap, x + 21, y + hp[i + 1], pz + gap,
                  x + 21, y + hp[i], pz, COLOR(M_STUNT_RED, (i & 1) + 1), RF_TWO_SIDED);
    }
    // The train goes back and forth along the track.
    s32 t = (frame * 4) % (16 * gap);
    if (t > 8 * gap) t = 16 * gap - t;
    s32 i = t / gap, f = t % gap;
    if (i > 7) { i = 7; f = gap; }
    s32 ty = y + hp[i] + ((hp[i + 1] - hp[i]) * f) / gap, tz = z0 + t;
    r_box(x - 18, ty, tz - 33, x + 18, ty + 27, tz + 33, M_BLD4);
}

static void draw_prop(const Prop *p, s32 y, s32 depth, s32 frame)
{
    s32 x = p->x, z = p->z, v = p->var;
    switch (p->type) {
    case P_TREE: {
        s32 s = 22 + (v & 15), h = 50 + (v >> 3 & 31);
        if (depth < 450) r_box(x - 5, y, z - 5, x + 5, y + 26, z + 5, M_BLD1);
        r_box(x - s, y + (depth < 450 ? 22 : 0), z - s, x + s, y + 22 + h, z + s, M_TREE);
        break;
    }
    case P_PINE: {
        s32 s = 26 + (v & 15), h = 110 + (v >> 2 & 63);
        if (depth < 450) r_box(x - 5, y, z - 5, x + 5, y + 20, z + 5, M_BLD1);
        pyramid(x, y + (depth < 450 ? 16 : 0), z, s, h, M_TREE);
        break;
    }
    case P_BUILDING: {
        s32 w = 50 + (v & 31), d = 50 + ((v >> 2) & 31), h = 140 + (v >> 3) * 9;
        r_box(x - w, y, z - d, x + w, y + h, z + d, M_BLD0 + v % 6);
        if (depth < 700 && (v & 3) == 0) r_box(x - w / 2, y + h, z - d / 2, x + w / 2, y + h + 40, z + d / 2, M_BLD3);
        break;
    }
    case P_HOUSE: {
        s32 w = 40 + (v & 15), d = 34;
        s32 m = v & 1 ? M_BLD3 : M_BLD0;
        if (p->rot & 1) { r_box(x - d, y, z - w, x + d, y + 44, z + w, m); roof(x - d - 6, z - w - 6, x + d + 6, z + w + 6, y + 44, 34, 0, M_BLD1); }
        else            { r_box(x - w, y, z - d, x + w, y + 44, z + d, m); roof(x - w - 6, z - d - 6, x + w + 6, z + d + 6, y + 44, 34, 1, M_BLD1); }
        break;
    }
    case P_STAND: draw_stand(x, y, z, p->rot); break;
    case P_FERRIS: draw_ferris(x, y, z, frame); break;
    case P_TENT: draw_tent(x, y, z, v + p->rot); break;
    case P_BALLOON: draw_balloon(x, y, z, frame); break;
    case P_TOWER:
        r_box(x - 18, y, z - 18, x + 18, y + 630, z + 18, M_STUNT_WHITE);
        r_box(x - 24, y + 630, z - 24, x + 24, y + 675, z + 24, M_STUNT_RED);
        break;
    case P_ROCK: {
        s32 a = 30 + (v & 31), b = 24 + ((v >> 3) & 31), h = 40 + ((v >> 1) & 63);
        r_box(x - a, y - 10, z - b, x + a, y + h, z + b, M_BLD5);
        if (depth < 800) r_box(x - b / 2, y + h - 10, z - a / 2, x + b, y + h + 24, z + a / 3, M_BLD5);
        break;
    }
    case P_COLUMN:
        if (v & 1) {
            r_box(x - 9, y, z - 9, x + 9, y + 100 + (v & 31), z + 9, M_BLD3);
            r_box(x - 14, y + 100 + (v & 31), z - 14, x + 14, y + 110 + (v & 31), z + 14, M_BLD3);
        } else {
            r_box(x - 9, y, z - 9, x + 9, y + 30 + (v & 15), z + 9, M_BLD3);   // fallen to a stump
            r_box(x + 14, y, z - 9, x + 74, y + 18, z + 9, M_BLD3);
        }
        break;
    case P_TEMPLE: draw_temple(x, y + 10, z, depth); break;
    case P_CLIFF: draw_cliff(x, y, z, p->rot, v, depth); break;
    case P_COASTER: draw_coaster(x, y, z, depth, frame); break;
    case P_CRANE:
        r_box(x - 21, y, z - 21, x + 21, y + 390, z + 21, M_BLD4);
        r_box(x - 24, y + 390, z - 330, x + 24, y + 420, z + 120, M_BLD4);
        r_box(x - 30, y + 345, z - 30, x + 30, y + 393, z + 30, M_BLD1);
        break;
    case P_LIGHTHOUSE:
        for (s32 k = 0; k < 5; k++)
            r_box(x - 36 + k * 3, y + k * 75, z - 36 + k * 3, x + 36 - k * 3, y + k * 75 + 75, z + 36 - k * 3,
                  k & 1 ? M_STUNT_RED : M_STUNT_WHITE);
        r_box(x - 24, y + 375, z - 24, x + 24, y + 420, z + 24, (frame >> 3) & 1 ? M_LINE : M_GLASS);
        break;
    case P_SIGN: {
        s32 m = v & 1 ? M_LINE : M_STUNT_RED;
        if (p->rot & 1) {
            r_box(x - 3, y, z - 80, x + 3, y + 60, z - 74, M_STUNT_WHITE);
            r_box(x - 3, y, z + 74, x + 3, y + 60, z + 80, M_STUNT_WHITE);
            r_box(x - 4, y + 60, z - 110, x + 4, y + 120, z + 110, m);
        } else {
            r_box(x - 80, y, z - 3, x - 74, y + 60, z + 3, M_STUNT_WHITE);
            r_box(x + 74, y, z - 3, x + 80, y + 60, z + 3, M_STUNT_WHITE);
            r_box(x - 110, y + 60, z - 4, x + 110, y + 120, z + 4, m);
        }
        break;
    }
    case P_BOAT: {
        s32 bob = (isin(frame * 4 + x) * 3) >> 14;
        if (p->rot & 1) {
            r_box(x - 30, bob, z - 90, x + 30, bob + 20, z + 90, M_STUNT_WHITE);
            r_box(x - 20, bob + 20, z - 40, x + 20, bob + 50, z + 20, M_BLD2);
        } else {
            r_box(x - 90, bob, z - 30, x + 90, bob + 20, z + 30, M_STUNT_WHITE);
            r_box(x - 40, bob + 20, z - 20, x + 20, bob + 50, z + 20, M_BLD2);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------- road

// Camera-space cross-sections: each point's centre at ground level and its
// normal, transformed once a frame, so every strip's corners are a few adds.
typedef struct { Vec3 c, n; u32 stamp; } XPt;
static XPt xpt[MAX_POINTS] EWRAM_BSS;
static u32 xstamp;
static Vec3 xup;          // camera-space up, Q14

static inline const XPt *xget(const TrackPt *pts, s32 i)
{
    XPt *p = &xpt[i];
    if (p->stamp != xstamp) {
        p->stamp = xstamp;
        r_xform(pts[i].x, 0, pts[i].z, &p->c);
        r_xform_dir(nrx[i], 0, nrz[i], &p->n);
    }
    return p;
}

static inline void xvert(const XPt *p, s32 off, s32 y, Vec3 *out)
{
    out->x = p->c.x + ((p->n.x * off + xup.x * y) >> 14);
    out->y = p->c.y + ((p->n.y * off + xup.y * y) >> 14);
    out->z = p->c.z + ((p->n.z * off + xup.z * y) >> 14);
}

#define MAX_VIS 96
static u8  vis_seg[MAX_VIS] EWRAM_BSS;
static s16 vis_depth[MAX_VIS] EWRAM_BSS;

// Edge points of point i at lateral offset `off` (+ = right).
#define EX(i, off) (pts[i].x + ((nrx[i] * (off)) >> 14))
#define EZ(i, off) (pts[i].z + ((nrz[i] * (off)) >> 14))

IWRAM_CODE static void draw_strip(const TrackPt *pts, s32 a, s32 b, s32 o0, s32 o1, s32 ya, s32 yb, u8 color)
{
    const XPt *pa = xget(pts, a), *pb = xget(pts, b);
    Vec3 q[4];
    xvert(pa, o0, ya, &q[0]);
    xvert(pa, o1, ya, &q[1]);
    xvert(pb, o1, yb, &q[2]);
    xvert(pb, o0, yb, &q[3]);
    r_ground_cam(q, 4, color);
}

// A strip from (o0, y0) to (o1, y1) across the road at both ends (embankments).
IWRAM_CODE static void draw_slope(const TrackPt *pts, s32 a, s32 b, s32 oa0, s32 ya0, s32 oa1, s32 ya1,
                                  s32 ob0, s32 yb0, s32 ob1, s32 yb1, u8 color)
{
    const XPt *pa = xget(pts, a), *pb = xget(pts, b);
    Vec3 q[4];
    xvert(pa, oa0, ya0, &q[0]);
    xvert(pa, oa1, ya1, &q[1]);
    xvert(pb, ob1, yb1, &q[2]);
    xvert(pb, ob0, yb0, &q[3]);
    r_ground_cam(q, 4, color);
}

// Rarely drawn details stay in ROM rather than being inlined into the
// IWRAM road functions that call them (IWRAM is full).
#define ROM_CODE __attribute__((noinline))

// Kerbs: red and white blocks along both edges, three to a stretch close
// up so they stay a car length or so long.
#define KERB_W 24
ROM_CODE static void draw_kerbs(const TrackPt *pts, s32 a, s32 b, s32 ya, s32 yb, s32 depth)
{
    const s32 W = TRACK_HALF_W;
    const u8 red = COLOR(M_STUNT_RED, 0), white = COLOR(M_STUNT_WHITE, 0);
    if (depth >= 450 || b != (a + 1 == g_track->count ? 0 : a + 1)) {
        u8 k = (a & 1) ? red : white;
        draw_strip(pts, a, b, -W - KERB_W, -W, ya, yb, k);
        draw_strip(pts, a, b, W, W + KERB_W, ya, yb, k);
        return;
    }
    const XPt *pa = xget(pts, a), *pb = xget(pts, b);
    Vec3 l0, l1, r0, r1, q[4];
    xvert(pa, -W - KERB_W, ya, &l0); xvert(pa, -W, ya, &l1);
    xvert(pa, W, ya, &r0); xvert(pa, W + KERB_W, ya, &r1);
    Vec3 dl0, dl1, dr0, dr1;      // a third of the way to b
    xvert(pb, -W - KERB_W, yb, &dl0); xvert(pb, -W, yb, &dl1);
    xvert(pb, W, yb, &dr0); xvert(pb, W + KERB_W, yb, &dr1);
#define THIRD(v, e) v.x = (e.x - v.x) / 3; v.y = (e.y - v.y) / 3; v.z = (e.z - v.z) / 3
    THIRD(dl0, l0); THIRD(dl1, l1); THIRD(dr0, r0); THIRD(dr1, r1);
#undef THIRD
    for (s32 k = 0; k < 3; k++) {
        u8 c = ((a * 3 + k) & 1) ? red : white;
        q[0] = l0; q[1] = l1;
        l0.x += dl0.x; l0.y += dl0.y; l0.z += dl0.z;
        l1.x += dl1.x; l1.y += dl1.y; l1.z += dl1.z;
        q[2] = l1; q[3] = l0;
        r_ground_cam(q, 4, c);
        q[0] = r0; q[1] = r1;
        r0.x += dr0.x; r0.y += dr0.y; r0.z += dr0.z;
        r1.x += dr1.x; r1.y += dr1.y; r1.z += dr1.z;
        q[2] = r1; q[3] = r0;
        r_ground_cam(q, 4, c);
    }
}

// The chequered start line across the road at point a.
ROM_CODE static void draw_start_line(const TrackPt *pts, s32 a, s32 ya)
{
    const s32 W = TRACK_HALF_W, n = 10, w = 2 * W / 10;
    const TrackPt *pa = &pts[a];
    s32 fx = pa->ux >> 9, fz = pa->uz >> 9;      // rows 32 units deep
    for (s32 i = 0; i < n; i++) {
        s32 o0 = -W + i * w, o1 = o0 + w;
        for (s32 row = 0; row < 2; row++)
            ground4(EX(a, o0) + fx * row, ya, EZ(a, o0) + fz * row, EX(a, o1) + fx * row, ya, EZ(a, o1) + fz * row,
                    EX(a, o1) + fx * (row + 1), ya, EZ(a, o1) + fz * (row + 1),
                    EX(a, o0) + fx * (row + 1), ya, EZ(a, o0) + fz * (row + 1),
                    ((i + row) & 1) ? COLOR(M_SHADOW, 0) : COLOR(M_STUNT_WHITE, 0));
    }
}

// Far off, stretches are drawn two or four at a time (about 300 and 600
// units of road). Returns the end point to draw to, or -1 when a longer
// stretch before this one covers it.
static s32 lod_stride(s32 depth) { return depth > 1000 ? 4 : depth > 450 ? 2 : 1; }

static s32 seg_mid_depth(s32 i)
{
    const TrackPt *p = &g_track->pts[i];
    s32 side;
    return r_depth(p->x + ((p->ux * p->len) >> 15), p->z + ((p->uz * p->len) >> 15), &side);
}

static s32 seg_end(s32 a, s32 depth)
{
    const TrackDef *t = g_track;
    s32 s = lod_stride(depth);
    while (s > 1 && (a & (s - 1))) {
        if (lod_stride(seg_mid_depth(a & ~(s - 1))) >= s) return -1;
        s >>= 1;
    }
    s32 b = a + s;
    return b >= t->count ? b - t->count : b;
}

IWRAM_CODE static void draw_segment(s32 a, s32 depth)
{
    const TrackDef *t = g_track;
    const TrackPt *pts = t->pts;
    s32 b = seg_end(a, depth);
    if (b < 0) return;
    const TrackPt *pa = &pts[a];
    s32 ya = pa->y, yb = pts[b].y;
    s32 bridge = pa->flags & TF_BRIDGE;
    s32 ea = barrier(pa), eb = barrier(&pts[b]);
    s32 e = ea < eb ? ea : eb;
    s32 W = TRACK_HALF_W;

    if (!bridge && (ya || yb) && depth < 850) {
        s32 oa = ea + ya * BANK / 2, ob = eb + yb * BANK / 2;
        if (depth < 500) {
            // Close up: flat verges out to the barriers, then embankments.
            u8 g = COLOR(M_GRASS, (a >> 1) & 1);
            draw_slope(pts, a, b, -oa, 0, -ea, ya, -ob, 0, -eb, yb, COLOR(M_GRASS, 2));
            draw_slope(pts, a, b, ea, ya, oa, 0, eb, yb, ob, 0, COLOR(M_GRASS, 2));
            draw_strip(pts, a, b, -e, -W, ya, yb, g);
            draw_strip(pts, a, b, W, e, ya, yb, g);
        } else {
            // Further off one slope from the road edge down to the plain will do.
            draw_slope(pts, a, b, -oa, 0, -W, ya, -ob, 0, -W, yb, COLOR(M_GRASS, 1));
            draw_slope(pts, a, b, W, ya, oa, 0, W, yb, ob, 0, COLOR(M_GRASS, 1));
        }
    }
    draw_strip(pts, a, b, -W, W, ya, yb, COLOR(M_ASPHALT, 0));
    if (depth < 600) {
        u8 line = COLOR(M_STUNT_WHITE, 1);
        draw_strip(pts, a, b, -W + 6, -W + 11, ya, yb, line);
        draw_strip(pts, a, b, W - 11, W - 6, ya, yb, line);
    }
    if ((pa->flags & TF_KERB_L) && !bridge && depth < 900) draw_kerbs(pts, a, b, ya, yb, depth);
    if (pa->flags & TF_START) draw_start_line(pts, a, ya);
}

// Walls, railings, gantries and bridge structure: depth-sorted faces.
static void draw_segment_faces(s32 a, s32 depth)
{
    const TrackDef *t = g_track;
    const TrackPt *pts = t->pts;
    s32 b = seg_end(a, depth);
    if (b < 0) return;
    const TrackPt *pa = &pts[a];
    s32 ya = pa->y, yb = pts[b].y;
    if (pa->flags & TF_BRIDGE) {
        s32 o = TRACK_HALF_W + 4;
        for (s32 s = -1; s <= 1; s += 2) {
            s32 xa = EX(a, s * o), za = EZ(a, s * o), xb = EX(b, s * o), zb = EZ(b, s * o);
            face4(xa, ya + RAIL_H, za, xb, yb + RAIL_H, zb, xb, yb - 26, zb, xa, ya - 26, za,
                  COLOR(M_STUNT_WHITE, 1 + (s > 0)), RF_TWO_SIDED);
        }
        // Piers and towers along the stretch (several points of it far off).
        // Faces always cover the road, so from above the deck the piers
        // (hidden under it) are left out and the towers start at the deck.
        const XPt *xa = xget(pts, a);
        s32 cam_y = -((xa->c.x * xup.x + xa->c.y * xup.y + xa->c.z * xup.z) >> 14);
        for (s32 i = a; i != b; i = i + 1 == t->count ? 0 : i + 1) {
            const TrackPt *p = &pts[i];
            if (!(p->flags & TF_BRIDGE)) continue;
            s32 above = cam_y > p->y - 26;
            if (!above && depth < 1200 && (p->dist >> PIER_SHIFT) != ((p->dist + p->len) >> PIER_SHIFT)) {
                s32 o = TRACK_HALF_W - 30, y = p->y - 26;
                r_box(EX(i, -o) - 12, 0, EZ(i, -o) - 12, EX(i, -o) + 12, y, EZ(i, -o) + 12, M_SIDEWALK);
                r_box(EX(i, o) - 12, 0, EZ(i, o) - 12, EX(i, o) + 12, y, EZ(i, o) + 12, M_SIDEWALK);
            }
            if (!tower_at[i]) continue;
            s32 o2 = TRACK_HALF_W + 20, top = p->y + TOWER_H, foot = above ? p->y - 26 : 0;
            s32 lx = EX(i, -o2), lz = EZ(i, -o2), rx = EX(i, o2), rz = EZ(i, o2);
            r_box(lx - 16, foot, lz - 16, lx + 16, top, lz + 16, M_STUNT_RED);
            r_box(rx - 16, foot, rz - 16, rx + 16, top, rz + 16, M_STUNT_RED);
            // Cross beam, then cables sweeping down both ways.
            face4(lx, top - 10, lz, rx, top - 10, rz, rx, top - 40, rz, lx, top - 40, lz, COLOR(M_STUNT_RED, 1), RF_TWO_SIDED);
            for (s32 s = -1; s <= 1; s += 2) {
                s32 j = i + s * tower_at[i];
                if (j < 0) j += t->count;
                if (j >= t->count) j -= t->count;
                if (!(pts[j].flags & TF_BRIDGE)) continue;
                s32 ox = TRACK_HALF_W + 6;
                face4(lx, top, lz, lx, top - 6, lz, EX(j, -ox), pts[j].y + RAIL_H, EZ(j, -ox),
                      EX(j, -ox), pts[j].y + RAIL_H + 4, EZ(j, -ox), COLOR(M_STUNT_RED, 0), RF_TWO_SIDED);
                face4(rx, top, rz, rx, top - 6, rz, EX(j, ox), pts[j].y + RAIL_H, EZ(j, ox),
                      EX(j, ox), pts[j].y + RAIL_H + 4, EZ(j, ox), COLOR(M_STUNT_RED, 0), RF_TWO_SIDED);
            }
        }
        return;
    }
    if (pa->flags & TF_TUNNEL) {
        // Walls and a roof close in over the road; a portal at each end.
        s32 ea = barrier(pa), eb = barrier(&pts[b]), H = 160;
        s32 prev = a ? a - 1 : t->count - 1;
        if (depth < 1150) {
            for (s32 s = -1; s <= 1; s += 2)
                face4(EX(a, s * ea), ya + H, EZ(a, s * ea), EX(b, s * eb), yb + H, EZ(b, s * eb),
                      EX(b, s * eb), yb, EZ(b, s * eb), EX(a, s * ea), ya, EZ(a, s * ea),
                      COLOR(M_SHADOW, 0), RF_TWO_SIDED);
            face4(EX(a, -ea), ya + H, EZ(a, -ea), EX(a, ea), ya + H, EZ(a, ea),
                  EX(b, eb), yb + H, EZ(b, eb), EX(b, -eb), yb + H, EZ(b, -eb), COLOR(M_SHADOW, 0), RF_TWO_SIDED);
        }
        for (s32 end = 0; end < 2; end++) {
            s32 i = end ? b : a;
            if (end ? (pts[b].flags & TF_TUNNEL) : (pts[prev].flags & TF_TUNNEL)) continue;
            s32 e = barrier(&pts[i]), y = pts[i].y, top = y + H + 110, wide = e + 300;
            face4(EX(i, -wide), top, EZ(i, -wide), EX(i, -e), top, EZ(i, -e),
                  EX(i, -e), y - 10, EZ(i, -e), EX(i, -wide), y - 10, EZ(i, -wide), COLOR(M_SIDEWALK, 1), RF_TWO_SIDED);
            face4(EX(i, e), top, EZ(i, e), EX(i, wide), top, EZ(i, wide),
                  EX(i, wide), y - 10, EZ(i, wide), EX(i, e), y - 10, EZ(i, e), COLOR(M_SIDEWALK, 1), RF_TWO_SIDED);
            face4(EX(i, -e), top, EZ(i, -e), EX(i, e), top, EZ(i, e),
                  EX(i, e), y + H, EZ(i, e), EX(i, -e), y + H, EZ(i, -e), COLOR(M_SIDEWALK, 2), RF_TWO_SIDED);
        }
        return;
    }
    if (depth > 1150) return;
    // Barrier walls on both sides: tyre-wall red and white near corners,
    // otherwise the course's guard rail colour.
    s32 ea = barrier(pa), eb = barrier(&pts[b]);
    s32 kerb = pa->flags & TF_KERB_L;
    u8 c0 = kerb ? COLOR(M_STUNT_RED, 0) : COLOR(M_STUNT_WHITE, 1);
    u8 c1 = kerb ? COLOR(M_STUNT_WHITE, 0) : COLOR(t->scene == SCENE_MOUNTAINS ? M_BLD5 : M_BLD2, 1);
    u8 c = (a & 1) ? c0 : c1;
    face4(EX(a, -ea), ya + RAIL_H, EZ(a, -ea), EX(b, -eb), yb + RAIL_H, EZ(b, -eb),
          EX(b, -eb), yb, EZ(b, -eb), EX(a, -ea), ya, EZ(a, -ea), c, RF_TWO_SIDED);
    face4(EX(a, ea), ya + RAIL_H, EZ(a, ea), EX(b, eb), yb + RAIL_H, EZ(b, eb),
          EX(b, eb), yb, EZ(b, eb), EX(a, ea), ya, EZ(a, ea), c, RF_TWO_SIDED);
    // Start gantry.
    if (pa->flags & TF_START) {
        s32 o = TRACK_HALF_W + 24, h = ya + 110;
        s32 lx = EX(a, -o), lz = EZ(a, -o), rx = EX(a, o), rz = EZ(a, o);
        r_box(lx - 6, ya, lz - 6, lx + 6, h, lz + 6, M_STUNT_WHITE);
        r_box(rx - 6, ya, rz - 6, rx + 6, h, rz + 6, M_STUNT_WHITE);
        face4(lx, h + 20, lz, rx, h + 20, rz, rx, h - 4, rz, lx, h - 4, lz, COLOR(M_STUNT_RED, 0), RF_TWO_SIDED);
    }
}

void track_draw(s32 focus_x, s32 focus_z, s32 frame)
{
    const TrackDef *t = g_track;
    const TrackPt *pts = t->pts;

    // Water first: it lies under everything. Only the tiles in reach of the
    // camera (within ~400 of the focus) are tried.
    const s32 reach = r_far + 400;
    for (s32 w = 0; w < t->water_count; w++) {
        const s16 *r = &t->water[w * 4];
        s32 x0 = r[0], z0 = r[1], xe = r[2], ze = r[3];
        if (focus_x - reach > x0) x0 += ((focus_x - reach - x0) >> 9) << 9;
        if (focus_z - reach > z0) z0 += ((focus_z - reach - z0) >> 9) << 9;
        if (xe > focus_x + reach) xe = focus_x + reach;
        if (ze > focus_z + reach) ze = focus_z + reach;
        for (s32 z = z0; z < ze; z += 512)
            for (s32 x = x0; x < xe; x += 512) {
                s32 x1 = x + 512 < r[2] ? x + 512 : r[2], z1 = z + 512 < r[3] ? z + 512 : r[3];
                if (!r_visible((x + x1) / 2, (z + z1) / 2, 400)) continue;
                ground4(x, 0, z1, x1, 0, z1, x1, 0, z, x, 0, z, COLOR(M_WATER, 0));
            }
    }

    xstamp++;
    r_xform_dir(0, 16384, 0, &xup);

    // Visible stretches of road, drawn far to near (the list runs farthest
    // first; when it is full the farthest gives way).
    s32 n = 0;
    for (s32 i = 0; i < t->count; i++) {
        if (!(i & (BLOCK_PTS - 1)) && !r_visible(blk_x[i / BLOCK_PTS], blk_z[i / BLOCK_PTS], blk_r[i / BLOCK_PTS])) {
            i += BLOCK_PTS - 1;
            continue;
        }
        const TrackPt *p = &pts[i];
        s32 mx = p->x + ((p->ux * p->len) >> 15), mz = p->z + ((p->uz * p->len) >> 15);
        s32 radius = p->len / 2 + barrier(p) + p->y * BANK / 2 + 16;
        if (!r_visible(mx, mz, radius)) continue;
        s32 side, d = r_depth(mx, mz, &side);
        if (n == MAX_VIS) {
            if (d >= vis_depth[0]) continue;
            for (s32 k = 1; k < n; k++) { vis_depth[k - 1] = vis_depth[k]; vis_seg[k - 1] = vis_seg[k]; }
            n--;
        }
        s32 k = n++;
        while (k > 0 && vis_depth[k - 1] < d) {
            vis_depth[k] = vis_depth[k - 1];
            vis_seg[k] = vis_seg[k - 1];
            k--;
        }
        vis_depth[k] = d;
        vis_seg[k] = i;
    }
    for (s32 k = 0; k < n; k++) draw_segment(vis_seg[k], vis_depth[k]);

    // Faces: nearest first so the budget goes to what matters.
    for (s32 k = n - 1; k >= 0; k--) draw_segment_faces(vis_seg[k], vis_depth[k]);

    // Landmarks first so trees and rocks can't use up the face budget on them.
    for (s32 i = 0; i < t->mark_count; i++) {
        const Prop *p = &t->marks[i];
        if (!r_visible(p->x, p->z, prop_reach[p->type])) continue;
        s32 side, d = r_depth(p->x, p->z, &side);
        draw_prop(p, p->y2 * 2, d, frame);
    }

    // The rest of the scenery near the camera (within ~400 of the focus).
    const s32 range = (SMALL_FAR + 600 + (1 << TRACK_PCELL_SHIFT) - 1) >> TRACK_PCELL_SHIFT;
    s32 cx0 = (focus_x >> TRACK_PCELL_SHIFT) - range, cx1 = (focus_x >> TRACK_PCELL_SHIFT) + range;
    s32 cz0 = (focus_z >> TRACK_PCELL_SHIFT) - range, cz1 = (focus_z >> TRACK_PCELL_SHIFT) + range;
    if (cx0 < 0) cx0 = 0;
    if (cz0 < 0) cz0 = 0;
    if (cx1 > TRACK_PGRID - 1) cx1 = TRACK_PGRID - 1;
    if (cz1 > TRACK_PGRID - 1) cz1 = TRACK_PGRID - 1;
    for (s32 cz = cz0; cz <= cz1; cz++)
        for (s32 cx = cx0; cx <= cx1; cx++) {
            s32 c = cz * TRACK_PGRID + cx;
            s32 first = t->prop_first[c], end = first + t->prop_count[c];
            for (s32 i = first; i < end; i++) {
                const Prop *p = &t->props[i];
                if (!r_visible(p->x, p->z, 120)) continue;
                s32 side, d = r_depth(p->x, p->z, &side);
                if (d > SMALL_FAR) continue;
                draw_prop(p, p->y2 * 2, d, frame);
            }
        }
}

// ---------------------------------------------------------------- map

static void map_pixel(volatile u16 *page, s32 x, s32 y, u8 c)
{
    if ((u32)x >= SCREEN_W || (u32)y >= SCREEN_H) return;
    volatile u16 *p = page + (y * SCREEN_W + x) / 2;
    *p = (x & 1) ? ((*p & 0x00FF) | (c << 8)) : ((*p & 0xFF00) | c);
}

void track_map(volatile u16 *page, s32 index, s32 x0, s32 y0, s32 w, s32 h, u8 color, s32 dot_d, u8 dot_color)
{
    const TrackDef *t = &g_tracks[index];
    s32 minx = 99999, maxx = -1, minz = 99999, maxz = -1;
    for (s32 i = 0; i < t->count; i++) {
        const TrackPt *p = &t->pts[i];
        if (p->x < minx) minx = p->x;
        if (p->x > maxx) maxx = p->x;
        if (p->z < minz) minz = p->z;
        if (p->z > maxz) maxz = p->z;
    }
    s32 sx = ((maxx - minx) << 8) / (w - 4), sz = ((maxz - minz) << 8) / (h - 4);
    s32 s = sx > sz ? sx : sz;
    if (!s) s = 1;
    s32 ox = x0 + (w - (((maxx - minx) << 8) / s)) / 2, oy = y0 + (h + (((maxz - minz) << 8) / s)) / 2;
    s32 dot = -1;
    for (s32 i = 0; i < t->count; i++) {
        const TrackPt *a = &t->pts[i], *b = &t->pts[i + 1 == t->count ? 0 : i + 1];
        s32 ax = ox + (((a->x - minx) << 8) / s), ay = oy - (((a->z - minz) << 8) / s);
        s32 bx = ox + (((b->x - minx) << 8) / s), by = oy - (((b->z - minz) << 8) / s);
        s32 steps = iabs(bx - ax) > iabs(by - ay) ? iabs(bx - ax) : iabs(by - ay);
        if (!steps) steps = 1;
        for (s32 k = 0; k <= steps; k++) {
            s32 px = ax + ((bx - ax) * k) / steps, py = ay + ((by - ay) * k) / steps;
            map_pixel(page, px, py, color);
            map_pixel(page, px + 1, py, color);
            map_pixel(page, px, py + 1, color);
            map_pixel(page, px + 1, py + 1, color);
        }
        if (dot_d >= a->dist && dot_d < a->dist + a->len) dot = i;
        if (i == 0)
            for (s32 k = -3; k <= 4; k++) { map_pixel(page, ax + k, ay - 1, COLOR(M_STUNT_WHITE, 0)); map_pixel(page, ax + k, ay + 2, COLOR(M_STUNT_WHITE, 0)); }
    }
    if (dot >= 0) {
        const TrackPt *a = &t->pts[dot];
        s32 ax = ox + (((a->x - minx) << 8) / s), ay = oy - (((a->z - minz) << 8) / s);
        for (s32 dy = -2; dy <= 3; dy++)
            for (s32 dx = -2; dx <= 3; dx++) map_pixel(page, ax + dx, ay + dy, dot_color);
    }
}
