// Race circuits in the style of Virtua Racing: Big Forest, Bay Bridge and
// Acropolis. While one is loaded the world queries (height, walls, surface)
// and drawing come from the circuit instead of Freedom City.
#ifndef TRACK_H
#define TRACK_H

#include "gba.h"

#define TRACK_HALF_W 84       // half the road width
#define TRACK_WORLD  8192     // circuits lie inside 0..TRACK_WORLD on x and z
#define TRACK_COUNT  3

// One sample along the centreline; the road runs from this point to the next.
typedef struct {
    s16 x, z, y;              // position and road height
    s16 ux, uz;               // unit direction to the next point, Q14
    u16 len, dist;            // length to the next point, distance from the line
    u16 heading;              // 65536 per turn, 0 = +z (the car's convention)
    u16 radius;               // corner radius, 4000 = straight
    u8 flags, verge;          // TF_* and the grass width outside the road
} TrackPt;

enum { TF_KERB_L = 1, TF_KERB_R = 2, TF_BRIDGE = 4, TF_START = 8, TF_TUNNEL = 16 };

typedef struct { u8 type, rot, var, y2; s16 x, z; } Prop;   // y2 = ground height / 2

typedef struct {
    const char *name, *level;
    const TrackPt *pts;
    u16 count, lap;           // points, lap length in world units
    const u16 *cell_first;    // nearest-road grid, 64 x 64 cells of 128 units
    const u8 *cell_count, *cell_list;
    const Prop *props;        // scenery, sorted into 16 x 16 cells of 512 units
    const u16 *prop_first;
    const u8 *prop_count;
    u16 prop_total;
    const s16 *water;         // rectangles x0, z0, x1, z1
    u8 water_count;
    u8 laps, start_time, cp_time;   // race length, seconds on the clock
    u8 scene;                 // backdrop style (SCENE_*)
} TrackDef;

extern const TrackDef g_tracks[TRACK_COUNT];
extern const TrackDef *g_track;        // NULL in Freedom City

// Nearest point on the road. Returns 0 when (x, z) is nowhere near it.
typedef struct {
    s32 seg;        // point index
    s32 along;      // distance past that point
    s32 lat;        // signed distance from the centreline, + = right
    s32 dist;       // unsigned distance from the centreline
    s32 h;          // road height there, Q8
} TrackHit;
s32  track_find(s32 x, s32 z, TrackHit *hit);

extern s16 track_pitch[];               // slope of each stretch (1024-unit angle)
void track_load(s32 index);            // -1 goes back to the city
s32  track_height(s32 x, s32 z);       // Q8
s32  track_surface(s32 x, s32 z);
void track_gradient(s32 x, s32 z, s32 *gx, s32 *gz);   // road slope, Q8 rise per unit
s32  track_ground_plane(s32 x, s32 z, s32 r, s32 *h0, s32 *gx, s32 *gz);   // see world_ground_plane
s32  track_height_near(s32 seg, s32 x, s32 z);    // track_height, searching only near seg
s32  track_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz);
void track_draw(s32 focus_x, s32 focus_z, s32 frame);
// A point `d` units along the lap (wrapping) and `lat` to the right of the
// centreline: world position, road height (Q8) and heading (65536 units).
void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint);
// Draws the course outline into the back page (menus).
void track_map(volatile u16 *page, s32 index, s32 x0, s32 y0, s32 w, s32 h, u8 color, s32 dot_d, u8 dot_color);

#endif
