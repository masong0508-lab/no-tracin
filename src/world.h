// Freedom City: streets, buildings and the stunt features you can drive on.
#ifndef WORLD_H
#define WORLD_H

#include "gba.h"

#define BLOCK      1024               // city block pitch in world units (~51 m); a power of 2
#define ROAD_HALF  144                // half the road width: two 3.6 m lanes each way
#define WALK       48                 // sidewalk width
#define BLOCKS     20                 // city is BLOCKS x BLOCKS
#define WORLD      (BLOCK * BLOCKS)   // must stay below 32768 (s16 coordinates)
#define LANE       (ROAD_HALF / 2)    // lane width
#define MAX_BUILDING_H 560            // tallest roof in town

// The Stunt Park replaces a 2x3 group of blocks near the south-west corner.
#define PARK_X0    (BLOCK * 1 + ROAD_HALF)
#define PARK_X1    (BLOCK * 3 - ROAD_HALF)
#define PARK_Z0    (BLOCK * 1 + ROAD_HALF)
#define PARK_Z1    (BLOCK * 4 - ROAD_HALF)

static inline s32 park_contains(s32 x, s32 z, s32 margin)
{
    return x > PARK_X0 - margin && x < PARK_X1 + margin &&
           z > PARK_Z0 - margin && z < PARK_Z1 + margin;
}

// Places in the park, as offsets from its south-west corner.
#define PX(o)      (PARK_X0 + (o))
#define PZ(o)      (PARK_Z0 + (o))

// The park circuit: north up lane A from the start line through the loop,
// round the banked curve, then south down lane B over the canal jump. The
// stunts are the size they always were; the bigger park buys a longer run
// at the loop and down lane B, and room to turn round past the start.
#define START_Z    PZ(171)            // the start line (gate 0)
#define LOOP_X     (BANK_X - BANK_ROUT)   // the loop, straight below lane A's lead-in
#define LOOP_Z     (START_Z + 1303)
#define BANK_X     PX(850)            // centre of the banked half-circle
#define BANK_Z     (LOOP_Z + 640)     // lane A's lead-in starts 300 past the loop
#define BANK_RIN   250
#define BANK_ROUT  430
#define BANK_H     100                // rim height
#define CANAL_Z0   (START_Z + 523)    // the canal runs across the park
#define CANAL_Z1   (CANAL_Z0 + 330)
#define LANE_B_X0  (BANK_X + BANK_RIN - 10)   // the kicker and landing ramp
#define LANE_B_X1  (LANE_B_X0 + 200)
#define LANDING_Z0 (CANAL_Z0 - 250)

// A vertical loop. The car enters at (x, z) heading +z and leaves
// `shift` units to the right, still heading +z.
typedef struct {
    s32 x, z;
    s32 radius;
    s32 shift;
    s32 width;
} Loop;

extern const Loop the_loop;
extern s32 world_overflow;            // table entries dropped for lack of room (tests read it)

void world_init(void);
void world_draw(s32 focus_x, s32 focus_z);

// Height of the drivable surface in Q8 world units.
s32 world_height(s32 x, s32 z);
s32 world_in_water(s32 x, s32 z);

// What the ground is made of at a point (for dust and tyre noise).
enum { SURF_ROAD, SURF_SIDEWALK, SURF_GRASS };
s32 world_surface(s32 x, s32 z);

// Pushes a circle out of walls. Returns penetration depth in world units
// (0 = no contact) and the outward normal in Q14.
s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz);
s32 world_ground_plane(s32 x, s32 z, s32 r, s32 *h0, s32 *gx, s32 *gz);

// Loop centreline at angle theta (1024 units), in Q8 world units.
void loop_point(s32 theta, s32 *x, s32 *y, s32 *z);

// Things placed with the map editor: ramps, walls and boost pads standing
// at any heading. They join the stunt features, so the car (either physics
// model) and the props drive up them and bump into them like the built-in
// ones. Each is a rectangle centred on (x, z), half_w across and half_l
// along `angle` (1024 units, 0 = +z: the way you drive up a ramp). A ramp
// runs from h0 at its back to h1 at its front; a wall is h0 tall; a pad is
// flat and fires the nitro.
enum { DYN_RAMP, DYN_WALL, DYN_PAD };
#define MAX_DYN 64
void world_dyn_clear(void);
s32  world_dyn_add(s32 kind, s32 x, s32 z, s32 angle, s32 half_w, s32 half_l, s32 h0, s32 h1);   // 0 when full
void world_dyn_done(void);                // after adding: re-index them for the queries
s32  world_dyn_pad(s32 x, s32 z);         // on a boost pad?
// Draws one without adding it (the editor's preview).
void world_draw_dyn(s32 kind, s32 x, s32 z, s32 angle, s32 half_w, s32 half_l, s32 h0, s32 h1);
// A building, tree, water or the edge of the city within r of (x, z)?
s32  world_blocked(s32 x, s32 z, s32 r);

#endif
