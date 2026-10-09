// Freedom City: building layout, the Stunt Park, street ramps, the height
// field the physics drives on, wall collisions and drawing.
#include "world.h"
#include "track.h"

extern volatile u32 g_frames;

#define MAX_SOLIDS   2400
#define MAX_FEATURES 160     // feat_refs holds feature numbers as bytes

typedef struct { s16 x0, z0, x1, z1, h; u8 material; } Solid;

// F_RAMP: rectangle x0..x1 / z0..z1 whose height runs linearly from h0 at
//         the low end to h1 at the high end of its axis (0 = z, 1 = x).
// F_WATER: rectangle of deep water.
// F_BANK: banked half-circle north of centre (x0, z0), inner radius x1,
//         outer radius z1, rim height h1, with a rail on the rim.
// F_BANKLEAD: straight lead-in to the bank: height runs across x from h0
//         (at x0) to h1 (at x1) and eases in along z from nothing at z0 to
//         full at z1, with a rail on the high edge.
enum { F_RAMP, F_WATER, F_BANK, F_BANKLEAD };
typedef struct { u8 type, axis; s16 x0, z0, x1, z1, h0, h1; } Feature;

// The world's tables live in EWRAM: IWRAM is kept for code and the stack.
static Solid   solids[MAX_SOLIDS] EWRAM_BSS;
static s32     solid_count;
static Feature features[MAX_FEATURES] EWRAM_BSS;
static s32     feature_count;
static u8      block_kind[BLOCKS][BLOCKS] EWRAM_BSS;
// Features touching each block, so height and wall queries only look at
// the few that can matter.
#define MAX_FEAT_REFS 512
static u8      feat_refs[MAX_FEAT_REFS] EWRAM_BSS;
static u16     cell_first[BLOCKS][BLOCKS] EWRAM_BSS;
static u8      cell_count[BLOCKS][BLOCKS] EWRAM_BSS;
static u16     block_first[BLOCKS][BLOCKS] EWRAM_BSS;
static u8      block_count[BLOCKS][BLOCKS] EWRAM_BSS;
// Anything the tables had no room for (solids, features, refs, faces). The
// city is built the same way every boot, so a test that reads 0 here once
// proves nothing is ever dropped.
s32 world_overflow EWRAM_BSS;

const Loop the_loop = { LOOP_X, LOOP_Z, 120, 100, 84 };

#define RAIL_H    14
#define ALLEY     32                              // gap between buildings on a lot
#define LOT       (BLOCK - 2 * (ROAD_HALF + WALK))   // the building plot inside the sidewalk
#define KICKERS   (24 * (WORLD / 512) * (WORLD / 512) / 256)   // street kickers to try: 24 per 16 x 16 old blocks

// ---------------------------------------------------------------- layout

