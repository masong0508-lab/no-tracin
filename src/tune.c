// The physics lab's sliders: names, ranges, the save, and the factors the
// physics reads (worked out here so the physics only multiplies).
#include "tune.h"
#include "car.h"
#include "menu.h"

EWRAM_BSS u8 g_tune[TUNE_COUNT];
EWRAM_BSS Tune tn;

typedef struct { const char *name, *hint; u8 lo, hi; } TuneDef;
static const TuneDef defs[TUNE_COUNT] = {
    [TN_GRAVITY]       = { "GRAVITY",         "PULL OF THE EARTH: JUMPS, SLOPES",   0, 30 },
    [TN_POWER]         = { "ENGINE POWER",    "ACCELERATION AT EVERY SPEED",        0, 30 },
    [TN_TOPSPEED]      = { "TOP SPEED",       "LESS AIR DRAG: A HIGHER TOP SPEED",  2, 30 },
    [TN_BRAKES]        = { "BRAKES",          "BRAKING FORCE (B)",                  0, 30 },
    [TN_GRIP]          = { "TYRE GRIP",       "CORNERING GRIP. 10% IS SHEER ICE",   1, 30 },
    [TN_HANDBRAKE]     = { "HANDBRAKE GRIP",  "REAR GRIP WITH L HELD: DRIFTS",      0, 30 },
    [TN_STEER]         = { "STEERING",        "STEERING LOCK AND HOW FAST",         0, 30 },
    [TN_WEIGHT]        = { "CAR WEIGHT",      "HOW HARD YOU AND PROPS SHOVE",       1, 30 },
    [TN_DRAG]          = { "AIR DRAG",        "DRAG IN FLIGHT, ROLLING LOSSES",     0, 30 },
    [TN_SPRINGS]       = { "SPRINGS",         "SUSPENSION STIFFNESS",               0, 30 },
    [TN_DAMPING]       = { "DAMPERS",         "SUSPENSION DAMPING: LESS BOUNCE",    0, 20 },
    [TN_ANTIROLL]      = { "ANTI-ROLL",       "STIFFER: THE BODY LEANS LESS",       0, 30 },
    [TN_DOWNFORCE]     = { "DOWNFORCE",       "GRIP AT SPEED. UNDER 100%: LIFT",    0, 30 },
    [TN_STRENGTH]      = { "BODY STRENGTH",   "SOFT-BODY: FORCE TO BEND METAL",     0, 30 },
    [TN_CRUMPLE]       = { "CRUMPLE",         "SOFT-BODY: HOW DEEP DENTS GO",       0, 30 },
    [TN_BOUNCE]        = { "WALL BOUNCE",     "HOW FAR WALLS THROW YOU BACK",       0, 30 },
    [TN_CRASH]         = { "CRASH TOLERANCE", "KNOCK IT TAKES TO WRECK THE CAR",    1, 30 },
    [TN_AIR]           = { "AIR CONTROL",     "TRICK SPINS. SOFT-BODY ABOVE 100%",  0, 30 },
    [TN_JUMP]          = { "JUMP POWER",      "LAUNCH SPEED OFF RAMPS, KICKERS",    0, 30 },
    [TN_NITRO]         = { "NITRO POWER",     "PUSH FROM THE NITRO (R)",            0, 30 },
    [TN_PROP_M]        = { "PROP WEIGHT",     "MASS OF CONES, DRUMS, CRATES",       1, 30 },
    [TN_PROP_BOUNCE]   = { "PROP BOUNCE",     "HOW LIVELY PROPS BOUNCE",            0, 30 },
    [TN_PROP_FRICTION] = { "PROP FRICTION",   "HOW FAST KNOCKED PROPS STOP",        0, 30 },
};

s32 tune_pct(s32 i) { return g_tune[i] * 10; }
s32 tune_min(s32 i) { return defs[i].lo; }
s32 tune_max(s32 i) { return defs[i].hi; }
const char *tune_name(s32 i) { return defs[i].name; }
const char *tune_hint(s32 i) { return defs[i].hint; }

s32 tune_stock(void)
{
    for (s32 i = 0; i < TUNE_COUNT; i++)
        if (g_tune[i] != TUNE_STOCK) return 0;
    return 1;
}

