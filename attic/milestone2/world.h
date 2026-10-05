// Freedom City: streets, buildings and the stunt features you can drive on.
#ifndef WORLD_H
#define WORLD_H

#include "gba.h"

#define BLOCK      512                // city block pitch in world units (~25 m)
#define ROAD_HALF  64                 // half the road width
#define BLOCKS     16                 // city is BLOCKS x BLOCKS
#define WORLD      (BLOCK * BLOCKS)

// The Stunt Park replaces a 3x4 group of blocks near the south-west corner.
#define PARK_X0    (BLOCK * 1 + ROAD_HALF)
#define PARK_X1    (BLOCK * 4 - ROAD_HALF)
#define PARK_Z0    (BLOCK * 1 + ROAD_HALF)
#define PARK_Z1    (BLOCK * 5 - ROAD_HALF)

// A vertical loop. The car enters at (x, z) heading +z and leaves
// `shift` units to the right, still heading +z.
typedef struct {
    s32 x, z;
    s32 radius;
    s32 shift;
    s32 width;
} Loop;

extern const Loop the_loop;

void world_init(void);
void world_draw(s32 focus_x, s32 focus_z);

// Height of the drivable surface in Q8 world units.
s32 world_height(s32 x, s32 z);
s32 world_in_water(s32 x, s32 z);

// Pushes a circle out of walls. Returns penetration depth in world units
// (0 = no contact) and the outward normal in Q14.
s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz);

// Loop centreline at angle theta (1024 units), in Q8 world units.
void loop_point(s32 theta, s32 *x, s32 *y, s32 *z);

#endif
