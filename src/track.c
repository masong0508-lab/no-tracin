// Race circuits: lookup of the nearest stretch of road, the height field
// and barriers built from it, and drawing the road, verges, embankments,
// bridges and scenery.
#include "track.h"
#include "world.h"

#include "tracks_data.h"

const TrackDef g_tracks[TRACK_COUNT] = {
    { "BIG FOREST", "BEGINNER", bf_pts, BF_POINTS, BF_LAP, bf_cell_first, bf_cell_count, bf_cell_list,
      bf_props, bf_prop_first, bf_prop_count, BF_PROPS, bf_water, BF_WATER, 4, 42, 17, SCENE_FOREST },
    { "BAY BRIDGE", "MEDIUM", bb_pts, BB_POINTS, BB_LAP, bb_cell_first, bb_cell_count, bb_cell_list,
      bb_props, bb_prop_first, bb_prop_count, BB_PROPS, bb_water, BB_WATER, 4, 38, 15, SCENE_BAY },
    { "ACROPOLIS", "EXPERT", ac_pts, AC_POINTS, AC_LAP, ac_cell_first, ac_cell_count, ac_cell_list,
      ac_props, ac_prop_first, ac_prop_count, AC_PROPS, ac_water, AC_WATER, 4, 44, 18, SCENE_MOUNTAINS },
};

const TrackDef *g_track;

#define MAX_POINTS 256
#define RAIL_H     16
#define BANK       2        // embankments fall 1 unit for every 1 outward

static s16 nrx[MAX_POINTS] EWRAM_BSS, nrz[MAX_POINTS] EWRAM_BSS;   // right-pointing normal at each point, Q14
static u8  tower_at[MAX_POINTS] EWRAM_BSS;
static u16 inv_len[MAX_POINTS] EWRAM_BSS;      // 65536 / length of each stretch
s16 track_pitch[MAX_POINTS] EWRAM_BSS;         // slope of each stretch, 1024-unit angle
// Bounding circles of runs of 8 points, to skip whole stretches off screen.
#define BLOCK_PTS 8
static s16 blk_x[MAX_POINTS / BLOCK_PTS], blk_z[MAX_POINTS / BLOCK_PTS], blk_r[MAX_POINTS / BLOCK_PTS];              // bridge towers stand at these points

enum {
    P_TREE, P_PINE, P_BUILDING, P_STAND, P_FERRIS, P_TENT, P_TOWER, P_ROCK,
    P_COLUMN, P_TEMPLE, P_CRANE, P_LIGHTHOUSE, P_BALLOON, P_HOUSE, P_SIGN, P_BOAT,
};

// ---------------------------------------------------------------- queries

static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }

static inline s32 barrier(const TrackPt *p) { return TRACK_HALF_W + p->verge; }

