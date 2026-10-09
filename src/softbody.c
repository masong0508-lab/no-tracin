// Soft-body car physics, after BeamNG.drive.
//
// The body is 18 point masses (nodes): a lower ring along the sills, an
// upper ring along the beltline, the four roof corners and the two tips of
// the rear wing. Springs (beams) join every pair of frame nodes closer than
// 50 units, about 80 in all, and each step they are pulled back toward their
// rest lengths a few times over (Verlet integration with position-based
// constraints). A beam squeezed or stretched past its yield point takes a
// permanent set, so a wall at speed crumples the nose for good, and the
// wing's mounts snap clean off.
//
// The wheels are four heavier hubs, each held under its corner by a slider
// that lets it move up and down within the suspension travel, pushed down by
// a damped spring. The tyres grip with a friction circle: each wheel's load
// (from its spring) limits how hard it can corner, brake and drive combined,
// so weight transfer, wheelspin, power oversteer and lift-off slides come out
// of the simulation rather than being scripted. Tyre and suspension forces
// act on the body as a whole (a rigid impulse, so the car isn't torn apart
// by its own tyres); walls, the ground and other cars push on single nodes,
// and that is what bends the frame. A hard enough knock tears a wheel off.
//
// Units: positions Q8 world units (20 units = 1 m), velocities Q8 units per
// step (1/60 s), axes Q14, angular velocity Q16 radians per step. Every
// node weighs 1 except the hubs (3).
#include "softbody.h"
#include "world.h"
#include "track.h"
#include "game.h"
#include "hud.h"
#include "sound.h"

s32 car_softbody = 1;

#define NF       16                 // frame nodes
#define NB       18                 // ... plus the wing tips
#define NN       22                 // ... plus the four hubs
#define HUB0     NB
#define MAXBEAMS 96
#define ITER     3                  // constraint passes per step
#define STIFF    200                // beam stiffness per pass, of 256
#define HUB_M    3
#define TOTAL_M  (NB + 4 * HUB_M)
#define INV_M    (65536 / TOTAL_M)
#define WHEEL_R  7
#define DROOP    (5 << 8)           // suspension travel below the design height
#define BUMP     (4 << 8)           // ... and above it
#define C_DAMP   420                // Q8: about half of critical
#define K_ROLL   128                // anti-roll bars, Q8 of the spring rate
#define NODE_R   (1 << 8)
#define G        car_g
#define BRAKE    31
#define REV_ACC  10
#define REV_MAX  (-1024)
#define A0       16
#define CRASH_WALL 1700
#define CRUSH    560                // most speed a knock takes off the car in one step: past it, the frame crumples
#define MAXC     24
#define VMAX     6000               // fastest any node moves, Q8 per step

// The car at rest, in its own units: x right, y up, z forward, origin on the
// ground under the middle of the car.
static const s8 rest[NN][3] = {
    { -20, 5, 40 },  { 20, 5, 40 },   { -20, 5, 0 },   { 20, 5, 0 },     // 0-5 sills
    { -20, 5, -42 }, { 20, 5, -42 },
    { -19, 11, 42 }, { 19, 11, 42 },  { -20, 17, 4 },  { 20, 17, 4 },    // 6-11 beltline
    { -20, 17, -42 }, { 20, 17, -42 },
    { -15, 29, -2 }, { 15, 29, -2 },  { 15, 29, -18 }, { -15, 29, -18 }, // 12-15 roof
    { -20, 26, -41 }, { 20, 26, -41 },                                   // 16-17 wing tips
    { -17, 7, 30 },  { 17, 7, 30 },   { -17, 7, -30 }, { 17, 7, -30 },   // 18-21 hubs
};
// Wing mounts (these can snap).
static const u8 wing_beams[][2] = {
    { 16, 10 }, { 16, 11 }, { 16, 15 }, { 16, 17 }, { 17, 11 }, { 17, 10 }, { 17, 14 },
};

typedef struct { s32 x, y, z, px, py, pz; } Node;
typedef struct {
    u8 a, b, alive, snaps;
    s32 r, r0;          // rest length now and as built, Q6
    s32 r2;             // r squared, Q12
    s32 inv2r;          // (1 << 20) / 2r
    s32 kr;             // stiffness / r, scaled for a half share at each end
    s32 yield, snap;    // strains (as lengths, Q6) where it bends and breaks
} Beam;

static Node nd[NN];
EWRAM_BSS static Beam bm[MAXBEAMS];
static s32 nbeams;
static s32 ax_r[3], ax_u[3], ax_f[3];   // body axes, Q14
static s32 cen[3];                      // centroid of the frame nodes, Q8
static s32 hub_loc[4][3];               // where each hub sits, relative to cen, Q8
static s32 inv_i[3];                    // inverse inertia about R, U, F: Q24 per unit^2
static s32 accv[3], accw[3];            // rigid impulse gathered this step: Q16, Q20
static u8  hub_on[4];
static s32 hub_s[4];                    // spring compression last step, Q8
static s32 hub_load[4];
static s32 toe[4];                      // bent steering, 1024 units per turn
static s32 hub_spin[4];
static s32 dmg;                         // permanent set taken by the beams, Q6
static s32 wheels_off, wing_off;
static s32 air, launch_x, launch_z, upside_steps, water_steps, crash_cool;
static s32 prev_r[3], prev_u[3], prev_f[3];
static s32 safe_timer, hub_land;
static s32 stress;                      // steps left to solve the frame carefully after a knock
static s32 hub_gh[4];                   // ground under each hub this step
static s32 stop_t[4], stop_l[4];        // impulse per unit of tyre slip, sideways and along (Q8)
static s32 gp_ok, gp_x, gp_z, gp_h, gp_gx, gp_gz;   // the ground round the car as a plane
static s32 gp_seg = -1;                             // on a circuit: the stretch the car is on
static s32 step_n;

#define GP_R 46

// Ground height (Q8) at (x, z) in world units: from the plane when the car
// sits on one, which saves most of the world queries.
static s32 ground(s32 x, s32 z)
{
    if (gp_seg >= 0) return track_height_near(gp_seg, x, z);
    s32 dx = x - gp_x, dz = z - gp_z;
    if (gp_ok && dx < GP_R && dx > -GP_R && dz < GP_R && dz > -GP_R)
        return gp_h + gp_gx * dx + gp_gz * dz;
    return world_height(x, z);
}
static const Car *owner;                // the car the frame belongs to (0: none built)
static s32 parked;                      // the arcade model had the car for a while
static s32 cl_rest[3];                  // centroid of the frame at rest, car units Q8
static s32 out_x, out_z;                // where we last put it

static s32 iabs(s32 v) { return v < 0 ? -v : v; }
// Rounded shift: plain >> rounds toward minus infinity, and summed over many
// small impulses that bias would push the car steadily one way.
#define RS(v, n) (((v) + (1 << ((n) - 1))) >> (n))

void sb_invalidate(void) { owner = 0; }
void sb_park(void) { parked = 1; }
s32  sb_valid(const Car *c) { return owner == c; }

s32 sb_damage(void) { s32 d = dmg / 160; return d > 100 ? 100 : d; }
s32 sb_wheels_off(void) { return wheels_off; }

static s32 attached(s32 i)
{
    if (i < NF) return 1;
    if (i < NB) return !wing_off;
    return hub_on[i - HUB0];
}

static void beam_set(Beam *b, s32 r)
{
    b->r = r;
    b->r2 = r * r;
    b->inv2r = (1 << 20) / (2 * r);
    b->kr = (STIFF << 11) / r;
    b->yield = r / 12;
    b->snap = (r * 3) >> 3;
}

static void add_beam(s32 a, s32 b, s32 snaps)
{
    if (nbeams >= MAXBEAMS) return;
    s32 dx = rest[a][0] - rest[b][0], dy = rest[a][1] - rest[b][1], dz = rest[a][2] - rest[b][2];
    Beam *m = &bm[nbeams++];
    m->a = a; m->b = b; m->alive = 1; m->snaps = snaps;
    m->r0 = isqrt((dx * dx + dy * dy + dz * dz) << 12);
    beam_set(m, m->r0);
}

// Q8 vector in, Q14 unit vector out.
static void normalize(s32 *v)
{
    u32 l2 = (u32)(v[0] * v[0]) + (u32)(v[1] * v[1]) + (u32)(v[2] * v[2]);
    s32 l = isqrt(l2);
    if (!l) { v[0] = 0; v[1] = 0; v[2] = 16384; return; }
    for (s32 k = 0; k < 3; k++) v[k] = (v[k] << 14) / l;
}

