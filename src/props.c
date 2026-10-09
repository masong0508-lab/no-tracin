// Loose things in Freedom City: traffic cones, oil drums, crates and concrete
// blocks. Each is a small rigid body (an upright cylinder for collisions,
// with a heading and a tip-over angle for looks) that sleeps until something
// touches it.
#include "props.h"
#include "softbody.h"
#include "world.h"
#include "track.h"
#include "game.h"
#include "hud.h"
#include "sound.h"
#include "tune.h"

enum { K_CONE, K_DRUM, K_CRATE, K_BLOCK, K_COUNT };
enum { POLE, BOX, SLAB };      // tips over and lies down / tumbles / only slides

typedef struct {
    u8 radius, half_h, mass, shape;
    u8 tip;                    // speed change (Q8 per step) that knocks it over
    u8 score;
    u8 draw_far;               // drawn out to this depth, in 4-unit steps
} Kind;

static const Kind kinds[K_COUNT] = {
    [K_CONE]  = { 5, 6, 1, POLE, 40, 10, 60 },
    [K_DRUM]  = { 6, 8, 4, POLE, 70, 25, 80 },
    [K_CRATE] = { 8, 8, 3, BOX, 40, 30, 110 },
    [K_BLOCK] = { 10, 8, 24, SLAB, 255, 0, 110 },
};

typedef struct {
    s32 p[3], v[3];            // centre and velocity, Q8
    s16 yaw, spin;             // 65536 per turn
    s16 tilt, tilt_v;          // 0 upright; 16384 on its side
    s16 fall;                  // the heading it tips toward
    u8 kind, awake, still, down;
} Loose;

#define MAX_PROPS 124           // 60 of the city's own, the rest for the map editor (cells hold s8)
static Loose props[MAX_PROPS] EWRAM_BSS;
static s32 nprops EWRAM_BSS, smashed EWRAM_BSS, burst EWRAM_BSS, burst_n EWRAM_BSS, burst_timer EWRAM_BSS;

// Who is near whom: props hashed into 32-unit cells each step.
#define GRID 64
static s8 cell_head[GRID] EWRAM_BSS, cell_next[MAX_PROPS] EWRAM_BSS;
static s32 cell_of(s32 cx, s32 cz) { return (cx * 31 + cz * 17) & (GRID - 1); }

static s32 iabs(s32 v) { return v < 0 ? -v : v; }
static s32 clamp(s32 v, s32 lim) { return v > lim ? lim : v < -lim ? -lim : v; }

s32 props_smashed(void) { return smashed; }

// A kind's weight with the physics lab's PROP WEIGHT slider.
static s32 mass_of(const Kind *k) { s32 m = (k->mass * tn.prop_m) >> 8; return m > 0 ? m : 1; }

static void add(s32 kind, s32 x, s32 z, s32 y, s32 yaw)
{
    if (nprops >= MAX_PROPS) return;
    Loose *o = &props[nprops++];
    o->p[0] = x << 8;
    o->p[2] = z << 8;
    o->p[1] = world_height(x, z) + ((y + kinds[kind].half_h) << 8);
    o->v[0] = o->v[1] = o->v[2] = 0;
    o->yaw = yaw;
    o->spin = o->tilt = o->tilt_v = o->fall = 0;
    o->kind = kind;
    o->awake = o->still = o->down = 0;
}

// A pyramid of crates, rows of n, n-1 ... 1, stacked across the x axis.
static void pyramid(s32 x, s32 z, s32 n)
{
    for (s32 row = 0; row < n; row++)
        for (s32 i = 0; i < n - row; i++)
            add(K_CRATE, x + (i * 2 - (n - row - 1)) * 8, z, row * 16, 0);
}

s32 props_add(s32 kind, s32 x, s32 z, s32 y, s32 heading)
{
    if (nprops >= MAX_PROPS || kind < 0 || kind >= K_COUNT) return 0;
    add(kind, x, z, y, heading);
    return 1;
}

