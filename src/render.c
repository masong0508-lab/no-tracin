// Flat-shaded polygon renderer for Mode 4 (240x160, 256 colors).
// Pipeline: world -> camera space -> near-plane clip -> project -> cull ->
// bucket sort (at queue time) -> convex scanline fill, far to near.
// The backdrop is a sky gradient with a distant skyline and hazy ground.
#include "gba.h"
#include "sintab.h"

#define NEAR      24       // near clip plane in world units
#define MAX_FACES 320
#define MAXV      6        // a quad clipped by the near plane has at most 5
#define BUCKETS   512      // depth buckets, 8 units each
#define SUB       12       // fixed-point bits for edge walking
#define RECIPS    (R_FAR_MAX * 2)
#define LIMIT     16000    // projected coordinates are clamped to this

#define PANO_W    944      // skyline panorama: about FOCAL * 2 * pi pixels round
#define PANO_H    32

#define REG_DMA3SAD (*(volatile u32 *)0x040000D4)
#define REG_DMA3DAD (*(volatile u32 *)0x040000D8)
#define REG_DMA3CNT (*(volatile u32 *)0x040000DC)
#define DMA_FILL32  (0x80000000 | 0x04000000 | 0x01000000)
#define DMA_COPY16  0x80000000

typedef struct {
    s16 x[MAXV], y[MAXV];
    s16 key, next;
    u8  n, color;
} Face;

static Face faces[MAX_FACES];
static s16  heads[BUCKETS];
static s32  face_count;
s32 r_far = R_FAR_MAX;

static volatile u16 *target;
static s32 cam_x, cam_y, cam_z;
static s32 cy_cos, cy_sin, cp_cos, cp_sin;

static u16 recip[RECIPS] EWRAM_BSS;           // (FOCAL << 12) / z
static u32 inv_dy[1024] EWRAM_BSS;           // 65536 / dy, for edge slopes
static u16 pano[PANO_H][PANO_W / 2] EWRAM_BSS;  // skyline, row 0 just above the horizon

extern void hspan(volatile u16 *p, s32 count, u32 value);

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

static u32 rng_state = 12345;
static s32 scene;
static u32 rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 16;
}

static s32 ground_mat = M_ASPHALT;