static inline s32 dot14(const s32 *a, const s32 *b)
{
    return RS(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], 14);
}

static s32 corr[3][3];                  // the frame's own axes at rest, in car axes

// Body axes and centroid from where the frame nodes are now.
static void frame(void)
{
    static const u8 front[4] = { 0, 1, 6, 7 }, back[4] = { 4, 5, 10, 11 };
    s32 s[3] = { 0, 0, 0 }, f[3] = { 0, 0, 0 }, r[3] = { 0, 0, 0 };
    for (s32 i = 0; i < NF; i++) { s[0] += nd[i].x; s[1] += nd[i].y; s[2] += nd[i].z; }
    for (s32 k = 0; k < 3; k++) cen[k] = s[k] >> 4;
    for (s32 i = 0; i < 4; i++) {
        const s32 *a = &nd[front[i]].x, *b = &nd[back[i]].x;
        for (s32 k = 0; k < 3; k++) f[k] += a[k] - b[k];
    }
    for (s32 i = 0; i < 12; i += 2) {
        const s32 *a = &nd[i + 1].x, *b = &nd[i].x;      // right side minus left
        for (s32 k = 0; k < 3; k++) r[k] += a[k] - b[k];
    }
    for (s32 k = 0; k < 3; k++) { f[k] >>= 2; r[k] >>= 3; }
    normalize(f);
    s32 d = (r[0] * f[0] + r[1] * f[1] + r[2] * f[2]) >> 14;
    for (s32 k = 0; k < 3; k++) r[k] -= (f[k] * d) >> 14;
    normalize(r);
    s32 u[3] = { (f[1] * r[2] - f[2] * r[1]) >> 14,
                 (f[2] * r[0] - f[0] * r[2]) >> 14,
                 (f[0] * r[1] - f[1] * r[0]) >> 14 };
    // The nodes' axes lean a little (the nose is lower than the tail), so
    // turn them back to the car's own.
    for (s32 k = 0; k < 3; k++) {
        ax_r[k] = (corr[0][0] * r[k] + corr[1][0] * u[k] + corr[2][0] * f[k]) >> 14;
        ax_u[k] = (corr[0][1] * r[k] + corr[1][1] * u[k] + corr[2][1] * f[k]) >> 14;
        ax_f[k] = (corr[0][2] * r[k] + corr[1][2] * u[k] + corr[2][2] * f[k]) >> 14;
    }
}

static void to_local(const s32 *p, s32 *l)
{
    s32 d[3] = { p[0] - cen[0], p[1] - cen[1], p[2] - cen[2] };
    l[0] = dot14(d, ax_r);
    l[1] = dot14(d, ax_u);
    l[2] = dot14(d, ax_f);
}

// ---------------------------------------------------------------- rigid impulses

// An impulse j (Q8 velocity x mass) at world point p, gathered into a change
// of the whole body's motion.
static void impulse(const s32 *j, const s32 *p)
{
    for (s32 k = 0; k < 3; k++) accv[k] += RS(j[k] * INV_M, 8);
    s32 r[3] = { RS(p[0] - cen[0], 8), RS(p[1] - cen[1], 8), RS(p[2] - cen[2], 8) };
    s32 t[3] = { RS(r[1] * j[2] - r[2] * j[1], 4),
                 RS(r[2] * j[0] - r[0] * j[2], 4),
                 RS(r[0] * j[1] - r[1] * j[0], 4) };
    s32 w0 = RS(dot14(t, ax_r) * inv_i[0], 8);
    s32 w1 = RS(dot14(t, ax_u) * inv_i[1], 8);
    s32 w2 = RS(dot14(t, ax_f) * inv_i[2], 8);
    for (s32 k = 0; k < 3; k++) accw[k] += RS(ax_r[k] * w0 + ax_u[k] * w1 + ax_f[k] * w2, 14);
}

// Velocity change the gathered impulses give point p (units from cen).
static void acc_at(const s32 *r, s32 *dv)
{
    dv[0] = RS(accv[0] + RS(accw[1] * r[2] - accw[2] * r[1], 4), 8);
    dv[1] = RS(accv[1] + RS(accw[2] * r[0] - accw[0] * r[2], 4), 8);
    dv[2] = RS(accv[2] + RS(accw[0] * r[1] - accw[1] * r[0], 4), 8);
}


// How easily the body moves along unit direction d when pushed at p (Q16).
static s32 inv_mass(const s32 *p, const s32 *d)
{
    s32 r[3] = { (p[0] - cen[0]) >> 8, (p[1] - cen[1]) >> 8, (p[2] - cen[2]) >> 8 };
    s32 c[3] = { (r[1] * d[2] - r[2] * d[1]) >> 14,
                 (r[2] * d[0] - r[0] * d[2]) >> 14,
                 (r[0] * d[1] - r[1] * d[0]) >> 14 };
    s32 c0 = dot14(c, ax_r), c1 = dot14(c, ax_u), c2 = dot14(c, ax_f);
    return INV_M + ((c0 * c0 * inv_i[0] + c1 * c1 * inv_i[1] + c2 * c2 * inv_i[2]) >> 8);
}

static void apply_rigid(void)
{
    for (s32 i = 0; i < NN; i++) {
        if (!attached(i)) continue;
        Node *n = &nd[i];
        s32 r[3] = { (n->x - cen[0]) >> 8, (n->y - cen[1]) >> 8, (n->z - cen[2]) >> 8 }, dv[3];
        acc_at(r, dv);
        if (i >= HUB0) {
            // Hubs follow the body sideways and fore and aft, but its heave
            // is the springs' business: unsprung weight stays on the road.
            s32 up = dot14(dv, ax_u);
            for (s32 k = 0; k < 3; k++) dv[k] -= (ax_u[k] * up) >> 14;
        }
        n->x += dv[0]; n->y += dv[1]; n->z += dv[2];
    }
    for (s32 k = 0; k < 3; k++) { accv[k] = 0; accw[k] = 0; }
}

// ---------------------------------------------------------------- building

void sb_reset(Car *c)
{
    // The frame's axes at rest, measured in car axes.
    for (s32 i = 0; i < NF; i++) { nd[i].x = rest[i][0] << 8; nd[i].y = rest[i][1] << 8; nd[i].z = rest[i][2] << 8; }
    for (s32 j = 0; j < 3; j++)
        for (s32 k = 0; k < 3; k++) corr[j][k] = j == k ? 16384 : 0;
    frame();
    for (s32 k = 0; k < 3; k++) { corr[0][k] = ax_r[k]; corr[1][k] = ax_u[k]; corr[2][k] = ax_f[k]; }

    s32 h = c->heading >> 6;
    s32 s = isin(h), co = icos(h);
    ax_f[0] = s;  ax_f[1] = 0;     ax_f[2] = co;
    ax_r[0] = co; ax_r[1] = 0;     ax_r[2] = -s;
    ax_u[0] = 0;  ax_u[1] = 16384; ax_u[2] = 0;
    for (s32 i = 0; i < NN; i++) {
        s32 lx = rest[i][0] << 8, ly = rest[i][1] << 8, lz = rest[i][2] << 8;
        Node *n = &nd[i];
        n->x = c->x + ((ax_r[0] * lx + ax_f[0] * lz) >> 14);
        n->y = c->y + ly;
        n->z = c->z + ((ax_r[2] * lx + ax_f[2] * lz) >> 14);
        n->px = n->x - c->vx;
        n->py = n->y - c->vy;
        n->pz = n->z - c->vz;
    }
    nbeams = 0;
    for (s32 a = 0; a < NF; a++)
        for (s32 b = a + 1; b < NF; b++) {
            s32 dx = rest[a][0] - rest[b][0], dy = rest[a][1] - rest[b][1], dz = rest[a][2] - rest[b][2];
            if (dx * dx + dy * dy + dz * dz <= 50 * 50) add_beam(a, b, 0);
        }
    for (u32 i = 0; i < sizeof(wing_beams) / 2; i++) add_beam(wing_beams[i][0], wing_beams[i][1], 1);

    // Centroid of the frame, the hubs' places relative to it, and the
    // inertia of the whole car about it.
    s32 *cl = cl_rest;
    cl[0] = cl[1] = cl[2] = 0;
    for (s32 i = 0; i < NF; i++)
        for (s32 k = 0; k < 3; k++) cl[k] += rest[i][k] << 8;
    for (s32 k = 0; k < 3; k++) cl[k] >>= 4;
    for (s32 w = 0; w < 4; w++)
        for (s32 k = 0; k < 3; k++) hub_loc[w][k] = (rest[HUB0 + w][k] << 8) - cl[k];
    s32 ix = 0, iy = 0, iz = 0;
    for (s32 i = 0; i < NN; i++) {
        s32 m = i >= HUB0 ? HUB_M : 1;
        s32 x = rest[i][0] - (cl[0] >> 8), y = rest[i][1] - (cl[1] >> 8), z = rest[i][2] - (cl[2] >> 8);
        ix += m * (y * y + z * z);
        iy += m * (x * x + z * z);
        iz += m * (x * x + y * y);
    }
    inv_i[0] = (1 << 24) / ix;
    inv_i[1] = (1 << 24) / iy;
    inv_i[2] = (1 << 24) / iz;
    // How hard each tyre must push to stop its contact patch sliding, from
    // where it sits (the body's turning makes the corners lighter).
    for (s32 w = 0; w < 4; w++) {
        s32 rx = hub_loc[w][0] >> 8, ry = hub_loc[w][1] >> 8, rz = hub_loc[w][2] >> 8;
        s32 wt = INV_M + ((rz * rz * inv_i[1] + ry * ry * inv_i[2]) >> 8);
        s32 wl = INV_M + ((ry * ry * inv_i[0] + rx * rx * inv_i[1]) >> 8);
        stop_t[w] = (1 << 24) / wt;
        stop_l[w] = (1 << 24) / wl;
    }
    for (s32 w = 0; w < 4; w++) { hub_on[w] = 1; hub_s[w] = DROOP; toe[w] = 0; hub_spin[w] = 0; hub_load[w] = 0; }
    for (s32 k = 0; k < 3; k++) { accv[k] = 0; accw[k] = 0; }
    dmg = 0;
    wheels_off = 0;
    wing_off = 0;
    air = 0;
    upside_steps = 0;
    water_steps = 0;
    crash_cool = 0;
    frame();
    for (s32 k = 0; k < 3; k++) { prev_r[k] = ax_r[k]; prev_u[k] = ax_u[k]; prev_f[k] = ax_f[k]; }
    owner = c;
    parked = 0;
    gp_ok = 0;
    gp_seg = -1;
    out_x = c->x;
    out_z = c->z;
}