void props_reset(void)
{
    nprops = smashed = burst = burst_n = burst_timer = 0;
    if (g_track) return;
    // Stunt Park. A crate pyramid just past the start, beside lane A (crates
    // pushed all the way up the lane cost the speed the loop needs), a cone
    // slalom the other side, drums along the canal and concrete blocks.
    pyramid(LOOP_X + 150, START_Z + 233, 4);
    for (s32 i = 0; i < 10; i++) add(K_CONE, LOOP_X - 120 + ((i & 1) ? 24 : -24), START_Z + 73 + i * 64, 0, 0);
    for (s32 i = 0; i < 7; i++) add(K_CONE, LOOP_X + 60 + i * 16, LOOP_Z - 130, 0, i * 3000);
    pyramid(PX(680), START_Z + 380, 3);
    for (s32 i = 0; i < 6; i++) add(K_DRUM, PX(780) + i * 15, CANAL_Z0 - 90, 0, i * 9000);
    for (s32 i = 0; i < 3; i++) add(K_BLOCK, LOOP_X - 190 + i * 22, LOOP_Z + 30, 0, 0);
    // Out on the streets: another pyramid in a lane, a line of cones along
    // the street north of the park and drums on a corner of a crossing.
    pyramid(BLOCK * 4 + LANE / 2, BLOCK * 2 + 700, 3);
    for (s32 i = 0; i < 8; i++) add(K_CONE, BLOCK + 360 + i * 40, BLOCK * 4 + LANE / 2, 0, 0);
    for (s32 i = 0; i < 4; i++) add(K_DRUM, BLOCK * 6 - 8 + (i & 1) * 15, BLOCK * 4 - 8 + (i >> 1) * 15, 0, i * 7000);
}

static char *put_num(char *p, s32 v)
{
    char t[8];
    s32 n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v && n < 8);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

// Something hit it with impulse j.
static void knock(Loose *o, s32 j)
{
    const Kind *k = &kinds[o->kind];
    o->awake = 1;
    o->still = 0;
    s32 dv = j / mass_of(k);
    if (dv <= k->tip) return;
    if (dv > 1500) dv = 1500;
    s32 vx = o->v[0], vz = o->v[2], odd = o->p[0] ^ o->p[2];
    if ((vx || vz) && (!o->tilt || (k->shape == POLE && o->tilt < 2048))) o->fall = iatan2(vx, vz) << 6;
    if (k->shape == POLE && o->tilt < 4096) o->tilt_v = 700 + (dv << 1);
    if (k->shape == BOX) o->tilt_v = clamp(o->tilt_v + (dv << 2) * ((odd & 256) ? 1 : -1), 8000);
    o->spin = clamp(o->spin + dv * ((odd & 512) ? 6 : -6), 12000);
    if (!o->down && k->score) {
        o->down = 1;
        smashed++;
        burst = burst_timer > 0 ? burst + k->score : k->score;
        burst_n = burst_timer > 0 ? burst_n + 1 : 1;
        burst_timer = 90;
        g_game.score += k->score;
        g_game.nitro += k->score;
        if (g_game.nitro > NITRO_MAX) g_game.nitro = NITRO_MAX;
        char a[16], b[16], *p = a;
        const char *s = burst_n > 1 ? "SMASH X" : "SMASH!";
        while (*s) *p++ = *s++;
        *p = 0;
        if (burst_n > 1) put_num(p, burst_n);
        b[0] = '+';
        put_num(b + 1, burst);
        game_message(a, b, PAL_YELLOW, 90);
    }
}

