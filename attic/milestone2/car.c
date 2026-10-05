// Car physics: engine and drag along the car, tyre grip across it (so the
// car drifts when it asks for more than the tyres have), gravity on slopes,
// ballistic flight off ramps, the vertical loop, and crashes.
//
// Scale: 20 world units ~ 1 metre, one step = 1/60 s, gravity is 2.5 g
// (arcade-heavy so jumps land inside the city).
#include "car.h"
#include "world.h"

#define G           35      // gravity, Q8 units per step^2
#define GRIP        38      // max sideways velocity change per step (~1.1 g)
#define HAND_GRIP   12      // rear grip with the handbrake on
#define A0          16      // full-throttle acceleration at low speed
#define POWER       28000   // engine power: accel = POWER / speed above A0
#define BRAKE       31
#define REV_ACC     10
#define REV_MAX     (-1024)
#define CRASH_WALL  1700    // sideways-into-wall speed that wrecks the car (~45 mph)
#define CRASH_LAND  1800    // landing impact that wrecks the car
#define CAR_R       28      // collision radius
#define STEP_UP     (10 << 8)
#define LOOP_ALIGN  4551    // must enter the loop within 25 degrees of straight

static u32 rng = 12345;
static s32 rnd(s32 n) { rng = rng * 1103515245 + 12345; return (rng >> 16) % n; }

static s32 iabs(s32 v) { return v < 0 ? -v : v; }

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
    c->heading = heading;
    c->safe_x = x;
    c->safe_z = z;
    c->safe_heading = heading;
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
    if (can_crash && -vn > CRASH_WALL) {
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
    if (keys & KEY_A)
        engine = vlong < -64 ? BRAKE : engine_force(vlong);
    else if (keys & KEY_B)
        engine = vlong > 64 ? -BRAKE : (vlong > REV_MAX ? -REV_ACC : 0);
    else if (vlong > 0)
        engine = vlong > 3 ? -3 : -vlong;
    else if (vlong < 0)
        engine = vlong < -3 ? 3 : -vlong;
    along += engine;
    along -= (vlong * iabs(vlong)) >> 21;   // air drag
    along -= vlong >> 10;                   // rolling resistance
    vlong += along;

    // Tyres cancel sideways motion up to their grip; beyond that the car slides.
    s32 hand = keys & KEY_L;
    s32 grip = hand ? HAND_GRIP : GRIP;
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

    // The loop: crossing its entry line lined up and moving forward.
    const Loop *l = &the_loop;
    if ((c->z >> 8) < l->z && (nz >> 8) >= l->z && iabs((nx >> 8) - l->x) < l->width / 2 &&
        iabs((s16)c->heading) < LOOP_ALIGN && vlong > 256) {
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
        if (sx > PARK_X0 && sx < PARK_X1 && sz > PARK_Z0 && sz < PARK_Z1) {
            // In the Stunt Park you restart from the start line.
            c->safe_x = the_loop.x;
            c->safe_z = PARK_Z0 + 90;
            c->safe_heading = 0;
        } else if (c->mode == CAR_GROUND && hnew == 0 && gx == 0 && gz == 0 &&
                   !world_in_water(sx, sz)) {
            c->safe_x = sx;
            c->safe_z = sz;
            c->safe_heading = c->heading;
        }
    }
}