// Puts the frame, bent as it is, where the car now is (after the loop ride,
// say), moving at the car's velocity. Loose parts stay where they fell.
static void place(Car *c)
{
    s32 h = c->heading >> 6, s = isin(h), co = icos(h);
    s32 nr[3] = { co, 0, -s }, nu[3] = { 0, 16384, 0 }, nf[3] = { s, 0, co };
    s32 org[3];
    for (s32 k = 0; k < 3; k++)
        org[k] = (&c->x)[k] + ((nr[k] * cl_rest[0] + nu[k] * cl_rest[1] + nf[k] * cl_rest[2]) >> 14);
    for (s32 i = 0; i < NN; i++) {
        if (!attached(i)) continue;
        Node *n = &nd[i];
        s32 l[3];
        to_local(&n->x, l);
        n->x = org[0] + ((nr[0] * l[0] + nu[0] * l[1] + nf[0] * l[2]) >> 14);
        n->y = org[1] + ((nr[1] * l[0] + nu[1] * l[1] + nf[1] * l[2]) >> 14);
        n->z = org[2] + ((nr[2] * l[0] + nu[2] * l[1] + nf[2] * l[2]) >> 14);
        n->px = n->x - c->vx;
        n->py = n->y - c->vy;
        n->pz = n->z - c->vz;
    }
    frame();
    for (s32 k = 0; k < 3; k++) { prev_r[k] = ax_r[k]; prev_u[k] = ax_u[k]; prev_f[k] = ax_f[k]; }
    air = 0;
    parked = 0;
}

// ---------------------------------------------------------------- the frame

// Pulls every beam toward its rest length; on the last pass, beams pushed
// past their yield point take a set, and the wing's mounts can snap. A
// quiet pass (passes 0) only trues the frame up: it moves the nodes'
// previous places too, so it adds no motion (run every few steps, a
// correction that turned into speed would overshoot).
IWRAM_CODE static void solve(s32 passes)
{
    s32 quiet = !passes;
    if (quiet) passes = 1;
    for (s32 it = 0; it < passes; it++) {
        s32 last = passes > 1 && it == passes - 1;
        for (s32 k = 0; k < nbeams; k++) {
            Beam *b = &bm[k];
            if (!b->alive) continue;
            Node *p = &nd[b->a], *q = &nd[b->b];
            s32 dx = q->x - p->x, dy = q->y - p->y, dz = q->z - p->z;
            s32 ex = dx >> 2, ey = dy >> 2, ez = dz >> 2;     // Q6
            if (ex > 16000 || ex < -16000 || ey > 16000 || ey < -16000 || ez > 16000 || ez < -16000) {
                if (b->snaps) b->alive = 0;
                continue;
            }
            s32 l2 = ex * ex + ey * ey + ez * ez;
            s32 diff = (s32)(((long long)(l2 - b->r2) * b->inv2r) >> 20);
            if (diff > (b->r >> 2) || diff < -(b->r >> 2)) diff = isqrt(l2) - b->r;
            if (last) {
                s32 ad = diff < 0 ? -diff : diff;
                if (ad > b->yield) {
                    if (b->snaps && ad > b->snap) { b->alive = 0; continue; }
                    s32 give = (ad - b->yield) >> 1;
                    s32 nr = b->r + (diff > 0 ? give : -give);
                    if (nr < (b->r0 * 9) >> 4) nr = (b->r0 * 9) >> 4;
                    if (nr > (b->r0 * 3) >> 1) nr = (b->r0 * 3) >> 1;
                    dmg += iabs(nr - b->r);
                    beam_set(b, nr);
                    diff += diff > 0 ? -give : give;
                }
            }
            s32 f = (diff * b->kr) >> 8;
            s32 cx = (dx * f) >> 12, cy = (dy * f) >> 12, cz = (dz * f) >> 12;
            p->x += cx; p->y += cy; p->z += cz;
            q->x -= cx; q->y -= cy; q->z -= cz;
            if (quiet) {
                p->px += cx; p->py += cy; p->pz += cz;
                q->px -= cx; q->py -= cy; q->pz -= cz;
            }
        }
    }
}

// ---------------------------------------------------------------- the world

// A node against the ground: returns how fast it hit (Q8).
static s32 ground_node(Node *n, s32 radius, s32 bounce)
{
    s32 gh = ground(n->x >> 8, n->z >> 8) + radius;
    if (n->y >= gh) return 0;
    if (gh - n->y > (12 << 8)) {        // the face of a step or a ramp: a wall
        n->x = n->px;
        n->z = n->pz;
        return 0;
    }
    s32 vy = n->py - n->y;              // speed it was falling at
    n->y = gh;
    n->py = gh + (bounce && vy > 0 ? vy / 3 : 0);
    n->px += (n->x - n->px) >> 3;       // scraping along the ground
    n->pz += (n->z - n->pz) >> 3;
    return vy;
}

// A node against walls: returns how fast it hit (Q8).
static s32 wall_node(Node *n, s32 radius)
{
    s32 nx, nz;
    s32 pen = world_collide(n->x >> 8, n->z >> 8, radius, &nx, &nz);
    if (pen <= 0) return 0;
    s32 vx = n->x - n->px, vz = n->z - n->pz;
    s32 vn = (vx * nx + vz * nz) >> 14;
    n->x += (pen * nx) >> 6;
    n->z += (pen * nz) >> 6;
    if (vn < 0) {
        vx -= (nx * vn) >> 14;
        vz -= (nz * vn) >> 14;
    }
    vx -= vx >> 3;
    vz -= vz >> 3;
    n->px = n->x - vx;
    n->pz = n->z - vz;
    return vn < 0 ? -vn : 0;
}

static s32 mean_vx(void) { s32 v = 0; for (s32 i = 0; i < NF; i++) v += nd[i].x - nd[i].px; return v >> 4; }
static s32 mean_vz(void) { s32 v = 0; for (s32 i = 0; i < NF; i++) v += nd[i].z - nd[i].pz; return v >> 4; }

// Contacts with the world this step: node, how deep (Q8) and which way out.
static u8  cn_i[MAXC];
static s32 cn_pen[MAXC], cn_n[MAXC][3];
static s32 ncon;
static s32 surf_vx, surf_vz;            // the thing hit is moving (another car)

static void add_contact(s32 i, s32 pen, s32 nx, s32 ny, s32 nz)
{
    if (ncon >= MAXC) return;
    cn_i[ncon] = i;
    cn_pen[ncon] = pen;
    cn_n[ncon][0] = nx; cn_n[ncon][1] = ny; cn_n[ncon][2] = nz;
    ncon++;
}