// Two loose things bumping into each other, as upright cylinders. A
// sleeping one stays put (it is resting on something) unless hit hard.
static void bump(Loose *a, Loose *b)
{
    const Kind *ka = &kinds[a->kind], *kb = &kinds[b->kind];
    s32 r = (ka->radius + kb->radius) << 8, hh = (ka->half_h + kb->half_h) << 8;
    s32 dx = b->p[0] - a->p[0], dy = b->p[1] - a->p[1], dz = b->p[2] - a->p[2];
    if (iabs(dy) >= hh + (3 << 8) || iabs(dx) >= r + (3 << 8) || iabs(dz) >= r + (3 << 8)) return;
    // Distances in Q4, squared, so the square root is only taken when needed.
    s32 l2 = (dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4), r4 = r >> 4, near = r4 + 48;
    s32 sp = iabs(a->v[0]) + iabs(a->v[1]) + iabs(a->v[2]);
    if (!b->awake && sp > 64 && l2 < near * near) { b->awake = 1; b->still = 0; }   // the pile shifts
    if (iabs(dy) >= hh || l2 >= r4 * r4) return;
    s32 n[3], pen, py = hh - iabs(dy), side = r4 - (py >> 4);
    if (side <= 0 || l2 > side * side || l2 < 16 * 16) {
        // Mostly on top of each other: push apart vertically.
        n[0] = 0; n[1] = dy < 0 ? -16384 : 16384; n[2] = 0;
        pen = py;
    } else {
        s32 l = isqrt((u32)l2), inv = (1 << 24) / l;                // Q4
        n[0] = ((dx >> 4) * inv) >> 10; n[1] = 0; n[2] = ((dz >> 4) * inv) >> 10;
        pen = r - (l << 4);
    }
    s32 ma = mass_of(ka), mb = mass_of(kb);
    s32 rel = ((a->v[0] - b->v[0]) * n[0] + (a->v[1] - b->v[1]) * n[1] + (a->v[2] - b->v[2]) * n[2]) >> 14;
    if (!b->awake) {
        // Something it rests on, or a gentle nudge: it stays put. One resting
        // on top of this, or a hard knock, wakes it.
        if (rel > 48 || n[1] > 0) { b->awake = 1; b->still = 0; }
        else mb = 0;
    }
    s32 wa = mb ? (mb << 8) / (ma + mb) : 256, wb = 256 - wa;
    for (s32 k = 0; k < 3; k++) {
        s32 m = (n[k] * (pen >> 4)) >> 10;                 // Q8
        a->p[k] -= (m * wa) >> 8;
        b->p[k] += (m * wb) >> 8;
    }
    if (rel <= 0) return;
    s32 dv = rel > 64 ? rel * (200 + tn.prop_bounce) / 200 : rel;   // bouncy only when hit hard
    for (s32 k = 0; k < 3; k++) {
        a->v[k] -= (n[k] * ((dv * wa) >> 8)) >> 14;
        b->v[k] += (n[k] * ((dv * wb) >> 8)) >> 14;
    }
    if (rel > 64) {
        s32 j = (dv * ma * (mb ? mb : 32)) / (ma + (mb ? mb : 32));
        knock(a, j);
        if (mb) knock(b, j);
    }
}

// The arcade car (no soft body): two balls, nose and tail.
static s32 hit_arcade(Car *c, Loose *o, const Kind *k)
{
    s32 h = c->heading >> 6, fx = isin(h), fz = icos(h), j = 0, m = mass_of(k);
    for (s32 s = -1; s <= 1; s += 2) {
        s32 bx = c->x + ((fx * 22 * s) >> 6), bz = c->z + ((fz * 22 * s) >> 6);
        s32 dx = (o->p[0] - bx) >> 4, dz = (o->p[2] - bz) >> 4;
        s32 rr = (22 + k->radius) << 4;
        if (iabs(dx) >= rr || iabs(dz) >= rr) continue;
        s32 l = isqrt((u32)(dx * dx + dz * dz));
        if (l >= rr || !l) continue;
        s32 nx = (dx << 14) / l, nz = (dz << 14) / l;
        o->p[0] += (nx * (rr - l)) >> 10;
        o->p[2] += (nz * (rr - l)) >> 10;
        s32 rel = ((c->vx - o->v[0]) * nx + (c->vz - o->v[2]) * nz) >> 14;
        if (rel <= 0) continue;
        s32 kick = rel * (100 + tn.prop_bounce) / 100;   // twice rel at stock bounce
        o->v[0] += (nx * kick) >> 14;
        o->v[2] += (nz * kick) >> 14;
        o->v[1] += rel >> 2;
        j += rel * 2 * m;
        s32 slow = (rel * 2 * m) / (m + ((30 * tn.weight) >> 8));   // the car weighs 30
        c->vx -= (nx * slow) >> 14;
        c->vz -= (nz * slow) >> 14;
    }
    return j;
}

