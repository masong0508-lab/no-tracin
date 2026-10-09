// Soft-body car physics in the spirit of BeamNG.drive: the body is a frame
// of point masses joined by springs that bend for good when overloaded and
// snap when pulled apart, so crashes really crumple the car.
#ifndef SOFTBODY_H
#define SOFTBODY_H

#include "car.h"

extern s32 car_softbody;               // options: 1 = soft-body physics, 0 = arcade

void sb_reset(Car *c);                 // build a fresh frame at the car's pose
void sb_invalidate(void);              // a new car: rebuild (and repair) on the next step
void sb_park(void);                    // the arcade model has the car: re-place it on return
s32  sb_valid(const Car *c);           // a frame is built for this car
void sb_step(Car *c, u16 keys);        // one physics step; writes the car's pose
void sb_draw(const Car *c, const Mesh *body);   // the deformed body and its wheels

// A solid sphere (another car, a loose object) pushing on the frame. (x, y, z)
// and radius in world units, (vx, vz) its velocity in Q8. Returns the impact
// speed in Q8 (0 if it didn't touch); *push gets the sideways shove it took.
s32  sb_push(s32 x, s32 y, s32 z, s32 radius, s32 vx, s32 vz, s32 *push_x, s32 *push_z);

// A loose object (an upright capsule of radius r and half height h in world
// units, weighing m nodes, with its centre and velocity in Q8) against the
// car's body: both take the knock, the object is put back outside, and a
// heavy one dents the car. Returns the impulse it took (0 if it didn't touch).
s32  sb_hit_prop(s32 *pos, s32 *vel, s32 radius, s32 half_h, s32 mass);
void sb_hit_done(void);                // call after a round of sb_hit_prop

s32  sb_damage(void);                  // 0..100 percent
s32  sb_wheels_off(void);              // wheels torn off

#endif
