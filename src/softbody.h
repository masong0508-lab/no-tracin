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

s32  sb_damage(void);                  // 0..100 percent
s32  sb_wheels_off(void);              // wheels torn off

#endif