// The whole car takes each knock as an impulse where it lands (with a little
// bounce and some friction), up to what the crumple zones can bear
// in a step; then the nodes are put back outside the world, and whatever the
// impulse couldn't stop in time is a dent. Returns the hardest hit (Q8).
static s32 resolve_contacts(void)
{
    s32 hit = 0, budget = CRUSH * TOTAL_M;
    for (s32 c = 0; c < ncon; c++) {
        Node *n = &nd[cn_i[c]];
        const s32 *nv = cn_n[c];
        s32 r[3] = { (n->x - cen[0]) >> 8, (n->y - cen[1]) >> 8, (n->z - cen[2]) >> 8 }, dv[3];
        acc_at(r, dv);
        s32 v[3] = { n->x - n->px + dv[0] - surf_vx, n->y - n->py + dv[1], n->z - n->pz + dv[2] - surf_vz };
        s32 vn = dot14(v, nv);
        if (vn >= 0) continue;
        if (vn < -VMAX) vn = -VMAX;
        if (-vn > hit) hit = -vn;
        s32 w = inv_mass(&n->x, nv);
        s32 jn = ((-vn * 5) << 14) / w;                 // stop it, and bounce back a quarter
        if (jn > budget) jn = budget;
        budget -= jn;
        // Scraping along the surface.
        s32 t[3] = { v[0] - ((nv[0] * vn) >> 14), v[1] - ((nv[1] * vn) >> 14), v[2] - ((nv[2] * vn) >> 14) };
        s32 tl = isqrt((u32)(t[0] * t[0]) + (u32)(t[1] * t[1]) + (u32)(t[2] * t[2]));
        s32 jt = 0;
        if (tl > 0) {
            jt = (tl << 16) / w;
            s32 most = (jn * (nv[1] ? 128 : 24)) >> 8;     // the body scrapes on the ground; walls are slick
            if (jt > most) jt = most;
            jt = (jt << 8) / tl;                        // per unit of slide, Q8
        }
        s32 j[3];
        for (s32 k = 0; k < 3; k++) j[k] = ((nv[k] * jn) >> 14) - ((t[k] * jt) >> 8);
        impulse(j, &n->x);
    }
    // How far the impulses alone carry each node back out.
    for (s32 c = 0; c < ncon; c++) {
        Node *n = &nd[cn_i[c]];
        s32 r[3] = { (n->x - cen[0]) >> 8, (n->y - cen[1]) >> 8, (n->z - cen[2]) >> 8 }, dv[3];
        acc_at(r, dv);
        cn_pen[c] -= dot14(dv, cn_n[c]);
    }
    apply_rigid();
    for (s32 c = 0; c < ncon; c++) {
        Node *n = &nd[cn_i[c]];
        const s32 *nv = cn_n[c];
        s32 pen = cn_pen[c];
        if (pen <= 0) continue;
        if (pen > (3 << 8)) pen = 3 << 8;               // a deep step in the world comes out over a few steps
        s32 *x = &n->x, *px = &n->px;
        for (s32 k = 0; k < 3; k++) { s32 m = (nv[k] * pen) >> 14; x[k] += m; px[k] += m; }
        // The node itself stops going in.
        s32 v[3] = { x[0] - px[0] - surf_vx, x[1] - px[1], x[2] - px[2] - surf_vz };
        s32 vn = dot14(v, nv);
        if (vn < 0) for (s32 k = 0; k < 3; k++) px[k] += (nv[k] * vn) >> 14;
    }
    return hit;
}

s32 sb_push(s32 x, s32 y, s32 z, s32 radius, s32 vx, s32 vz, s32 *push_x, s32 *push_z)
{
    // The ball is a moving wall: the nodes inside it become contacts, and
    // the car takes the knock the same way it takes one from a barrier.
    ncon = 0;
    s32 r = radius + 1;
    for (s32 i = 0; i < NN; i++) {
        if (!attached(i)) continue;
        Node *n = &nd[i];
        s32 dx = (n->x >> 8) - x, dz = (n->z >> 8) - z, dy = (n->y >> 8) - y;
        if (dy > radius || dy < -radius) continue;
        if (dx > r || dx < -r || dz > r || dz < -r) continue;
        s32 d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        s32 d = isqrt(d2);
        if (!d) { dx = 1; d = 1; }
        add_contact(i, (r - d) << 8, (dx << 14) / d, 0, (dz << 14) / d);
    }
    if (!ncon) return 0;
    s32 v0x = mean_vx(), v0z = mean_vz();
    surf_vx = vx; surf_vz = vz;
    s32 hit = resolve_contacts();
    surf_vx = surf_vz = 0;
    if (hit > 64) stress = 5;
    // What the car gave the ball is what it lost itself.
    *push_x -= mean_vx() - v0x;
    *push_z -= mean_vz() - v0z;
    return hit;
}

// The body's outline for loose objects: they meet these edges, not just the
// nodes (a cone would slip between the front corners otherwise).
static const u8 hull[][2] = {
    { 0, 1 }, { 1, 3 }, { 3, 5 }, { 5, 4 }, { 4, 2 }, { 2, 0 },          // sills
    { 6, 7 }, { 7, 9 }, { 9, 11 }, { 11, 10 }, { 10, 8 }, { 8, 6 },      // beltline
    { 0, 6 }, { 1, 7 }, { 4, 10 }, { 5, 11 },                            // corners
    { 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 12 },                      // roof
};

#define HULL_EDGES (sizeof(hull) / 2)
static s16 ebox[HULL_EDGES][6] EWRAM_BSS;
static s32 hit_pending, ebox_ok;

// Beams into nodes a and b that a blow has squashed past their yield take
// that set for good.
static void set_dent(s32 a, s32 b)
{
    for (s32 k = 0; k < nbeams; k++) {
        Beam *m = &bm[k];
        if (!m->alive || (m->a != a && m->b != a && m->a != b && m->b != b)) continue;
        const Node *p = &nd[m->a], *q = &nd[m->b];
        s32 ex = (q->x - p->x) >> 2, ey = (q->y - p->y) >> 2, ez = (q->z - p->z) >> 2;   // Q6
        if (iabs(ex) > 16000 || iabs(ey) > 16000 || iabs(ez) > 16000) continue;
        s32 l = isqrt((u32)(ex * ex + ey * ey + ez * ez));
        if (l >= m->r - m->yield) continue;
        s32 nr = l + m->yield;
        if (nr < (m->r0 * 9) >> 4) nr = (m->r0 * 9) >> 4;
        dmg += m->r - nr;
        beam_set(m, nr);
    }
}

// The body takes up the knocks from this step's sb_hit_prop calls.
void sb_hit_done(void)
{
    if (hit_pending) apply_rigid();
    hit_pending = 0;
    ebox_ok = 0;
}