// Distant skyline drawn once into the panorama: city towers over low hills,
// forest hills, the bay's skyline over the water, or mountains.
static void build_panorama(void)
{
    static u8 far[PANO_W] EWRAM_BSS, near[PANO_W] EWRAM_BSS;
    rng_state = 12345 + scene * 977;
    for (s32 x = 0; x < PANO_W; x++) {
        s32 a = (x * 1024) / PANO_W;
        if (scene == SCENE_MOUNTAINS)
            far[x] = 8 + ((isin(a * 2) + isin(a * 5 + 300) / 2 + isin(a * 11 + 50) / 4 + 2 * 16384) * 3 >> 13);
        else if (scene == SCENE_FOREST)
            far[x] = 4 + ((isin(a * 3) + isin(a * 5 + 100) + 2 * 16384) * 3 >> 14);
        else
            far[x] = 3 + ((isin(a * 3) + isin(a * 7 + 100) + 2 * 16384) >> 13);
        near[x] = 0;
    }
    if (scene == SCENE_MOUNTAINS) {
        // Jagged peaks.
        for (s32 x = 0; x < PANO_W;) {
            s32 w = 20 + rnd() % 40, h = 14 + rnd() % 17, x0 = x;
            for (; x < x0 + w && x < PANO_W; x++) {
                s32 d = x - x0 < w / 2 ? x - x0 : x0 + w - x;
                s32 v = d * h * 2 / w + (rnd() & 1);
                if (far[x] < v) far[x] = v > PANO_H - 1 ? PANO_H - 1 : v;
            }
        }
        for (s32 x = 0; x < PANO_W; x++) {
            s32 a = (x * 1024) / PANO_W;
            near[x] = 3 + ((isin(a * 4 + 200) + 16384) * 7 >> 15);
        }
    } else if (scene == SCENE_FOREST) {
        // Tree tops along the near hills.
        for (s32 x = 0; x < PANO_W;) {
            s32 w = 3 + rnd() % 5, h = 3 + rnd() % 5;
            s32 a = (x * 1024) / PANO_W;
            s32 base = 2 + ((isin(a * 6) + 16384) * 3 >> 15);
            for (s32 i = 0; i < w && x < PANO_W; i++, x++)
                near[x] = base + (i == 0 || i == w - 1 ? h - 1 : h);
        }
    } else {
        for (s32 x = 0; x < PANO_W;) {
            s32 w = 6 + rnd() % 14, h = 6 + rnd() % 13;
            if (rnd() % 7 == 0) h += 8;
            for (s32 i = 0; i < w && x < PANO_W; i++, x++)
                if (far[x] < h) far[x] = h;
            x += rnd() % 10;
        }
        for (s32 x = 0; x < PANO_W;) {
            s32 w = 10 + rnd() % 22, h = 2 + rnd() % 9;
            if (scene == SCENE_BAY) h = 1 + rnd() % 3;     // low waterfront
            for (s32 i = 0; i < w && x < PANO_W; i++, x++) near[x] = h;
            s32 gap = rnd() % 6;
            for (s32 i = 0; i < gap && x < PANO_W; i++, x++) near[x] = 1;
        }
    }
    for (s32 k = 0; k < PANO_H; k++)
        for (s32 x = 0; x < PANO_W; x += 2) {
            u32 c[2];
            for (s32 j = 0; j < 2; j++) {
                c[j] = SKY_BASE + (k >> 2);
                if (k < far[x + j]) c[j] = SKYLINE_FAR;
                if (k < near[x + j]) c[j] = SKYLINE_NEAR;
            }
            pano[k][x / 2] = c[0] | (c[1] << 8);
        }
}

void r_set_scene(s32 s)
{
    if (s == scene) return;
    scene = s;
    ground_mat = s == SCENE_CITY ? M_ASPHALT : M_GRASS;
    build_panorama();
}

void r_init(void)
{
    for (s32 z = 1; z < RECIPS; z++)
        recip[z] = (FOCAL << 12) / z;
    for (s32 d = 1; d < 1024; d++)
        inv_dy[d] = 65536 / d;
    // atan via binary search on tan, using the Q14 sine table.
    for (s32 i = 0; i <= 256; i++) {
        s32 a = 0;
        while (a < 128 && (isin(a + 1) << 8) <= i * icos(a + 1)) a++;
        atan_tab[i] = a;
    }
    build_panorama();
}

// (dx << SUB) / dy without a division for common heights.
static inline __attribute__((always_inline)) s32 edge_slope(s32 dx, s32 dy)
{
    if (dy < 1024)
        return (s32)(((long long)dx * inv_dy[dy]) >> (16 - SUB));
    return (dx << SUB) / dy;
}

// Blend (r, g, b) t/32 of the way toward (hr, hg, hb).
static u16 mix(s32 r, s32 g, s32 b, s32 hr, s32 hg, s32 hb, s32 t)
{
    return RGB15(r + ((hr - r) * t) / 32, g + ((hg - g) * t) / 32, b + ((hb - b) * t) / 32);
}

// Looks: haze, sky overhead and light tint (out of 32) for each time of day.
static const u8 themes[THEME_COUNT][9] = {
    { 23, 26, 30,  8, 14, 29,  32, 32, 32 },   // day
    { 30, 19, 13, 10,  6, 18,  32, 24, 20 },   // sunset
    {  3,  4,  9,  0,  0,  4,  12, 12, 18 },   // night
    { 24,  6, 20,  4,  0, 12,  26, 14, 32 },   // synthwave
    { 23, 26, 30,  8, 14, 29,  32, 32, 32 },   // game boy (recoloured below)
};
static s32 cur_theme;