static void air_step(Car *c, u16 keys)
{
    c->vy -= G;
    c->vx -= c->vx >> 9;
    c->vz -= c->vz >> 9;
    if (keys & KEY_LEFT)  c->heading -= 40;   // a touch of air control
    if (keys & KEY_RIGHT) c->heading += 40;

    s32 nx = c->x + c->vx, nz = c->z + c->vz;
    collide(c, &nx, &nz, 1);
    c->x = nx; c->z = nz;
    c->y += c->vy;
    if (c->mode == CAR_CRASH) return;
    c->air_steps++;

    // The nose follows the flight path; the body levels out.
    s32 h = c->heading >> 6;
    s32 fx = isin(h), fz = icos(h);
    s32 fwd = (c->vx * fx + c->vz * fz) >> 14;
    s32 path = iatan2(c->vy, fwd < 64 ? 64 : fwd) << 8;
    c->pitch += (path - c->pitch) / 16;
    c->roll -= c->roll / 16;

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
    s32 tilt = iabs((c->pitch >> 8) - iatan2(sf, 256));
    if (impact > CRASH_LAND || tilt > 150) {
        c->y = hg;
        crash(c, EV_CRASH);
        return;
    }
    c->y = hg;
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
    if (c->air_steps > 20) {
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

void car_step(Car *c, u16 keys)
{
    switch (c->mode) {
    case CAR_GROUND: ground_step(c, keys); break;
    case CAR_AIR:    air_step(c, keys);    break;
    case CAR_LOOP:   loop_step(c, keys);   break;
    default:         crash_step(c);        break;
    }
    // Suspension travel settles back to rest.
    c->bob_v -= c->bob >> 3;
    c->bob_v -= c->bob_v >> 2;
    c->bob += c->bob_v;
    if (c->bob > (8 << 8)) c->bob = 8 << 8;
    if (c->bob < (-8 << 8)) c->bob = -8 << 8;
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

void car_draw(const Car *c)
{
    s32 x = c->x >> 8, y = (c->y + c->bob) >> 8, z = c->z >> 8;
    s32 h = c->heading >> 6;

    // Shadow on flat ground while flying.
    if ((c->mode == CAR_AIR || c->mode == CAR_CRASH) && c->y > (4 << 8) &&
        world_height(x, z) == 0 && !world_in_water(x, z)) {
        s32 fx = isin(h), fz = icos(h);
        Vec3 q[4];
        static const s8 corner[4][2] = { { -20, 42 }, { 20, 42 }, { 20, -42 }, { -20, -42 } };
        for (s32 i = 0; i < 4; i++) {
            s32 lx = corner[i][0], lz = corner[i][1];
            q[i].x = x + ((lx * fz + lz * fx) >> 14);
            q[i].y = 0;
            q[i].z = z + ((lz * fz - lx * fx) >> 14);
        }
        r_ground(q, 4, COLOR(M_SHADOW, 0));
    }

    s32 m[9];
    car_matrix(h, c->pitch >> 8, c->roll >> 8, m);
    r_box_mat(x, y, z, m, -22, 0, -30, -12, 14, -14, M_TIRE);
    r_box_mat(x, y, z, m, 12, 0, -30, 22, 14, -14, M_TIRE);
    r_box_mat(x, y, z, m, -22, 0, 18, -12, 14, 34, M_TIRE);
    r_box_mat(x, y, z, m, 12, 0, 18, 22, 14, 34, M_TIRE);
    r_box_mat(x, y, z, m, -20, 5, -42, 20, 20, 44, M_CAR);
    r_box_mat(x, y, z, m, -16, 20, -20, 16, 33, 12, M_GLASS);
    r_box_mat(x, y, z, m, -19, 24, -44, 19, 28, -36, M_CAR_TRIM);

    if (c->mode == CAR_CRASH) {
        if (c->crash_kind == EV_SPLASH) {
            if (c->timer < 70) {
                s32 s = 20 + c->timer / 2, hgt = c->timer < 35 ? c->timer * 3 : (70 - c->timer) * 3;
                r_box(x - s, 0, z - s, x + s, hgt, z + s, M_SPLASH);
            }
        } else if (c->timer < 90) {
            s32 s = 8 + c->timer / 2;
            if (c->timer < 45)
                r_box(x - s, y, z - s, x + s, y + s * 2, z + s, M_FIRE);
            r_box(x - s / 2, y + s * 2, z - s / 2, x + s / 2, y + s * 3, z + s / 2, M_SMOKE);
        }
    }
}