s32 sb_hit_prop(s32 *pos, s32 *vel, s32 radius, s32 half_h, s32 mass)
{
    if (!owner) return 0;
    s32 R = radius << 8;
    for (s32 k = 0; k < 3; k++)
        if (iabs(pos[k] - cen[k]) > R + (56 << 8)) return 0;
    // The deepest touch: an edge of the body, or a wheel.
    s32 best = 0, bn[3] = { 0, 0, 0 }, bc[3] = { 0, 0, 0 }, ba = -1, bb = -1, bt = 0;
    u32 best_l2 = (u32)((R >> 4) * (R >> 4));
    if (!ebox_ok) {
        // Each hull edge's box in whole units, worked out once a step.
        for (u32 e = 0; e < HULL_EDGES; e++) {
            const Node *P = &nd[hull[e][0]], *Q = &nd[hull[e][1]];
            for (s32 k = 0; k < 3; k++) {
                s32 a = (&P->x)[k] >> 8, c = (&Q->x)[k] >> 8;
                ebox[e][k * 2] = a < c ? a : c;
                ebox[e][k * 2 + 1] = a < c ? c : a;
            }
        }
        ebox_ok = 1;
    }
    s32 lo[3], hi[3];
    // The object is an upright capsule: a ball of radius R swept up and
    // down `band` from its centre, so its sides push straight out.
    s32 band = (half_h << 8) - (R >> 1);
    if (band < 0) band = 0;
    for (s32 k = 0; k < 3; k++) { lo[k] = (pos[k] >> 8) - radius - 1; hi[k] = (pos[k] >> 8) + radius + 1; }
    lo[1] -= band >> 8; hi[1] += band >> 8;
    for (u32 e = 0; e < HULL_EDGES; e++) {
        // Skip edges whose box (grown by the radius) misses the centre.
        const s16 *b = ebox[e];
        if (b[1] < lo[0] || b[0] > hi[0] || b[5] < lo[2] || b[4] > hi[2] || b[3] < lo[1] || b[2] > hi[1]) continue;
        const Node *P = &nd[hull[e][0]], *Q = &nd[hull[e][1]];
        s32 dx = (Q->x - P->x) >> 4, dy = (Q->y - P->y) >> 4, dz = (Q->z - P->z) >> 4;      // Q4
        s32 qx = (pos[0] - P->x) >> 4, qy = (pos[1] - P->y) >> 4, qz = (pos[2] - P->z) >> 4;
        s32 dd = (dx * dx + dy * dy + dz * dz) >> 8;
        s32 t = dd ? ((qx * dx + qy * dy + qz * dz) >> 8) * 256 / dd : 0;                   // Q8 along the edge
        if (t < 0) t = 0; else if (t > 256) t = 256;
        s32 c[3] = { P->x + ((Q->x - P->x) >> 8) * t, P->y + ((Q->y - P->y) >> 8) * t, P->z + ((Q->z - P->z) >> 8) * t };
        s32 ay = c[1] < pos[1] - band ? pos[1] - band : c[1] > pos[1] + band ? pos[1] + band : c[1];
        s32 ex = (pos[0] - c[0]) >> 4, ey = (ay - c[1]) >> 4, ez = (pos[2] - c[2]) >> 4;
        if (iabs(ex) > R >> 4 || iabs(ey) > R >> 4 || iabs(ez) > R >> 4) continue;
        u32 l2 = ex * ex + ey * ey + ez * ez;                                                 // Q8
        if (l2 >= best_l2) continue;
        best_l2 = l2; ba = hull[e][0]; bb = hull[e][1]; bt = t;
        bn[0] = ex; bn[1] = ey; bn[2] = ez;
        for (s32 k = 0; k < 3; k++) bc[k] = c[k];
    }
    if (ba >= 0) {
        // Only the closest edge needs its distance and normal worked out.
        s32 l = isqrt(best_l2);                                                              // Q4
        best = R - (l << 4);
        if (l) { s32 inv = (1 << 24) / l; for (s32 k = 0; k < 3; k++) bn[k] = (bn[k] * inv) >> 10; }
        else   { bn[0] = 0; bn[1] = 16384; bn[2] = 0; }
    }
    for (s32 w = 0; w < 4; w++) {
        if (!hub_on[w]) continue;
        const Node *h = &nd[HUB0 + w];
        s32 ay = h->y < pos[1] - band ? pos[1] - band : h->y > pos[1] + band ? pos[1] + band : h->y;
        s32 ex = (pos[0] - h->x) >> 4, ey = (ay - h->y) >> 4, ez = (pos[2] - h->z) >> 4;
        s32 reach = (R + (WHEEL_R << 8)) >> 4;
        if (iabs(ex) > reach || iabs(ey) > reach || iabs(ez) > reach) continue;
        s32 l = isqrt((u32)(ex * ex + ey * ey + ez * ez));
        s32 pen = (reach - l) << 4;
        if (pen <= best || !l) continue;
        best = pen; ba = bb = HUB0 + w; bt = 0;
        bn[0] = (ex << 14) / l; bn[1] = (ey << 14) / l; bn[2] = (ez << 14) / l;
        for (s32 k = 0; k < 3; k++) bc[k] = (&h->x)[k] + ((bn[k] * WHEEL_R) >> 6);
    }
    if (ba < 0) return 0;

    // How fast the body there closes on the object.
    const Node *A = &nd[ba], *B = &nd[bb];
    s32 vc[3] = { ((A->x - A->px) * (256 - bt) + (B->x - B->px) * bt) >> 8,
                  ((A->y - A->py) * (256 - bt) + (B->y - B->py) * bt) >> 8,
                  ((A->z - A->pz) * (256 - bt) + (B->z - B->pz) * bt) >> 8 };
    s32 rel[3] = { vc[0] - vel[0], vc[1] - vel[1], vc[2] - vel[2] };
    s32 vn = dot14(rel, bn);
    s32 J = 0;
    if (vn > 0) {
        if (vn > VMAX) vn = VMAX;
        s32 w = inv_mass(bc, bn), wp = 65536 / mass;
        J = ((vn * 5) << 14) / (w + wp);                // a lively bounce
        for (s32 k = 0; k < 3; k++) vel[k] += (bn[k] * (J / mass)) >> 14;
        // It's dragged along a little by the body sliding past.
        s32 vt[3];
        for (s32 k = 0; k < 3; k++) vt[k] = rel[k] - ((bn[k] * vn) >> 14);
        for (s32 k = 0; k < 3; k++) vel[k] += vt[k] >> 2;
        s32 j[3] = { -((bn[0] * J) >> 14), -((bn[1] * J) >> 14), -((bn[2] * J) >> 14) };
        impulse(j, bc);
        hit_pending = 1;
        if (J > 40 * TOTAL_M) stress = 5;
    }
    // Apart again: the object takes most of the move, the car the rest (a
    // heavy one leaves a dent there).
    s32 dent = (best * mass) / (mass + TOTAL_M);
    for (s32 k = 0; k < 3; k++) pos[k] += (bn[k] * ((best - dent) >> 8)) >> 6;
    // A hard hit on something heavy crumples the body there.
    s32 crush = J / TOTAL_M - 200;
    if (crush > 0) {
        crush = crush * mass / 4;
        dent += crush > (10 << 8) ? 10 << 8 : crush;
    }
    s32 crumple = crush > 0 && dent > 64;
    if (dent > 64) {
        Node *a = &nd[ba], *b = &nd[bb];
        for (s32 k = 0; k < 3; k++) {
            s32 m = (bn[k] * (dent >> 8)) >> 6;
            s32 ma = (m * (256 - bt)) >> 8, mb = (m * bt) >> 8;
            (&a->x)[k] -= ma; (&a->px)[k] -= ma;
            if (b != a) { (&b->x)[k] -= mb; (&b->px)[k] -= mb; }
        }
        if (crumple && ba < NF) set_dent(ba, bb);
        stress = 5;
    }
    return J ? J : 1;
}

// ---------------------------------------------------------------- wheels

static s32 engine_force(s32 v)
{
    if (v < car_power / A0) return A0;
    return car_power / v;
}

// Hubs: ground, the slider that keeps each under its corner, and the spring.
// Returns how many tyres are on the ground.
static s32 suspension(void)
{
    s32 contacts = 0;
    // Preloaded springs hold the car at its design height, and twice as
    // stiff as a plain spring that sagged there would be.
    s32 load0 = TOTAL_M * G / 4;
    s32 k_spring = ((load0 * 2) << 16) / DROOP;
    for (s32 w = 0; w < 4; w++) {
        Node *h = &nd[HUB0 + w];
        hub_load[w] = 0;
        if (!hub_on[w]) continue;
        s32 *hp = &h->x;
        s32 gh = ground(h->x >> 8, h->z >> 8) + (WHEEL_R << 8);
        hub_gh[w] = gh;
        s32 touch = 0;
        if (h->y < gh) {
            if (gh - h->y > ((WHEEL_R + 5) << 8)) {
                h->x = h->px;            // a kerb or step too tall to roll up
                h->z = h->pz;
            } else {
                if (h->py - h->y > hub_land) hub_land = h->py - h->y;
                h->y = gh;
                if (h->py > gh) h->py = gh;
                touch = 1;
            }
        } else if (h->y < gh + (1 << 8)) {
            touch = 1;
        }

        s32 l[3];
        to_local(hp, l);
        s32 *t = hub_loc[w];
        s32 d0 = t[0] - l[0], d2 = t[2] - l[2], d1 = 0;
        if (l[1] < t[1] - DROOP) d1 = t[1] - DROOP - l[1];
        else if (l[1] > t[1] + BUMP) d1 = t[1] + BUMP - l[1];
        // A big sideways knock bends the corner; a bigger one tears it off.
        s32 side = iabs(d0) + iabs(d2);
        if (side > (15 << 8)) {
            hub_on[w] = 0;
            wheels_off++;
            game_message("WHEEL OFF!", "", PAL_RED, 120);
            sound_play(SFX_CRASH);
            continue;
        }
        if (side > (4 << 8)) {
            t[0] -= d0 >> 2;
            t[2] -= d2 >> 2;
            toe[w] += (d0 > 0 ? -1 : 1) * (side >> 9);
            if (toe[w] > 24) toe[w] = 24;
            if (toe[w] < -24) toe[w] = -24;
            dmg += side >> 2;
        }
        // The slider holds the hub under its corner, and the spring pushes
        // it down and the body up; both act on the body as one impulse.
        s32 s = l[1] + d1 - (t[1] - DROOP);
        s32 f = load0 + (((s - DROOP) * k_spring) >> 16) + (((s - hub_s[w]) * C_DAMP) >> 8);
        if (hub_on[w ^ 1]) f += (((s - hub_s[w ^ 1]) * k_spring) >> 16) * K_ROLL >> 8;   // anti-roll bar
        if (s > DROOP + BUMP - (3 << 7)) f += ((s - (DROOP + BUMP - (3 << 7))) * k_spring) >> 14;   // bump rubber
        if (s <= 0) f = 0;                                                    // hanging free
        hub_s[w] = s;
        if (f < 0) f = 0;
        s32 j[3];
        for (s32 k = 0; k < 3; k++) {
            s32 m = RS(RS(ax_r[k] * d0 + ax_u[k] * d1 + ax_f[k] * d2, 14) * 7, 3);
            s32 up = RS(ax_u[k] * f, 14);
            hp[k] += m - ((up * (65536 / HUB_M)) >> 16);
            j[k] = up - m * HUB_M;
        }
        impulse(j, hp);
        if (touch) {
            hub_load[w] = f + HUB_M * G;
            contacts++;
        }
    }
    return contacts;
}