void props_step(Car *c)
{
    if (!nprops) return;
    if (burst_timer > 0) burst_timer--;
    s32 soft = sb_valid(c), awake = 0;
    for (s32 i = 0; i < nprops; i++) {
        Loose *o = &props[i];
        const Kind *k = &kinds[o->kind];
        // The car.
        if (c->mode != CAR_LOOP && iabs(o->p[0] - c->x) < (64 << 8) && iabs(o->p[2] - c->z) < (64 << 8) &&
            iabs(o->p[1] - c->y) < (56 << 8)) {
            s32 j = soft ? sb_hit_prop(o->p, o->v, k->radius, k->half_h, mass_of(k)) : hit_arcade(c, o, k);
            if (j) {
                if (j > 160 * mass_of(k)) sound_play(k->mass > 2 ? SFX_CRASH : SFX_LAND);
                knock(o, j);
            }
        }
        awake += o->awake;
    }
    if (soft) sb_hit_done();
    if (!awake) return;

    for (s32 h = 0; h < GRID; h++) cell_head[h] = -1;
    for (s32 i = 0; i < nprops; i++) {
        s32 h = cell_of(props[i].p[0] >> 13, props[i].p[2] >> 13);
        cell_next[i] = cell_head[h];
        cell_head[h] = i;
    }
    for (s32 i = 0; i < nprops; i++) {
        Loose *o = &props[i];
        const Kind *k = &kinds[o->kind];
        if (!o->awake) continue;

        // Flight, the ground and walls.
        o->v[1] -= car_g;
        o->p[0] += o->v[0];
        o->p[1] += o->v[1];
        o->p[2] += o->v[2];
        o->yaw += o->spin;
        o->tilt += o->tilt_v;
        if (k->shape == POLE) {
            if (o->tilt >= 16384) { o->tilt = 16384; o->tilt_v = o->tilt_v > 900 ? -(o->tilt_v >> 2) : 0; }
            if (o->tilt <= 0) { o->tilt = 0; if (o->tilt_v < 0) o->tilt_v = 0; }
        }
        // How far its centre sits above whatever it rests on.
        s32 t = o->tilt >> 6, ct = iabs(icos(t)), st = iabs(isin(t)), up;
        if (k->shape == BOX) up = (k->half_h * (ct + st)) >> 6;
        else up = (k->half_h * ct + k->radius * st) >> 6;
        s32 x = o->p[0] >> 8, z = o->p[2] >> 8;
        s32 g = world_height(x, z);
        if (o->p[1] - up < g) {
            o->p[1] = g + up;
            if (o->v[1] < 0) o->v[1] = -(o->v[1] * 3 * tn.prop_bounce) / 800;
            if (o->v[1] < 40) o->v[1] = 0;
            // Sliding to a stop; a drum on its side rolls on.
            s32 roll = o->kind == K_DRUM && o->tilt > 12000;
            s32 fr = tn.prop_fric;                          // 256 at stock
            o->v[0] -= (o->v[0] * fr) >> (roll ? 14 : 11);
            o->v[2] -= (o->v[2] * fr) >> (roll ? 14 : 11);
            if (iabs(o->v[0]) < 12 && iabs(o->v[2]) < 12) o->v[0] = o->v[2] = 0;
            o->spin -= o->spin >> 2;
            if (k->shape == POLE) {
                // Past the balance point it falls the rest of the way;
                // short of it, it rocks back up.
                if (o->tilt >= 16384) o->tilt_v = 0;
                else if (o->tilt > 6000) o->tilt_v += 400;
                else if (o->tilt > 0) o->tilt_v -= 400;
                o->tilt_v -= o->tilt_v >> 4;
            } else if (k->shape == BOX) {
                // A crate comes to rest on its nearest face.
                o->tilt_v -= o->tilt_v >> 2;
                if (iabs(o->tilt_v) < 600) {
                    s32 want = ((o->tilt + 8192) >> 14) << 14;
                    o->tilt += (s16)(want - o->tilt) >> 2;
                    o->tilt_v = 0;
                }
            }
        }
        s32 wx, wz, pen = world_collide(x, z, k->radius, &wx, &wz);
        if (pen > 0) {
            o->p[0] += (wx * pen) >> 6;
            o->p[2] += (wz * pen) >> 6;
            s32 vn = (o->v[0] * wx + o->v[2] * wz) >> 14;
            if (vn < 0) {
                s32 ex = wx * vn, ez = wz * vn, pb = tn.prop_bounce - 100;   // 1.5 times at stock
                o->v[0] -= (ex * 3 / 2 + (ex / 200) * pb) >> 14;
                o->v[2] -= (ez * 3 / 2 + (ez / 200) * pb) >> 14;
            }
        }
        s32 cx = o->p[0] >> 13, cz = o->p[2] >> 13, seen[9], ns = 0;
        for (s32 dz = -1; dz <= 1; dz++)
            for (s32 dx = -1; dx <= 1; dx++) {
                s32 h = cell_of(cx + dx, cz + dz), q;
                for (q = 0; q < ns && seen[q] != h; q++) {}
                if (q < ns) continue;
                seen[ns++] = h;
                for (s32 m = cell_head[h]; m >= 0; m = cell_next[m]) {
                    s32 ddx = props[m].p[0] - o->p[0], ddz = props[m].p[2] - o->p[2];
                    if (m != i && iabs(ddx) < (24 << 8) && iabs(ddz) < (24 << 8)) bump(o, &props[m]);
                }
            }
        if (o->p[1] - up < g) o->p[1] = g + up;            // never shoved into the ground

        // Back to sleep once it has settled.
        s32 sp = iabs(o->v[0]) + iabs(o->v[1]) + iabs(o->v[2]) + (iabs(o->tilt_v) >> 4) + (iabs(o->spin) >> 4);
        if (sp < 24) {
            if (++o->still > 30) {
                o->awake = 0;
                o->v[0] = o->v[1] = o->v[2] = 0;
                o->spin = o->tilt_v = 0;
            }
        } else {
            o->still = 0;
        }
    }
}