static u32 hash(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352d;
    x ^= x >> 15; x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static void add_solid(s32 x0, s32 z0, s32 x1, s32 z1, s32 h, s32 material)
{
    if (solid_count >= MAX_SOLIDS) { world_overflow++; return; }
    Solid *s = &solids[solid_count++];
    s->x0 = x0; s->z0 = z0; s->x1 = x1; s->z1 = z1; s->h = h; s->material = material;
}

static void add_feature(s32 type, s32 axis, s32 x0, s32 z0, s32 x1, s32 z1, s32 h0, s32 h1)
{
    if (feature_count >= MAX_FEATURES) { world_overflow++; return; }
    Feature *f = &features[feature_count++];
    f->type = type; f->axis = axis;
    f->x0 = x0; f->z0 = z0; f->x1 = x1; f->z1 = z1; f->h0 = h0; f->h1 = h1;
}

// What stands on each lot: a plaza of trees, a tower with low wings, a
// quarter of four buildings, nine low ones in rows, or three buildings and
// a little square. Parcels are split by alleys so no wall gets wider than
// a lot quarter (long faces sort badly against things in front of them).
enum { LOT_PLAZA, LOT_TOWER, LOT_QUAD, LOT_ROWS, LOT_MIXED };
static const u8 lot_kinds[16] = {
    LOT_PLAZA, LOT_PLAZA, LOT_TOWER, LOT_TOWER, LOT_TOWER, LOT_QUAD, LOT_QUAD, LOT_QUAD,
    LOT_QUAD, LOT_ROWS, LOT_ROWS, LOT_ROWS, LOT_ROWS, LOT_MIXED, LOT_MIXED, LOT_MIXED,
};
#define KIND_PARK 255

static void tree(s32 x, s32 z, u32 r)
{
    s32 w = 20 + (r & 3), h = 70 + (r >> 2) % 21;
    add_solid(x - w, z - w, x + w, z + w, h, M_TREE);
}

// Trees in a plaza, in sixteenths of the lot.
static const u8 plaza_trees[5][2] = { { 3, 4 }, { 11, 3 }, { 7, 8 }, { 3, 12 }, { 12, 12 } };

static void build_lot(s32 kind, u32 r, s32 x0, s32 z0)
{
    const s32 half = (LOT - ALLEY) / 2, third = (LOT - 2 * ALLEY) / 3;
    s32 x1 = x0 + LOT, z1 = z0 + LOT;
    switch (kind) {
    case LOT_PLAZA:
        for (s32 i = 0; i < 5; i++) {
            u32 t = hash(r + i);
            tree(x0 + plaza_trees[i][0] * (LOT / 16) + (t & 63) - 32,
                 z0 + plaza_trees[i][1] * (LOT / 16) + ((t >> 6) & 63) - 32, t >> 12);
        }
        break;
    case LOT_TOWER: {
        // A tall tower in the middle, low wings either side, trees in front.
        const s32 m = (LOT - 320) / 2;
        s32 mat = M_BLD0 + (r >> 8) % 6, wing = M_BLD0 + (r >> 20) % 6;
        s32 hw = 60 + (r >> 24) % 40;
        add_solid(x0 + m, z0 + m, x1 - m, z1 - m, MAX_BUILDING_H - 200 + (r >> 12) % 201, mat);
        if (r & 1) {
            add_solid(x0, z0 + m, x0 + m - ALLEY, z1 - m, hw, wing);
            add_solid(x1 - m + ALLEY, z0 + m, x1, z1 - m, hw, wing);
            tree(x0 + m / 2, z0 + m / 2, r >> 3);
            tree(x1 - m / 2, z1 - m / 2, r >> 5);
        } else {
            add_solid(x0 + m, z0, x1 - m, z0 + m - ALLEY, hw, wing);
            add_solid(x0 + m, z1 - m + ALLEY, x1 - m, z1, hw, wing);
            tree(x1 - m / 2, z0 + m / 2, r >> 3);
            tree(x0 + m / 2, z1 - m / 2, r >> 5);
        }
        break;
    }
    case LOT_ROWS:
        for (s32 i = 0; i < 9; i++) {
            u32 t = hash(r + i);
            s32 px = x0 + (i % 3) * (third + ALLEY), pz = z0 + (i / 3) * (third + ALLEY);
            add_solid(px, pz, px + third, pz + third, 80 + t % 161, M_BLD0 + (t >> 9) % 6);
        }
        break;
    default: {   // LOT_QUAD, LOT_MIXED
        s32 open = kind == LOT_MIXED ? (s32)(r >> 28) & 3 : -1;
        for (s32 i = 0; i < 4; i++) {
            u32 t = hash(r + i);
            s32 px = x0 + (i & 1) * (half + ALLEY), pz = z0 + (i >> 1) * (half + ALLEY);
            if (i == open) {
                tree(px + half / 3, pz + half / 3, t);
                tree(px + half * 2 / 3, pz + half * 2 / 3, t >> 7);
                continue;
            }
            add_solid(px, pz, px + half, pz + half, 140 + t % 281, M_BLD0 + (t >> 9) % 6);
        }
        break;
    }
    }
}

static void build_city(void)
{
    for (s32 bz = 0; bz < BLOCKS; bz++)
        for (s32 bx = 0; bx < BLOCKS; bx++) {
            block_first[bz][bx] = solid_count;
            block_count[bz][bx] = 0;
            s32 cx = bx * BLOCK + BLOCK / 2, cz = bz * BLOCK + BLOCK / 2;
            if (park_contains(cx, cz, 0)) {
                block_kind[bz][bx] = KIND_PARK;
                continue;
            }
            u32 r = hash(bx * 131 + bz * 7919 + 17);
            s32 kind = lot_kinds[r & 15];
            block_kind[bz][bx] = kind;
            build_lot(kind, r, bx * BLOCK + ROAD_HALF + WALK, bz * BLOCK + ROAD_HALF + WALK);
            block_count[bz][bx] = solid_count - block_first[bz][bx];
        }
}

static void build_stunts(void)
{
    // Stunt Park circuit: north up lane A through the loop, round the banked
    // curve, then south down lane B over the canal jump. The lead-ins run
    // straight off the ends of the bank.
    add_feature(F_BANK, 0, BANK_X, BANK_Z, BANK_RIN, BANK_ROUT, 0, BANK_H);
    add_feature(F_BANKLEAD, 0, BANK_X - BANK_ROUT, BANK_Z - 340, BANK_X - BANK_RIN, BANK_Z, BANK_H, 0);   // lane A, rail on the west
    add_feature(F_BANKLEAD, 0, BANK_X + BANK_RIN, BANK_Z - 300, BANK_X + BANK_ROUT, BANK_Z, 0, BANK_H);   // lane B, rail on the east
    add_feature(F_RAMP, 0, LANE_B_X0, CANAL_Z1 + 20, LANE_B_X1, CANAL_Z1 + 200, 70, 0);   // kicker, rises toward the canal
    add_feature(F_WATER, 0, PX(700), CANAL_Z0, PARK_X1, CANAL_Z1, 0, 0);
    add_feature(F_RAMP, 0, LANE_B_X0, LANDING_Z0, LANE_B_X1, CANAL_Z0, 0, 60);   // landing ramp
    add_feature(F_RAMP, 1, PX(750), START_Z + 53, PX(930), START_Z + 213, 0, 40);   // practice kicker, heading +x

    // Kickers scattered along city streets for free-roam stunts, one per
    // street segment at most, in the middle of it. Each sits in the
    // carriageway that drives up it (traffic keeps right).
    for (u32 i = 0; i < KICKERS; i++) {
        u32 r = hash(i * 977 + 5);
        s32 line = (1 + r % (BLOCKS - 1)) * BLOCK;
        s32 b = (r >> 8) % BLOCKS;
        s32 a0 = b * BLOCK + (BLOCK - 170) / 2, a1 = a0 + 170;
        s32 up = (r >> 16) & 1;               // rises toward +z (or +x)
        s32 axis = !(i & 1), taken = 0;
        // Heading +z the right-hand side is +x; heading +x it is -z.
        s32 c0 = up != axis ? line + LANE - 44 : line - LANE - 44, c1 = c0 + 88;
        for (s32 j = 0; j < feature_count; j++) {
            const Feature *f = &features[j];
            s32 across = f->axis ? f->z0 + 44 : f->x0 + 44;
            if (f->type == F_RAMP && f->axis == axis && across - line < ROAD_HALF &&
                line - across < ROAD_HALF && (f->axis ? f->x0 : f->z0) == a0)
                taken = 1;
        }
        if (taken) continue;
        if (axis == 0) {
            if (park_contains(line, (a0 + a1) / 2, 300)) continue;
            add_feature(F_RAMP, 0, c0, a0, c1, a1, up ? 0 : 48, up ? 48 : 0);
        } else {
            if (park_contains((a0 + a1) / 2, line, 300)) continue;
            add_feature(F_RAMP, 1, a0, c0, a1, c1, up ? 0 : 48, up ? 48 : 0);
        }
    }
}

static void feature_bounds(const Feature *f, s32 *x0, s32 *z0, s32 *x1, s32 *z1)
{
    if (f->type == F_BANK) {
        *x0 = f->x0 - f->z1; *x1 = f->x0 + f->z1;
        *z0 = f->z0;         *z1 = f->z0 + f->z1;
    } else {
        *x0 = f->x0; *x1 = f->x1; *z0 = f->z0; *z1 = f->z1;
    }
}

// Two passes over the features: count how many touch each block, lay the
// lists out one after another, then fill them in (in feature order).
static void index_features(void)
{
    const s32 margin = 80;      // covers the car's and the soft body's reach near rails
    for (s32 pass = 0; pass < 2; pass++) {
        if (pass) {
            s32 refs = 0;
            for (s32 bz = 0; bz < BLOCKS; bz++)
                for (s32 bx = 0; bx < BLOCKS; bx++) {
                    cell_first[bz][bx] = refs;
                    refs += cell_count[bz][bx];
                    cell_count[bz][bx] = 0;
                }
        }
        for (s32 i = 0; i < feature_count; i++) {
            s32 x0, z0, x1, z1;
            feature_bounds(&features[i], &x0, &z0, &x1, &z1);
            s32 bx0 = (x0 - margin) / BLOCK, bx1 = (x1 + margin) / BLOCK;
            s32 bz0 = (z0 - margin) / BLOCK, bz1 = (z1 + margin) / BLOCK;
            if (bx0 < 0) bx0 = 0;
            if (bz0 < 0) bz0 = 0;
            if (bx1 > BLOCKS - 1) bx1 = BLOCKS - 1;
            if (bz1 > BLOCKS - 1) bz1 = BLOCKS - 1;
            for (s32 bz = bz0; bz <= bz1; bz++)
                for (s32 bx = bx0; bx <= bx1; bx++) {
                    if (!pass) { cell_count[bz][bx]++; continue; }
                    s32 at = cell_first[bz][bx] + cell_count[bz][bx];
                    if (at < MAX_FEAT_REFS) { feat_refs[at] = i; cell_count[bz][bx]++; }
                    else world_overflow++;
                }
        }
    }
}

// Features near (x, z): sets *first to the start of their list in feat_refs.
static s32 features_at(s32 x, s32 z, const u8 **first)
{
    if (x < 0 || z < 0 || x >= WORLD || z >= WORLD) return 0;
    s32 bx = x / BLOCK, bz = z / BLOCK;
    *first = &feat_refs[cell_first[bz][bx]];
    return cell_count[bz][bx];
}

static void build_feature_faces(void);

void world_init(void)
{
    build_city();
    build_stunts();
    index_features();
    build_feature_faces();
}

// ---------------------------------------------------------------- physics queries

// Smoothstep of t in Q8: 0 -> 0, 256 -> 256, with zero slope at both ends.
static s32 smooth(s32 t)
{
    if (t <= 0) return 0;
    if (t >= 256) return 256;
    return (t * t * (768 - 2 * t)) >> 16;
}

static s32 lead_height(const Feature *f, s32 x, s32 z)
{
    s32 across = f->h0 + ((f->h1 - f->h0) * (x - f->x0)) / (f->x1 - f->x0);
    return across * smooth(((z - f->z0) << 8) / (f->z1 - f->z0));
}

s32 world_height(s32 x, s32 z)
{
    if (g_track) return track_height(x, z);
    s32 h = 0;
    const u8 *list;
    s32 n = features_at(x, z, &list);
    for (s32 k = 0; k < n; k++) {
        const Feature *f = &features[list[k]];
        if (f->type == F_BANK) {
            s32 dx = x - f->x0, dz = z - f->z0;
            if (dz <= 0 || dx > f->z1 || dx < -f->z1 || dz > f->z1) continue;
            s32 r = isqrt(dx * dx + dz * dz);
            if (r <= f->x1 || r >= f->z1) continue;
            s32 b = (f->h1 * (r - f->x1) << 8) / (f->z1 - f->x1);
            if (b > h) h = b;
            continue;
        }
        if (x < f->x0 || x >= f->x1 || z < f->z0 || z >= f->z1) continue;
        if (f->type == F_WATER) return -400 << 8;
        if (f->type == F_BANKLEAD) {
            s32 b = lead_height(f, x, z);
            if (b > h) h = b;
            continue;
        }
        s32 along = f->axis ? x - f->x0 : z - f->z0;
        s32 len   = f->axis ? f->x1 - f->x0 : f->z1 - f->z0;
        s32 r = (f->h0 << 8) + (((f->h1 - f->h0) << 8) * along) / len;
        if (r > h) h = r;
    }
    return h;
}

// The ground around (x, z), out to r, as one plane: h0 + gx * dx + gz * dz
// (Q8, with gx and gz in Q8 rise per unit). Returns 0 where it isn't one
// (a ramp, a bank or water nearby), and the caller asks point by point.
s32 world_ground_plane(s32 x, s32 z, s32 r, s32 *h0, s32 *gx, s32 *gz)
{
    if (g_track) return track_ground_plane(x, z, r, h0, gx, gz);
    *h0 = 0; *gx = 0; *gz = 0;
    if (x - r < 0 || z - r < 0 || x + r >= WORLD || z + r >= WORLD) return 0;
    s32 bx0 = (x - r) / BLOCK, bx1 = (x + r) / BLOCK, bz0 = (z - r) / BLOCK, bz1 = (z + r) / BLOCK;
    for (s32 bz = bz0; bz <= bz1; bz++)
        for (s32 bx = bx0; bx <= bx1; bx++) {
            const u8 *list = &feat_refs[cell_first[bz][bx]];
            for (s32 k = 0, n = cell_count[bz][bx]; k < n; k++) {
                const Feature *f = &features[list[k]];
                s32 x0 = f->x0, z0 = f->z0, x1 = f->x1, z1 = f->z1;
                if (f->type == F_BANK) { x0 = f->x0 - f->z1; x1 = f->x0 + f->z1; z1 = f->z0 + f->z1; }
                if (x + r > x0 && x - r < x1 && z + r > z0 && z - r < z1) return 0;
            }
        }
    return 1;
}

s32 world_in_water(s32 x, s32 z)
{
    if (g_track) return 0;
    const u8 *list;
    s32 n = features_at(x, z, &list);
    for (s32 k = 0; k < n; k++) {
        const Feature *f = &features[list[k]];
        if (f->type == F_WATER && x >= f->x0 && x < f->x1 && z >= f->z0 && z < f->z1)
            return 1;
    }
    return 0;
}

s32 world_surface(s32 x, s32 z)
{
    if (g_track) return track_surface(x, z);
    if (x < 0 || z < 0 || x >= WORLD || z >= WORLD) return SURF_ROAD;
    s32 bx = x / BLOCK, bz = z / BLOCK, lx = x - bx * BLOCK, lz = z - bz * BLOCK;
    s32 kind = block_kind[bz][bx];
    if (kind == KIND_PARK || lx < ROAD_HALF || lx >= BLOCK - ROAD_HALF ||
        lz < ROAD_HALF || lz >= BLOCK - ROAD_HALF)
        return SURF_ROAD;
    if (kind == LOT_PLAZA && lx >= ROAD_HALF + WALK && lx < BLOCK - ROAD_HALF - WALK &&
        lz >= ROAD_HALF + WALK && lz < BLOCK - ROAD_HALF - WALK)
        return SURF_GRASS;
    return SURF_SIDEWALK;
}

static void deeper(s32 pen, s32 nx, s32 nz, s32 *best, s32 *bx, s32 *bz)
{
    if (pen > *best) { *best = pen; *bx = nx; *bz = nz; }
}

s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{
    if (g_track) return track_collide(x, z, radius, nx, nz);
    s32 best = 0;
    *nx = 0; *nz = 0;

    // City limits.
    deeper(radius - x, 16384, 0, &best, nx, nz);
    deeper(x + radius - WORLD, -16384, 0, &best, nx, nz);
    deeper(radius - z, 0, 16384, &best, nx, nz);
    deeper(z + radius - WORLD, 0, -16384, &best, nx, nz);

    // Buildings in the block under the car (they sit well inside their blocks).
    s32 bx = x / BLOCK, bz = z / BLOCK;
    if (bx >= 0 && bx < BLOCKS && bz >= 0 && bz < BLOCKS) {
        s32 first = block_first[bz][bx], end = first + block_count[bz][bx];
        for (s32 i = first; i < end; i++) {
            const Solid *s = &solids[i];
            s32 cx = x < s->x0 ? s->x0 : x > s->x1 ? s->x1 : x;
            s32 cz = z < s->z0 ? s->z0 : z > s->z1 ? s->z1 : z;
            s32 dx = x - cx, dz = z - cz;
            s32 d2 = dx * dx + dz * dz;
            if (d2 >= radius * radius) continue;
            if (d2 == 0) {
                // Centre inside the box: push out the shortest way.
                s32 l = x - s->x0, r = s->x1 - x, b = z - s->z0, t = s->z1 - z;
                s32 m = l < r ? l : r, n = b < t ? b : t;
                if (m < n) deeper(m + radius, l < r ? -16384 : 16384, 0, &best, nx, nz);
                else       deeper(n + radius, 0, b < t ? -16384 : 16384, &best, nx, nz);
            } else {
                s32 d = isqrt(d2);
                deeper(radius - d, (dx << 14) / d, (dz << 14) / d, &best, nx, nz);
            }
        }
    }

    // Rails along the high edge of the bank and its lead-ins, from either side.
    const u8 *list;
    s32 n = features_at(x, z, &list);
    for (s32 k = 0; k < n; k++) {
        const Feature *f = &features[list[k]];
        if (f->type == F_BANKLEAD) {
            if (z < f->z0 || z > f->z1) continue;
            s32 rail = f->h0 > f->h1 ? f->x0 : f->x1;
            s32 inward = f->h0 > f->h1 ? 16384 : -16384;   // toward the low side
            s32 d = x - rail;
            if ((d > 0) == (inward > 0)) deeper(radius - (d < 0 ? -d : d), inward, 0, &best, nx, nz);
            else                          deeper(radius - (d < 0 ? -d : d), -inward, 0, &best, nx, nz);
            continue;
        }
        if (f->type != F_BANK) continue;
        s32 dx = x - f->x0, dz = z - f->z0;
        if (dz <= 0) continue;
        s32 r = isqrt(dx * dx + dz * dz);
        if (r == 0) continue;
        s32 ux = (dx << 14) / r, uz = (dz << 14) / r;
        if (r < f->z1) deeper(r + radius - f->z1, -ux, -uz, &best, nx, nz);
        else           deeper(f->z1 + radius - r, ux, uz, &best, nx, nz);
    }
    return best;
}

void loop_point(s32 theta, s32 *x, s32 *y, s32 *z)
{
    const Loop *l = &the_loop;
    *x = (l->x << 8) + (l->shift * theta) / 4;      // shift * 256 * theta / 1024
    *z = (l->z << 8) + ((l->radius * isin(theta)) >> 6);
    *y = (l->radius << 8) - ((l->radius * icos(theta)) >> 6);
}

// ---------------------------------------------------------------- drawing

#define visible r_visible

static inline void ground_rect(s32 x0, s32 z0, s32 x1, s32 z1, u8 color)
{
    Vec3 q[4] = { { x0, 0, z1 }, { x1, 0, z1 }, { x1, 0, z0 }, { x0, 0, z0 } };
    r_ground(q, 4, color);
}

// ---------------------------------------------------------------- feature geometry

// The stunt features never move, so their faces are built once and only
// culled and queued each frame.
typedef struct {
    s16 x[4], y[4], z[4];
    s16 cx, cz, r;          // bounding circle on the ground
    u8 color, flags;
} WorldFace;

#define MAX_WORLD_FACES 704
static WorldFace world_faces[MAX_WORLD_FACES] EWRAM_BSS;
static s32 world_face_count;
static u16 feat_face_first[MAX_FEATURES + 1] EWRAM_BSS;   // the loop goes last
static u8  feat_face_count[MAX_FEATURES + 1] EWRAM_BSS;

static void add_face(const Vec3 *q, s32 n, u8 color, u32 flags)
{
    if (world_face_count >= MAX_WORLD_FACES) { world_overflow++; return; }
    WorldFace *w = &world_faces[world_face_count++];
    s32 cx = 0, cz = 0, r = 0;
    for (s32 i = 0; i < 4; i++) {
        w->x[i] = q[i].x; w->y[i] = q[i].y; w->z[i] = q[i].z;
        cx += q[i].x; cz += q[i].z;
    }
    cx /= 4; cz /= 4;
    for (s32 i = 0; i < 4; i++) {
        s32 dx = q[i].x - cx, dz = q[i].z - cz;
        s32 d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        if (d > r) r = d;
    }
    w->cx = cx; w->cz = cz; w->r = r;
    w->color = color; w->flags = flags;
    (void)n;
}

static void draw_ramp(const Feature *f)
{
    s32 x0 = f->x0, x1 = f->x1, z0 = f->z0, z1 = f->z1;
    // Corner heights: h00 at (x0, z0), h10 at (x1, z0), h01 at (x0, z1), h11 at (x1, z1).
    s32 h00 = f->h0, h10, h01, h11 = f->h1;
    if (f->axis) { h10 = f->h1; h01 = f->h0; }
    else         { h10 = f->h0; h01 = f->h1; }

    u32 fl = RF_SURFACE;
    Vec3 top[4] = { { x0, h01, z1 }, { x1, h11, z1 }, { x1, h10, z0 }, { x0, h00, z0 } };
    add_face(top, 4, COLOR(M_RAMP, 0), fl);
    if (h00 | h10) {
        Vec3 q[4] = { { x0, h00, z0 }, { x1, h10, z0 }, { x1, 0, z0 }, { x0, 0, z0 } };
        add_face(q, 4, COLOR(M_STUNT_RED, 1), fl);
    }
    if (h01 | h11) {
        Vec3 q[4] = { { x1, h11, z1 }, { x0, h01, z1 }, { x0, 0, z1 }, { x1, 0, z1 } };
        add_face(q, 4, COLOR(M_STUNT_RED, 1), fl);
    }
    if (h00 | h01) {
        Vec3 q[4] = { { x0, h01, z1 }, { x0, h00, z0 }, { x0, 0, z0 }, { x0, 0, z1 } };
        add_face(q, 4, COLOR(M_RAMP, 2), fl);
    }
    if (h10 | h11) {
        Vec3 q[4] = { { x1, h10, z0 }, { x1, h11, z1 }, { x1, 0, z1 }, { x1, 0, z0 } };
        add_face(q, 4, COLOR(M_RAMP, 3), fl);
    }
}

static void draw_rail(s32 ax, s32 ah, s32 az, s32 bx, s32 bh, s32 bz, s32 stripe)
{
    Vec3 r[4] = { { ax, ah + RAIL_H, az }, { bx, bh + RAIL_H, bz }, { bx, bh, bz }, { ax, ah, az } };
    add_face(r, 4, COLOR(stripe ? M_STUNT_RED : M_STUNT_WHITE, 0), RF_TWO_SIDED);
}

static void draw_bank(const Feature *f)
{
    const s32 segs = 16;
    s32 cx = f->x0, cz = f->z0, rin = f->x1, rout = f->z1;
    for (s32 i = 0; i < segs; i++) {
        s32 a = (i * 512) / segs, b = ((i + 1) * 512) / segs;
        s32 ca = icos(a), sa = isin(a), cb = icos(b), sb = isin(b);
        s32 ha = f->h1, hb = f->h1;
        s32 iax = cx + ((rin * ca) >> 14), iaz = cz + ((rin * sa) >> 14);
        s32 ibx = cx + ((rin * cb) >> 14), ibz = cz + ((rin * sb) >> 14);
        s32 oax = cx + ((rout * ca) >> 14), oaz = cz + ((rout * sa) >> 14);
        s32 obx = cx + ((rout * cb) >> 14), obz = cz + ((rout * sb) >> 14);

        Vec3 top[4] = { { ibx, 0, ibz }, { obx, hb, obz }, { oax, ha, oaz }, { iax, 0, iaz } };
        add_face(top, 4, COLOR(M_SIDEWALK, (i & 1) ? 1 : 0), RF_SURFACE);
        Vec3 wall[4] = { { oax, ha, oaz }, { obx, hb, obz }, { obx, 0, obz }, { oax, 0, oaz } };
        add_face(wall, 4, COLOR(M_SIDEWALK, 2), RF_SURFACE);
        draw_rail(oax, ha, oaz, obx, hb, obz, i & 1);
    }
}

static void draw_bank_lead(const Feature *f)
{
    const s32 segs = 4;
    s32 high_left = f->h0 > f->h1;
    s32 rail_x = high_left ? f->x0 : f->x1;
    s32 hmax = high_left ? f->h0 : f->h1;
    s32 pz = f->z0, ph = 0;
    for (s32 i = 1; i <= segs; i++) {
        s32 z = f->z0 + ((f->z1 - f->z0) * i) / segs;
        s32 h = (hmax * smooth((i << 8) / segs)) >> 8;
        s32 hl0 = high_left ? ph : 0, hr0 = high_left ? 0 : ph;
        s32 hl1 = high_left ? h : 0,  hr1 = high_left ? 0 : h;
        Vec3 top[4] = { { f->x0, hl1, z }, { f->x1, hr1, z }, { f->x1, hr0, pz }, { f->x0, hl0, pz } };
        add_face(top, 4, COLOR(M_SIDEWALK, (i & 1) ? 1 : 0), RF_SURFACE);
        if (high_left) {
            Vec3 w[4] = { { f->x0, h, z }, { f->x0, ph, pz }, { f->x0, 0, pz }, { f->x0, 0, z } };
            add_face(w, 4, COLOR(M_SIDEWALK, 2), RF_SURFACE);
        } else {
            Vec3 w[4] = { { f->x1, ph, pz }, { f->x1, h, z }, { f->x1, 0, z }, { f->x1, 0, pz } };
            add_face(w, 4, COLOR(M_SIDEWALK, 3), RF_SURFACE);
        }
        draw_rail(rail_x, ph, pz, rail_x, h, z, i & 1);
        pz = z;
        ph = h;
    }
}

static void draw_loop(void)
{
    const Loop *l = &the_loop;
    const s32 segs = 24, half = l->width / 2;
    s32 px, py, pz;
    loop_point(0, &px, &py, &pz);
    for (s32 i = 1; i <= segs; i++) {
        s32 x, y, z;
        loop_point((i * 1024) / segs, &x, &y, &z);
        Vec3 q[4] = {
            { (px >> 8) - half, py >> 8, pz >> 8 }, { (px >> 8) + half, py >> 8, pz >> 8 },
            { (x >> 8) + half, y >> 8, z >> 8 },    { (x >> 8) - half, y >> 8, z >> 8 },
        };
        add_face(q, 4, COLOR((i & 1) ? M_STUNT_RED : M_STUNT_WHITE, (i & 2) ? 1 : 0),
               RF_TWO_SIDED | RF_SURFACE);
        px = x; py = y; pz = z;
    }
}

// A ground rectangle, if any of it can be on screen.
static inline void ground_rect_vis(s32 x0, s32 z0, s32 x1, s32 z1, u8 color)
{
    if (visible((x0 + x1) >> 1, (z0 + z1) >> 1, (x1 - x0 + z1 - z0) >> 1))
        ground_rect(x0, z0, x1, z1, color);
}

static void draw_park_ground(void)
{
    const Loop *l = &the_loop;
    const s32 half = l->width / 2, line = 4;
    const s32 lead_z = BANK_Z - 340;      // where lane A's lead-in starts
    u8 white = COLOR(M_STUNT_WHITE, 1);
    // Curbs around the park, in pieces short enough to haze and cull smoothly.
    u8 curb = COLOR(M_SIDEWALK, 0);
    const s32 wx = (PARK_X1 - PARK_X0) / 2, wz = (PARK_Z1 - PARK_Z0) / 3;
    for (s32 i = 0; i < 2; i++) {
        ground_rect_vis(PARK_X0 + i * wx, PARK_Z0, PARK_X0 + (i + 1) * wx, PARK_Z0 + WALK, curb);
        ground_rect_vis(PARK_X0 + i * wx, PARK_Z1 - WALK, PARK_X0 + (i + 1) * wx, PARK_Z1, curb);
    }
    for (s32 i = 0; i < 3; i++) {
        ground_rect_vis(PARK_X0, PARK_Z0 + i * wz, PARK_X0 + WALK, PARK_Z0 + (i + 1) * wz, curb);
        ground_rect_vis(PARK_X1 - WALK, PARK_Z0 + i * wz, PARK_X1, PARK_Z0 + (i + 1) * wz, curb);
    }
    // Guide lines into and out of the loop.
    ground_rect_vis(l->x - half - line, START_Z - 24, l->x - half + line, l->z, white);
    ground_rect_vis(l->x + half - line, START_Z - 24, l->x + half + line, l->z, white);
    ground_rect_vis(l->x + l->shift - half - line, l->z, l->x + l->shift - half + line, lead_z, white);
    ground_rect_vis(l->x + l->shift + half - line, l->z, l->x + l->shift + half + line, lead_z, white);
    // Start line.
    if (visible(l->x, START_Z, half))
        for (s32 i = 0; i < 6; i++)
            ground_rect(l->x - half + i * 14, START_Z - 7, l->x - half + i * 14 + 14, START_Z + 7,
                        (i & 1) ? COLOR(M_SHADOW, 0) : white);
}

static void build_feature_faces(void)
{
    for (s32 i = 0; i <= feature_count; i++) {
        feat_face_first[i] = world_face_count;
        if (i == feature_count) draw_loop();
        else if (features[i].type == F_RAMP) draw_ramp(&features[i]);
        else if (features[i].type == F_BANK) draw_bank(&features[i]);
        else if (features[i].type == F_BANKLEAD) draw_bank_lead(&features[i]);
        feat_face_count[i] = world_face_count - feat_face_first[i];
    }
}

IWRAM_CODE static void queue_feature(s32 i)
{
    const WorldFace *w = &world_faces[feat_face_first[i]];
    for (s32 k = feat_face_count[i]; k > 0; k--, w++) {
        if (!visible(w->cx, w->cz, w->r)) continue;
        Vec3 q[4];
        for (s32 j = 0; j < 4; j++) { q[j].x = w->x[j]; q[j].y = w->y[j]; q[j].z = w->z[j]; }
        r_face(q, 4, w->color, w->flags);
    }
}

// The sidewalk and the paved lot as one quad (the buildings stand on it),
// with a lawn on a plaza.
IWRAM_CODE static void draw_block_ground(s32 bx, s32 bz)
{
    s32 x0 = bx * BLOCK + ROAD_HALF, x1 = (bx + 1) * BLOCK - ROAD_HALF;
    s32 z0 = bz * BLOCK + ROAD_HALF, z1 = (bz + 1) * BLOCK - ROAD_HALF;
    ground_rect(x0, z0, x1, z1, COLOR(M_SIDEWALK, 0));
    if (block_kind[bz][bx] == LOT_PLAZA)
        ground_rect(x0 + WALK, z0 + WALK, x1 - WALK, z1 - WALK, COLOR(M_GRASS, 0));
}

// Road markings near the car on the street along x = line (along z when
// `across` is set): a double centre line and dashed lines between the
// lanes, painted only between the crossings.
#define PAINT_REACH 520
#define PAINT_EDGE  (ROAD_HALF + 24)          // paint stops this far from a crossing's centre
#define DASH        64
#define DASH_PERIOD 192
#define DASH_SKIP   ((BLOCK - 2 * PAINT_EDGE - 3 * DASH_PERIOD - DASH) / 2)   // centres 4 dashes per segment

static void paint_rect(s32 a0, s32 c0, s32 a1, s32 c1, s32 across, u8 color)
{
    if (across) ground_rect_vis(a0, c0, a1, c1, color);
    else        ground_rect_vis(c0, a0, c1, a1, color);
}

// Thumb code in ROM: only a few calls a frame, and IWRAM is kept for the stack.
static __attribute__((noinline)) void paint_street(s32 line, s32 focus, s32 across)
{
    s32 b0 = (focus - PAINT_REACH) / BLOCK, b1 = (focus + PAINT_REACH) / BLOCK;
    if (b0 < 0) b0 = 0;
    if (b1 > BLOCKS - 1) b1 = BLOCKS - 1;
    u8 yellow = COLOR(M_LINE, 0), white = COLOR(M_STUNT_WHITE, 1);
    for (s32 b = b0; b <= b1; b++) {
        s32 s0 = b * BLOCK + PAINT_EDGE, s1 = (b + 1) * BLOCK - PAINT_EDGE;
        if (s1 < focus - PAINT_REACH || s0 > focus + PAINT_REACH) continue;
        s32 mid = (s0 + s1) >> 1;
        if (across ? park_contains(mid, line, ROAD_HALF) : park_contains(line, mid, ROAD_HALF)) continue;
        paint_rect(s0, line - 8, s1, line - 3, across, yellow);
        paint_rect(s0, line + 3, s1, line + 8, across, yellow);
        for (s32 a = s0 + DASH_SKIP; a + DASH <= s1; a += DASH_PERIOD) {
            if (a + DASH < focus - PAINT_REACH || a > focus + PAINT_REACH) continue;
            paint_rect(a, line - LANE - 3, a + DASH, line - LANE + 3, across, white);
            paint_rect(a, line + LANE - 3, a + DASH, line + LANE + 3, across, white);
        }
    }
}

// Blocks drawn this frame, as (bx, bz) pairs. The scan never reaches
// further than RANGE_MAX blocks from the focus.
#define RANGE_MAX ((R_FAR_MAX + 400) / BLOCK + 1)
#define MAX_SHOWN ((2 * RANGE_MAX + 1) * (2 * RANGE_MAX + 1))
static u8 shown[MAX_SHOWN][2] EWRAM_BSS;

IWRAM_CODE void world_draw(s32 focus_x, s32 focus_z)
{
    if (g_track) { track_draw(focus_x, focus_z, g_frames); return; }
    s32 shown_count = 0;

    // Only blocks within drawing range of the camera (which is near the focus).
    s32 range = (r_far + 400) / BLOCK + 1;
    if (range > RANGE_MAX) range = RANGE_MAX;
    s32 bx0 = focus_x / BLOCK - range, bx1 = focus_x / BLOCK + range;
    s32 bz0 = focus_z / BLOCK - range, bz1 = focus_z / BLOCK + range;
    if (bx0 < 0) bx0 = 0;
    if (bz0 < 0) bz0 = 0;
    if (bx1 > BLOCKS - 1) bx1 = BLOCKS - 1;
    if (bz1 > BLOCKS - 1) bz1 = BLOCKS - 1;

    // Ground layer, drawn in order: sidewalks, grass, water, markings.
    for (s32 bz = bz0; bz <= bz1; bz++)
        for (s32 bx = bx0; bx <= bx1; bx++) {
            s32 cx = bx * BLOCK + BLOCK / 2, cz = bz * BLOCK + BLOCK / 2;
            if (!visible(cx, cz, BLOCK * 3 / 4)) continue;
            shown[shown_count][0] = bx;
            shown[shown_count++][1] = bz;
            if (block_kind[bz][bx] != KIND_PARK) draw_block_ground(bx, bz);
        }

    if (visible((PARK_X0 + PARK_X1) / 2, (PARK_Z0 + PARK_Z1) / 2,
                (PARK_X1 - PARK_X0 + PARK_Z1 - PARK_Z0) / 2)) {
        draw_park_ground();
        for (s32 i = 0; i < feature_count; i++) {
            const Feature *f = &features[i];
            if (f->type != F_WATER) continue;
            // In two halves across, so neither is wider than the haze bands.
            s32 mx = (f->x0 + f->x1) >> 1;
            ground_rect_vis(f->x0, f->z0, mx, f->z1, COLOR(M_WATER, 0));
            ground_rect_vis(mx, f->z0, f->x1, f->z1, COLOR(M_WATER, 0));
        }
    }

    for (s32 k = 1; k < BLOCKS; k++) {
        s32 line = k * BLOCK;
        if (focus_x - line < PAINT_REACH + ROAD_HALF && line - focus_x < PAINT_REACH + ROAD_HALF)
            paint_street(line, focus_z, 0);
        if (focus_z - line < PAINT_REACH + ROAD_HALF && line - focus_z < PAINT_REACH + ROAD_HALF)
            paint_street(line, focus_x, 1);
    }

    // Stunt features touching the blocks on show, each once.
    u32 seen[(MAX_FEATURES + 31) / 32];
    for (u32 i = 0; i < sizeof(seen) / 4; i++) seen[i] = 0;
    for (s32 b = 0; b < shown_count; b++) {
        s32 bx = shown[b][0], bz = shown[b][1];
        const u8 *list = &feat_refs[cell_first[bz][bx]];
        for (s32 k = 0, n = cell_count[bz][bx]; k < n; k++) {
            s32 i = list[k];
            if (seen[i >> 5] & (1u << (i & 31))) continue;
            seen[i >> 5] |= 1u << (i & 31);
            const Feature *f = &features[i];
            if (f->type == F_BANK ? visible(f->x0, f->z0 + f->z1 / 2, f->z1)
                                  : visible((f->x0 + f->x1) >> 1, (f->z0 + f->z1) >> 1,
                                            (f->x1 - f->x0 + f->z1 - f->z0) >> 1))
                queue_feature(i);
        }
    }
    if (visible(the_loop.x, the_loop.z, 300))
        queue_feature(feature_count);

    // Buildings.
    for (s32 b = 0; b < shown_count; b++) {
        s32 bx = shown[b][0], bz = shown[b][1];
        s32 first = block_first[bz][bx], end = first + block_count[bz][bx];
        for (s32 i = first; i < end; i++) {
            const Solid *s = &solids[i];
            if (!visible((s->x0 + s->x1) / 2, (s->z0 + s->z1) / 2,
                         (s->x1 - s->x0 + s->z1 - s->z0) / 2)) continue;
            r_box(s->x0, 0, s->z0, s->x1, s->h, s->z1, s->material);
        }
    }
}
