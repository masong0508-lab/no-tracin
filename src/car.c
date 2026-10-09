// Car physics: engine and drag along the car, tyre grip across it (so the
// car drifts when it asks for more than the tyres have), gravity on slopes,
// ballistic flight off ramps, the vertical loop, and crashes.
//
// Scale: 20 world units ~ 1 metre, one step = 1/60 s, gravity is 2.5 g
// (arcade-heavy so jumps land inside the city).
#include "car.h"
#include "world.h"
#include "track.h"
#include "softbody.h"

#define G           car_g     // gravity, Q8 units per step^2 (35 = 2.5 g)
#define GRIP        car_grip  // max sideways velocity change per step (38 = ~1.1 g)
#define HAND_GRIP   12      // rear grip with the handbrake on
#define A0          16      // full-throttle acceleration at low speed
#define POWER       car_power // engine power: accel = POWER / speed above A0
#define BRAKE       31
#define REV_ACC     10
#define REV_MAX     (-1024)
#define CRASH_WALL  1700    // sideways-into-wall speed that wrecks the car (~45 mph)
#define CRASH_LAND  1800    // landing impact that wrecks the car
#define CAR_R       28      // collision radius
#define STEP_UP     (10 << 8)
#define LOOP_ALIGN  4551    // must enter the loop within 25 degrees of straight

s32 car_g = 35, car_grip = 38, car_power = 28000, car_crashes = 1;
s32 cheat_fly, cheat_hop, cheat_glitch;

static u32 rng = 12345;
static s32 rnd(s32 n) { rng = rng * 1103515245 + 12345; return (rng >> 16) % n; }

static s32 iabs(s32 v) { return v < 0 ? -v : v; }
// Body angles are Q8 of 1024-unit turns; this folds one into -half..half a turn.
static s32 wrap_angle(s32 a) { return (s32)((u32)a << 14) >> 14; }

static s32 engine_force(s32 v)
{
    if (v < POWER / A0) return A0;
    return POWER / v;
}

// Surface height that ignores the canal so slopes near its edge stay sane.
static s32 surf(s32 x, s32 z, s32 base)
{
    s32 h = world_height(x, z);
    return h < (-100 << 8) ? base : h;
}

// Slope from one-sided differences, taking the gentler side so the edge of
// a ramp or a ledge doesn't read as a cliff.
static s32 slope(s32 here, s32 ahead, s32 behind)
{
    s32 a = (ahead - here) >> 3, b = (here - behind) >> 3;
    s32 r = iabs(a) < iabs(b) ? a : b;
    return r > 256 ? 256 : r < -256 ? -256 : r;
}

static void gradient(s32 ux, s32 uz, s32 base, s32 *gx, s32 *gz)
{
    if (g_track) { track_gradient(ux, uz, gx, gz); return; }
    s32 here = surf(ux, uz, base);
    *gx = slope(here, surf(ux + 8, uz, base), surf(ux - 8, uz, base));
    *gz = slope(here, surf(ux, uz + 8, base), surf(ux, uz - 8, base));
}

static void spring(s32 *a, s32 *v, s32 target)
{
    *v += (target - *a) >> 3;
    *v -= *v >> 2;
    *a += *v;
}

void car_reset(Car *c, s32 x, s32 z, s32 heading)
{
    u8 *p = (u8 *)c;
    for (u32 i = 0; i < sizeof(*c); i++) p[i] = 0;
    c->x = x << 8;
    c->z = z << 8;
    c->y = world_height(x, z);
    if (c->y < 0) c->y = 0;
    c->heading = heading;
    c->safe_x = x;
    c->safe_z = z;
    c->safe_heading = heading;
    sb_invalidate();
}

void car_respawn(Car *c)
{
    car_reset(c, c->safe_x, c->safe_z, c->safe_heading);
}

s32 car_speed(const Car *c)
{
    if (c->mode == CAR_LOOP) return iabs(c->loop_v);
    return isqrt(c->vx * c->vx + c->vz * c->vz);
}

static void crash(Car *c, s32 kind)
{
    c->mode = CAR_CRASH;
    c->timer = 0;
    c->event = kind;
    c->crash_kind = kind;
    if (kind == EV_SPLASH) {
        c->spin_p = 2; c->spin_r = 3; c->spin_h = 0;
    } else {
        c->spin_p = 8 + rnd(16);
        c->spin_r = 10 + rnd(24);
        c->spin_h = rnd(1200) - 600;
        if (rnd(2)) c->spin_r = -c->spin_r;
        c->vy += 400 + rnd(300);   // the wreck gets thrown up
    }
}

