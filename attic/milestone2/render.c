// Flat-shaded polygon renderer for Mode 4 (240x160, 256 colors).
// Pipeline: world -> camera space -> near-plane clip -> project ->
// back-face cull -> painter's sort -> convex scanline fill.
#include "gba.h"
#include "sintab.h"

#define FOCAL   150     // projection scale in pixels
#define NEAR    24      // near clip plane in world units
#define FAR     1700    // faces beyond this depth are skipped
#define MAX_FACES 360
#define SUB     12      // fixed-point bits for edge walking

#define REG_DMA3SAD (*(volatile u32 *)0x040000D4)
#define REG_DMA3DAD (*(volatile u32 *)0x040000D8)
#define REG_DMA3CNT (*(volatile u32 *)0x040000DC)
#define DMA_ON_FILL32 (0x80000000 | 0x04000000 | 0x01000000)

typedef struct { s16 x, y, z; } SVec3;

typedef struct {
    SVec3 v[4];
    s32   key;
    u8    n, color, flags;
} Face;

static Face  faces[MAX_FACES];
static s16   order[MAX_FACES];
static s32   face_count;

static volatile u16 *target;
static s32 cam_x, cam_y, cam_z;
static s32 cy_cos, cy_sin, cp_cos, cp_sin;
static s32 horizon;

static u16 recip[FAR * 2] EWRAM_BSS;   // (FOCAL << 12) / z
static u32 inv_dy[1024] EWRAM_BSS;     // 65536 / dy, for edge slopes


s32 isin(s32 a) { return sintab[a & 1023]; }
s32 icos(s32 a) { return sintab[(a + 256) & 1023]; }

// atan(i / 256) in 1024-unit angles, i = 0..256.
static u8 atan_tab[257];

s32 iatan2(s32 y, s32 x)
{
    if (x == 0 && y == 0) return 0;
    s32 ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a;
    if (ax >= ay) a = atan_tab[(ay << 8) / ax];
    else          a = 256 - atan_tab[(ax << 8) / ay];
    if (x < 0) a = 512 - a;
    return y < 0 ? -a : a;
}

