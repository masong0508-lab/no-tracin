// Shared GBA hardware definitions and engine interface for No Tracin'.
#ifndef GBA_H
#define GBA_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;

#define REG_DISPCNT   (*(volatile u16 *)0x04000000)
#define REG_VCOUNT    (*(volatile u16 *)0x04000006)
#define REG_WAITCNT   (*(volatile u16 *)0x04000204)
#define REG_TM0CNT    (*(volatile u32 *)0x04000100)
#define REG_TM1CNT    (*(volatile u32 *)0x04000104)
#define REG_TM0D      (*(volatile u16 *)0x04000100)
#define REG_TM1D      (*(volatile u16 *)0x04000104)
#define REG_KEYINPUT  (*(volatile u16 *)0x04000130)
#define PAL_BG        ((volatile u16 *)0x05000000)
#define VRAM_PAGE0    ((volatile u16 *)0x06000000)
#define VRAM_PAGE1    ((volatile u16 *)0x0600A000)

#define MODE4     0x0004
#define BG2_ON    0x0400
#define PAGE_BIT  0x0010

#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200

#define RGB15(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

#define SCREEN_W 240
#define SCREEN_H 160

#define IWRAM_CODE __attribute__((section(".iwram"), target("arm"), noinline))
#define EWRAM_BSS  __attribute__((section(".ewram")))

// Angles: 1024 units per full turn. Trig results are Q14 (16384 = 1.0).
s32 isin(s32 a);
s32 icos(s32 a);
s32 iatan2(s32 y, s32 x);   // angle from +x toward +y, -512..511
s32 isqrt(u32 v);

// Materials: each owns 4 palette entries, shade 0 (brightest) to 3 (darkest).
enum {
    M_SKY, M_ASPHALT, M_LINE, M_SIDEWALK, M_GRASS, M_TREE,
    M_BLD0, M_BLD1, M_BLD2, M_BLD3, M_BLD4, M_BLD5,
    M_CAR, M_CAR_TRIM, M_GLASS, M_TIRE, M_RAMP, M_HUD, M_HUD_BG,
    M_WATER, M_STUNT_RED, M_STUNT_WHITE, M_FIRE, M_SMOKE, M_SHADOW, M_SPLASH,
    M_COUNT
};
#define COLOR(m, shade) ((u8)((m) * 4 + (shade)))

typedef struct { s32 x, y, z; } Vec3;

typedef struct {
    s32 x, y, z;       // world position
    s32 yaw, pitch;    // 1024-unit angles; positive pitch looks down
} Camera;

void r_init(void);
void r_init_palette(void);
void r_begin(volatile u16 *page, const Camera *cam);
// A flat polygon lying on the ground, drawn immediately in submission order.
void r_ground(const Vec3 *v, s32 n, u8 color);
// A solid polygon, depth-sorted and drawn by r_flush. Vertices clockwise
// as seen from the side that should be visible.
#define RF_TWO_SIDED 1   // draw from both sides
#define RF_SURFACE   2   // something you drive on: sorts behind objects resting on it
void r_face(const Vec3 *v, s32 n, u8 color, u32 flags);
// Axis-aligned box with shading per side (top brightest).
void r_box(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material);
// Box rotated by yaw around (cx, cz), given in its own local coordinates.
void r_box_rot(s32 cx, s32 cy, s32 cz, s32 yaw,
               s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material);
// Box transformed by a Q14 3x3 matrix m (columns: local x, y, z axes) and
// placed at (px, py, pz).
void r_box_mat(s32 px, s32 py, s32 pz, const s32 *m,
               s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material);
// Returns camera-space depth of a world point (for culling).
s32  r_depth(s32 x, s32 z, s32 *side);
void r_flush(void);
void r_rect(s32 x, s32 y, s32 w, s32 h, u8 color);

#endif
