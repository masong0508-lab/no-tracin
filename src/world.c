// Freedom City: building layout, the Stunt Park, street ramps, the height
// field the physics drives on, wall collisions and drawing.
#include "world.h"
#include "track.h"

extern volatile u32 g_frames;

#define MAX_SOLIDS   1100
#define MAX_FEATURES 48

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

static Solid   solids[MAX_SOLIDS] EWRAM_BSS;
static s32     solid_count;
static Feature features[MAX_FEATURES];
static s32     feature_count;
static u8      block_kind[BLOCKS][BLOCKS];
// Features touching each block, so height and wall queries only look at
// the few that can matter.
#define MAX_FEAT_REFS 320
static u8      feat_refs[MAX_FEAT_REFS];
static u16     cell_first[BLOCKS][BLOCKS];
static u8      cell_count[BLOCKS][BLOCKS];
static u16     block_first[BLOCKS][BLOCKS];
static u8      block_count[BLOCKS][BLOCKS];

const Loop the_loop = { 820, 1550, 120, 100, 84 };

#define KIND_PARK 255
#define BANK_H    100
#define RAIL_H    14

// ---------------------------------------------------------------- layout

static u32 hash(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352d;
    x ^= x >> 15; x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static s32 in_park(s32 x, s32 z, s32 margin)
{
    return x > PARK_X0 - margin && x < PARK_X1 + margin &&
           z > PARK_Z0 - margin && z < PARK_Z1 + margin;
}

static void add_solid(s32 x0, s32 z0, s32 x1, s32 z1, s32 h, s32 material)
{
    if (solid_count >= MAX_SOLIDS) return;
    Solid *s = &solids[solid_count++];
    s->x0 = x0; s->z0 = z0; s->x1 = x1; s->z1 = z1; s->h = h; s->material = material;
}

static void add_feature(s32 type, s32 axis, s32 x0, s32 z0, s32 x1, s32 z1, s32 h0, s32 h1)
{
    if (feature_count >= MAX_FEATURES) return;
    Feature *f = &features[feature_count++];
    f->type = type; f->axis = axis;
    f->x0 = x0; f->z0 = z0; f->x1 = x1; f->z1 = z1; f->h0 = h0; f->h1 = h1;
}

static void build_city(void)
{
    for (s32 bz = 0; bz < BLOCKS; bz++)
        for (s32 bx = 0; bx < BLOCKS; bx++) {
            block_first[bz][bx] = solid_count;
            block_count[bz][bx] = 0;
            s32 cx = bx * BLOCK + BLOCK / 2, cz = bz * BLOCK + BLOCK / 2;
            if (in_park(cx, cz, 0)) {
                block_kind[bz][bx] = KIND_PARK;
                continue;
            }
            u32 r = hash(bx * 131 + bz * 7919 + 17);
            s32 kind = r % 6;
            block_kind[bz][bx] = kind;
            s32 x0 = bx * BLOCK + ROAD_HALF + 24, x1 = (bx + 1) * BLOCK - ROAD_HALF - 24;
            s32 z0 = bz * BLOCK + ROAD_HALF + 24, z1 = (bz + 1) * BLOCK - ROAD_HALF - 24;
            s32 mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
            s32 mat = M_BLD0 + (r >> 8) % 6;
            s32 h = 120 + (r >> 12) % 280;
            switch (kind) {
            case 0:   // park with trees
                add_solid(mx - 90, mz - 90, mx - 50, mz - 50, 70, M_TREE);
                add_solid(mx + 40, mz + 30, mx + 84, mz + 74, 90, M_TREE);
                break;
            case 1: case 2:   // one big building
                add_solid(x0, z0, x1, z1, h, mat);
                break;
            case 3:   // two buildings
                add_solid(x0, z0, mx - 12, z1, h, mat);
                add_solid(mx + 12, z0, x1, z1, h / 2 + 60, M_BLD0 + (r >> 20) % 6);
                break;
            default:  // four small buildings
                add_solid(x0, z0, mx - 12, mz - 12, h / 2 + 40, mat);
                add_solid(mx + 12, z0, x1, mz - 12, h, M_BLD0 + (r >> 18) % 6);
                add_solid(x0, mz + 12, mx - 12, z1, h / 3 + 60, M_BLD0 + (r >> 22) % 6);
                add_solid(mx + 12, mz + 12, x1, z1, h / 2 + 80, mat);
                break;
            }
            block_count[bz][bx] = solid_count - block_first[bz][bx];
        }
}

static void build_stunts(void)
{
    // Stunt Park circuit: north up lane A through the loop, round the banked
    // curve, then south down lane B over the canal jump.
    add_feature(F_BANK, 0, 1250, 2040, 250, 430, 0, BANK_H);
    add_feature(F_BANKLEAD, 0, 820, 1700, 1000, 2040, BANK_H, 0);    // lane A, rail on the west
    add_feature(F_BANKLEAD, 0, 1500, 1740, 1680, 2040, 0, BANK_H);   // lane B, rail on the east
    add_feature(F_RAMP, 0, 1490, 1520, 1690, 1700, 70, 0);   // kicker, rises toward the canal
    add_feature(F_WATER, 0, 1100, 1170, PARK_X1, 1500, 0, 0);
    add_feature(F_RAMP, 0, 1490, 920, 1690, 1170, 0, 60);    // landing ramp
    add_feature(F_RAMP, 1, 1150, 700, 1330, 860, 0, 40);     // practice kicker, heading +x

    // Kickers scattered along city streets for free-roam stunts.
    for (u32 i = 0; i < 24; i++) {
        u32 r = hash(i * 977 + 5);
        s32 line = (1 + r % (BLOCKS - 1)) * BLOCK;
        s32 b = (r >> 8) % BLOCKS;
        s32 a0 = b * BLOCK + 170, a1 = a0 + 170;
        s32 up = (r >> 16) & 1;
        s32 taken = 0;
        for (s32 j = 0; j < feature_count; j++)
            if (features[j].type == F_RAMP && features[j].axis == !(i & 1) &&
                (features[j].axis ? features[j].z0 + 44 : features[j].x0 + 44) == line &&
                (features[j].axis ? features[j].x0 : features[j].z0) == a0)
                taken = 1;
        if (taken) continue;
        if (i & 1) {
            if (in_park(line, (a0 + a1) / 2, 300)) continue;
            add_feature(F_RAMP, 0, line - 44, a0, line + 44, a1, up ? 0 : 48, up ? 48 : 0);
        } else {
            if (in_park((a0 + a1) / 2, line, 300)) continue;
            add_feature(F_RAMP, 1, a0, line - 44, a1, line + 44, up ? 0 : 48, up ? 48 : 0);
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

static void index_features(void)
{
    s32 refs = 0;
    const s32 margin = 48;      // covers the car's collision radius near rails
    for (s32 bz = 0; bz < BLOCKS; bz++)
        for (s32 bx = 0; bx < BLOCKS; bx++) {
            cell_first[bz][bx] = refs;
            for (s32 i = 0; i < feature_count; i++) {
                s32 x0, z0, x1, z1;
                feature_bounds(&features[i], &x0, &z0, &x1, &z1);
                if (x1 + margin < bx * BLOCK || x0 - margin >= (bx + 1) * BLOCK ||
                    z1 + margin < bz * BLOCK || z0 - margin >= (bz + 1) * BLOCK)
                    continue;
                if (refs < MAX_FEAT_REFS) feat_refs[refs++] = i;
            }
            cell_count[bz][bx] = refs - cell_first[bz][bx];
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
    if (kind == 0 && lx >= ROAD_HALF + 24 && lx < BLOCK - ROAD_HALF - 24 &&
        lz >= ROAD_HALF + 24 && lz < BLOCK - ROAD_HALF - 24)
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

#define MAX_WORLD_FACES 320
static WorldFace world_faces[MAX_WORLD_FACES] EWRAM_BSS;
static s32 world_face_count;
static u16 feat_face_first[MAX_FEATURES + 1];   // the loop goes last
static u8  feat_face_count[MAX_FEATURES + 1];

static void add_face(const Vec3 *q, s32 n, u8 color, u32 flags)
{
    if (world_face_count >= MAX_WORLD_FACES) return;
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

static void draw_park_ground(void)
{
    const Loop *l = &the_loop;
    const s32 half = l->width / 2, line = 4;
    u8 white = COLOR(M_STUNT_WHITE, 1);
    // Curbs around the park.
    u8 curb = COLOR(M_SIDEWALK, 0);
    ground_rect(PARK_X0, PARK_Z0, PARK_X1, PARK_Z0 + 24, curb);
    ground_rect(PARK_X0, PARK_Z1 - 24, PARK_X1, PARK_Z1, curb);
    ground_rect(PARK_X0, PARK_Z0, PARK_X0 + 24, PARK_Z1, curb);
    ground_rect(PARK_X1 - 24, PARK_Z0, PARK_X1, PARK_Z1, curb);
    // Guide lines into and out of the loop.
    ground_rect(l->x - half - line, PARK_Z0 + 40, l->x - half + line, l->z, white);
    ground_rect(l->x + half - line, PARK_Z0 + 40, l->x + half + line, l->z, white);
    ground_rect(l->x + l->shift - half - line, l->z, l->x + l->shift - half + line, 1700, white);
    ground_rect(l->x + l->shift + half - line, l->z, l->x + l->shift + half + line, 1700, white);
    // Start line.
    for (s32 i = 0; i < 6; i++)
        ground_rect(l->x - half + i * 14, 640, l->x - half + i * 14 + 14, 654,
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

// Sidewalk around whatever stands in the block. Close up, only the strips
// that can be seen are drawn, not the ground hidden under the buildings.
IWRAM_CODE static void draw_block_ground(s32 bx, s32 bz, s32 near)
{
    s32 kind = block_kind[bz][bx];
    s32 x0 = bx * BLOCK + ROAD_HALF, x1 = (bx + 1) * BLOCK - ROAD_HALF;
    s32 z0 = bz * BLOCK + ROAD_HALF, z1 = (bz + 1) * BLOCK - ROAD_HALF;
    s32 ix0 = x0 + 24, ix1 = x1 - 24, iz0 = z0 + 24, iz1 = z1 - 24;
    u8 walk = COLOR(M_SIDEWALK, 0);
    if (!near) {
        ground_rect(x0, z0, x1, z1, walk);
    } else {
        ground_rect(x0, z0, x1, iz0, walk);
        ground_rect(x0, iz1, x1, z1, walk);
        ground_rect(x0, iz0, ix0, iz1, walk);
        ground_rect(ix1, iz0, x1, iz1, walk);
        s32 mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
        if (kind == 3 || kind >= 4)
            ground_rect(mx - 12, iz0, mx + 12, iz1, walk);
        if (kind >= 4) {
            ground_rect(ix0, mz - 12, mx - 12, mz + 12, walk);
            ground_rect(mx + 12, mz - 12, ix1, mz + 12, walk);
        }
    }
    if (kind == 0) ground_rect(ix0, iz0, ix1, iz1, COLOR(M_GRASS, 0));
}

IWRAM_CODE void world_draw(s32 focus_x, s32 focus_z)
{
    if (g_track) { track_draw(focus_x, focus_z, g_frames); return; }
    u8 shown[BLOCKS * BLOCKS];
    s32 shown_count = 0;

    // Only blocks within drawing range of the camera (which is near the focus).
    const s32 range = (r_far + 400) / BLOCK + 1;
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
            shown[shown_count++] = bz * BLOCKS + bx;
            if (block_kind[bz][bx] == KIND_PARK) continue;
            s32 side;
            draw_block_ground(bx, bz, r_depth(cx, cz, &side) < 900);
        }

    if (visible((PARK_X0 + PARK_X1) / 2, (PARK_Z0 + PARK_Z1) / 2, 1100)) {
        draw_park_ground();
        for (s32 i = 0; i < feature_count; i++) {
            const Feature *f = &features[i];
            if (f->type == F_WATER && visible((f->x0 + f->x1) / 2, (f->z0 + f->z1) / 2, 500))
                ground_rect(f->x0, f->z0, f->x1, f->z1, COLOR(M_WATER, 0));
        }
    }

    const s32 reach = 520, dash = 40, gap = 88, half_w = 3;
    for (s32 k = 0; k <= BLOCKS; k++) {
        s32 line = k * BLOCK;
        if (focus_x - line < reach && line - focus_x < reach) {
            s32 start = ((focus_z - reach) / (dash + gap)) * (dash + gap);
            for (s32 z = start; z < focus_z + reach; z += dash + gap) {
                if (z < 0 || z > WORLD || ((z + dash / 2) % BLOCK) < ROAD_HALF + dash) continue;
                if (in_park(line, z, ROAD_HALF) || !visible(line, z + dash / 2, dash)) continue;
                ground_rect(line - half_w, z, line + half_w, z + dash, COLOR(M_LINE, 0));
            }
        }
        if (focus_z - line < reach && line - focus_z < reach) {
            s32 start = ((focus_x - reach) / (dash + gap)) * (dash + gap);
            for (s32 x = start; x < focus_x + reach; x += dash + gap) {
                if (x < 0 || x > WORLD || ((x + dash / 2) % BLOCK) < ROAD_HALF + dash) continue;
                if (in_park(x, line, ROAD_HALF) || !visible(x + dash / 2, line, dash)) continue;
                ground_rect(x, line - half_w, x + dash, line + half_w, COLOR(M_LINE, 0));
            }
        }
    }

    // Stunt features.
    for (s32 i = 0; i < feature_count; i++) {
        const Feature *f = &features[i];
        if (f->type == F_RAMP && visible((f->x0 + f->x1) / 2, (f->z0 + f->z1) / 2, 200))
            queue_feature(i);
        else if (f->type == F_BANK && visible(f->x0, f->z0 + f->z1 / 2, f->z1))
            queue_feature(i);
        else if (f->type == F_BANKLEAD && visible((f->x0 + f->x1) / 2, (f->z0 + f->z1) / 2, 250))
            queue_feature(i);
    }
    if (visible(the_loop.x, the_loop.z, 300))
        queue_feature(feature_count);

    // Buildings.
    for (s32 b = 0; b < shown_count; b++) {
        s32 bz = shown[b] / BLOCKS, bx = shown[b] % BLOCKS;
        s32 first = block_first[bz][bx], end = first + block_count[bz][bx];
        for (s32 i = first; i < end; i++) {
            const Solid *s = &solids[i];
            if (!visible((s->x0 + s->x1) / 2, (s->z0 + s->z1) / 2,
                         (s->x1 - s->x0 + s->z1 - s->z0) / 2)) continue;
            r_box(s->x0, 0, s->z0, s->x1, s->h, s->z1, s->material);
        }
    }
}
