// Particles live in world space (Q4 units) and are drawn as sprites, so
// smoke can be see-through. Skid marks are dark quads laid on the road.
#include "fx.h"
#include "world.h"
#include "hud.h"

enum { P_SMOKE, P_DUST, P_FIRE, P_BLACK, P_SPRAY, P_SPARK, P_DROP, P_TYPES };

typedef struct {
    u8 life;          // steps
    u8 size0, size1;  // radius in world units at birth and death
    s8 lift;          // Q4 units per step^2, up positive
    u8 shape, pal, blend, bounce;
} Kind;

static const Kind kinds[P_TYPES] = {
    [P_SMOKE] = { 40, 6, 26, 1, SPR_PUFF, PAL_SMOKE, 1, 0 },
    [P_DUST]  = { 28, 5, 19, 0, SPR_PUFF, PAL_DUST, 1, 0 },
    [P_FIRE]  = { 22, 9, 17, 3, SPR_PUFF, PAL_FIRE, 1, 0 },
    [P_BLACK] = { 70, 12, 44, 2, SPR_PUFF, PAL_DARK_SMOKE, 1, 0 },
    [P_SPRAY] = { 34, 10, 34, -1, SPR_PUFF, PAL_SPLASH, 1, 0 },
    [P_SPARK] = { 20, 3, 3, -3, SPR_SPARK, PAL_SPARK, 0, 1 },
    [P_DROP]  = { 40, 3, 3, -3, SPR_DROP, PAL_SPLASH, 0, 0 },
};

#define MAX_PARTICLES 44
typedef struct {
    s32 x, y, z;          // Q4
    s16 vx, vy, vz;       // Q4 per step
    u8 type, age;
} Particle;

static Particle parts[MAX_PARTICLES];
static s32 next_part;

#define MAX_SKIDS 48
typedef struct { s16 x[4], z[4]; } Skid;
static Skid skids[MAX_SKIDS] EWRAM_BSS;
static s32 skid_count, next_skid;
static s32 last_wx[2], last_wz[2], skid_live;

static s32 prev_mode, prev_shift, tick;
static u32 seed = 777;

static s32 rnd(s32 n)
{
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) % n;
}

void fx_reset(void)
{
    for (s32 i = 0; i < MAX_PARTICLES; i++) parts[i].age = 255;
    skid_count = 0;
    next_skid = 0;
    skid_live = 0;
    prev_mode = 0;
}

// x, y, z in world units; velocities in Q4 units per step.
static void emit(s32 type, s32 x, s32 y, s32 z, s32 vx, s32 vy, s32 vz)
{
    Particle *p = &parts[next_part];
    next_part = next_part + 1 == MAX_PARTICLES ? 0 : next_part + 1;
    p->x = x << 4; p->y = y << 4; p->z = z << 4;
    p->vx = vx; p->vy = vy; p->vz = vz;
    p->type = type;
    p->age = 0;
}

// A point on the car body, in world units: lx right, ly up, lz forward.
static void car_point(const Car *c, s32 lx, s32 ly, s32 lz, s32 *x, s32 *y, s32 *z)
{
    s32 h = c->heading >> 6, fx = isin(h), fz = icos(h);
    *x = (c->x >> 8) + ((lx * fz + lz * fx) >> 14);
    *z = (c->z >> 8) + ((lz * fz - lx * fx) >> 14);
    *y = (c->y >> 8) + ly;
}

static void burst(s32 type, s32 count, s32 x, s32 y, s32 z, s32 speed, s32 up)
{
    for (s32 i = 0; i < count; i++)
        emit(type, x + rnd(17) - 8, y, z + rnd(17) - 8,
             rnd(speed * 2 + 1) - speed, up + rnd(up / 2 + 1), rnd(speed * 2 + 1) - speed);
}

static void lay_skids(const Car *c)
{
    for (s32 w = 0; w < 2; w++) {
        s32 x, y, z;
        car_point(c, w ? 17 : -17, 0, -30, &x, &y, &z);
        if (skid_live) {
            s32 h = c->heading >> 6, rx = icos(h), rz = -isin(h);   // the car's right
            s32 ox = (rx * 3) >> 14, oz = (rz * 3) >> 14;
            Skid *s = &skids[next_skid];
            next_skid = next_skid + 1 == MAX_SKIDS ? 0 : next_skid + 1;
            if (skid_count < MAX_SKIDS) skid_count++;
            // Clockwise from above: back-left, front-left, front-right, back-right.
            s->x[0] = last_wx[w] - ox; s->z[0] = last_wz[w] - oz;
            s->x[1] = x - ox;          s->z[1] = z - oz;
            s->x[2] = x + ox;          s->z[2] = z + oz;
            s->x[3] = last_wx[w] + ox; s->z[3] = last_wz[w] + oz;
        }
        last_wx[w] = x;
        last_wz[w] = z;
    }
    skid_live = 1;
}