// ---------------------------------------------------------------- drawing

// A cone: a square base, four sides to a point and a white band.
static const s8 cone_v[] = {
    -5, -6, -5,   5, -6, -5,   5, -6, 5,   -5, -6, 5,
    -3, -1, -3,   3, -1, -3,   3, -1, 3,   -3, -1, 3,
    -2, 2, -2,    2, 2, -2,    2, 2, 2,    -2, 2, 2,
    0, 6, 0,
};
static const u8 cone_f[] = {
    4, COLOR(M_FIRE, 1), 0, 4, 5, 1,   4, COLOR(M_FIRE, 2), 1, 5, 6, 2,
    4, COLOR(M_FIRE, 1), 2, 6, 7, 3,   4, COLOR(M_FIRE, 2), 3, 7, 4, 0,
    4, COLOR(M_STUNT_WHITE, 0), 4, 8, 9, 5,   4, COLOR(M_STUNT_WHITE, 1), 5, 9, 10, 6,
    4, COLOR(M_STUNT_WHITE, 0), 6, 10, 11, 7, 4, COLOR(M_STUNT_WHITE, 1), 7, 11, 8, 4,
    3, COLOR(M_FIRE, 0), 8, 12, 9,   3, COLOR(M_FIRE, 1), 9, 12, 10,
    3, COLOR(M_FIRE, 0), 10, 12, 11, 3, COLOR(M_FIRE, 1), 11, 12, 8,
    4, COLOR(M_FIRE, 3), 0, 1, 2, 3,
};
static const Mesh cone_mesh = { cone_v, cone_f, 13, 13 };
// Far away: just the four sides.
static const s8 cone_far_v[] = { -5, -6, -5,   5, -6, -5,   5, -6, 5,   -5, -6, 5,   0, 6, 0 };
static const u8 cone_far_f[] = {
    3, COLOR(M_FIRE, 1), 0, 4, 1,   3, COLOR(M_FIRE, 2), 1, 4, 2,
    3, COLOR(M_FIRE, 1), 2, 4, 3,   3, COLOR(M_FIRE, 2), 3, 4, 0,
};
static const Mesh cone_far = { cone_far_v, cone_far_f, 5, 4 };