// Tyre forces: drive, brakes and cornering grip, limited by each wheel's
// load (a friction circle). Two passes so the four tyres settle together.
static void tyres(Car *c, u16 keys, s32 vlong)
{
    s32 gas = keys & KEY_A, brk = keys & KEY_B, hand = keys & KEY_L;
    s32 mu = (car_grip << 8) / (G > 0 ? G : 1);
    s32 power = 256 - (sb_damage() * 3) / 2;      // a crushed nose loses power
    s32 drive = gas ? (engine_force(vlong < 0 ? 0 : vlong) * TOTAL_M * power) >> 9 : 0;
    c->skid = 0;
    c->spin = 0;
    for (s32 w = 0; w < 4; w++) {
        if (!hub_on[w] || !hub_load[w]) continue;
        Node *h = &nd[HUB0 + w];
        // The wheel's heading and its contact patch.
        s32 fw[3], rw[3], p[3], r[3], v[3], dv[3];
        s32 delta = toe[w] + (w < 2 ? c->steer : 0);
        if (delta) {
            s32 cs = icos(delta), sn = isin(delta);
            for (s32 k = 0; k < 3; k++) {
                fw[k] = RS(ax_f[k] * cs + ax_r[k] * sn, 14);
                rw[k] = RS(ax_r[k] * cs - ax_f[k] * sn, 14);
            }
        } else {
            for (s32 k = 0; k < 3; k++) { fw[k] = ax_f[k]; rw[k] = ax_r[k]; }
        }
        // The tyre's forces act at the hub, as if the suspension's roll
        // centre sat at axle height: the car leans less and doesn't trip
        // over its own grip.
        for (s32 k = 0; k < 3; k++) {
            p[k] = (&h->x)[k];
            r[k] = (p[k] - cen[k]) >> 8;
        }
        acc_at(r, dv);
        v[0] = h->x - h->px + dv[0];
        v[1] = h->y - h->py + dv[1];
        v[2] = h->z - h->pz + dv[2];
        s32 vl = dot14(v, fw), vt = dot14(v, rw);
        if (vl > 16000) vl = 16000; else if (vl < -16000) vl = -16000;
        if (vt > 16000) vt = 16000; else if (vt < -16000) vt = -16000;

        s32 lim = (mu * hub_load[w]) >> 8;
        if (hand && w >= 2) lim = (lim * 5) >> 3;
        s32 L = lim * 4;
        s32 jt = -(vt * stop_t[w]) >> 8, jl;               // what it takes to stop sliding sideways
        if (hand && w >= 2) {
            jl = -(vl * stop_l[w]) >> 8;                    // locked
        } else if ((brk && vl > 64) || (gas && vl < -64)) {
            jl = -(vl * stop_l[w]) >> 8;
            s32 cap = BRAKE * TOTAL_M / 4;
            if (jl > cap) jl = cap;
            if (jl < -cap) jl = -cap;
        } else if (gas && w >= 2) {
            jl = drive;
        } else if (brk && w >= 2) {
            jl = vl > REV_MAX ? -(REV_ACC * TOTAL_M) / 2 : 0;
        } else {
            jl = -(vl * stop_l[w]) >> 8;                    // rolling to a stop
            s32 cap = TOTAL_M / 2;
            if (jl > cap) jl = cap;
            if (jl < -cap) jl = -cap;
        }
        if (jt > L) jt = L;
        if (jt < -L) jt = -L;
        if (jl > L) jl = L;
        if (jl < -L) jl = -L;
        // The friction circle: drive, brakes and cornering share the grip.
        if (jt * jt + jl * jl > lim * lim) {
            s32 mag = isqrt((u32)(jt * jt) + (u32)(jl * jl));
            s32 k = (lim * 7) >> 3;                         // sliding grips a little less
            jt = jt * k / mag;
            jl = jl * k / mag;
            if (iabs(vt) > 200) c->skid = 1;
            if (gas && w >= 2 && vlong < 900) c->spin = 1;
        }
        s32 j[3];
        for (s32 k = 0; k < 3; k++) j[k] = RS(rw[k] * jt + fw[k] * jl, 14);
        impulse(j, p);
        hub_spin[w] += vl >> 4;
    }
}

// ---------------------------------------------------------------- stepping

static void integrate(void)
{
    s32 g = G;
    for (s32 i = 0; i < NN; i++) {
        Node *n = &nd[i];
        s32 vx = n->x - n->px, vy = n->y - n->py, vz = n->z - n->pz;
        // Nothing on the car outruns this (and a bad knock can't fling it off the map).
        if (vx > VMAX) vx = VMAX; else if (vx < -VMAX) vx = -VMAX;
        if (vy > VMAX) vy = VMAX; else if (vy < -VMAX) vy = -VMAX;
        if (vz > VMAX) vz = VMAX; else if (vz < -VMAX) vz = -VMAX;
        n->px = n->x; n->py = n->y; n->pz = n->z;
        n->x += vx - (vx >> 9);
        n->y += vy - (vy >> 9) - g;
        n->z += vz - (vz >> 9);
    }
}

// Stands the car (bent as it is) back on its wheels where it lies.
static void recover(Car *c)
{
    s32 h = c->heading >> 6, s = isin(h), co = icos(h);
    s32 nr[3] = { co, 0, -s }, nu[3] = { 0, 16384, 0 }, nf[3] = { s, 0, co };
    s32 org[3] = { cen[0], world_height(cen[0] >> 8, cen[2] >> 8) + (16 << 8), cen[2] };
    for (s32 i = 0; i < NN; i++) {
        if (!attached(i)) continue;
        Node *n = &nd[i];
        s32 l[3];
        to_local(&n->x, l);
        n->x = org[0] + ((nr[0] * l[0] + nu[0] * l[1] + nf[0] * l[2]) >> 14);
        n->y = org[1] + ((nr[1] * l[0] + nu[1] * l[1] + nf[1] * l[2]) >> 14);
        n->z = org[2] + ((nr[2] * l[0] + nu[2] * l[1] + nf[2] * l[2]) >> 14);
        n->px = n->x; n->py = n->y; n->pz = n->z;
    }
    upside_steps = 0;
    frame();
}