// Pushes the car out of walls, bouncing its velocity. Hitting a wall
// head-on fast enough wrecks it.
static void collide(Car *c, s32 *nx, s32 *nz, s32 can_crash)
{
    s32 n_x, n_z;
    s32 pen = world_collide(*nx >> 8, *nz >> 8, CAR_R, &n_x, &n_z);
    if (pen <= 0) return;
    *nx += (pen * n_x) >> 6;
    *nz += (pen * n_z) >> 6;
    s32 vn = (c->vx * n_x + c->vz * n_z) >> 14;
    if (vn >= 0) return;
    if (-vn > c->hit) c->hit = -vn;
    if (cheat_glitch && -vn > 200) {
        // Physics glitch: walls fling you across the map.
        s32 kick = vn * 7;
        c->vx -= (kick * n_x) >> 14;
        c->vz -= (kick * n_z) >> 14;
        c->vy += -vn * 3 + 500;
        c->roll_v += vn * 8;
        return;
    }
    if (can_crash && car_crashes && -vn > CRASH_WALL) {
        crash(c, EV_CRASH);
        vn = (vn * 3) / 2;
    }
    s32 kick = (vn * 13) / 10;     // restitution 0.3
    c->vx -= (kick * n_x) >> 14;
    c->vz -= (kick * n_z) >> 14;
    c->vx -= c->vx >> 4;            // scraping slows the car
    c->vz -= c->vz >> 4;
    c->roll_v += vn * 2;
}

static void take_off(Car *c)
{
    c->mode = CAR_AIR;
    c->air_steps = 0;
    c->air_pv = c->air_rv = c->air_hv = 0;
    c->trick_p = c->trick_r = c->trick_h = 0;
    c->fly_v = isqrt(c->vx * c->vx + c->vz * c->vz);
    c->launch_x = c->x;
    c->launch_z = c->z;
}

static void enter_loop(Car *c, s32 vlong)
{
    c->mode = CAR_LOOP;
    c->loop_s = c->z - (the_loop.z << 8);
    c->loop_v = vlong;
    c->heading = 0;
    c->steer = 0;
}

// Crossing the loop's entry line, lined up and moving forward.
static s32 at_loop(const Car *c, s32 z0, s32 nx, s32 nz, s32 vlong)
{
    const Loop *l = &the_loop;
    return !g_track && (z0 >> 8) < l->z && (nz >> 8) >= l->z && iabs((nx >> 8) - l->x) < l->width / 2 &&
           iabs((s16)c->heading) < LOOP_ALIGN && vlong > 256;
}