static u16 tint(u16 c, const u8 *t)
{
    return RGB15((c & 31) * t[6] / 32, ((c >> 5) & 31) * t[7] / 32, ((c >> 10) & 31) * t[8] / 32);
}

void r_init_palette(s32 theme, u16 paint)
{
    const u8 *t = themes[theme];
    const s32 HAZE_R = t[0], HAZE_G = t[1], HAZE_B = t[2];
    cur_theme = theme;
    static const u16 base[M_COUNT] = {
        [M_SKY]      = RGB15(14, 20, 30),
        [M_ASPHALT]  = RGB15(11, 11, 13),
        [M_LINE]     = RGB15(30, 28, 12),
        [M_SIDEWALK] = RGB15(23, 21, 18),
        [M_GRASS]    = RGB15(9, 21, 6),
        [M_TREE]     = RGB15(5, 15, 5),
        [M_BLD0]     = RGB15(28, 24, 18),
        [M_BLD1]     = RGB15(25, 11, 8),
        [M_BLD2]     = RGB15(14, 18, 26),
        [M_BLD3]     = RGB15(26, 26, 25),
        [M_BLD4]     = RGB15(28, 20, 7),
        [M_BLD5]     = RGB15(15, 22, 18),
        [M_CAR]      = RGB15(31, 5, 3),
        [M_CAR_TRIM] = RGB15(7, 7, 9),
        [M_GLASS]    = RGB15(9, 16, 24),
        [M_TIRE]     = RGB15(5, 5, 6),
        [M_RAMP]     = RGB15(31, 22, 4),
        [M_HUD]      = RGB15(31, 30, 8),
        [M_HUD_BG]   = RGB15(4, 4, 7),
        [M_WATER]    = RGB15(5, 13, 25),
        [M_STUNT_RED]   = RGB15(30, 4, 4),
        [M_STUNT_WHITE] = RGB15(30, 30, 30),
        [M_FIRE]     = RGB15(31, 18, 2),
        [M_SMOKE]    = RGB15(10, 10, 10),
        [M_SHADOW]   = RGB15(5, 5, 6),
        [M_SPLASH]   = RGB15(24, 28, 31),
    };
    // Colours that change with the scene (circuits reuse city materials).
    static const u16 forest[M_COUNT] = {
        [M_GRASS] = RGB15(8, 20, 5), [M_TREE] = RGB15(4, 13, 4), [M_BLD1] = RGB15(15, 9, 5),
        [M_ASPHALT] = RGB15(12, 12, 13),
    };
    static const u16 bay[M_COUNT] = {
        [M_GRASS] = RGB15(10, 20, 8), [M_ASPHALT] = RGB15(12, 12, 14), [M_WATER] = RGB15(4, 12, 24),
    };
    static const u16 mountains[M_COUNT] = {
        [M_GRASS] = RGB15(20, 18, 9), [M_TREE] = RGB15(7, 13, 5), [M_BLD5] = RGB15(18, 14, 10),
        [M_BLD1] = RGB15(14, 9, 5), [M_BLD3] = RGB15(29, 28, 25), [M_ASPHALT] = RGB15(13, 12, 12),
    };
    const u16 *over = scene == SCENE_FOREST ? forest : scene == SCENE_BAY ? bay :
                      scene == SCENE_MOUNTAINS ? mountains : 0;
    static const s32 scale[4] = { 32, 26, 20, 14 };
    for (s32 m = 0; m < M_COUNT; m++) {
        u16 bc = m == M_CAR ? paint : (over && over[m]) ? over[m] : base[m];
        if (m != M_LINE && m != M_FIRE) bc = tint(bc, t);   // road paint and fire stay bright
        s32 r = bc & 31, g = (bc >> 5) & 31, b = (bc >> 10) & 31;
        for (s32 s = 0; s < 4; s++) {
            s32 sr = r * scale[s] / 32, sg = g * scale[s] / 32, sb = b * scale[s] / 32;
            PAL_BG[m * 4 + s] = RGB15(sr, sg, sb);
            PAL_BG[FOG1_BASE + m * 4 + s] = mix(sr, sg, sb, HAZE_R, HAZE_G, HAZE_B, 13);
        }
        PAL_BG[FOG2_BASE + m] = mix(r * 23 / 32, g * 23 / 32, b * 23 / 32,
                                    HAZE_R, HAZE_G, HAZE_B, 23);
    }
    // Sky: haze at the horizon up to deep blue overhead.
    for (s32 i = 0; i < SKY_STEPS; i++)
        PAL_BG[SKY_BASE + i] = mix(HAZE_R, HAZE_G, HAZE_B, t[3], t[4], t[5], (i * 32) / (SKY_STEPS - 1));
    static const u8 skyline[4][6] = {
        { 14, 17, 23, 10, 12, 17 },     // city
        { 12, 17, 18,  4, 11,  5 },     // forest
        { 14, 17, 23, 10, 12, 17 },     // bay
        { 15, 15, 21, 15, 12,  8 },     // mountains
    };
    const u8 *sk = skyline[scene];
    PAL_BG[SKYLINE_FAR]  = mix(sk[0] * t[6] / 32, sk[1] * t[7] / 32, sk[2] * t[8] / 32, HAZE_R, HAZE_G, HAZE_B, 14);
    PAL_BG[SKYLINE_NEAR] = mix(sk[3] * t[6] / 32, sk[4] * t[7] / 32, sk[5] * t[8] / 32, HAZE_R, HAZE_G, HAZE_B, 8);
    // Menu colours, untouched by the time of day.
    static const u16 ui[8] = {
        RGB15(31, 29, 8), RGB15(31, 31, 31), RGB15(16, 17, 20), RGB15(12, 28, 31),
        RGB15(3, 3, 8), RGB15(9, 11, 22), RGB15(0, 0, 2), RGB15(31, 8, 6),
    };
    if (theme == THEME_GAMEBOY) {
        static const u16 gb[4] = { RGB15(3, 7, 3), RGB15(8, 15, 4), RGB15(17, 23, 6), RGB15(24, 28, 13) };
        for (s32 i = 0; i < 256; i++) {
            u16 c = PAL_BG[i];
            s32 l = (c & 31) * 5 + ((c >> 5) & 31) * 9 + ((c >> 10) & 31) * 2;   // 0..496
            PAL_BG[i] = gb[l * 4 / 497];
        }
    }
    for (s32 i = 0; i < 8; i++) PAL_BG[COLOR(M_HUD, 0) + i] = ui[i];
}

