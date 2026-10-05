// Car physics for No Tracin'. One step = 1/60 s; positions and speeds are Q8.
#ifndef CAR_H
#define CAR_H

#include "gba.h"

enum { CAR_GROUND, CAR_AIR, CAR_LOOP, CAR_CRASH };
enum { EV_NONE, EV_JUMP, EV_LOOP, EV_CRASH, EV_SPLASH, EV_BIG_LANDING, EV_FELL };

typedef struct {
    s32 x, y, z;           // position, Q8 world units
    s32 vx, vy, vz;        // velocity, Q8 units per step
    s32 heading;           // 65536 per turn, 0 = +z, increasing turns right
    s32 steer;             // front wheel angle, 1024 units per turn
    s32 pitch, roll;       // body angles, Q8 of 1024-unit angles (nose up / right side down)
    s32 pitch_v, roll_v;
    s32 bob, bob_v;        // suspension travel, Q8 units
    s32 mode, timer;
    s32 loop_s, loop_v;    // distance and speed along the loop, Q8
    s32 air_steps, launch_x, launch_z;
    s32 spin_p, spin_r, spin_h;
    s32 safe_x, safe_z, safe_heading, safe_timer;
    s32 skid, crash_kind;
    s32 event, event_a, event_b;
    s32 rpm, gear, shift_timer;  // automatic gearbox (gear 0 = reverse)
    s32 hit;                // wall impact speed this step, 0 if none
    s32 landed;             // landing impact this step, 0 if none
    s32 spin;               // rear wheels spinning on a hard launch
    s32 throttle;           // gas held this step
    s32 boost;              // nitro firing (set by the game before car_step)
    s32 air_pv, air_rv, air_hv;          // trick spin rates in the air
    s32 trick_p, trick_r, trick_h;       // rotation done this jump: flips, rolls, spins
    s32 event_c, event_d, event_e;       // tricks landed: flips, rolls, spins
    s32 fly_v;              // airspeed with the fly cheat
} Car;

// Tunables the options menu changes.
extern s32 car_g, car_grip, car_power, car_crashes;
extern s32 cheat_fly, cheat_hop, cheat_glitch;

void car_reset(Car *c, s32 x, s32 z, s32 heading);
void car_respawn(Car *c);
void car_step(Car *c, u16 keys);
void car_draw(const Car *c, s32 ghost);
// A CPU rival at (x, y, z) facing `heading` (65536 units), pitched by `pitch`
// (1024 units); `depth` picks the detail.
void car_draw_rival(s32 x, s32 y, s32 z, s32 heading, s32 pitch, s32 color, s32 depth);
s32  car_speed(const Car *c);   // Q8 units per step, always positive

#endif