// Damps the frame's shaking: each node's motion is drawn toward moving with
// the car as one piece (all the way when nothing is pushing on it, a
// quarter of the way while it takes a knock). Without this the frame
// would ring like a bell after every bump.
static void settle(s32 full)
{
    // The car's turn this step, from how its axes moved (Q14 radians).
    s32 w[3] = { 0, 0, 0 };
    const s32 *a[3] = { prev_r, prev_u, prev_f }, *b[3] = { ax_r, ax_u, ax_f };
    for (s32 j = 0; j < 3; j++) {
        w[0] += (a[j][1] * b[j][2] - a[j][2] * b[j][1]) >> 15;
        w[1] += (a[j][2] * b[j][0] - a[j][0] * b[j][2]) >> 15;
        w[2] += (a[j][0] * b[j][1] - a[j][1] * b[j][0]) >> 15;
    }
    s32 mv[3] = { 0, 0, 0 };
    for (s32 i = 0; i < NF; i++) { mv[0] += nd[i].x - nd[i].px; mv[1] += nd[i].y - nd[i].py; mv[2] += nd[i].z - nd[i].pz; }
    for (s32 k = 0; k < 3; k++) mv[k] >>= 4;
    s32 last = wing_off ? NF : NB;
    for (s32 i = 0; i < last; i++) {
        Node *n = &nd[i];
        s32 r[3] = { n->x - cen[0], n->y - cen[1], n->z - cen[2] };
        s32 want[3] = { mv[0] + ((w[1] * r[2] - w[2] * r[1]) >> 14),
                        mv[1] + ((w[2] * r[0] - w[0] * r[2]) >> 14),
                        mv[2] + ((w[0] * r[1] - w[1] * r[0]) >> 14) };
        s32 *x = &n->x, *px = &n->px;
        for (s32 k = 0; k < 3; k++) {
            s32 v = x[k] - px[k];
            v = full ? want[k] : v + ((want[k] - v) >> 2);
            px[k] = x[k] - v;
        }
    }
}

// Rotation since last step (Q8 of 1024-unit turns) from how the axes moved.
static s32 turned(const s32 *a_now, const s32 *b_prev)
{
    return (dot14(a_now, b_prev) * 2607) >> 10;
}

void sb_step(Car *c, u16 keys)
{
    // A fresh car, or one moved by something else (respawn, a loaded game).
    ebox_ok = 0;
    if (owner != c) sb_reset(c);
    else if (parked || iabs(c->x - out_x) > (32 << 8) || iabs(c->z - out_z) > (32 << 8)) place(c);
    integrate();
    frame();
    // The ground round the car, every other step (it reaches far enough
    // to cover the step in between).
    if (!(++step_n & 1) || !gp_ok) {
        gp_x = cen[0] >> 8;
        gp_z = cen[2] >> 8;
        gp_seg = -1;
        if (g_track) {
            // On a circuit the road bends and climbs: heights come from the
            // stretch under the car and its neighbours.
            TrackHit th;
            if (track_find(gp_x, gp_z, &th)) gp_seg = th.seg;
            gp_ok = 1;
        } else {
            gp_ok = world_ground_plane(gp_x, gp_z, GP_R + 16, &gp_h, &gp_gx, &gp_gz);
        }
    }

    // Mean motion of the frame (for speed, drag and the gearbox).
    s32 mv[3] = { 0, 0, 0 };
    for (s32 i = 0; i < NF; i++) {
        mv[0] += nd[i].x - nd[i].px;
        mv[1] += nd[i].y - nd[i].py;
        mv[2] += nd[i].z - nd[i].pz;
    }
    for (s32 k = 0; k < 3; k++) mv[k] >>= 4;
    s32 vlong = dot14(mv, ax_f);

    // Steering: less lock at speed.
    s32 sp = iabs(vlong);
    if (sp > 3584) sp = 3584;
    s32 lock = 60 - (sp * 44) / 3584;
    s32 target = (keys & KEY_LEFT) ? -lock : (keys & KEY_RIGHT) ? lock : 0;
    if (c->steer < target)      c->steer = c->steer + 6 > target ? target : c->steer + 6;
    else if (c->steer > target) c->steer = c->steer - 6 < target ? target : c->steer - 6;

    hub_land = 0;
    s32 contacts = suspension();
    tyres(c, keys, vlong);

    // Air drag, nitro, and the hop cheat.
    s32 speed = isqrt((u32)(mv[0] * mv[0]) + (u32)(mv[1] * mv[1]) + (u32)(mv[2] * mv[2]));
    for (s32 k = 0; k < 3; k++) accv[k] -= RS(mv[k] * speed, 13);
    if (c->boost) for (s32 k = 0; k < 3; k++) accv[k] += RS(ax_f[k] * 22, 6);
    if (cheat_hop && (keys & (KEY_L | KEY_R)) == (KEY_L | KEY_R) && contacts) accv[1] += 900 << 8;
    apply_rigid();


    // Walls and the ground push on single nodes: this is what bends the car.
    // Only nodes that might touch are tested: the world queries are dear.
    s32 hit = 0, land = hub_land, tilted = ax_u[1] < 12000;
    s32 nx, nz, gmax = -(1 << 30), lost = wheels_off;
    for (s32 w = 0; w < 4; w++) if (hub_gh[w] > gmax) gmax = hub_gh[w];
    gmax += (3 - WHEEL_R) << 8;
    s32 reach = 48 + (speed >> 8);
    s32 near_wall = world_collide(cen[0] >> 8, cen[2] >> 8, reach, &nx, &nz) > 0;
    ncon = 0;
    for (s32 i = 0; i < NN; i++) {
        Node *n = &nd[i];
        if (i >= NB) {
            // Hubs only meet walls here (the suspension has the ground).
            if (!near_wall || !hub_on[i - HUB0]) continue;
        } else {
            if (i >= NF && (wing_off || !tilted)) continue;
            if ((i < 6 && (lost || n->y < gmax)) || tilted) {
                s32 gh = ground(n->x >> 8, n->z >> 8) + NODE_R;
                if (n->y < gh) {
                    if (gh - n->y > (12 << 8)) {
                        // The face of a step or a ramp: back the way it came.
                        s32 vx = n->x - n->px, vz = n->z - n->pz;
                        s32 l = isqrt((u32)(vx * vx) + (u32)(vz * vz));
                        if (l > 0) add_contact(i, l, -(vx << 14) / l, 0, -(vz << 14) / l);
                    } else {
                        add_contact(i, gh - n->y, 0, 16384, 0);
                    }
                }
            }
            if (!near_wall || (i >= 8 && !tilted)) continue;
        }
        // Only the side facing the wall.
        s32 side = (((n->x - cen[0]) >> 8) * nx + ((n->z - cen[2]) >> 8) * nz) >> 14;
        if (side > 4) continue;
        s32 wx, wz, pen = world_collide(n->x >> 8, n->z >> 8, i >= NB ? WHEEL_R - 1 : 2, &wx, &wz);
        if (pen > 0) add_contact(i, pen << 8, wx, 0, wz);
    }
    if (ncon) {
        s32 h = resolve_contacts();
        if (h > 64) stress = 5;
        // Ground knocks count as landings, the rest as wall hits.
        for (s32 c = 0; c < ncon; c++)
            if (cn_n[c][1]) { if (h > land) land = h; } else if (h > hit) hit = h;
    }
    for (s32 w = 0; w < 4; w++) {
        Node *h = &nd[HUB0 + w];
        if (hub_on[w]) continue;
        ground_node(h, WHEEL_R << 8, 1);
        wall_node(h, WHEEL_R);
        hub_spin[w] += 40;
    }
    // Moving as one piece hardly strains the frame, so one quick pass keeps
    // it true; after a knock it gets the full treatment, and can bend.
    if (stress) { solve(2); stress--; }
    else if (!(step_n & 7)) solve(0);
    if (wing_off && (nd[16].y < nd[16].py || nd[17].y < nd[17].py)) {
        ground_node(&nd[16], NODE_R, 1);
        ground_node(&nd[17], NODE_R, 1);
    }
    if (!wing_off) {
        s32 alive = 0;
        for (s32 k = nbeams - (s32)(sizeof(wing_beams) / 2); k < nbeams; k++)
            if (bm[k].alive && !(bm[k].a == 16 && bm[k].b == 17)) alive++;
        if (!alive) { wing_off = 1; game_message("WING OFF!", "", PAL_RED, 90); }
    }

    // ---- results for the rest of the game
    frame();
    // A frame knocked inside out can't be simulated: stand a fresh one up.
    for (s32 i = 0; i < NF; i++)
        if (iabs(nd[i].x - cen[0]) > (90 << 8) || iabs(nd[i].y - cen[1]) > (90 << 8) || iabs(nd[i].z - cen[2]) > (90 << 8)) {
            c->x = cen[0]; c->z = cen[2];
            c->y = ground(cen[0] >> 8, cen[2] >> 8);
            c->vx = c->vy = c->vz = 0;
            sb_reset(c);
            break;
        }
    settle(!stress);
    s32 org[3];
    {
        // The car's origin: the ground point under the middle of the frame.
        for (s32 k = 0; k < 3; k++)
            org[k] = cen[k] - ((ax_r[k] * cl_rest[0] + ax_u[k] * cl_rest[1] + ax_f[k] * cl_rest[2]) >> 14);
    }
    c->x = out_x = org[0];
    c->y = org[1];
    c->z = out_z = org[2];
    c->vx = mv[0];
    c->vy = mv[1];
    c->vz = mv[2];
    s32 hlen = isqrt((u32)(ax_f[0] * ax_f[0]) + (u32)(ax_f[2] * ax_f[2]));
    s32 hd = iatan2(ax_f[0], ax_f[2]) << 6;
    c->heading += (s16)(hd - c->heading);
    c->pitch = iatan2(ax_f[1], hlen) << 8;
    c->roll = iatan2(-ax_r[1], ax_u[1]) << 8;
    c->bob = 0;
    c->throttle = (keys & KEY_A) != 0;
    c->hit = hit;
    c->landed = land;
    if (hit > 300 && hit > land) sound_play(SFX_SCRAPE);

    if (crash_cool > 0) crash_cool--;
    if (hit > CRASH_WALL && !crash_cool) {
        c->event = EV_CRASH;
        crash_cool = 90;
    }

    // In the air or on the ground; jumps and tricks for the stunt score.
    s32 on_ground = contacts >= 2;
    if (!on_ground && !contacts) {
        if (!air) {
            air = 1;
            c->air_steps = 0;
            c->trick_p = c->trick_r = c->trick_h = 0;
            launch_x = c->x >> 8;
            launch_z = c->z >> 8;
        }
        c->air_steps++;
        c->trick_p += turned(ax_f, prev_u);
        c->trick_r -= turned(ax_r, prev_u);
        c->trick_h += turned(ax_f, prev_r);
        c->mode = CAR_AIR;
    } else {
        if (air && on_ground) {
            air = 0;
            if (c->air_steps > 15 && !c->event) {
                s32 dx = (c->x >> 8) - launch_x, dz = (c->z >> 8) - launch_z;
                const s32 turn = 1024 << 8, most = 768 << 8;
                c->event = land > 1800 ? EV_BIG_LANDING : EV_JUMP;
                c->event_a = isqrt(dx * dx + dz * dz) / 20;
                c->event_b = c->air_steps / 6;
                c->event_c = (iabs(c->trick_p) + turn - most) / turn;
                c->event_d = (iabs(c->trick_r) + turn - most) / turn;
                c->event_e = (iabs(c->trick_h) + turn - most) / turn;
            }
        }
        if (on_ground) c->mode = CAR_GROUND;
    }
    for (s32 k = 0; k < 3; k++) { prev_r[k] = ax_r[k]; prev_u[k] = ax_u[k]; prev_f[k] = ax_f[k]; }

    // Upside down and stuck: put it back on its wheels after a moment.
    if (ax_u[1] < 4000 && speed < 160) {
        if (++upside_steps > 150) recover(c);
    } else {
        upside_steps = 0;
    }

    // Into the water: it sinks, then the car comes back at the last safe spot.
    s32 cx = cen[0] >> 8, cz = cen[2] >> 8;
    if (cen[1] < (-16 << 8) && world_in_water(cx, cz)) {
        if (!water_steps) { c->event = EV_SPLASH; sound_play(SFX_SPLASH); }
        for (s32 i = 0; i < NN; i++) {
            Node *n = &nd[i];
            n->px += (n->x - n->px) >> 3;
            n->py += (n->y - n->py) >> 2;
            n->pz += (n->z - n->pz) >> 3;
        }
        if (++water_steps > 110) { car_respawn(c); return; }
    }

    // Remember flat, dry spots to come back to.
    if (++safe_timer >= 30) {
        safe_timer = 0;
        s32 in_park = cx > PARK_X0 && cx < PARK_X1 && cz > PARK_Z0 && cz < PARK_Z1;
        if (!g_track && !in_park && contacts == 4 && ax_u[1] > 15500 && !wheels_off &&
            world_height(cx, cz) == 0 && !world_in_water(cx, cz)) {
            c->safe_x = c->x >> 8;
            c->safe_z = c->z >> 8;
            c->safe_heading = c->heading;
        }
    }
}

