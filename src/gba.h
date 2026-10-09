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

// Palette layout beyond the near colors (M_COUNT * 4 = 104 entries).
#define FOG1_BASE    104   // each near color part way into the haze
#define FOG2_BASE    208   // one deep-haze color per material
#define SKY_BASE     234   // sky gradient, horizon first
#define SKY_STEPS    20
#define SKYLINE_FAR  254
#define SKYLINE_NEAR 255

#define FOCAL  150     // projection scale in pixels
#define R_FAR_MAX 2400 // longest draw distance anything may ask for (sizes the recip table)
// Draw distance and haze for each place: r_far comes in toward the minimum
// while frames run long and goes back out toward the maximum when there is
// room. The haze starts at fog1 and is deep from fog2.
#define FAR_MIN_CITY   1100
#define FAR_MAX_CITY   1500
#define FOG1_CITY      620
#define FOG2_CITY      1020
#define FAR_MIN_TRACK  950
#define FAR_MAX_TRACK  1500
#define FOG1_TRACK     620
#define FOG2_TRACK     1020
#define FAR_MENU       1100     // title and menu flyovers

typedef struct { s32 x, y, z; } Vec3;

typedef struct {
    s32 x, y, z;       // world position
    s32 yaw, pitch;    // 1024-unit angles; positive pitch looks down
} Camera;

void r_init(void);
enum { THEME_DAY, THEME_SUNSET, THEME_NIGHT, THEME_SYNTHWAVE, THEME_GAMEBOY, THEME_COUNT };
void r_init_palette(s32 theme, u16 paint);
void r_set_paint(u16 paint);
// Backdrop and ground colours for Freedom City or a race circuit. Takes
// effect at the next r_init_palette.
enum { SCENE_CITY, SCENE_FOREST, SCENE_BAY, SCENE_MOUNTAINS };
void r_set_scene(s32 scene);
void r_begin(volatile u16 *page, const Camera *cam);
// A flat polygon lying on the ground, drawn immediately in submission order.
void r_ground(const Vec3 *v, s32 n, u8 color);
// The same with vertices already in camera space (see r_xform).
void r_ground_cam(const Vec3 *cv, s32 n, u8 color);
void r_xform(s32 x, s32 y, s32 z, Vec3 *out);          // world point to camera space
void r_xform_dir(s32 dx, s32 dy, s32 dz, Vec3 *out);   // world direction (Q14) to camera space
// A solid polygon, depth-sorted and drawn by r_flush. Vertices clockwise
// as seen from the side that should be visible.
#define RF_TWO_SIDED 1   // draw from both sides
#define RF_SURFACE   2   // something you drive on: sorts behind objects resting on it
#define RF_DECAL     4   // lies on a surface (a shadow): sorts just in front of it
void r_face(const Vec3 *v, s32 n, u8 color, u32 flags);
// Axis-aligned box with shading per side (top brightest).
void r_box(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material);
// Box transformed by a Q14 3x3 matrix m (columns: local x, y, z axes) and
// placed at (px, py, pz).
void r_box_mat(s32 px, s32 py, s32 pz, const s32 *m,
               s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material);
// Low-poly model: vertices as signed bytes (x, y, z), faces as
// { vertex count, color, indices... } listed clockwise from outside.
// A count with MESH_DECAL set marks a face lying on the face before it.
#define MESH_MAX_VERTS 48
#define MESH_DECAL     0x80
typedef struct {
    const s8 *verts;
    const u8 *faces;
    u8 vert_count, face_count;
} Mesh;
void r_mesh(s32 px, s32 py, s32 pz, const s32 *m, const Mesh *mesh);
// The same model with its vertices already placed in the world (a bent car).
void r_mesh_world(const Vec3 *v, const Mesh *mesh);
extern s32 r_far;      // current draw distance, r_far_min..r_far_max
extern s32 r_far_min, r_far_max, r_fog1, r_fog2;
void r_set_far(s32 far_min, s32 far_max, s32 fog1, s32 fog2);
// Returns camera-space depth of a world point (for culling).
s32  r_depth(s32 x, s32 z, s32 *side);
// Could anything within `radius` of ground point (x, z) be on screen?
s32  r_visible(s32 x, s32 z, s32 radius);
// Projects a world point; returns its depth, or 0 when behind the camera.
s32  r_project(s32 x, s32 y, s32 z, s32 *sx, s32 *sy);
void r_flush(void);

#endif
