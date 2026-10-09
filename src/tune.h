// The physics lab: sliders that scale the car's and the props' physics on
// top of the option presets. Each slider is kept in notches of 10% (10 =
// 100%, the stock behaviour); tune_apply() turns them into the fixed-point
// factors the physics reads, so the hot loops only multiply and shift.
#ifndef TUNE_H
#define TUNE_H

#include "gba.h"

enum {
    TN_GRAVITY, TN_POWER, TN_TOPSPEED, TN_BRAKES, TN_GRIP, TN_HANDBRAKE, TN_STEER,
    TN_WEIGHT, TN_DRAG, TN_SPRINGS, TN_DAMPING, TN_ANTIROLL, TN_DOWNFORCE,
    TN_STRENGTH, TN_CRUMPLE, TN_BOUNCE, TN_CRASH, TN_AIR, TN_JUMP, TN_NITRO,
    TN_PROP_M, TN_PROP_BOUNCE, TN_PROP_FRICTION, TUNE_COUNT
};
#define TUNE_STOCK 10                  // notches for 100%

extern u8 g_tune[TUNE_COUNT];

// What the sliders work out to. Q8 factors are 256 at 100%; the rest are
// the stock constants they replace, scaled.
typedef struct {
    s32 a0, v0;            // full-throttle accel at low speed, and the speed power takes over (POWER / A0)
    s32 drag;              // quadratic air drag, Q8 (TOP SPEED: 1 / pct^3, so top speed follows the slider)
    s32 drag_lin;          // linear drag in flight and rolling resistance, Q8
    s32 brake;             // BRAKE
    s32 hand_grip;         // arcade rear grip on the handbrake (HAND_GRIP)
    s32 hand_lim;          // soft-body rear grip on the handbrake, Q8 of the tyre limit (5/8)
    s32 grip_min;          // arcade grip floor
    s32 steer, steer_rate; // steering lock (Q8) and how fast the wheels turn
    s32 weight, inv_weight;          // car weight against loose things, Q8, and its inverse
    s32 spring, damp;      // suspension spring rate and damping, Q8
    s32 roll;              // soft-body anti-roll bars (K_ROLL)
    s32 lean;              // arcade body lean per lateral g
    s32 downforce;         // grip from speed: load gained at (speed / 16)^2, Q16 (0 at 100%)
    s32 strength;          // soft-body beam yield and wheel mounts, Q8
    s32 crumple;           // how far a bent beam gives, Q8
    s32 crush;             // knock the body absorbs per step before it dents (CRUSH * TOTAL_M)
    s32 bounce;            // wall restitution: kick = vn * bounce / 100 (130 = 0.3)
    s32 sb_bounce;         // soft-body: wall impulse = vn * sb_bounce / 100 (500 = a quarter back)
    s32 crash_wall, crash_land;
    s32 air_p, air_r, air_h, air_steer;   // arcade trick spin rates and air steering
    s32 sb_air;            // soft-body air control, Q8 (0 at 100% and below: stock has none)
    s32 jump;              // upward speed kept at take-off, Q8
    s32 nitro, nitro_air, nitro_fly;      // nitro push on the ground, in the air (Q8) and flying
    s32 prop_m;            // prop weights, Q8
    s32 prop_bounce;       // prop restitution, percent (100 = stock)
    s32 prop_fric;         // prop ground friction, Q8
} Tune;
extern Tune tn;

s32  tune_pct(s32 i);                  // slider i in percent
s32  tune_min(s32 i);                  // range in notches
s32  tune_max(s32 i);
s32  tune_stock(void);                 // all sliders at 100%
const char *tune_name(s32 i);
const char *tune_hint(s32 i);
void tune_apply(void);                 // called by options_apply() once car_g, car_power and car_grip are set
void tune_load(void);                  // from the save, or all 100%
void tune_save(void);

#endif