// Repaint just the car (for the rainbow paint job).
void r_set_paint(u16 paint)
{
    const u8 *t = themes[cur_theme];
    static const s32 scale[4] = { 32, 26, 20, 14 };
    u16 bc = tint(paint, t);
    s32 r = bc & 31, g = (bc >> 5) & 31, b = (bc >> 10) & 31;
    for (s32 s = 0; s < 4; s++) {
        s32 sr = r * scale[s] / 32, sg = g * scale[s] / 32, sb = b * scale[s] / 32;
        PAL_BG[M_CAR * 4 + s] = RGB15(sr, sg, sb);
        PAL_BG[FOG1_BASE + M_CAR * 4 + s] = mix(sr, sg, sb, t[0], t[1], t[2], 13);
    }
}

// Distance haze in three bands; colors past the near set are left alone.
static inline __attribute__((always_inline)) u32 fog(u32 c, s32 depth)
{
    if (c >= FOG1_BASE) return c;
    if (depth > R_FOG2) return FOG2_BASE + (c >> 2);
    if (depth > R_FOG1) return c + FOG1_BASE;
    return c;
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

IWRAM_CODE s32 r_visible(s32 x, s32 z, s32 radius)
{
    s32 dx = x - cam_x, dz = z - cam_z;
    s32 side = (dx * cy_cos - dz * cy_sin) >> 14;
    s32 depth = (dx * cy_sin + dz * cy_cos) >> 14;
    if (depth < -radius || depth > r_far + radius) return 0;
    if (side < 0) side = -side;
    // Half the view is 0.8 wide per unit of depth (120 / FOCAL pixels).
    return side * 5 < depth * 4 + radius * 7;
}

IWRAM_CODE s32 r_project(s32 x, s32 y, s32 z, s32 *sx, s32 *sy)
{
    Vec3 c;
    to_camera(x, y, z, &c);
    if (c.z < NEAR) return 0;
    if (c.z < RECIPS) {
        s32 r = recip[c.z];
        *sx = SCREEN_W / 2 + ((c.x * r) >> 12);
        *sy = SCREEN_H / 2 - ((c.y * r) >> 12);
    } else {
        *sx = SCREEN_W / 2 + (c.x * FOCAL) / c.z;
        *sy = SCREEN_H / 2 - (c.y * FOCAL) / c.z;
    }
    return c.z;
}

// ---------------------------------------------------------------- backdrop

static void fill32(volatile void *dst, u32 value, u32 words)
{
    static volatile u32 src;
    src = value;
    REG_DMA3SAD = (u32)&src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = DMA_FILL32 | words;
}

static void fill_rows(s32 y0, s32 y1, u32 color)
{
    if (y0 < 0) y0 = 0;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (y0 < y1)
        fill32(target + y0 * (SCREEN_W / 2), color * 0x01010101u, (y1 - y0) * (SCREEN_W / 4));
}

static void copy16(volatile u16 *dst, const u16 *src, u32 halfwords)
{
    REG_DMA3SAD = (u32)src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = DMA_COPY16 | halfwords;
}

// Screen row where flat ground at camera depth d appears.
static s32 ground_row(s32 d)
{
    if (cp_cos < 2000) return SCREEN_H;
    s32 t = (d * 16384 - cam_y * cp_sin) / cp_cos;          // distance ahead
    if (t <= 0) return SCREEN_H;
    if (t > 30000) t = 30000;
    s32 yc = (-cam_y * cp_cos + t * cp_sin) >> 14;
    return SCREEN_H / 2 - (yc * FOCAL) / d;
}

static void draw_backdrop(s32 yaw)
{
    // Horizon row; upside down (cos < 0) the sky is below it.
    s32 num = FOCAL * cp_sin, den = cp_cos, horizon;
    if (den > -64 && den < 64)
        horizon = ((num > 0) == (den >= 0)) ? -1 : SCREEN_H + 1;
    else
        horizon = SCREEN_H / 2 - num / den;
    if (horizon < -1000) horizon = -1000;
    if (horizon > 1000) horizon = 1000;
    if (cp_cos < 0) {
        fill_rows(0, horizon, COLOR(ground_mat, 0));
        fill_rows(horizon, SCREEN_H, SKY_BASE + 6);
        return;
    }
    // Sky gradient, four rows per step, above the skyline band.
    fill_rows(0, horizon - 4 * (SKY_STEPS - 1), SKY_BASE + SKY_STEPS - 1);
    for (s32 k = SKY_STEPS - 2; k >= PANO_H / 4; k--)
        fill_rows(horizon - 4 * k - 4, horizon - 4 * k, SKY_BASE + k);
    s32 ofs = (((yaw & 1023) * PANO_W) >> 10) - SCREEN_W / 2;
    if (ofs < 0) ofs += PANO_W;
    ofs >>= 1;                                        // in halfwords
    s32 first = PANO_W / 2 - ofs;
    if (first > SCREEN_W / 2) first = SCREEN_W / 2;
    for (s32 k = 0; k < PANO_H; k++) {
        s32 y = horizon - 1 - k;
        if (y < 0) break;
        if (y >= SCREEN_H) continue;
        volatile u16 *row = target + y * (SCREEN_W / 2);
        copy16(row, &pano[k][ofs], first);
        if (first < SCREEN_W / 2) copy16(row + first, &pano[k][0], SCREEN_W / 2 - first);
    }
    // Ground fades into the haze toward the horizon.
    s32 r2 = ground_row(R_FOG2), r1 = ground_row(R_FOG1);
    if (r2 < horizon) r2 = horizon;
    if (r1 < r2) r1 = r2;
    fill_rows(horizon, r2, FOG2_BASE + ground_mat);
    fill_rows(r2, r1, FOG1_BASE + COLOR(ground_mat, 0));
    fill_rows(r1, SCREEN_H, COLOR(ground_mat, 0));
}

void r_begin(volatile u16 *page, const Camera *cam)
{
    target = page;
    cam_x = cam->x; cam_y = cam->y; cam_z = cam->z;
    cy_cos = icos(cam->yaw);   cy_sin = isin(cam->yaw);
    cp_cos = icos(cam->pitch); cp_sin = isin(cam->pitch);
    face_count = 0;
    fill32(heads, 0xFFFFFFFFu, BUCKETS / 2);
    draw_backdrop(cam->yaw);
}

// ---------------------------------------------------------------- rasterizer

// Rasterize a convex screen-space polygon. Edges that run upward in vertex
// order form one side and are stepped into lbuf first; the other side's
// edges are then stepped row by row and each row filled against lbuf.
// Rows cover pixel pairs, rounded outward so neighbouring polygons never
// leave a crack.
static s16 lbuf[SCREEN_H];

IWRAM_CODE static void fill_poly(const s16 *sx, const s16 *sy, s32 n, u32 color)
{
    s32 ymin = sy[0], ymax = sy[0];
    for (s32 i = 1; i < n; i++) {
        if (sy[i] < ymin) ymin = sy[i];
        if (sy[i] > ymax) ymax = sy[i];
    }
    if (ymin >= SCREEN_H || ymax <= 0 || ymin == ymax) return;

    for (s32 i = 0, j = n - 1; i < n; j = i++) {
        // Edge from vertex j up to vertex i.
        s32 y0 = sy[i], y1 = sy[j];
        if (y0 >= y1) continue;
        s32 s = edge_slope(sx[j] - sx[i], y1 - y0);
        s32 y = y0 < 0 ? 0 : y0, yend = y1 > SCREEN_H ? SCREEN_H : y1;
        s32 x = (sx[i] << SUB) + s * (y - y0);
        s16 *l = &lbuf[y];
        for (; y < yend; y++, x += s) *l++ = x >> SUB;
    }

    u32 pair = color * 0x01010101u;
    for (s32 i = 0, j = n - 1; i < n; j = i++) {
        // Edge from vertex j down to vertex i.
        s32 y0 = sy[j], y1 = sy[i];
        if (y0 >= y1) continue;
        s32 s = edge_slope(sx[i] - sx[j], y1 - y0);
        s32 y = y0 < 0 ? 0 : y0, yend = y1 > SCREEN_H ? SCREEN_H : y1;
        s32 x = (sx[j] << SUB) + s * (y - y0);
        const s16 *l = &lbuf[y];
        volatile u16 *row = target + y * (SCREEN_W / 2);
        for (; y < yend; y++, x += s, row += SCREEN_W / 2) {
            s32 a = *l++, b = x >> SUB;
            if (a > b) { s32 t = a; a = b; b = t; }
            if (a < 0) a = 0;
            if (b >= SCREEN_W) b = SCREEN_W - 1;
            a >>= 1;
            b = (b >> 1) + 1;
            if (a < b) hspan(row + a, b - a, pair);
        }
    }
}

// Clip a camera-space polygon against the near plane, project it and
// reject it when it is off screen or (with cull) facing away.
// Returns the vertex count written to sx/sy, 0 when nothing is visible.
IWRAM_CODE static s32 project_poly(const Vec3 *v, s32 n, s16 *sx, s16 *sy, s32 cull)
{
    Vec3 clip[MAXV];
    const Vec3 *p = v;
    s32 m = n, behind = 0;
    for (s32 i = 0; i < n; i++) behind |= v[i].z < NEAR;
    if (behind) {
        m = 0;
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
        if (m < 3) return 0;
        p = clip;
    }

    u32 left = 1, right = 1, top = 1, bottom = 1;
    for (s32 i = 0; i < m; i++) {
        s32 z = p[i].z, x, y;
        if (z < RECIPS) {
            s32 r = recip[z];
            x = SCREEN_W / 2 + ((p[i].x * r) >> 12);
            y = SCREEN_H / 2 - ((p[i].y * r) >> 12);
        } else {
            x = SCREEN_W / 2 + (p[i].x * FOCAL) / z;
            y = SCREEN_H / 2 - (p[i].y * FOCAL) / z;
        }
        if (x < -LIMIT) x = -LIMIT; else if (x > LIMIT) x = LIMIT;
        if (y < -LIMIT) y = -LIMIT; else if (y > LIMIT) y = LIMIT;
        sx[i] = x;
        sy[i] = y;
        left   &= x < 0;
        right  &= x >= SCREEN_W;
        top    &= y < 0;
        bottom &= y >= SCREEN_H;
    }
    if (left | right | top | bottom) return 0;

    if (cull) {
        long long area = 0;
        for (s32 i = 0; i < m; i++) {
            s32 j = (i + 1 == m) ? 0 : i + 1;
            area += (long long)sx[i] * sy[j] - (long long)sx[j] * sy[i];
        }
        if (area <= 0) return 0;
    }
    return m;
}

// Cheap tests in camera space, before any projection work.
// The view is 120 / FOCAL = 0.8 wide and 80 / FOCAL = 0.53 tall per unit of depth.
static inline __attribute__((always_inline)) s32 in_view(const Vec3 *v, s32 n)
{
    u32 left = 1, right = 1, top = 1, bottom = 1;
    for (s32 i = 0; i < n; i++) {
        s32 x5 = v[i].x * 5, z4 = v[i].z * 4, y15 = v[i].y * 15, z8 = v[i].z * 8;
        right  &= x5 > z4;
        left   &= -x5 > z4;
        top    &= y15 > z8;
        bottom &= -y15 > z8;
    }
    return !(left | right | top | bottom);
}

IWRAM_CODE void r_ground_cam(const Vec3 *cv, s32 n, u8 color)
{
    s32 near = 0, sum = 0;
    for (s32 i = 0; i < n; i++) {
        near += cv[i].z < NEAR;
        sum += cv[i].z;
    }
    if (near == n || sum > r_far * n || !in_view(cv, n)) return;
    s16 sx[MAXV], sy[MAXV];
    s32 m = project_poly(cv, n, sx, sy, 0);
    if (m) fill_poly(sx, sy, m, fog(color, sum / n));
}

IWRAM_CODE void r_ground(const Vec3 *v, s32 n, u8 color)
{
    Vec3 cv[MAXV];
    for (s32 i = 0; i < n; i++) to_camera(v[i].x, v[i].y, v[i].z, &cv[i]);
    r_ground_cam(cv, n, color);
}

void r_xform(s32 x, s32 y, s32 z, Vec3 *out) { to_camera(x, y, z, out); }

void r_xform_dir(s32 dx, s32 dy, s32 dz, Vec3 *out)
{
    s32 x1 = (dx * cy_cos - dz * cy_sin) >> 14;
    s32 z1 = (dx * cy_sin + dz * cy_cos) >> 14;
    out->x = x1;
    out->y = (dy * cp_cos + z1 * cp_sin) >> 14;
    out->z = (z1 * cp_cos - dy * cp_sin) >> 14;
}

// A face listed clockwise from outside faces away from the eye (at the
// origin) when its first vertex is on the inner side of its plane.
static inline __attribute__((always_inline)) s32 facing_away(const Vec3 *v)
{
    s32 ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, az = v[1].z - v[0].z;
    s32 bx = v[2].x - v[0].x, by = v[2].y - v[0].y, bz = v[2].z - v[0].z;
    s32 nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    long long d = (long long)nx * v[0].x + (long long)ny * v[0].y + (long long)nz * v[0].z;
    return d >= 0;
}

// Queues a face; returns its sort key, or -1 when it isn't drawn.
// A key of 0 or more passed in `forced` is used instead of the computed one.
IWRAM_CODE static s32 queue_cam_face(const Vec3 *cv, s32 n, u32 color, u32 flags, s32 forced)
{
    if (face_count >= MAX_FACES) return -1;
    s32 sum = 0, near = 0, far = -32768;
    for (s32 i = 0; i < n; i++) {
        s32 z = cv[i].z;
        sum += z;
        near += z < NEAR;
        if (z > far) far = z;
    }
    if (near == n || !in_view(cv, n)) return -1;
    static const u16 inv_n[MAXV + 1] = { 0, 0, 0, 0x5556, 0x4000, 0x3334, 0x2AAB };
    s32 avg = (sum * inv_n[n]) >> 16;
    if (avg > r_far) return -1;
    if (!(flags & RF_TWO_SIDED) && facing_away(cv)) return -1;
    Face *f = &faces[face_count];
    s32 m = project_poly(cv, n, f->x, f->y, 0);
    if (!m) return -1;
    f->n = m;
    f->color = fog(color, avg);
    // Drivable surfaces sort by their farthest point so things on them draw
    // on top; decals (shadows) sort just in front of the surface below.
    s32 key = (flags & RF_SURFACE) ? far + 8 : (flags & RF_DECAL) ? far + 4 : avg;
    if (forced >= 0) key = forced;
    if (key < 0) key = 0;
    f->key = key;
    s32 b = key >> 3;
    if (b >= BUCKETS) b = BUCKETS - 1;
    s16 *link = &heads[b];
    while (*link >= 0 && faces[*link].key > key) link = &faces[*link].next;
    f->next = *link;
    *link = face_count++;
    return key;
}

IWRAM_CODE void r_face(const Vec3 *v, s32 n, u8 color, u32 flags)
{
    Vec3 cv[MAXV];
    for (s32 i = 0; i < n; i++) to_camera(v[i].x, v[i].y, v[i].z, &cv[i]);
    queue_cam_face(cv, n, color, flags, -1);
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
        far    += cv[i].z > r_far;
    }
    if (behind == 8 || far == 8) return;
    for (s32 f = 0; f < 5; f++) {
        if (!(mask & (1 << f))) continue;
        Vec3 q[4];
        for (s32 k = 0; k < 4; k++) q[k] = cv[box_faces[f][k]];
        queue_cam_face(q, 4, COLOR(material, box_shade[f]), 0, -1);
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

IWRAM_CODE void r_mesh(s32 px, s32 py, s32 pz, const s32 *m, const Mesh *mesh)
{
    Vec3 cv[MESH_MAX_VERTS];
    const s8 *v = mesh->verts;
    for (s32 i = 0; i < mesh->vert_count; i++, v += 3) {
        s32 lx = v[0], ly = v[1], lz = v[2];
        to_camera(px + ((m[0] * lx + m[3] * ly + m[6] * lz) >> 14),
                  py + ((m[1] * lx + m[4] * ly + m[7] * lz) >> 14),
                  pz + ((m[2] * lx + m[5] * ly + m[8] * lz) >> 14), &cv[i]);
    }
    const u8 *f = mesh->faces;
    s32 parent = -1;
    for (s32 i = 0; i < mesh->face_count; i++) {
        s32 n = f[0] & 7;
        Vec3 q[MAXV];
        for (s32 k = 0; k < n; k++) q[k] = cv[f[2 + k]];
        if (!(f[0] & MESH_DECAL))
            parent = queue_cam_face(q, n, f[1], 0, -1);
        else if (parent > 0)
            queue_cam_face(q, n, f[1], 0, parent - 1);
        f += 2 + n;
    }
}

IWRAM_CODE void r_flush(void)
{
    for (s32 b = BUCKETS - 1; b >= 0; b--)
        for (s32 i = heads[b]; i >= 0; i = faces[i].next) {
            const Face *f = &faces[i];
            fill_poly(f->x, f->y, f->n, f->color);
        }
}
