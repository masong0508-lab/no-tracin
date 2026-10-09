// Stunt scoring and the Stunt Park lap.
#include "game.h"
#include "world.h"
#include "sound.h"
#include "hud.h"
#include "track.h"

Game g_game;
Records g_rec;
s32 g_new_best, g_units_kmh, cheat_nitro;
static s32 play_steps;

#define COMBO_STEPS 240     // 4 s to chain the next stunt
#define MAX_COMBO   5

// ---------------------------------------------------------------- text

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_num(char *p, s32 v)
{
    char tmp[12];
    s32 i = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { tmp[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = tmp[--i];
    *p = 0;
    return p;
}
// Physics steps as m:ss.t
static char *put_time(char *p, s32 steps)
{
    s32 tenths = steps / 6, secs = tenths / 10;
    p = put_num(p, secs / 60);
    *p++ = ':';
    *p++ = '0' + (secs % 60) / 10;
    *p++ = '0' + secs % 10;
    *p++ = '.';
    *p++ = '0' + tenths % 10;
    *p = 0;
    return p;
}

void game_message(const char *a, const char *b, s32 pal, s32 steps)
{
    put(g_game.msg1, a);
    put(g_game.msg2, b);
    g_game.msg_pal = pal;
    g_game.msg_timer = steps;
}

// ---------------------------------------------------------------- scoring

static void bank(const char *title, s32 points, s32 sfx)
{
    Game *g = &g_game;
    g->combo = g->combo_timer > 0 ? (g->combo < MAX_COMBO ? g->combo + 1 : MAX_COMBO) : 1;
    g->combo_timer = COMBO_STEPS;
    s32 total = points * g->combo;
    g->score += total;
    g->nitro += total / 6;                       // stunts refill the nitro
    if (g->nitro > NITRO_MAX) g->nitro = NITRO_MAX;
    char line[24], *p = put(line, "+");
    p = put_num(p, total);
    if (g->combo > 1) {
        p = put(p, "  X");
        put_num(p, g->combo);
    }
    game_message(title, line, PAL_YELLOW, 150);
    sound_play(g->combo > 1 ? SFX_COMBO : sfx);
}

static void lose_combo(void)
{
    g_game.combo = 0;
    g_game.combo_timer = 0;
    g_game.drift_steps = 0;
    g_game.drift_points = 0;
}

static void stunt_events(Car *c)
{
    char a[24], *p;
    switch (c->event) {
    case EV_JUMP:
    case EV_BIG_LANDING: {
        // Distance in metres and airtime in tenths of a second.
        s32 pts = c->event_a * 10 + c->event_b * 15;
        p = put(a, c->event == EV_JUMP ? "JUMP " : "HARD LANDING ");
        p = put_num(p, c->event_a);
        put(p, " M");
        if (c->event_c || c->event_d || c->event_e) {
            // Name the tricks instead: "2X FLIP + ROLL + 360".
            const char *names[3] = { "FLIP", "ROLL", "" };
            s32 n[3] = { c->event_c, c->event_d, c->event_e };
            p = a;
            for (s32 k = 0; k < 3; k++) {
                if (!n[k]) continue;
                if (p != a) p = put(p, "+");
                if (k == 2) p = put_num(p, n[k] * 360);
                else {
                    if (n[k] > 1) { p = put_num(p, n[k]); p = put(p, "X "); }
                    p = put(p, names[k]);
                }
            }
            pts += c->event_c * 600 + c->event_d * 500 + c->event_e * 400;
            g_rec.flips += c->event_c + c->event_d + c->event_e;
        }
        if (c->event == EV_BIG_LANDING) pts /= 2;
        g_rec.stunts++;
        if (c->event_a > g_rec.best_jump) g_rec.best_jump = c->event_a;
        if (c->event_b > g_rec.best_air) g_rec.best_air = c->event_b;
        bank(a, pts, SFX_STUNT);
        break;
    }
    case EV_LOOP:
        g_rec.stunts++;
        g_rec.loops++;
        p = put(a, "LOOP! ");
        p = put_num(p, g_units_kmh ? c->event_a * 16 / 10 : c->event_a);
        put(p, g_units_kmh ? " KMH" : " MPH");
        bank(a, 1500 + c->event_a * 5, SFX_LOOP);
        break;
    case EV_SPLASH:
        g_rec.crashes++;
        lose_combo();
        game_message("SPLASH!", "", PAL_CYAN, 150);
        sound_play(SFX_FAIL);
        break;
    case EV_FELL:
        g_rec.crashes++;
        lose_combo();
        game_message("TOO SLOW!", "FELL OFF THE LOOP", PAL_RED, 150);
        sound_play(SFX_FAIL);
        break;
    case EV_CRASH:
        g_rec.crashes++;
        lose_combo();
        game_message("CRASH!", "", PAL_RED, 150);
        sound_play(SFX_FAIL);
        break;
    }
    c->event = 0;
}

static void drift(Car *c)
{
    Game *g = &g_game;
    s32 mph = (car_speed(c) * 67) / 2560;
    if (c->mode == CAR_GROUND && c->skid && mph > 25) {
        g->drift_steps++;
        g->drift_points += mph / 6;
        if (g->combo_timer > 0 && g->combo_timer < 30) g->combo_timer = 30;   // a drift keeps a combo alive
        return;
    }
    if (g->drift_steps > 40 && c->mode != CAR_CRASH) {
        char a[24], *p = put(a, "DRIFT ");
        p = put_num(p, g->drift_steps / 6 / 10);
        p = put(p, ".");
        p = put_num(p, (g->drift_steps / 6) % 10);
        put(p, " S");
        g_rec.stunts++;
        bank(a, g->drift_points, SFX_STUNT);
    }
    g->drift_steps = 0;
    g->drift_points = 0;
}

// ---------------------------------------------------------------- the lap

typedef struct {
    s16 ax, az, ah, bx, bz, bh;     // gate ends: position and ground height
    s16 sx, sz;                     // where to restart after a crash
    u16 heading;
} Gate;

// Start line, loop exit, top of the banked curve, after the canal jump.
#define LANE_B_MID ((LANE_B_X0 + LANE_B_X1) / 2)
static const Gate gates[4] = {
    { LOOP_X - 52, START_Z, 0, LOOP_X + 52, START_Z, 0,     LOOP_X, START_Z + 43, 0 },
    { LOOP_X + 44, LOOP_Z + 90, 0, LOOP_X + 156, LOOP_Z + 90, 0,   LOOP_X + 100, LOOP_Z + 110, 0 },
    { BANK_X, BANK_Z + BANK_ROUT, BANK_H, BANK_X, BANK_Z + BANK_RIN, 0,   LOOP_X + 100, LOOP_Z + 110, 0 },
    { LANE_B_X1, LANDING_Z0 - 40, 0, LANE_B_X0, LANDING_Z0 - 40, 0,   LANE_B_MID, LANDING_Z0 - 70, 32768 },
};

#define GHOST_MAX 2400      // samples, one per two steps: 80 s
typedef struct { s16 x, y, z, h, p, r; } Sample;
static Sample rec[GHOST_MAX] EWRAM_BSS;
static Sample best[GHOST_MAX] EWRAM_BSS;
static s32 rec_count, best_count;
static s32 prev_x, prev_z, last_gate;

static s32 crossed(const Gate *g, s32 x0, s32 z0, s32 x1, s32 z1)
{
    s32 ex = g->bx - g->ax, ez = g->bz - g->az;
    s32 s0 = ex * (z0 - g->az) - ez * (x0 - g->ax);
    s32 s1 = ex * (z1 - g->az) - ez * (x1 - g->ax);
    if (!(s0 < 0 && s1 >= 0)) return 0;
    s32 t = (x1 - g->ax) * ex + (z1 - g->az) * ez;
    return t >= 0 && t <= ex * ex + ez * ez;
}

static void set_spawn(Car *c, s32 gate)
{
    c->safe_x = gates[gate].sx;
    c->safe_z = gates[gate].sz;
    c->safe_heading = gates[gate].heading;
}

static void start_lap(void)
{
    g_game.cp_flash = 0;
    g_game.lap_active = 1;
    g_game.lap_steps = 0;
    g_game.next_cp = 1;
    rec_count = 0;
}

static void lap(Car *c)
{
    Game *g = &g_game;
    s32 x = c->x >> 8, z = c->z >> 8;
    s32 dx = x - prev_x, dz = z - prev_z;
    s32 jumped = dx > 100 || dx < -100 || dz > 100 || dz < -100;   // respawned
    s32 in_park = park_contains(x, z, 300);

    if (g->lap_active) {
        g->lap_steps++;
        if ((g->lap_steps & 1) && rec_count < GHOST_MAX) {
            Sample *s = &rec[rec_count++];
            s->x = x; s->y = c->y >> 8; s->z = z;
            s->h = c->heading >> 6; s->p = c->pitch >> 8; s->r = c->roll >> 8;
        }
        if (!in_park || g->lap_steps > 60 * 60 * 5) {
            g->lap_active = 0;
            game_message("LAP ABANDONED", "", PAL_RED, 120);
        }
    }
    if (g->cp_flash > 0) g->cp_flash--;

    if (!jumped && c->mode != CAR_CRASH) {
        if (crossed(&gates[0], prev_x, prev_z, x, z)) {
            if (g->lap_active && g->next_cp == 4) {
                char a[24], b[24], *p;
                g->last_steps = g->lap_steps;
                p = put(a, "LAP ");
                put_time(p, g->lap_steps);
                if (!g->best_steps || g->lap_steps < g->best_steps) {
                    g->best_steps = g->lap_steps;
                    for (s32 i = 0; i < rec_count; i++) best[i] = rec[i];
                    best_count = rec_count;
                    g_rec.best_steps = g->best_steps;
                    g_new_best = 1;
                    game_message(a, "NEW BEST!", PAL_GREEN, 180);
                    sound_play(SFX_BEST);
                } else {
                    p = put(b, "BEST ");
                    put_time(p, g->best_steps);
                    game_message(a, b, PAL_WHITE, 180);
                    sound_play(SFX_LAP);
                }
                g->score += 1000;
                start_lap();
            } else {
                start_lap();
                game_message("GO!", "LAP STARTED", PAL_GREEN, 90);
                sound_play(SFX_GO);
            }
            last_gate = 0;
        } else if (g->lap_active && g->next_cp < 4 &&
                   crossed(&gates[g->next_cp], prev_x, prev_z, x, z)) {
            last_gate = g->next_cp++;
            g->cp_flash = 120;
            sound_play(SFX_CHECKPOINT);
        }
    }
    if (in_park) set_spawn(c, g->lap_active ? last_gate : 0);
    prev_x = x;
    prev_z = z;
}

// ---------------------------------------------------------------- stars

// Hidden around the city on the streets, plus a few you only reach in the
// air. XS(k, b, l) lies on the street x = k blocks, half way along block b,
// in lane l (-2..1, west to east); ZS the same on the street z = k blocks.
#define XS(k, b, l) { (k) * BLOCK + (l) * LANE + LANE / 2, (b) * BLOCK + BLOCK / 2, -1 }
#define ZS(k, b, l) { (b) * BLOCK + BLOCK / 2, (k) * BLOCK + (l) * LANE + LANE / 2, -1 }
static const s16 star_pos[STAR_COUNT][3] = {
    XS(3, 6, 0), XS(5, 2, -1), XS(7, 9, 1), XS(9, 4, -2), XS(11, 13, 0), XS(13, 7, -1),
    XS(15, 16, 1), XS(17, 3, -2), XS(4, 15, 0), XS(8, 18, -1), XS(12, 1, 1), XS(16, 11, -2),
    XS(18, 14, 0),
    ZS(5, 6, -1), ZS(8, 12, 1), ZS(10, 2, -2), ZS(12, 15, 0), ZS(14, 9, -1), ZS(17, 5, 1),
    ZS(2, 10, -2), ZS(6, 17, 0), ZS(15, 13, -1), ZS(19, 8, 1), ZS(11, 7, -2), ZS(7, 3, 0),
    { 7 * BLOCK, 11 * BLOCK, -1 },                      // in the middle of a crossing
    { LANE_B_MID, CANAL_Z0 + 150, 95 },                 // over the canal
    { PX(1030), START_Z + 133, 70 },                    // off the practice kicker
    { BANK_X, BANK_Z + 340, 120 },                      // above the banked curve
    { WORLD / 2, WORLD / 2, 260 },                      // high over the middle of town: a flying star
};
static s16 star_y[STAR_COUNT] EWRAM_BSS;   // worked out on first use (EWRAM is zeroed at boot)

static s32 star_height(s32 i)
{
    if (!star_y[i]) {
        s32 y = star_pos[i][2];
        if (y < 0) { y = world_height(star_pos[i][0], star_pos[i][1]) >> 8; y = (y < 0 ? 0 : y) + 16; }
        star_y[i] = y;
    }
    return star_y[i];
}

s32 game_stars(void)
{
    s32 n = 0;
    for (s32 i = 0; i < STAR_COUNT; i++) n += (g_rec.stars >> i) & 1;
    return n;
}

static void stars(Car *c)
{
    s32 x = c->x >> 8, y = c->y >> 8, z = c->z >> 8;
    for (s32 i = 0; i < STAR_COUNT; i++) {
        if ((g_rec.stars >> i) & 1) continue;
        s32 dx = x - star_pos[i][0], dz = z - star_pos[i][1], dy = y + 12 - star_height(i);
        if (dx < -40 || dx > 40 || dz < -40 || dz > 40 || dy < -50 || dy > 50) continue;
        g_rec.stars |= 1u << i;
        g_game.score += 250;
        g_new_best = 1;                          // autosave
        char a[24], b[24], *p = put(a, "STAR ");
        p = put_num(p, game_stars());
        p = put(p, "/");
        put_num(p, STAR_COUNT);
        if (game_stars() == STAR_COUNT) {
            g_game.score += 10000;
            put(b, "ALL STARS! +10000");
            sound_play(SFX_BEST);
        } else {
            put(b, "+250");
            sound_play(SFX_COMBO);
        }
        game_message(a, b, PAL_YELLOW, 120);
    }
}

void game_draw_stars(s32 frame)
{
    for (s32 i = 0; i < STAR_COUNT; i++) {
        if ((g_rec.stars >> i) & 1) continue;
        s32 side, d = r_depth(star_pos[i][0], star_pos[i][1], &side);
        if (d < 30 || d > r_far) continue;
        s32 bob = (isin(frame * 12 + i * 90) * 4) >> 14;
        s32 sx, sy;
        if (!r_project(star_pos[i][0], star_height(i) + bob, star_pos[i][1], &sx, &sy)) continue;
        if (sx < -8 || sx > SCREEN_W + 8 || sy < -8 || sy > SCREEN_H + 8) continue;
        if (d < 450) hud_text(sx - 6, sy - 7, "*", 1, (frame >> 2) & 1 ? PAL_YELLOW : PAL_WHITE);
        else         hud_text(sx - 3, sy - 3, "*", 0, PAL_YELLOW);
    }
}

// ---------------------------------------------------------------- interface

void game_reset(void)
{
    Game *g = &g_game;
    u8 *p = (u8 *)g;
    for (u32 i = 0; i < sizeof(*g); i++) p[i] = 0;
    rec_count = 0;
    g->nitro = NITRO_MAX / 2;
    g->best_steps = g_rec.best_steps;    // the best lap (and its ghost) outlive a restart
    last_gate = 0;
    prev_x = prev_z = 0;
}

void game_step(Car *c)
{
    Game *g = &g_game;
    if (g->msg_timer > 0) g->msg_timer--;
    if (g_track) {
        // On a circuit the race keeps score; stunts just count for the records.
        c->event = 0;
        if (c->boost && !cheat_nitro) g->nitro -= 6;
        if (g->nitro < 0) g->nitro = 0;
        if (cheat_nitro) g->nitro = NITRO_MAX;
        s32 mph = (car_speed(c) * 67) / 2560;
        if (c->mode != CAR_CRASH && mph > g_rec.top_mph) g_rec.top_mph = mph;
        if (++play_steps >= 60) { play_steps = 0; g_rec.play_secs++; }
        return;
    }
    if (g->combo_timer > 0 && --g->combo_timer == 0) g->combo = 0;
    drift(c);
    stunt_events(c);
    if (c->boost && !cheat_nitro) g->nitro -= 6;
    if (g->nitro < 0) g->nitro = 0;
    if (cheat_nitro) g->nitro = NITRO_MAX;
    stars(c);
    if (g->score > g_rec.high_score) g_rec.high_score = g->score;
    s32 mph = (car_speed(c) * 67) / 2560;
    if (c->mode != CAR_CRASH && mph > g_rec.top_mph) g_rec.top_mph = mph;
    if (++play_steps >= 60) { play_steps = 0; g_rec.play_secs++; }
    lap(c);
}

static void draw_gate(const Gate *g, s32 hot)
{
    const s32 post = 70, band = 14;
    s32 pm = hot ? M_STUNT_RED : M_SIDEWALK;
    r_box(g->ax - 4, g->ah, g->az - 4, g->ax + 4, g->ah + post, g->az + 4, pm);
    r_box(g->bx - 4, g->bh, g->bz - 4, g->bx + 4, g->bh + post, g->bz + 4, pm);
    // Banner in two halves so it reads as a chequered strip.
    s32 mx = (g->ax + g->bx) / 2, mz = (g->az + g->bz) / 2, mh = (g->ah + g->bh) / 2 + post;
    s32 ta = g->ah + post, tb = g->bh + post;
    u8 c0 = hot ? COLOR(M_LINE, 0) : COLOR(M_STUNT_WHITE, 1);
    u8 c1 = hot ? COLOR(M_STUNT_RED, 0) : COLOR(M_STUNT_WHITE, 2);
    Vec3 q0[4] = { { g->ax, ta, g->az }, { mx, mh, mz }, { mx, mh - band, mz }, { g->ax, ta - band, g->az } };
    Vec3 q1[4] = { { mx, mh, mz }, { g->bx, tb, g->bz }, { g->bx, tb - band, g->bz }, { mx, mh - band, mz } };
    r_face(q0, 4, c0, RF_TWO_SIDED);
    r_face(q1, 4, c1, RF_TWO_SIDED);
}

void game_draw_world(void)
{
    const Game *g = &g_game;
    for (s32 i = 0; i < 4; i++) {
        const Gate *gt = &gates[i];
        s32 side, d = r_depth((gt->ax + gt->bx) / 2, (gt->az + gt->bz) / 2, &side);
        if (d < -100 || d > r_far) continue;
        if (i == 0 || g->lap_active) draw_gate(gt, g->lap_active ? i == (g->next_cp & 3) : i == 0);
    }
}

s32 game_ghost(Car *ghost)
{
    const Game *g = &g_game;
    if (!g->lap_active || !best_count) return 0;
    s32 i = g->lap_steps / 2;
    if (i >= best_count) i = best_count - 1;
    const Sample *s = &best[i];
    u8 *p = (u8 *)ghost;
    for (u32 k = 0; k < sizeof(*ghost); k++) p[k] = 0;
    ghost->x = s->x << 8; ghost->y = s->y << 8; ghost->z = s->z << 8;
    ghost->heading = s->h << 6; ghost->pitch = s->p << 8; ghost->roll = s->r << 8;
    ghost->mode = CAR_GROUND;
    return 1;
}

s32 game_arrow(s32 x, s32 z)
{
    const Game *g = &g_game;
    if (!g->lap_active) return -1;
    const Gate *gt = &gates[g->next_cp & 3];
    s32 dx = (gt->ax + gt->bx) / 2 - x, dz = (gt->az + gt->bz) / 2 - z;
    return iatan2(dx, dz) & 1023;
}

void game_time_text(char *buf, s32 steps) { put_time(buf, steps); }

void *game_ghost_data(s32 **count, s32 *max_bytes)
{
    *count = &best_count;
    *max_bytes = sizeof(best);
    return best;
}