static void ground_step(Car *c, u16 keys)
{
    s32 h = c->heading >> 6;
    s32 fx = isin(h), fz = icos(h);
    s32 vlong = (c->vx * fx + c->vz * fz) >> 14;
    s32 vlat  = (c->vx * fz - c->vz * fx) >> 14;
    s32 ux = c->x >> 8, uz = c->z >> 8;

    // Gravity pulls down the slope.
    s32 gx, gz;
    gradient(ux, uz, c->y, &gx, &gz);
    s32 ax = -(G * gx) >> 8, az = -(G * gz) >> 8;
    s32 along = (ax * fx + az * fz) >> 14;
    s32 alat  = (ax * fz - az * fx) >> 14;

    // Engine, brakes and reverse.
    s32 engine = 0;
    c->spin = 0;
    if (keys & KEY_A) {
        engine = vlong < -64 ? BRAKE : engine_force(vlong);
        c->spin = vlong >= -64 && vlong < 520;   // full power below ~14 mph lights the tyres up
    } else if (keys & KEY_B)
        engine = vlong > 64 ? -BRAKE : (vlong > REV_MAX ? -REV_ACC : 0);
    else if (vlong > 0)
        engine = vlong > 3 ? -3 : -vlong;
    else if (vlong < 0)
        engine = vlong < -3 ? 3 : -vlong;
    along += engine;
    if (c->boost) along += 22;              // nitro
    along -= (vlong * iabs(vlong)) >> 21;   // air drag
    along -= vlong >> 10;                   // rolling resistance
    if (g_track && world_surface(ux, uz) == SURF_GRASS)
        along -= vlong >> 6;                // grass off the circuit slows you right down
    vlong += along;

    // Tyres cancel sideways motion up to their grip; beyond that the car slides.
    // Grip shrinks when the tyres are also braking or driving hard (so trail
    // braking or flooring it mid-corner can break traction), on grass, and when
    // the car goes light over a crest.
    s32 hand = keys & KEY_L;
    s32 grip = hand ? HAND_GRIP : GRIP;
    s32 lon = iabs(engine) + (c->boost ? 8 : 0);
    if (!hand) grip -= lon / 3;
    if (world_surface(ux, uz) == SURF_GRASS) grip = (grip * 5) >> 3;
    if (c->load == 0) c->load = 256;
    grip = (grip * c->load) >> 8;
    if (grip < 6) grip = 6;
    vlat += alat;
    c->skid = 0;
    if (vlat > grip)       { vlat -= grip; c->skid = 1; }
    else if (vlat < -grip) { vlat += grip; c->skid = 1; }
    else vlat = 0;
    if (c->skid) vlong -= vlong >> 7;
    if (hand) vlong -= vlong >> 7;

    // Steering: less lock at speed, yaw from the bicycle model.
    s32 sp = iabs(vlong);
    if (sp > 3584) sp = 3584;
    s32 lock = 60 - (sp * 44) / 3584;
    s32 target = (keys & KEY_LEFT) ? -lock : (keys & KEY_RIGHT) ? lock : 0;
    if (c->steer < target)      c->steer = c->steer + 6 > target ? target : c->steer + 6;
    else if (c->steer > target) c->steer = c->steer - 6 < target ? target : c->steer - 6;
    s32 yaw = (vlong * c->steer) >> 8;
    if (c->skid && !hand) yaw = (yaw * 3) >> 2;     // front tyres sliding: understeer
    if (hand && sp > 768)  yaw = (yaw * 3) >> 1;     // handbrake: the tail steps out
    // Weight transfer: braking loads the nose so it bites and the tail goes
    // light; power squats the rear and pushes the nose wide.
    if (engine < 0 && vlong > 512)       yaw = (yaw * 5) >> 2;
    else if (engine > 8 && vlong > 256)  yaw = (yaw * 7) >> 3;
    // The body has inertia, so it takes a moment to turn in and to settle.
    c->yaw_v += (yaw - c->yaw_v) >> 1;
    yaw = c->yaw_v;
    s32 align = vlat / 4;                            // a sliding car swings toward its path
    yaw += align > 200 ? 200 : align < -200 ? -200 : align;
    c->heading += yaw;

    c->vx = (vlong * fx + vlat * fz) >> 14;
    c->vz = (vlong * fz - vlat * fx) >> 14;

    // Body pitch and roll: the slope underneath plus squat, dive and lean.
    s32 sf = (gx * fx + gz * fz) >> 14, sr = (gx * fz - gz * fx) >> 14;
    s32 a_lat = (vlong * yaw) / 10430;
    spring(&c->pitch, &c->pitch_v, (iatan2(sf, 256) << 8) + engine * 48);
    spring(&c->roll, &c->roll_v, (-iatan2(sr, 256) << 8) - a_lat * 64);

    // Move, then resolve walls.
    s32 nx = c->x + c->vx, nz = c->z + c->vz;
    collide(c, &nx, &nz, 1);
    if (c->mode == CAR_CRASH) {
        c->x = nx; c->z = nz;
        return;
    }

    // Steps too tall to drive up (ramp sides and lips) act as walls.
    s32 hnew = world_height(nx >> 8, nz >> 8);
    if (hnew - c->y > STEP_UP) {
        if (world_height(nx >> 8, c->z >> 8) - c->y <= STEP_UP) {
            nz = c->z; c->vz = -c->vz / 4;
        } else if (world_height(c->x >> 8, nz >> 8) - c->y <= STEP_UP) {
            nx = c->x; c->vx = -c->vx / 4;
        } else {
            nx = c->x; nz = c->z;
            c->vx = -c->vx / 4; c->vz = -c->vz / 4;
        }
        hnew = world_height(nx >> 8, nz >> 8);
    }

    // Cheats: L+R hops, and with fly on, up at speed takes off.
    if ((cheat_hop && (keys & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) ||
        (cheat_fly && (keys & KEY_UP) && vlong > 1200)) {
        c->x = nx; c->z = nz;
        c->vy = cheat_fly ? 260 : 1000;
        c->y += c->vy;
        take_off(c);
        return;
    }

    // The loop: crossing its entry line lined up and moving forward.
    if (at_loop(c, c->z, nx, nz, vlong)) {
        c->x = nx; c->z = nz;
        enter_loop(c, vlong);
        return;
    }

    // Stay glued to the road unless it falls away faster than gravity
    // (with a little suspension droop so gentle crests don't launch you).
    s32 ballistic = c->y + c->vy - G;
    if (hnew < ballistic - 512) {
        c->vy -= G;
        c->y = ballistic;
        take_off(c);
    } else {
        // Climbing speed can't exceed what the steepest ramp allows at this
        // speed, so small ledges are absorbed instead of flinging the car up.
        s32 vy = hnew - c->y;
        s32 vmax = (isqrt(c->vx * c->vx + c->vz * c->vz) * 5) / 8 + 64;
        if (vy > vmax) vy = vmax;
        // Road curving away (crest) unloads the tyres, a dip loads them.
        s32 load = 256 + ((vy - c->vy) * 256) / (G > 0 ? G : 1);
        load = load < 96 ? 96 : load > 352 ? 352 : load;
        c->load += (load - c->load) >> 2;
        c->bob_v -= (vy - c->vy) >> 2;
        c->vy = vy;
        c->y = hnew;
    }
    c->x = nx;
    c->z = nz;

    // Remember flat, dry spots to respawn at.
    if (++c->safe_timer >= 30) {
        c->safe_timer = 0;
        s32 sx = nx >> 8, sz = nz >> 8;
        // In the Stunt Park the lap logic picks the restart point.
        s32 in_park = sx > PARK_X0 && sx < PARK_X1 && sz > PARK_Z0 && sz < PARK_Z1;
        if (!g_track && !in_park && c->mode == CAR_GROUND && hnew == 0 && gx == 0 && gz == 0 &&
            !world_in_water(sx, sz)) {
            c->safe_x = sx;
            c->safe_z = sz;
            c->safe_heading = c->heading;
        }
    }
}

static void fly(Car *c, u16 keys)
{
    // Like a plane: A throttle, B airbrake, up/down pitch, left/right bank and turn.
    s32 lr = (keys & KEY_RIGHT) ? 1 : (keys & KEY_LEFT) ? -1 : 0;
    if (keys & KEY_UP)   c->pitch -= 700;
    if (keys & KEY_DOWN) c->pitch += 700;
    c->pitch = wrap_angle(c->pitch);
    if (c->pitch > (200 << 8)) c->pitch = 200 << 8;
    if (c->pitch < -(200 << 8)) c->pitch = -(200 << 8);
    c->heading += lr * 500;
    c->roll += ((lr * (90 << 8)) - c->roll) / 8;
    if (keys & KEY_A) c->fly_v += 24;
    if (keys & KEY_B) c->fly_v -= 40;
    if (c->boost) c->fly_v += 40;
    c->fly_v -= c->fly_v >> 7;
    if (c->fly_v < 300) c->fly_v = 300;
    s32 h = c->heading >> 6, p = c->pitch >> 8;
    s32 flat = (c->fly_v * icos(p)) >> 14;
    c->vx = (flat * isin(h)) >> 14;
    c->vz = (flat * icos(h)) >> 14;
    c->vy = (c->fly_v * isin(p)) >> 14;
}

static void air_step(Car *c, u16 keys)
{
    s32 tricking = 0;
    if (cheat_fly) {
        fly(c, keys);
        goto move;
    }
    c->vy -= G;
    c->vx -= c->vx >> 9;
    c->vz -= c->vz >> 9;
    if (c->boost) {
        s32 h0 = c->heading >> 6;
        c->vx += isin(h0) >> 11;
        c->vz += icos(h0) >> 11;
    }

    // Tricks: up/down flip, L + left/right barrel roll, B + left/right spin.
    // Plain left/right is a touch of air control.
    s32 lr = (keys & KEY_RIGHT) ? 1 : (keys & KEY_LEFT) ? -1 : 0;
    s32 ud = (keys & KEY_DOWN) ? 1 : (keys & KEY_UP) ? -1 : 0;
    s32 roll_in = (keys & KEY_L) ? lr : 0, spin_in = (keys & KEY_B) ? lr : 0;
    if (!roll_in && !spin_in) c->heading += lr * 40;
    c->air_pv += (ud * 4800 - c->air_pv) / 6;
    c->air_rv += (roll_in * 5200 - c->air_rv) / 6;
    c->air_hv += (spin_in * 1150 - c->air_hv) / 6;
    c->pitch += c->air_pv;  c->trick_p += c->air_pv;
    c->roll += c->air_rv;   c->trick_r += c->air_rv;
    c->heading += c->air_hv; c->trick_h += c->air_hv;
    tricking = ud || roll_in || spin_in;
move:;

    s32 nx = c->x + c->vx, nz = c->z + c->vz;
    if (c->y < (400 << 8)) collide(c, &nx, &nz, 1);   // high enough to clear the rooftops
    c->x = nx; c->z = nz;
    c->y += c->vy;
    if (c->mode == CAR_CRASH) return;
    c->air_steps++;

    // The nose follows the flight path; the body levels out.
    s32 h = c->heading >> 6;
    s32 fx = isin(h), fz = icos(h);
    s32 fwd = (c->vx * fx + c->vz * fz) >> 14;
    s32 path = iatan2(c->vy, fwd < 64 ? 64 : fwd) << 8;
    if (!tricking && !cheat_fly) {
        c->pitch += wrap_angle(path - c->pitch) / 16;
        c->roll -= wrap_angle(c->roll) / 16;
    }

    s32 ux = nx >> 8, uz = nz >> 8;
    if (c->y < (-16 << 8) && world_in_water(ux, uz)) {
        crash(c, EV_SPLASH);
        return;
    }
    s32 hg = world_height(ux, uz);
    if (c->y > hg) return;

    // Landing: how hard did we hit the surface?
    s32 gx, gz;
    gradient(ux, uz, hg, &gx, &gz);
    s32 surf_vy = (c->vx * gx + c->vz * gz) >> 8;
    s32 impact = surf_vy - c->vy;
    s32 sf = (gx * fx + gz * fz) >> 14;
    c->pitch = wrap_angle(c->pitch);
    c->roll = wrap_angle(c->roll);
    s32 tilt = iabs((c->pitch >> 8) - iatan2(sf, 256));
    if (iabs(c->roll >> 8) > tilt) tilt = iabs(c->roll >> 8);
    if (car_crashes && (impact > CRASH_LAND || tilt > 150)) {
        c->y = hg;
        c->landed = impact;
        crash(c, EV_CRASH);
        return;
    }
    c->y = hg;
    c->landed = impact;
    c->vx -= (c->vx * impact) >> 13;
    c->vz -= (c->vz * impact) >> 13;
    c->bob_v -= impact / 2;
    c->pitch_v -= impact / 2;
    if (impact > 700) {
        c->vy = surf_vy + impact / 4;   // bounce
        c->y = hg + 256;
        return;
    }
    c->vy = surf_vy;
    c->mode = CAR_GROUND;
    c->event_c = (iabs(c->trick_p) + 65536) >> 18;    // whole turns, 3/4 counts
    c->event_d = (iabs(c->trick_r) + 65536) >> 18;
    c->event_e = (iabs(c->trick_h) + 16384) >> 16;
    if (c->air_steps > 20 || c->event_c || c->event_d || c->event_e) {
        s32 dx = (c->x - c->launch_x) >> 8, dz = (c->z - c->launch_z) >> 8;
        c->event = impact > 1200 ? EV_BIG_LANDING : EV_JUMP;
        c->event_a = isqrt(dx * dx + dz * dz) / 20;   // metres
        c->event_b = c->air_steps / 6;                 // tenths of a second
    }
}

static void loop_step(Car *c, u16 keys)
{
    const Loop *l = &the_loop;
    s32 len = (l->radius * 411775) >> 8;     // 2 pi R, Q8
    s32 theta = (c->loop_s * 1024) / len;
    s32 v = c->loop_v;

    s32 a = -((G * isin(theta)) >> 14);      // gravity along the track
    if (keys & KEY_A)      a += engine_force(v);
    else if (keys & KEY_B) a -= v > 0 ? BRAKE : 0;
    a -= (v * iabs(v)) >> 21;
    v += a;
    c->loop_s += v;
    c->loop_v = v;
    theta = (c->loop_s * 1024) / len;

    if (c->loop_s >= len) {                  // made it round
        s32 over = c->loop_s - len;
        c->mode = CAR_GROUND;
        c->x = (l->x + l->shift) << 8;
        c->z = (l->z << 8) + over;
        c->y = 0;
        c->vx = 0; c->vy = 0; c->vz = v;
        c->heading = 0;
        c->pitch = 0;
        c->event = EV_LOOP;
        c->event_a = (car_speed(c) * 67) / 2560;   // mph at the exit
        return;
    }
    if (c->loop_s < 0) {                     // rolled back out of the entry
        c->mode = CAR_GROUND;
        c->x = l->x << 8;
        c->z = (l->z << 8) + c->loop_s;
        c->y = 0;
        c->vx = 0; c->vy = 0; c->vz = v;
        c->heading = 0;
        c->pitch = 0;
        return;
    }

    s32 x, y, z;
    loop_point(theta, &x, &y, &z);
    c->x = x; c->y = y; c->z = z;
    c->vx = (v * l->shift * 256) / len;
    c->vz = (v * icos(theta)) >> 14;
    c->vy = (v * isin(theta)) >> 14;
    c->pitch = theta << 8;
    c->roll = 0;
    c->bob = 0;

    // The track can only push, not pull: too slow near the top and you fall.
    s32 normal = (v * v) / (l->radius << 8) + ((G * icos(theta)) >> 14);
    if (normal < 0) {
        crash(c, EV_FELL);
        c->vy -= 400;   // undo the upward throw: the car drops off
    }
}

static void crash_step(Car *c)
{
    c->timer++;
    s32 ux = c->x >> 8, uz = c->z >> 8;
    if (c->y < (-16 << 8) && world_in_water(ux, uz)) {
        // Sinking.
        c->vx -= c->vx >> 3;
        c->vz -= c->vz >> 3;
        c->vy = -48;
        if (c->y < (-70 << 8)) { c->y = -70 << 8; c->vy = 0; }
        c->spin_p -= c->spin_p >> 4;
        c->spin_r -= c->spin_r >> 4;
    } else {
        c->vy -= G;
    }
    s32 nx = c->x + c->vx, nz = c->z + c->vz;
    collide(c, &nx, &nz, 0);
    c->x = nx; c->z = nz;
    c->y += c->vy;
    c->pitch += c->spin_p << 8;
    c->roll += c->spin_r << 8;
    c->heading += c->spin_h;

    s32 hg = world_height(nx >> 8, nz >> 8);
    if (hg > (-100 << 8) && c->y <= hg) {
        // Tumble along the ground, losing energy each bounce.
        c->y = hg;
        c->vy = -c->vy / 3;
        if (c->vy < 2 * G) c->vy = 0;
        c->vx = (c->vx * 3) >> 2;
        c->vz = (c->vz * 3) >> 2;
        c->spin_p = (c->spin_p * 2) / 3;
        c->spin_r = (c->spin_r * 2) / 3;
        c->spin_h = (c->spin_h * 2) / 3;
    }
    if (c->timer > 150) car_respawn(c);
}

// Automatic five-speed box: sets the engine speed the sound and the rev
// counter follow. Gear tops in mph; each gear revs to 7000 at its top.
static const s32 gear_top[6] = { 20, 30, 52, 72, 92, 118 };

static void gearbox(Car *c, u16 keys)
{
    s32 h = c->heading >> 6;
    s32 vlong = c->mode == CAR_LOOP ? c->loop_v : (c->vx * isin(h) + c->vz * icos(h)) >> 14;
    s32 mph = (iabs(vlong) * 67) >> 8;          // tenths of a mph
    s32 gas = (keys & KEY_A) != 0;
    c->throttle = gas;
    if (c->shift_timer > 0) c->shift_timer--;

    s32 target;
    if (c->mode == CAR_CRASH) {
        target = 0;
    } else if (c->mode == CAR_AIR) {
        target = gas ? 7400 : 2200;              // wheels spin free in the air
    } else {
        if (vlong < -32 && !gas) c->gear = 0;
        else if (c->gear == 0) c->gear = 1;
        if (c->gear > 0) {
            if (c->gear < 5 && mph * 10 > gear_top[c->gear] * 96) {
                c->gear++;
                c->shift_timer = 8;
            } else if (c->gear > 1 && mph * 10 < gear_top[c->gear - 1] * 72) {
                c->gear--;
                c->shift_timer = 4;
            }
        }
        target = (mph * 700) / gear_top[c->gear];
        if (gas && target < 3400 && c->gear <= 1) target = 3400 + (c->spin ? 1800 : 0);
        if (target < 900) target = 900;
    }
    if (c->shift_timer > 4) target -= 1600;
    c->rpm += (target - c->rpm) / (target > c->rpm ? 6 : 4);
}

void car_step(Car *c, u16 keys)
{
    c->hit = 0;
    c->landed = 0;
    // Soft-body physics drives the car on the ground and in the air; the loop
    // ride, sinking wrecks and the fly cheat stay on the arcade model.
    if (car_softbody && !cheat_fly && (c->mode == CAR_GROUND || c->mode == CAR_AIR)) {
        s32 z0 = c->z;
        sb_step(c, keys);
        s32 h = c->heading >> 6;
        s32 vlong = (c->vx * isin(h) + c->vz * icos(h)) >> 14;
        if (c->mode == CAR_GROUND && at_loop(c, z0, c->x, c->z, vlong)) {
            enter_loop(c, vlong);
            sb_park();
        }
        gearbox(c, keys);
        return;
    }
    sb_park();
    switch (c->mode) {
    case CAR_GROUND: ground_step(c, keys); break;
    case CAR_AIR:    air_step(c, keys);    break;
    case CAR_LOOP:   loop_step(c, keys);   break;
    default:         crash_step(c);        break;
    }
    // Flying cheats can leave the map; keep the car over it.
    s32 ext = g_track ? TRACK_WORLD : WORLD;
    if (c->x < (16 << 8)) { c->x = 16 << 8; c->vx = -c->vx / 2; }
    if (c->z < (16 << 8)) { c->z = 16 << 8; c->vz = -c->vz / 2; }
    if (c->x > ((ext - 16) << 8)) { c->x = (ext - 16) << 8; c->vx = -c->vx / 2; }
    if (c->z > ((ext - 16) << 8)) { c->z = (ext - 16) << 8; c->vz = -c->vz / 2; }
    if (c->y > (3000 << 8)) { c->y = 3000 << 8; if (c->vy > 0) c->vy = 0; }

    // Suspension travel settles back to rest.
    c->bob_v -= c->bob >> 3;
    c->bob_v -= c->bob_v >> 2;
    c->bob += c->bob_v;
    if (c->bob > (8 << 8)) c->bob = 8 << 8;
    if (c->bob < (-8 << 8)) c->bob = -8 << 8;
    gearbox(c, keys);
}

// ---------------------------------------------------------------- drawing

// Rotation matrix (Q14, columns = car's right, up and forward axes in the
// world): roll, then pitch, then yaw.
static void car_matrix(s32 heading, s32 pitch, s32 roll, s32 *m)
{
    s32 ch = icos(heading), sh = isin(heading);
    s32 cp = icos(pitch), sp = isin(pitch);
    s32 cr = icos(roll), sr = isin(roll);
    for (s32 axis = 0; axis < 3; axis++) {
        s32 x = axis == 0 ? 16384 : 0, y = axis == 1 ? 16384 : 0, z = axis == 2 ? 16384 : 0;
        s32 t;
        t = (x * cr + y * sr) >> 14; y = (y * cr - x * sr) >> 14; x = t;   // roll
        t = (y * cp + z * sp) >> 14; z = (z * cp - y * sp) >> 14; y = t;   // pitch
        t = (x * ch + z * sh) >> 14; z = (z * ch - x * sh) >> 14; x = t;   // yaw
        m[axis * 3 + 0] = x;
        m[axis * 3 + 1] = y;
        m[axis * 3 + 2] = z;
    }
}

// Low-poly sports car: wedge nose, glasshouse, rear wing. Units: x right,
// y up, z forward; about 40 x 30 x 90 (2 x 1.5 x 4.5 m).
static const s8 car_verts[] = {
    -20, 5, -44,   20, 5, -44,   20, 5, 44,   -20, 5, 44,     // 0-3 sills
    -20, 17, -44,  20, 17, -44,  20, 11, 44,  -20, 11, 44,    // 4-7 tail and nose tops
    -20, 17, 12,   20, 17, 12,   -20, 17, -26, 20, 17, -26,   // 8-11 cowl, deck
    -15, 29, -2,   15, 29, -2,   15, 29, -18,  -15, 29, -18,  // 12-15 roof
    -20, 26, -36,  20, 26, -36,  20, 26, -47,  -20, 26, -47,  // 16-19 wing top
    -20, 23, -47,  20, 23, -47,                               // 20-21 wing trailing edge
    -20, 26, -34,  -20, 17, -47, -20, 17, -34,                // 22-24 left endplate
    20, 26, -34,   20, 17, -47,  20, 17, -34,                 // 25-27 right endplate
    -18, 15, -44,  -7, 15, -44,  -7, 10, -44,  -18, 10, -44,  // 28-31 tail lights
    7, 15, -44,    18, 15, -44,  18, 10, -44,  7, 10, -44,
    -18, 12, 38,   -9, 12, 38,   -9, 14, 28,   -18, 14, 28,   // 36-43 headlights
    9, 12, 38,     18, 12, 38,   18, 14, 28,   9, 14, 28,
    -20, 9, -44,   20, 9, -44,                                // 44-45 top of the rear bumper
};
#define CF(n, m, s) (n), COLOR(m, s)
#define DECAL MESH_DECAL   // drawn straight after the face before it
static const u8 car_faces[] = {
    CF(4, M_CAR, 0),        7, 6, 9, 8,         // hood
    CF(4 | DECAL, M_STUNT_WHITE, 0), 36, 37, 38, 39,
    CF(4 | DECAL, M_STUNT_WHITE, 0), 40, 41, 42, 43,
    CF(4, M_CAR, 0),        10, 11, 5, 4,       // rear deck
    CF(4, M_CAR, 0),        12, 13, 14, 15,     // roof
    CF(4, M_GLASS, 0),      13, 12, 8, 9,       // windshield
    CF(4, M_GLASS, 1),      15, 14, 11, 10,     // rear window
    CF(4, M_GLASS, 2),      12, 15, 10, 8,      // left side glass
    CF(4, M_GLASS, 3),      14, 13, 9, 11,      // right side glass
    CF(5, M_CAR, 2),        7, 8, 4, 0, 3,      // left flank
    CF(5, M_CAR, 3),        5, 9, 6, 2, 1,      // right flank
    CF(4, M_CAR_TRIM, 1),   6, 7, 3, 2,         // nose
    CF(4, M_CAR, 1),        4, 5, 1, 0,         // tail panel
    CF(4 | DECAL, M_STUNT_RED, 0), 28, 29, 30, 31,
    CF(4 | DECAL, M_STUNT_RED, 0), 32, 33, 34, 35,
    CF(4 | DECAL, M_CAR_TRIM, 1), 44, 45, 1, 0,  // rear bumper
    CF(4, M_CAR_TRIM, 3),   2, 3, 0, 1,         // floor
    CF(4, M_CAR, 0),        16, 17, 18, 19,     // wing top
    CF(4, M_CAR_TRIM, 1),   19, 18, 21, 20,     // wing back
    CF(4, M_CAR, 2),        17, 16, 20, 21,     // wing underside
    CF(4, M_CAR, 2),        22, 19, 23, 24,     // left endplate
    CF(4, M_CAR, 3),        18, 25, 27, 26,     // right endplate
};
#define CAR_FACES 22
static const Mesh car_mesh = { car_verts, car_faces, sizeof(car_verts) / 3, CAR_FACES };

// The ghost car: the same shape in pale blue.
EWRAM_BSS static u8 ghost_faces[sizeof(car_faces)];
static const Mesh ghost_mesh = { car_verts, ghost_faces, sizeof(car_verts) / 3, CAR_FACES };

static void make_ghost(void)
{
    const u8 *f = car_faces;
    u8 *g = ghost_faces;
    for (s32 i = 0; i < CAR_FACES; i++) {
        s32 n = (f[0] & 7) + 2;
        for (s32 k = 0; k < n; k++) g[k] = f[k];
        u32 shade = f[1] & 3;
        g[1] = COLOR(M_GLASS, shade < 2 ? 0 : 1);
        if (f[1] / 4 == M_GLASS) g[1] = COLOR(M_SPLASH, shade);
        f += n;
        g += n;
    }
}

static void wheel(s32 x, s32 y, s32 z, const s32 *m, s32 lx, s32 lz)
{
    r_box_mat(x, y, z, m, lx - 4, 0, lz - 8, lx + 4, 13, lz + 8, M_TIRE);
}

// Blob shadow on whatever is under the car: drawn straight onto flat
// ground, or as a decal on ramps and the bank.
static void draw_shadow(const Car *c, s32 x, s32 z, s32 h)
{
    static const s8 shape[6][2] = { { -17, 46 }, { 17, 46 }, { 24, 0 }, { 17, -46 }, { -17, -46 }, { -24, 0 } };
    if (world_in_water(x, z)) return;
    s32 ground = world_height(x, z);
    s32 above = (c->y >> 8) - (ground >> 8);
    if (above > 400) return;
    s32 size = 256 - above / 3;
    s32 fx = isin(h), fz = icos(h), flat = 1;
    Vec3 q[6];
    for (s32 i = 0; i < 6; i++) {
        s32 lx = (shape[i][0] * size) >> 8, lz = (shape[i][1] * size) >> 8;
        q[i].x = x + ((lx * fz + lz * fx) >> 14);
        q[i].z = z + ((lz * fz - lx * fx) >> 14);
        s32 g = g_track ? ground : world_height(q[i].x, q[i].z);   // circuits: flat across the car
        if (g < 0) g = 0;
        q[i].y = (g >> 8) + (g ? 2 : 0);
        if (g) flat = 0;
    }
    if (flat) r_ground(q, 6, COLOR(M_SHADOW, 0));
    else      r_face(q, 6, COLOR(M_SHADOW, 0), RF_DECAL);
}

// CPU rivals: the same car in another colour, with a cheaper version far off.
#define RIVAL_COLORS 7
EWRAM_BSS static u8 rival_faces[RIVAL_COLORS][sizeof(car_faces)];
static const u8 rival_mats[RIVAL_COLORS] = { M_STUNT_WHITE, M_BLD2, M_LINE, M_BLD5, M_BLD4, M_GLASS, M_BLD0 };

void car_draw_rival(s32 x, s32 y, s32 z, s32 heading, s32 pitch, s32 color, s32 depth)
{
    color %= RIVAL_COLORS;
    u8 *g = rival_faces[color];
    if (!g[0]) {
        const u8 *f = car_faces;
        for (s32 i = 0; i < CAR_FACES; i++) {
            s32 n = (f[0] & 7) + 2;
            for (s32 k = 0; k < n; k++) g[k] = f[k];
            if (f[1] / 4 == M_CAR) g[1] = COLOR(rival_mats[color], f[1] & 3);
            f += n;
            g += n;
        }
        g = rival_faces[color];
    }
    s32 m[9];
    car_matrix(heading >> 6, pitch, 0, m);
    if (depth < 320) {
        if (depth < 200) {
            wheel(x, y, z, m, -17, -30);
            wheel(x, y, z, m, 17, -30);
            wheel(x, y, z, m, -17, 30);
            wheel(x, y, z, m, 17, 30);
        }
        const Mesh mesh = { car_verts, g, sizeof(car_verts) / 3, CAR_FACES };
        r_mesh(x, y, z, m, &mesh);
    } else {
        r_box_mat(x, y, z, m, -20, 4, -44, 20, 16, 44, rival_mats[color]);
        if (depth < 600) r_box_mat(x, y, z, m, -15, 16, -20, 15, 28, 0, M_GLASS);
    }
}

void car_draw(const Car *c, s32 ghost)
{
    s32 x = c->x >> 8, y = (c->y + c->bob) >> 8, z = c->z >> 8;
    s32 h = c->heading >> 6;

    if (c->mode != CAR_LOOP && !ghost) draw_shadow(c, x, z, h);

    s32 m[9];
    car_matrix(h, c->pitch >> 8, c->roll >> 8, m);
    if (ghost) {
        if (!ghost_faces[0]) make_ghost();
        r_mesh(x, y, z, m, &ghost_mesh);
        return;
    }
    if (sb_valid(c)) {
        sb_draw(c, &car_mesh);
        return;
    }
    wheel(x, y, z, m, -17, -30);
    wheel(x, y, z, m, 17, -30);
    wheel(x, y, z, m, -17, 30);
    wheel(x, y, z, m, 17, 30);
    r_mesh(x, y, z, m, &car_mesh);
}