s32 isqrt(u32 v)
{
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void r_init(void)
{
    for (s32 z = 1; z < FAR * 2; z++)
        recip[z] = (FOCAL << 12) / z;
    for (s32 d = 1; d < 1024; d++)
        inv_dy[d] = 65536 / d;
    // atan via binary search on tan, using the Q14 sine table.
    for (s32 i = 0; i <= 256; i++) {
        s32 a = 0;
        while (a < 128 && (isin(a + 1) << 8) <= i * icos(a + 1)) a++;
        atan_tab[i] = a;
    }
}

// (dx << SUB) / dy without a division for common heights.
static inline __attribute__((always_inline)) s32 edge_slope(s32 dx, s32 dy)
{
    if (dy < 1024)
        return (s32)(((long long)dx * inv_dy[dy]) >> (16 - SUB));
    return (dx << SUB) / dy;
}

void r_init_palette(void)
{
    static const u16 base[M_COUNT] = {
        [M_SKY]      = RGB15(14, 20, 30),
        [M_ASPHALT]  = RGB15(12, 12, 13),
        [M_LINE]     = RGB15(30, 28, 12),
        [M_SIDEWALK] = RGB15(24, 22, 18),
        [M_GRASS]    = RGB15(8, 22, 6),
        [M_TREE]     = RGB15(4, 16, 5),
        [M_BLD0]     = RGB15(28, 24, 18),
        [M_BLD1]     = RGB15(24, 10, 8),
        [M_BLD2]     = RGB15(14, 18, 26),
        [M_BLD3]     = RGB15(26, 26, 26),
        [M_BLD4]     = RGB15(28, 20, 6),
        [M_BLD5]     = RGB15(16, 22, 18),
        [M_CAR]      = RGB15(31, 6, 4),
        [M_CAR_TRIM] = RGB15(20, 3, 3),
        [M_GLASS]    = RGB15(10, 20, 28),
        [M_TIRE]     = RGB15(5, 5, 6),
        [M_RAMP]     = RGB15(31, 22, 4),
        [M_HUD]      = RGB15(31, 30, 8),
        [M_HUD_BG]   = RGB15(4, 4, 7),
        [M_WATER]    = RGB15(6, 14, 26),
        [M_STUNT_RED]   = RGB15(30, 4, 4),
        [M_STUNT_WHITE] = RGB15(30, 30, 30),
        [M_FIRE]     = RGB15(31, 18, 2),
        [M_SMOKE]    = RGB15(10, 10, 10),
        [M_SHADOW]   = RGB15(6, 6, 7),
        [M_SPLASH]   = RGB15(24, 28, 31),
    };
    static const s32 scale[4] = { 32, 26, 20, 14 };
    for (s32 m = 0; m < M_COUNT; m++) {
        s32 r = base[m] & 31, g = (base[m] >> 5) & 31, b = (base[m] >> 10) & 31;
        for (s32 s = 0; s < 4; s++)
            PAL_BG[m * 4 + s] = RGB15(r * scale[s] / 32, g * scale[s] / 32, b * scale[s] / 32);
    }
}

static inline __attribute__((always_inline)) void to_camera(s32 x, s32 y, s32 z, Vec3 *out)
{
    s32 dx = x - cam_x, dy = y - cam_y, dz = z - cam_z;
    s32 x1 = (dx * cy_cos - dz * cy_sin) >> 14;
    s32 z1 = (dx * cy_sin + dz * cy_cos) >> 14;
    out->x = x1;
    out->y = (dy * cp_cos + z1 * cp_sin) >> 14;
    out->z = (z1 * cp_cos - dy * cp_sin) >> 14;
}

IWRAM_CODE s32 r_depth(s32 x, s32 z, s32 *side)
{
    s32 dx = x - cam_x, dz = z - cam_z;
    *side = (dx * cy_cos - dz * cy_sin) >> 14;
    return (dx * cy_sin + dz * cy_cos) >> 14;
}

static void fill32(volatile void *dst, u32 value, u32 words)
{
    static volatile u32 src;
    src = value;
    REG_DMA3SAD = (u32)&src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = DMA_ON_FILL32 | words;
}

void r_begin(volatile u16 *page, const Camera *cam)
{
    target = page;
    cam_x = cam->x; cam_y = cam->y; cam_z = cam->z;
    cy_cos = icos(cam->yaw);   cy_sin = isin(cam->yaw);
    cp_cos = icos(cam->pitch); cp_sin = isin(cam->pitch);
    face_count = 0;

    // Horizon row; upside down (cos < 0) the sky is below it.
    s32 num = FOCAL * cp_sin, den = cp_cos;
    if (den > -64 && den < 64)
        horizon = ((num > 0) == (den >= 0)) ? -1 : SCREEN_H + 1;
    else
        horizon = SCREEN_H / 2 - num / den;
    if (horizon < 0) horizon = 0;
    if (horizon > SCREEN_H) horizon = SCREEN_H;
    u32 sky = COLOR(M_SKY, 0) * 0x01010101u;
    u32 ground = COLOR(M_ASPHALT, 0) * 0x01010101u;
    u32 top = cp_cos >= 0 ? sky : ground, bottom = cp_cos >= 0 ? ground : sky;
    if (horizon > 0)
        fill32(page, top, horizon * SCREEN_W / 4);
    if (horizon < SCREEN_H)
        fill32(page + horizon * SCREEN_W / 2, bottom, (SCREEN_H - horizon) * SCREEN_W / 4);
}

// Fill pixels [x0, x1) of one row, two pixels at a time (Mode 4 has no byte writes).
static inline __attribute__((always_inline)) void span(s32 y, s32 x0, s32 x1, u32 pair)
{
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    s32 i = x0 >> 1, end = x1 >> 1;
    if (i >= end) return;
    volatile u16 *row = target + y * (SCREEN_W / 2);
    if (i & 1) row[i++] = (u16)pair;
    volatile u32 *w = (volatile u32 *)(row + i);
    u32 word = pair | (pair << 16);
    for (; i + 1 < end; i += 2) *w++ = word;
    if (i < end) row[i] = (u16)pair;
}

IWRAM_CODE static void span_rows(s32 y0, s32 y1, s32 x0, s32 x1, u8 color)
{
    u32 pair = color | (color << 8);
    for (s32 y = y0; y < y1; y++) span(y, x0, x1, pair);
}

// Rasterize a convex screen-space polygon by walking its left and right
// vertex chains down from the top vertex.
IWRAM_CODE static void fill_poly(const s32 *sx, const s32 *sy, s32 n, u8 color)
{
    s32 top = 0;
    for (s32 i = 1; i < n; i++)
        if (sy[i] < sy[top]) top = i;

    u32 pair = color | (color << 8);
    s32 y = sy[top];
    s32 li = top, ri = top, lcount = n, rcount = n;
    s32 lx = 0, ls = 0, ly1 = y, rx = 0, rs = 0, ry1 = y;

    for (;;) {
        while (y >= ly1) {
            if (--lcount < 0) return;
            s32 nx = li == 0 ? n - 1 : li - 1;
            ly1 = sy[nx];
            if (ly1 > y) {
                ls = edge_slope(sx[nx] - sx[li], ly1 - sy[li]);
                lx = (sx[li] << SUB) + ls * (y - sy[li]);
            }
            li = nx;
        }
        while (y >= ry1) {
            if (--rcount < 0) return;
            s32 nx = ri + 1 == n ? 0 : ri + 1;
            ry1 = sy[nx];
            if (ry1 > y) {
                rs = edge_slope(sx[nx] - sx[ri], ry1 - sy[ri]);
                rx = (sx[ri] << SUB) + rs * (y - sy[ri]);
            }
            ri = nx;
        }
        s32 yend = ly1 < ry1 ? ly1 : ry1;
        if (y < 0) {
            // Skip rows above the screen in one step.
            s32 skip = (yend < 0 ? yend : 0) - y;
            lx += ls * skip;
            rx += rs * skip;
            y += skip;
        }
        if (yend > SCREEN_H) yend = SCREEN_H;
        for (; y < yend; y++, lx += ls, rx += rs) {
            s32 a = lx >> SUB, b = rx >> SUB;
            if (a > b) { s32 t = a; a = b; b = t; }
            span(y, a, b + 1, pair);
        }
        if (y >= SCREEN_H) return;
    }
}

// Clip against the near plane, project, cull and fill one camera-space polygon.
IWRAM_CODE static void draw_cam_poly(const Vec3 *v, s32 n, u8 color, s32 cull)
{
    Vec3 clip[8];
    s32 m = 0;
    for (s32 i = 0; i < n; i++) {
        const Vec3 *a = &v[i], *b = &v[i + 1 == n ? 0 : i + 1];
        s32 ain = a->z >= NEAR, bin = b->z >= NEAR;
        if (ain) clip[m++] = *a;
        if (ain != bin) {
            s32 t = ((NEAR - a->z) << 12) / (b->z - a->z);
            clip[m].x = a->x + (((b->x - a->x) * t) >> 12);
            clip[m].y = a->y + (((b->y - a->y) * t) >> 12);
            clip[m].z = NEAR;
            m++;
        }
    }
    if (m < 3) return;

    s32 sx[8], sy[8];
    s32 left = 1, right = 1, top = 1, bottom = 1;
    for (s32 i = 0; i < m; i++) {
        s32 z = clip[i].z;
        if (z < FAR * 2) {
            s32 r = recip[z];
            sx[i] = SCREEN_W / 2 + ((clip[i].x * r) >> 12);
            sy[i] = SCREEN_H / 2 - ((clip[i].y * r) >> 12);
        } else {
            sx[i] = SCREEN_W / 2 + (clip[i].x * FOCAL) / z;
            sy[i] = SCREEN_H / 2 - (clip[i].y * FOCAL) / z;
        }
        left   &= sx[i] < 0;
        right  &= sx[i] >= SCREEN_W;
        top    &= sy[i] < 0;
        bottom &= sy[i] >= SCREEN_H;
    }
    if (left | right | top | bottom) return;

    if (cull) {
        s32 area = 0;
        for (s32 i = 0; i < m; i++) {
            s32 j = (i + 1 == m) ? 0 : i + 1;
            area += sx[i] * sy[j] - sx[j] * sy[i];
        }
        if (area <= 0) return;
    }
    fill_poly(sx, sy, m, color);
}

IWRAM_CODE void r_ground(const Vec3 *v, s32 n, u8 color)
{
    Vec3 cv[4];
    s32 near = 0, far = 0;
    for (s32 i = 0; i < n; i++) {
        to_camera(v[i].x, v[i].y, v[i].z, &cv[i]);
        near += cv[i].z < NEAR;
        far  += cv[i].z > FAR;
    }
    if (near == n || far == n) return;
    draw_cam_poly(cv, n, color, 0);
}

IWRAM_CODE static void queue_cam_face(const Vec3 *cv, s32 n, u8 color, u32 flags)
{
    if (face_count >= MAX_FACES) return;
    s32 sum = 0, near = 0, far = -32768;
    for (s32 i = 0; i < n; i++) {
        sum += cv[i].z;
        near += cv[i].z < NEAR;
        if (cv[i].z > far) far = cv[i].z;
    }
    if (near == n) return;
    Face *f = &faces[face_count];
    for (s32 i = 0; i < n; i++) {
        f->v[i].x = cv[i].x; f->v[i].y = cv[i].y; f->v[i].z = cv[i].z;
    }
    f->n = n;
    f->color = color;
    f->flags = flags;
    // Drivable surfaces sort by their farthest point so cars on them draw on top.
    f->key = (flags & RF_SURFACE) ? far + 8 : (n == 4 ? sum >> 2 : sum / n);
    order[face_count] = face_count;
    face_count++;
}

IWRAM_CODE void r_face(const Vec3 *v, s32 n, u8 color, u32 flags)
{
    Vec3 cv[4];
    for (s32 i = 0; i < n; i++) to_camera(v[i].x, v[i].y, v[i].z, &cv[i]);
    queue_cam_face(cv, n, color, flags);
}

// Corner index bits: bit0 = x1, bit1 = y1, bit2 = z1. Each face is listed
// clockwise as seen from outside the box.
static const u8 box_faces[5][4] = {
    { 2, 3, 1, 0 },   // front  (z0)
    { 7, 6, 4, 5 },   // back   (z1)
    { 6, 2, 0, 4 },   // left   (x0)
    { 3, 7, 5, 1 },   // right  (x1)
    { 6, 7, 3, 2 },   // top    (y1)
};
static const u8 box_shade[5] = { 1, 2, 2, 3, 0 };

IWRAM_CODE static void queue_box(const Vec3 *cv, s32 material, u32 mask)
{
    s32 behind = 0, far = 0;
    for (s32 i = 0; i < 8; i++) {
        behind += cv[i].z < NEAR;
        far    += cv[i].z > FAR;
    }
    if (behind == 8 || far == 8) return;
    for (s32 f = 0; f < 5; f++) {
        if (!(mask & (1 << f))) continue;
        Vec3 q[4];
        for (s32 k = 0; k < 4; k++) q[k] = cv[box_faces[f][k]];
        queue_cam_face(q, 4, COLOR(material, box_shade[f]), 0);
    }
}

IWRAM_CODE void r_box(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material)
{
    Vec3 cv[8];
    for (s32 i = 0; i < 8; i++)
        to_camera(i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0, &cv[i]);
    // Only faces whose outside the camera is on can be visible.
    u32 mask = (cam_z < z0) | (cam_z > z1) << 1 | (cam_x < x0) << 2 |
               (cam_x > x1) << 3 | (cam_y > y1) << 4;
    queue_box(cv, material, mask);
}

IWRAM_CODE void r_box_rot(s32 cx, s32 cy, s32 cz, s32 yaw,
               s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material)
{
    s32 c = icos(yaw), s = isin(yaw);
    Vec3 cv[8];
    for (s32 i = 0; i < 8; i++) {
        s32 lx = i & 1 ? x1 : x0, ly = i & 2 ? y1 : y0, lz = i & 4 ? z1 : z0;
        s32 wx = cx + ((lx * c + lz * s) >> 14);
        s32 wz = cz + ((lz * c - lx * s) >> 14);
        to_camera(wx, cy + ly, wz, &cv[i]);
    }
    queue_box(cv, material, 31);
}

IWRAM_CODE void r_box_mat(s32 px, s32 py, s32 pz, const s32 *m,
                          s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material)
{
    Vec3 cv[8];
    for (s32 i = 0; i < 8; i++) {
        s32 lx = i & 1 ? x1 : x0, ly = i & 2 ? y1 : y0, lz = i & 4 ? z1 : z0;
        to_camera(px + ((m[0] * lx + m[3] * ly + m[6] * lz) >> 14),
                  py + ((m[1] * lx + m[4] * ly + m[7] * lz) >> 14),
                  pz + ((m[2] * lx + m[5] * ly + m[8] * lz) >> 14), &cv[i]);
    }
    queue_box(cv, material, 31);
}

IWRAM_CODE void r_flush(void)
{
    // Shell sort, farthest first.
    s32 n = face_count;
    for (s32 gap = n / 2; gap > 0; gap /= 2)
        for (s32 i = gap; i < n; i++) {
            s16 t = order[i];
            s32 key = faces[t].key, j = i;
            while (j >= gap && faces[order[j - gap]].key < key) {
                order[j] = order[j - gap];
                j -= gap;
            }
            order[j] = t;
        }
    for (s32 i = 0; i < n; i++) {
        const Face *f = &faces[order[i]];
        Vec3 v[4];
        for (s32 k = 0; k < f->n; k++) {
            v[k].x = f->v[k].x; v[k].y = f->v[k].y; v[k].z = f->v[k].z;
        }
        draw_cam_poly(v, f->n, f->color, !(f->flags & RF_TWO_SIDED));
    }
}

void r_rect(s32 x, s32 y, s32 w, s32 h, u8 color)
{
    s32 y0 = y < 0 ? 0 : y, y1 = y + h > SCREEN_H ? SCREEN_H : y + h;
    span_rows(y0, y1, x, x + w, color);
}