s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    const TrackDef *t = g_track;
    if (x < 0 || z < 0 || x >= 8192 || z >= 8192) return 0;
    s32 cell = (z >> 7) * 64 + (x >> 7);
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
    // Bridge towers a quarter and three quarters of the way across long spans.
    for (s32 i = 0; i < t->count; i++) {
        if (!(t->pts[i].flags & TF_BRIDGE) || (t->pts[i ? i - 1 : t->count - 1].flags & TF_BRIDGE)) continue;
        s32 n = 0;
        while (n < t->count && (t->pts[(i + n) % t->count].flags & TF_BRIDGE)) n++;
        if (n >= 10) {
            tower_at[(i + n / 4) % t->count] = 1;
            tower_at[(i + n * 3 / 4) % t->count] = 1;
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
    const s32 R = 170, cy = y + R + 40, spokes = 12;
    // A-frame legs.
    face4(x - 70, y, z - 14, x - 4, cy, z - 14, x + 4, cy, z - 14, x + 70, y, z - 14, COLOR(M_STUNT_WHITE, 2), RF_TWO_SIDED);
    face4(x - 70, y, z + 14, x - 4, cy, z + 14, x + 4, cy, z + 14, x + 70, y, z + 14, COLOR(M_STUNT_WHITE, 2), RF_TWO_SIDED);
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
                face4(x - 2, cy, z, x + 2, cy, z, x + ex + 2, cy + ey, z, x + ex - 2, cy + ey, z,
                      COLOR(M_STUNT_WHITE, 1), RF_TWO_SIDED);
            else
                r_box(x + ex - 9, cy + ey - 22, z - 9, x + ex + 9, cy + ey - 4, z + 9, i & 2 ? M_BLD2 : M_BLD4);
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
    s32 cy = y + 380 + ((isin(frame * 3) * 30) >> 14);
    const s32 r = 60;
    s32 px = r, pz = 0;
    for (s32 i = 1; i <= 8; i++) {
        s32 a = i * 128;
        s32 ex = (icos(a) * r) >> 14, ez = (isin(a) * r) >> 14;
        s32 m = i & 1 ? M_LINE : M_STUNT_RED;
        Vec3 top[3] = { { x, cy + 70, z }, { x + ex, cy, z + ez }, { x + px, cy, z + pz } };
        Vec3 bot[3] = { { x, cy - 80, z }, { x + px, cy, z + pz }, { x + ex, cy, z + ez } };
        r_face(top, 3, COLOR(m, 0), 0);
        r_face(bot, 3, COLOR(m, 2), 0);
        px = ex; pz = ez;
    }
    r_box(x - 10, cy - 110, z - 10, x + 10, cy - 94, z + 10, M_BLD1);
}

// Grandstand facing direction rot (0 +z, 1 +x, 2 -z, 3 -x), centred on (x, z).
static void draw_stand(s32 x, s32 y, s32 z, s32 rot)
{
    const s32 half = 170;
    for (s32 k = 0; k < 4; k++) {
        s32 d0 = -k * 30, d1 = -k * 30 - 30, h = 20 + k * 22;   // steps rise away from the road
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
    case 0: r_box(x - half, y + 120, z - 130, x + half, y + 128, z + 10, M_STUNT_RED); break;
    case 1: r_box(x - 130, y + 120, z - half, x + 10, y + 128, z + half, M_STUNT_RED); break;
    case 2: r_box(x - half, y + 120, z - 10, x + half, y + 128, z + 130, M_STUNT_RED); break;
    default: r_box(x - 10, y + 120, z - half, x + 130, y + 128, z + half, M_STUNT_RED); break;
    }
}

static void draw_temple(s32 x, s32 y, s32 z, s32 depth)
{
    const s32 w = 160, d = 90, ch = 90;
    r_box(x - w - 12, y, z - d - 12, x + w + 12, y + 16, z + d + 12, M_BLD3);
    s32 step = depth > 900 ? 2 : 1;
    for (s32 i = 0; i < 6; i += step) {
        s32 cx = x - w + 8 + i * ((2 * w - 16) / 5);
        r_box(cx - 8, y + 16, z - d, cx + 8, y + 16 + ch, z - d + 16, M_BLD3);
        r_box(cx - 8, y + 16, z + d - 16, cx + 8, y + 16 + ch, z + d, M_BLD3);
    }
    r_box(x - w - 6, y + 16 + ch, z - d - 6, x + w + 6, y + 30 + ch, z + d + 6, M_BLD3);
    roof(x - w - 6, z - d - 6, x + w + 6, z + d + 6, y + 30 + ch, 34, 1, M_BLD3);
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
        r_box(x - 12, y, z - 12, x + 12, y + 420, z + 12, M_STUNT_WHITE);
        r_box(x - 16, y + 420, z - 16, x + 16, y + 450, z + 16, M_STUNT_RED);
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
    case P_CRANE:
        r_box(x - 14, y, z - 14, x + 14, y + 260, z + 14, M_BLD4);
        r_box(x - 16, y + 260, z - 220, x + 16, y + 280, z + 80, M_BLD4);
        r_box(x - 20, y + 230, z - 20, x + 20, y + 262, z + 20, M_BLD1);
        break;
    case P_LIGHTHOUSE:
        for (s32 k = 0; k < 5; k++)
            r_box(x - 24 + k * 2, y + k * 50, z - 24 + k * 2, x + 24 - k * 2, y + k * 50 + 50, z + 24 - k * 2,
                  k & 1 ? M_STUNT_RED : M_STUNT_WHITE);
        r_box(x - 16, y + 250, z - 16, x + 16, y + 280, z + 16, (frame >> 3) & 1 ? M_LINE : M_GLASS);
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
static u8  vis_seg[MAX_VIS];
static s16 vis_depth[MAX_VIS];

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

// Far off, stretches are drawn two or four at a time. Returns the end point
// to draw to, or -1 when a longer stretch before this one covers it.
static s32 lod_stride(s32 depth) { return depth > 600 ? 4 : depth > 260 ? 2 : 1; }

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
    if ((pa->flags & TF_KERB_L) && !bridge && depth < 900) {
        u8 k = (a & 1) ? COLOR(M_STUNT_RED, 0) : COLOR(M_STUNT_WHITE, 0);
        draw_strip(pts, a, b, -W - 14, -W, ya, yb, k);
        draw_strip(pts, a, b, W, W + 14, ya, yb, k);
    }
    if (pa->flags & TF_START) {
        for (s32 i = 0; i < 8; i++) {
            s32 o0 = -W + i * (2 * W / 8), o1 = o0 + 2 * W / 8;
            ground4(EX(a, o0), ya, EZ(a, o0), EX(a, o1), ya, EZ(a, o1),
                    EX(a, o1) + (pa->ux >> 10), ya, EZ(a, o1) + (pa->uz >> 10),
                    EX(a, o0) + (pa->ux >> 10), ya, EZ(a, o0) + (pa->uz >> 10),
                    (i & 1) ? COLOR(M_SHADOW, 0) : COLOR(M_STUNT_WHITE, 0));
            ground4(EX(a, o0) + (pa->ux >> 10), ya, EZ(a, o0) + (pa->uz >> 10),
                    EX(a, o1) + (pa->ux >> 10), ya, EZ(a, o1) + (pa->uz >> 10),
                    EX(a, o1) + (pa->ux >> 9), ya, EZ(a, o1) + (pa->uz >> 9),
                    EX(a, o0) + (pa->ux >> 9), ya, EZ(a, o0) + (pa->uz >> 9),
                    (i & 1) ? COLOR(M_STUNT_WHITE, 0) : COLOR(M_SHADOW, 0));
        }
    }
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
            if ((a % 3) == 0 && depth < 1200)
                r_box(xa - 8, 0, za - 8, xa + 8, ya - 26, za + 8, M_SIDEWALK);
        }
        if (tower_at[a]) {
            s32 o2 = TRACK_HALF_W + 20, top = ya + 260;
            s32 lx = EX(a, -o2), lz = EZ(a, -o2), rx = EX(a, o2), rz = EZ(a, o2);
            r_box(lx - 12, 0, lz - 12, lx + 12, top, lz + 12, M_STUNT_RED);
            r_box(rx - 12, 0, rz - 12, rx + 12, top, rz + 12, M_STUNT_RED);
            // Cross beam, then cables sweeping down both ways.
            face4(lx, top - 10, lz, rx, top - 10, rz, rx, top - 34, rz, lx, top - 34, lz, COLOR(M_STUNT_RED, 1), RF_TWO_SIDED);
            for (s32 s = -1; s <= 1; s += 2) {
                s32 j = a + s * 7;
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

    // Water first: it lies under everything.
    for (s32 w = 0; w < t->water_count; w++) {
        const s16 *r = &t->water[w * 4];
        for (s32 z = r[1]; z < r[3]; z += 512)
            for (s32 x = r[0]; x < r[2]; x += 512) {
                s32 x1 = x + 512 < r[2] ? x + 512 : r[2], z1 = z + 512 < r[3] ? z + 512 : r[3];
                if (!r_visible((x + x1) / 2, (z + z1) / 2, 400)) continue;
                ground4(x, 0, z1, x1, 0, z1, x1, 0, z, x, 0, z, COLOR(M_WATER, 0));
            }
    }

    xstamp++;
    r_xform_dir(0, 16384, 0, &xup);

    // Visible stretches of road, drawn far to near.
    s32 n = 0;
    for (s32 i = 0; i < t->count && n < MAX_VIS; i++) {
        if (!(i & (BLOCK_PTS - 1)) && !r_visible(blk_x[i / BLOCK_PTS], blk_z[i / BLOCK_PTS], blk_r[i / BLOCK_PTS])) {
            i += BLOCK_PTS - 1;
            continue;
        }
        const TrackPt *p = &pts[i];
        s32 mx = p->x + ((p->ux * p->len) >> 15), mz = p->z + ((p->uz * p->len) >> 15);
        s32 radius = p->len / 2 + barrier(p) + p->y * BANK / 2 + 16;
        if (!r_visible(mx, mz, radius)) continue;
        s32 side, d = r_depth(mx, mz, &side);
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

    // Scenery near the camera.
    const s32 range = (r_far + 600) / 512;
    s32 cx0 = focus_x / 512 - range, cx1 = focus_x / 512 + range;
    s32 cz0 = focus_z / 512 - range, cz1 = focus_z / 512 + range;
    if (cx0 < 0) cx0 = 0;
    if (cz0 < 0) cz0 = 0;
    if (cx1 > 15) cx1 = 15;
    if (cz1 > 15) cz1 = 15;
    for (s32 cz = cz0; cz <= cz1; cz++)
        for (s32 cx = cx0; cx <= cx1; cx++) {
            s32 c = cz * 16 + cx;
            s32 first = t->prop_first[c], end = first + t->prop_count[c];
            for (s32 i = first; i < end; i++) {
                const Prop *p = &t->props[i];
                s32 big = p->type == P_FERRIS || p->type == P_TEMPLE || p->type == P_STAND ||
                          p->type == P_BALLOON || p->type == P_LIGHTHOUSE || p->type == P_TOWER;
                if (!r_visible(p->x, p->z, big ? 260 : 120)) continue;
                s32 side, d = r_depth(p->x, p->z, &side);
                if (!big && d > 760) continue;
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