// pct of v, and pct of 256.
static s32 of(s32 v, s32 i) { return v * tune_pct(i) / 100; }
static s32 q8(s32 i) { return of(256, i); }

// Anything out of range (an old or damaged save) goes back to 100%.
static void check(void)
{
    for (s32 i = 0; i < TUNE_COUNT; i++)
        if (g_tune[i] < defs[i].lo || g_tune[i] > defs[i].hi) g_tune[i] = TUNE_STOCK;
}

void tune_apply(void)
{
    check();
    car_g = of(car_g, TN_GRAVITY);
    car_power = of(car_power, TN_POWER);
    tn.a0 = of(16, TN_POWER);
    tn.v0 = tn.a0 ? car_power / tn.a0 : 0;
    s32 p = tune_pct(TN_TOPSPEED);
    tn.drag = 256000000 / (p * p * p);
    tn.drag_lin = q8(TN_DRAG);
    tn.brake = of(31, TN_BRAKES);
    car_grip = of(car_grip, TN_GRIP);
    tn.grip_min = of(6, TN_GRIP);
    tn.hand_grip = of(12, TN_HANDBRAKE);
    tn.hand_lim = of(160, TN_HANDBRAKE);
    tn.steer = q8(TN_STEER);
    tn.steer_rate = of(6, TN_STEER);
    if (tn.steer_rate < 1) tn.steer_rate = 1;      // so the wheels still come back straight
    tn.weight = q8(TN_WEIGHT);
    tn.inv_weight = 25600 / tune_pct(TN_WEIGHT);
    tn.spring = q8(TN_SPRINGS);
    tn.damp = q8(TN_DAMPING);
    tn.roll = of(128, TN_ANTIROLL);
    tn.lean = 12800 / (100 + tune_pct(TN_ANTIROLL));
    tn.downforce = (tune_pct(TN_DOWNFORCE) - 100) * 7 / 4;   // +1 g of load at 90 mph on 300%
    tn.strength = q8(TN_STRENGTH);
    tn.crumple = q8(TN_CRUMPLE);
    p = tune_pct(TN_CRUMPLE);
    tn.crush = p ? 25600 / p : 1 << 16;
    tn.bounce = 100 + of(30, TN_BOUNCE);
    tn.sb_bounce = 400 + tune_pct(TN_BOUNCE);
    tn.crash_wall = of(1700, TN_CRASH);
    tn.crash_land = of(1800, TN_CRASH);
    tn.air_p = of(4800, TN_AIR);
    tn.air_r = of(5200, TN_AIR);
    tn.air_h = of(1150, TN_AIR);
    tn.air_steer = of(40, TN_AIR);
    p = tune_pct(TN_AIR);
    tn.sb_air = p > 100 ? (p - 100) * 192 / 100 : 0;      // 1.5 times the arcade rates at 300%
    tn.jump = q8(TN_JUMP);
    tn.nitro = of(22, TN_NITRO);
    tn.nitro_air = q8(TN_NITRO);
    tn.nitro_fly = of(40, TN_NITRO);
    tn.prop_m = q8(TN_PROP_M);
    tn.prop_bounce = tune_pct(TN_PROP_BOUNCE);
    tn.prop_fric = q8(TN_PROP_FRICTION);
}

// In the save after the race records, clear of the map editor's area.
#define TUNE_MAGIC   0x4255544E   // "NTUB"
#define TUNE_SAVE_AT 0x7D00
#define TUNE_SAVE_N  32           // room for more sliders later

void tune_load(void)
{
    u8 b[TUNE_SAVE_N];
    s32 ok = load_blob(TUNE_MAGIC, TUNE_SAVE_AT, b, TUNE_SAVE_N);
    for (s32 i = 0; i < TUNE_COUNT; i++) g_tune[i] = ok ? b[i] : TUNE_STOCK;
    check();
}

void tune_save(void)
{
    u8 b[TUNE_SAVE_N];
    for (s32 i = 0; i < TUNE_SAVE_N; i++) b[i] = i < TUNE_COUNT ? g_tune[i] : 0;
    save_blob(TUNE_MAGIC, TUNE_SAVE_AT, b, TUNE_SAVE_N);
}