// ---------------------------------------------------------------- drawing

EWRAM_BSS static u8  sk_n0[MESH_MAX_VERTS];
EWRAM_BSS static u8  sk_n1[MESH_MAX_VERTS];
EWRAM_BSS static u16 sk_w[MESH_MAX_VERTS];
EWRAM_BSS static s8  sk_o[MESH_MAX_VERTS][3];
EWRAM_BSS static u8  sk_wing[MESH_MAX_VERTS];
static u8  sk_ready;

// Ties each vertex of the body model to the two nearest frame nodes (the
// wing's to the wing tips), keeping its offset from them.
static void bind(const Mesh *m)
{
    for (s32 v = 0; v < m->vert_count; v++) {
        const s8 *p = &m->verts[v * 3];
        s32 a = 0, b = 1, da = 1 << 30, db = 1 << 30;
        s32 wing = v >= 16 && v <= 27;            // the rear wing and its endplates
        sk_wing[v] = wing;
        if (wing) {
            a = 16; b = 17;
            sk_w[v] = p[0] < 0 ? 256 : 0;
        } else {
            for (s32 i = 0; i < NF; i++) {
                s32 dx = p[0] - rest[i][0], dy = p[1] - rest[i][1], dz = p[2] - rest[i][2];
                s32 d = dx * dx + dy * dy + dz * dz;
                if (d < da) { b = a; db = da; a = i; da = d; }
                else if (d < db) { b = i; db = d; }
            }
            s32 sa = isqrt(da), sb = isqrt(db);
            sk_w[v] = sa + sb ? (sb * 256) / (sa + sb) : 128;
        }
        sk_n0[v] = a;
        sk_n1[v] = b;
        s32 w = sk_w[v];
        for (s32 k = 0; k < 3; k++)
            sk_o[v][k] = p[k] - ((rest[a][k] * w + rest[b][k] * (256 - w)) >> 8);
    }
    sk_ready = 1;
}

void sb_draw(const Car *c, const Mesh *body)
{
    if (owner != c) return;
    if (!sk_ready) bind(body);
    // A wing lying on the road keeps its own axes.
    s32 wr[3], wu[3] = { 0, 16384, 0 }, wf[3];
    if (wing_off) {
        wr[0] = nd[17].x - nd[16].x; wr[1] = 0; wr[2] = nd[17].z - nd[16].z;
        normalize(wr);
        wf[0] = -wr[2]; wf[1] = 0; wf[2] = wr[0];
    }
    EWRAM_BSS static Vec3 wv[MESH_MAX_VERTS];
    for (s32 v = 0; v < body->vert_count; v++) {
        const Node *a = &nd[sk_n0[v]], *b = &nd[sk_n1[v]];
        s32 w = sk_w[v];
        const s32 *R = ax_r, *U = ax_u, *F = ax_f;
        if (sk_wing[v] && wing_off) { R = wr; U = wu; F = wf; }
        s32 ox = sk_o[v][0], oy = sk_o[v][1], oz = sk_o[v][2];
        wv[v].x = ((a->x >> 8) * w + (b->x >> 8) * (256 - w)) / 256 + ((R[0] * ox + U[0] * oy + F[0] * oz) >> 14);
        wv[v].y = ((a->y >> 8) * w + (b->y >> 8) * (256 - w)) / 256 + ((R[1] * ox + U[1] * oy + F[1] * oz) >> 14);
        wv[v].z = ((a->z >> 8) * w + (b->z >> 8) * (256 - w)) / 256 + ((R[2] * ox + U[2] * oy + F[2] * oz) >> 14);
    }
    r_mesh_world(wv, body);

    for (s32 w = 0; w < 4; w++) {
        const Node *h = &nd[HUB0 + w];
        s32 m[9];
        if (hub_on[w]) {
            s32 delta = toe[w] + (w < 2 ? c->steer : 0);
            s32 cs = icos(delta), sn = isin(delta);
            for (s32 k = 0; k < 3; k++) {
                m[k] = (ax_r[k] * cs - ax_f[k] * sn) >> 14;
                m[3 + k] = ax_u[k];
                m[6 + k] = (ax_f[k] * cs + ax_r[k] * sn) >> 14;
            }
        } else {
            // A loose wheel tumbles as it rolls away.
            s32 a = hub_spin[w] >> 2, cs = icos(a), sn = isin(a);
            for (s32 k = 0; k < 3; k++) {
                m[k] = ax_r[k];
                m[3 + k] = (ax_u[k] * cs - ax_f[k] * sn) >> 14;
                m[6 + k] = (ax_f[k] * cs + ax_u[k] * sn) >> 14;
            }
        }
        r_box_mat(h->x >> 8, h->y >> 8, h->z >> 8, m, -4, -7, -7, 4, 6, 7, M_TIRE);
    }
}