// An oil drum: an eight-sided can, its lids cut into three quads each.
static const s8 drum_v[] = {
    -6, -8, -2,  -2, -8, -6,  2, -8, -6,  6, -8, -2,  6, -8, 2,  2, -8, 6,  -2, -8, 6,  -6, -8, 2,
    -6, 8, -2,   -2, 8, -6,   2, 8, -6,   6, 8, -2,   6, 8, 2,   2, 8, 6,   -2, 8, 6,   -6, 8, 2,
};
static const u8 drum_f[] = {
    4, COLOR(M_STUNT_RED, 1), 0, 8, 9, 1,    4, COLOR(M_STUNT_RED, 2), 1, 9, 10, 2,
    4, COLOR(M_STUNT_RED, 1), 2, 10, 11, 3,  4, COLOR(M_STUNT_RED, 2), 3, 11, 12, 4,
    4, COLOR(M_STUNT_RED, 1), 4, 12, 13, 5,  4, COLOR(M_STUNT_RED, 2), 5, 13, 14, 6,
    4, COLOR(M_STUNT_RED, 1), 6, 14, 15, 7,  4, COLOR(M_STUNT_RED, 2), 7, 15, 8, 0,
    4, COLOR(M_STUNT_RED, 0), 8, 15, 14, 13,  4, COLOR(M_STUNT_RED, 0), 13, 12, 11, 10,
    4, COLOR(M_STUNT_RED, 0), 8, 13, 10, 9,
    4, COLOR(M_STUNT_RED, 3), 0, 1, 2, 3,     4, COLOR(M_STUNT_RED, 3), 3, 4, 5, 6,
    4, COLOR(M_STUNT_RED, 3), 0, 3, 6, 7,
};
static const Mesh drum_mesh = { drum_v, drum_f, 16, 14 };

void props_draw(void)
{
    for (s32 i = 0; i < nprops; i++) {
        const Loose *o = &props[i];
        const Kind *k = &kinds[o->kind];
        s32 x = o->p[0] >> 8, y = o->p[1] >> 8, z = o->p[2] >> 8;
        s32 side, d = r_depth(x, z, &side);
        if (d < -16 || d > k->draw_far * 4 || !r_visible(x, z, 16)) continue;
        // Columns: the local x, y (up) and z axes in the world. Upright it
        // just faces `yaw`; tipped, its up axis leans toward `fall`.
        s32 m[9];
        s32 a = o->yaw >> 6, cs = icos(a), sn = isin(a);
        if (o->tilt) {
            a = (s16)(o->yaw - o->fall) >> 6;             // heading relative to the fall
            cs = icos(a); sn = isin(a);
            s32 f = o->fall >> 6, fc = icos(f), fs = isin(f);
            s32 t = o->tilt >> 6, tc = icos(t), ts = isin(t);
            s32 up[3] = { (fs * ts) >> 14, tc, (fc * ts) >> 14 };
            s32 hinge[3] = { fc, 0, -fs };
            s32 fwd[3] = { (fs * tc) >> 14, -ts, (fc * tc) >> 14 };
            for (s32 c = 0; c < 3; c++) {
                m[c] = (hinge[c] * cs - fwd[c] * sn) >> 14;
                m[3 + c] = up[c];
                m[6 + c] = (hinge[c] * sn + fwd[c] * cs) >> 14;
            }
        } else {
            m[0] = cs;  m[1] = 0;     m[2] = -sn;
            m[3] = 0;   m[4] = 16384; m[5] = 0;
            m[6] = sn;  m[7] = 0;     m[8] = cs;
        }
        switch (o->kind) {
        case K_CONE:  r_mesh(x, y, z, m, d < 110 ? &cone_mesh : &cone_far); break;
        case K_DRUM:
            if (d < 140) r_mesh(x, y, z, m, &drum_mesh);
            else r_box_mat(x, y, z, m, -6, -8, -6, 6, 8, 6, M_STUNT_RED);
            break;
        case K_CRATE: r_box_mat(x, y, z, m, -8, -8, -8, 8, 8, 8, M_BLD4); break;
        default:      r_box_mat(x, y, z, m, -10, -8, -10, 10, 8, 10, M_SIDEWALK); break;
        }
    }
}