void fx_step(const Car *c)
{
    tick++;
    s32 speed = car_speed(c);
    s32 cx = c->x >> 8, cy = c->y >> 8, cz = c->z >> 8;

    if (c->mode == CAR_GROUND) {
        s32 sliding = (c->skid && speed > 500) || c->spin;
        // Tyre smoke from the rear wheels; a launch only puffs a little.
        if (sliding && (tick & (c->spin ? 7 : 3)) == 0) {
            s32 x, y, z;
            car_point(c, (tick & 4) ? 17 : -17, 4, -32, &x, &y, &z);
            emit(P_SMOKE, x, y, z, (c->vx >> 6) / 2, 4, (c->vz >> 6) / 2);
        }
        // Rubber on the road: only on flat ground.
        s32 flat = c->y < 256 && world_height(cx, cz) == 0;
        if (sliding && flat) {
            if ((tick & 3) == 0) lay_skids(c);
        } else {
            skid_live = 0;
        }
        // Dust off the grass.
        if (speed > 640 && (tick & 3) == 2 && world_surface(cx, cz) == SURF_GRASS) {
            s32 x, y, z;
            car_point(c, rnd(41) - 20, 2, -36, &x, &y, &z);
            emit(P_DUST, x, y, z, (c->vx >> 6) / 3, 6, (c->vz >> 6) / 3);
        }
        // Nitro flames out of both exhausts.
        if (c->boost && (tick & 1)) {
            s32 x, y, z;
            car_point(c, (tick & 2) ? 10 : -10, 8, -50, &x, &y, &z);
            emit(P_FIRE, x, y, z, -(c->vx >> 7), 1, -(c->vz >> 7));
        }
        // A backfire when the box changes up under full throttle.
        if (c->shift_timer == 8 && prev_shift != 8 && c->throttle) {
            s32 x, y, z;
            car_point(c, 10, 8, -48, &x, &y, &z);
            emit(P_FIRE, x, y, z, 0, 2, 0);
        }
    } else {
        skid_live = 0;
    }
    prev_shift = c->shift_timer;

    if (c->landed > 380) {
        s32 n = c->landed > 900 ? 8 : 5;
        for (s32 i = 0; i < n; i++) {
            s32 a = (i * 1024) / n;
            emit(P_DUST, cx + ((isin(a) * 34) >> 14), cy + 2, cz + ((icos(a) * 44) >> 14),
                 isin(a) >> 10, 4, icos(a) >> 10);
        }
        if (c->landed > 1000) burst(P_SPARK, 6, cx, cy + 3, cz, 24, 20);
    }
    if (c->hit > 260) {
        s32 n = c->hit / 220;
        burst(P_SPARK, n > 7 ? 7 : n, cx, cy + 8, cz, 28, 16);
    }

    // Crashes and splashes.
    if (c->mode == CAR_CRASH) {
        if (prev_mode != CAR_CRASH) {
            if (c->crash_kind == EV_SPLASH) {
                burst(P_DROP, 14, cx, 0, cz, 22, 48);
                burst(P_SPRAY, 4, cx, 4, cz, 10, 10);
            } else {
                burst(P_SPARK, 10, cx, cy + 6, cz, 36, 28);
                burst(P_FIRE, 3, cx, cy + 10, cz, 6, 6);
            }
        } else if (c->crash_kind != EV_SPLASH) {
            if (c->timer < 50 && (tick % 5) == 0) emit(P_FIRE, cx + rnd(21) - 10, cy + 12, cz + rnd(21) - 10, 0, 4, 0);
            if (c->timer < 130 && (tick % 7) == 0) emit(P_BLACK, cx + rnd(21) - 10, cy + 20, cz + rnd(21) - 10, 2, 6, 1);
        } else if (c->timer < 40 && (tick & 7) == 0) {
            emit(P_SPRAY, cx + rnd(31) - 15, 2, cz + rnd(31) - 15, 0, 4, 0);
        }
    }
    prev_mode = c->mode;

    // Move everything.
    for (s32 i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &parts[i];
        if (p->age == 255) continue;
        const Kind *k = &kinds[p->type];
        if (++p->age >= k->life) { p->age = 255; continue; }
        p->vy += k->lift;
        if (k->shape == SPR_PUFF) {          // puffs slow down in the air
            p->vx -= p->vx / 8;
            p->vz -= p->vz / 8;
            p->vy -= p->vy / 16;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        if (p->y < 0) {
            p->y = 0;
            if (k->bounce) p->vy = -p->vy / 2;
            else if (p->type == P_DROP) p->age = 255;
            else p->vy = 0;
        }
    }
}

void fx_draw_ground(void)
{
    for (s32 i = 0; i < skid_count; i++) {
        const Skid *s = &skids[i];
        s32 side, depth = r_depth(s->x[1], s->z[1], &side);
        if (depth < -40 || depth > 900) continue;
        Vec3 q[4];
        for (s32 k = 0; k < 4; k++) { q[k].x = s->x[k]; q[k].y = 0; q[k].z = s->z[k]; }
        r_ground(q, 4, COLOR(M_ASPHALT, 3));
    }
}

void fx_draw_sprites(void)
{
    // Project, then emit nearest first so near puffs cover far ones.
    s16 order[MAX_PARTICLES], sx[MAX_PARTICLES], sy[MAX_PARTICLES], rad[MAX_PARTICLES];
    s32 depth[MAX_PARTICLES], n = 0;
    for (s32 i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &parts[i];
        if (p->age == 255) continue;
        const Kind *k = &kinds[p->type];
        s32 x, y, d = r_project(p->x >> 4, p->y >> 4, p->z >> 4, &x, &y);
        if (d <= 0 || d > 900) continue;
        s32 size = k->size0 + ((k->size1 - k->size0) * p->age) / k->life;
        s32 r = (size * FOCAL) / d;
        if (x + r < 0 || x - r >= SCREEN_W || y + r < 0 || y - r >= SCREEN_H) continue;
        s32 j = n++;
        while (j > 0 && depth[order[j - 1]] > d) { order[j] = order[j - 1]; j--; }
        order[j] = i;
        depth[i] = d;
        sx[i] = x; sy[i] = y; rad[i] = r;
    }
    for (s32 j = 0; j < n; j++) {
        s32 i = order[j];
        const Kind *k = &kinds[parts[i].type];
        s32 pal = k->pal;
        // Fire cools to smoke as it ages.
        if (parts[i].type == P_FIRE && parts[i].age > k->life / 2) pal = PAL_DARK_SMOKE;
        hud_particle(sx[i], sy[i], rad[i], k->shape, pal, k->blend);
    }
}
