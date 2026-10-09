// No Tracin': Race Drivin' x Payback x Virtua Racing for the GBA.
// A gas, B brake/reverse, L handbrake, d-pad steer, R nitro, SELECT change
// camera, START pause menu (options, saving, reset the car).
#include "gba.h"
#include "world.h"
#include "car.h"
#include "hud.h"
#include "fx.h"
#include "sound.h"
#include "game.h"
#include "menu.h"
#include "track.h"
#include "race.h"
#include "props.h"
#include "softbody.h"

#define STEP_CYCLES 280896   // one 60 Hz physics step in CPU cycles
#define OBJ_ON      0x1000
#define OBJ_1D      0x0040

volatile u32 g_frames;       // read by the test harness
Car g_car;

static inline u32 cycles(void) { return REG_TM0D | (REG_TM1D << 16); }

static u32 seed = 4321;
static s32 rnd(s32 n)
{
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) % n;
}

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

// ---------------------------------------------------------------- camera

enum { VIEW_CHASE, VIEW_FAR, VIEW_OVERHEAD, VIEW_COCKPIT, VIEW_COUNT };

typedef struct {
    s32 x, y, z;        // eye, Q8
    s32 lx, ly, lz;     // point looked at, Q8
    s32 heading;        // smoothed car heading, 65536 units
    s32 shake;          // shake amplitude, Q8 world units
    s32 ready;
    s32 prev_mode;
} CamState;

static void look_at(Camera *cam, s32 ex, s32 ey, s32 ez, s32 tx, s32 ty, s32 tz)
{
    s32 dx = (tx - ex) >> 8, dy = (ty - ey) >> 8, dz = (tz - ez) >> 8;
    cam->x = ex >> 8; cam->y = ey >> 8; cam->z = ez >> 8;
    cam->yaw = iatan2(dx, dz);
    cam->pitch = iatan2(-dy, isqrt(dx * dx + dz * dz));
}

// Once per physics step, so the camera moves the same however fast we draw.
static void camera_step(CamState *cs, const Car *c, s32 view)
{
    // Impacts shake the camera; it settles quickly.
    s32 kick = 0;
    if (c->landed > 250) kick = c->landed;
    if (c->hit > 200) kick = c->hit;
    if (c->mode == CAR_CRASH && cs->prev_mode != CAR_CRASH) kick = 2600;
    cs->prev_mode = c->mode;
    if (g_opt[OPT_SHAKE]) kick = 0;
    if (kick > cs->shake) cs->shake = kick;
    cs->shake -= cs->shake / 10 + 1;
    if (cs->shake < 0) cs->shake = 0;

    s32 diff = (s16)(c->heading - cs->heading);
    cs->heading += diff / 9;
    if (view == VIEW_COCKPIT && c->mode != CAR_CRASH) { cs->ready = 0; return; }

    s32 speed = car_speed(c), h = cs->heading >> 6;
    s32 fx = isin(c->heading >> 6), fz = icos(c->heading >> 6);
    s32 lead = (c->mode == CAR_GROUND || c->mode == CAR_AIR) ? speed / 12 : 0;   // look ahead at speed
    s32 tx = c->x + ((fx * lead) >> 6), ty = c->y + (18 << 8), tz = c->z + ((fz * lead) >> 6);
    s32 ex, ey, ez;
    if (c->mode == CAR_LOOP && view != VIEW_OVERHEAD) {
        // Side-on view of the loop so you can watch the car go round.
        ex = (the_loop.x - 460) << 8;
        ey = 150 << 8;
        ez = (the_loop.z - 40) << 8;
        tx = c->x; ty = c->y; tz = c->z;
    } else if (c->mode == CAR_CRASH && cs->ready) {
        ex = cs->x; ey = cs->y; ez = cs->z;      // hold still and watch the wreck
        tx = c->x; ty = c->y + (10 << 8); tz = c->z;
    } else {
        // Pull back and up a little as the speed builds.
        s32 back, height;
        if (view == VIEW_OVERHEAD)  { back = 300; height = MAX_BUILDING_H + 60; }   // over the rooftops
        else if (view == VIEW_FAR)  { back = 270 + speed / 40; height = 110 + speed / 90; }
        else                        { back = 168 + speed / 50; height = 62 + speed / 140; }
        ex = c->x - ((isin(h) * back) >> 6);
        ez = c->z - ((icos(h) * back) >> 6);
        ey = (c->y > 0 ? c->y : 0) + (height << 8);
    }

    s32 dx = (ex - cs->x) >> 8, dz = (ez - cs->z) >> 8;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    if (!cs->ready || dx + dz > 800) {
        cs->x = ex; cs->y = ey; cs->z = ez;
        cs->lx = tx; cs->ly = ty; cs->lz = tz;
        cs->ready = 1;
    } else {
        cs->x += (ex - cs->x) / 7;
        cs->y += (ey - cs->y) / 7;
        cs->z += (ez - cs->z) / 7;
        cs->lx += (tx - cs->lx) / 3;
        cs->ly += (ty - cs->ly) / 3;
        cs->lz += (tz - cs->lz) / 3;
    }
    // Keep the camera out of buildings and above the ground.
    s32 nx, nz, pen = world_collide(cs->x >> 8, cs->z >> 8, 20, &nx, &nz);
    if (pen > 0) {
        cs->x += (pen * nx) >> 6;
        cs->z += (pen * nz) >> 6;
    }
    s32 floor = world_height(cs->x >> 8, cs->z >> 8);
    if (floor < 0) floor = 0;
    if (cs->y < floor + (24 << 8)) cs->y = floor + (24 << 8);
}

// Once per frame: the final camera, with shake.
static void camera_frame(Camera *cam, CamState *cs, const Car *c, s32 view)
{
    s32 s = cs->shake, mph = (car_speed(c) * 67) / 2560;
    if (c->mode == CAR_GROUND && mph > 70 && !g_opt[OPT_SHAKE] && s < (mph - 70) * 12) s = (mph - 70) * 12;   // road rumble
    s32 jx = s ? rnd(2 * s + 1) - s : 0, jy = s ? rnd(2 * s + 1) - s : 0;

    if (view == VIEW_COCKPIT && c->mode != CAR_CRASH) {
        // Driver's eye, turning and pitching with the car (even round the loop).
        s32 h = c->heading >> 6, p = c->pitch >> 8;
        s32 up = 30, fwd = 4;
        s32 y1 = (up * icos(p) + fwd * isin(p)) >> 14;
        s32 z1 = (fwd * icos(p) - up * isin(p)) >> 14;
        cam->x = (c->x >> 8) + ((z1 * isin(h)) >> 14);
        cam->z = (c->z >> 8) + ((z1 * icos(h)) >> 14);
        cam->y = ((c->y + c->bob + jy / 2) >> 8) + y1;
        cam->yaw = h;
        cam->pitch = -p + (jx >> 9);
        return;
    }
    look_at(cam, cs->x + jx, cs->y + jy, cs->z, cs->lx, cs->ly, cs->lz);
}

// ---------------------------------------------------------------- frame helpers

static inline void wait_vblank(void)
{
    while (REG_VCOUNT >= SCREEN_H) {}
    while (REG_VCOUNT < SCREEN_H) {}
}

volatile u16 *back_page(void)
{
    return (REG_DISPCNT & PAGE_BIT) ? VRAM_PAGE0 : VRAM_PAGE1;
}

void present(void)
{
    hud_end();
    wait_vblank();
    REG_DISPCNT ^= PAGE_BIT;
    hud_commit();
    g_frames++;
}

// ---------------------------------------------------------------- front end

// Menu colours (fixed palette entries, untouched by the time of day).
#define C_YELLOW COLOR(M_HUD, 0)
#define C_WHITE  COLOR(M_HUD, 1)
#define C_GREY   COLOR(M_HUD, 2)
#define C_CYAN   COLOR(M_HUD, 3)
#define C_PANEL  COLOR(M_HUD_BG, 0)
#define C_HILITE COLOR(M_HUD_BG, 1)
#define C_INK    COLOR(M_HUD_BG, 2)
#define C_RED    COLOR(M_HUD_BG, 3)

static void panel(volatile u16 *page, s32 x0, s32 y0, s32 x1, s32 y1, u32 c)
{
    u16 pair = c | (c << 8);
    if (y0 < 0) y0 = 0;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    for (s32 y = y0; y < y1; y++) {
        volatile u16 *row = page + y * (SCREEN_W / 2);
        for (s32 x = x0 >> 1; x < (x1 >> 1); x++) row[x] = pair;
    }
}

// A box with a one-pixel border.
static void box(volatile u16 *page, s32 x0, s32 y0, s32 x1, s32 y1, u32 fill, u32 edge)
{
    panel(page, x0, y0, x1, y1, edge);
    panel(page, x0 + 2, y0 + 1, x1 - 2, y1 - 1, fill);
}

// The arcade look: a red band across the top with the screen's name.
static void header(volatile u16 *page, const char *title)
{
    panel(page, 0, 4, SCREEN_W, 26, C_RED);
    panel(page, 0, 26, SCREEN_W, 28, C_YELLOW);
    hud_text_centered(9, title, 1, PAL_WHITE);
}

// Camera gliding along a circuit (title and menu backdrops).
static void flyover(Camera *cam, s32 t, s32 *focus_x, s32 *focus_z)
{
    s32 d = t * 9, x, z, y, h, hint = 0, tx, tz, ty;
    track_point(d, -60, &x, &z, &y, &h, &hint);
    track_point(d + 420, 20, &tx, &tz, &ty, &h, &hint);
    look_at(cam, x << 8, y + (70 << 8), z << 8, tx << 8, ty + (10 << 8), tz << 8);
    *focus_x = x;
    *focus_z = z;
}

static void scene_frame(s32 t)
{
    Camera cam;
    s32 fx, fz;
    r_set_far(FAR_MENU, FAR_MENU, FOG1_TRACK, FOG2_TRACK);
    flyover(&cam, t, &fx, &fz);
    r_begin(back_page(), &cam);
    world_draw(fx, fz);
    r_flush();
}

static u16 pressed_keys(u16 *prev)
{
    u16 keys = ~REG_KEYINPUT & 0x3FF, p = keys & ~*prev;
    *prev = keys;
    return p;
}

static void show_track(s32 i)
{
    if (g_track != &g_tracks[i]) {
        track_load(i);
        options_apply();
    }
}

static void title_screen(s32 *t)
{
    u16 prev = ~REG_KEYINPUT;
    sound_silence();
    show_track(0);
    for (;;) {
        u16 p = pressed_keys(&prev);
        if (p & (KEY_START | KEY_A)) { sound_play(SFX_START); return; }
        scene_frame(*t);
        hud_begin();
        hud_text_huge(26, "NO TRACIN'", PAL_YELLOW);
        hud_text_centered(56, "VIRTUA EDITION", 1, PAL_WHITE);
        if ((*t >> 5) & 1) hud_text_centered(112, "PRESS START", 1, PAL_YELLOW);
        hud_text_centered(148, "3 COURSES  7 RIVALS  STUNT CITY", 0, PAL_WHITE);
        present();
        (*t)++;
    }
}

enum { MODE_ARCADE, MODE_FREE, MODE_CITY, MODE_LOAD, MODE_OPTIONS, MODE_RECORDS, MODE_COUNT };

// Returns a MODE_*, or -1 to go back to the title.
static s32 mode_select(s32 *t, s32 *sel)
{
    static const char *const names[MODE_COUNT] = {
        "ARCADE", "FREE RUN", "STUNT CITY", "LOAD GAME", "OPTIONS", "RECORDS",
    };
    static const char *const about[MODE_COUNT][2] = {
        { "RACE 7 CARS", "BEAT THE CLOCK" }, { "PRACTICE LAPS", "NO TIME LIMIT" },
        { "OPEN WORLD", "CRASH AND SMASH" }, { "STUNT CITY", "SAVE SLOTS" },
        { "GAME SETUP", "" }, { "STUNT RECORDS", "" },
    };
    u16 prev = ~REG_KEYINPUT;
    for (;;) {
        u16 p = pressed_keys(&prev);
        if (p & KEY_UP)   { *sel = (*sel + MODE_COUNT - 1) % MODE_COUNT; sound_play(SFX_BEEP); }
        if (p & KEY_DOWN) { *sel = (*sel + 1) % MODE_COUNT; sound_play(SFX_BEEP); }
        if (p & KEY_B) return -1;
        if (p & (KEY_A | KEY_START)) { sound_play(SFX_CHECKPOINT); return *sel; }

        scene_frame(*t);
        volatile u16 *page = back_page();
        hud_begin();
        header(page, "MODE SELECT");
        for (s32 i = 0; i < MODE_COUNT; i++) {
            s32 y = 36 + i * 18, on = i == *sel;
            box(page, 10, y, 122, y + 15, on ? C_RED : C_PANEL, on ? C_YELLOW : C_GREY);
            hud_text(20, y + 4, names[i], 0, on ? PAL_YELLOW : PAL_WHITE);
        }
        box(page, 130, 36, 230, 84, C_PANEL, C_CYAN);
        hud_text(138, 44, about[*sel][0], 0, PAL_CYAN);
        hud_text(138, 56, about[*sel][1], 0, PAL_WHITE);
        if ((*t >> 4) & 1 && *sel < 2) hud_text(138, 72, "3 COURSES", 0, PAL_YELLOW);
        hud_text_centered(150, "A SELECT  B BACK", 0, PAL_WHITE);
        present();
        (*t)++;
    }
}

// Returns the circuit to race on, or -1 for back.
static s32 course_select(s32 *t, s32 *sel, s32 mode)
{
    static const u8 level_pal[TRACK_COUNT] = { PAL_GREEN, PAL_YELLOW, PAL_RED };
    u16 prev = ~REG_KEYINPUT;
    for (;;) {
        u16 p = pressed_keys(&prev);
        if (p & KEY_LEFT)  { *sel = (*sel + TRACK_COUNT - 1) % TRACK_COUNT; sound_play(SFX_BEEP); *t = 0; }
        if (p & KEY_RIGHT) { *sel = (*sel + 1) % TRACK_COUNT; sound_play(SFX_BEEP); *t = 0; }
        if (p & KEY_B) return -1;
        if (p & (KEY_A | KEY_START)) { sound_play(SFX_START); return *sel; }
        show_track(*sel);
        const TrackDef *td = &g_tracks[*sel];

        scene_frame(*t);
        volatile u16 *page = back_page();
        hud_begin();
        header(page, mode == MODE_ARCADE ? "ARCADE" : "FREE RUN");
        for (s32 i = 0; i < TRACK_COUNT; i++) {
            s32 x0 = 4 + i * 78, on = i == *sel;
            box(page, x0, 34, x0 + 76, 50, on ? C_RED : C_PANEL, on ? C_YELLOW : C_GREY);
            hud_text(x0 + 38 - hud_text_width(g_tracks[i].name, 0) / 2, 39, g_tracks[i].name, 0,
                     on ? PAL_YELLOW : PAL_WHITE);
        }
        // Course map with a dot following the camera, and the course facts.
        box(page, 6, 56, 116, 142, C_PANEL, C_CYAN);
        track_map(page, *sel, 10, 60, 102, 78, C_WHITE, (*t * 9) % td->lap, C_YELLOW);
        box(page, 122, 56, 234, 142, C_PANEL, C_CYAN);
        char buf[24], *q;
        hud_text(130, 62, td->level, 0, level_pal[*sel]);
        q = put(buf, "LAPS ");
        if (mode == MODE_ARCADE) put_num(q, td->laps); else put(q, "FREE");
        hud_text(130, 76, buf, 0, PAL_WHITE);
        q = put(buf, "LENGTH ");
        q = put_num(q, td->lap / 20 / 1000);
        q = put(q, ".");
        q = put_num(q, (td->lap / 20 / 100) % 10);
        put(q, " KM");
        hud_text(130, 88, buf, 0, PAL_WHITE);
        hud_text(130, 102, "BEST LAP", 0, PAL_CYAN);
        s32 b = race_best_lap(*sel);
        if (b) { q = put_num(buf, b / 3600); *q++ = '\''; *q++ = '0' + (b / 60 % 60) / 10; *q++ = '0' + b / 60 % 10;
                 *q++ = '"'; *q++ = '0' + (b % 60 * 100 / 60) / 10; *q++ = '0' + (b % 60 * 100 / 60) % 10; *q = 0; }
        else put(buf, "--");
        hud_text_right(228, 102, buf, 0, PAL_WHITE);
        hud_text(130, 116, "BEST RACE", 0, PAL_CYAN);
        b = race_best_time(*sel);
        if (b) { q = put_num(buf, b / 3600); *q++ = '\''; *q++ = '0' + (b / 60 % 60) / 10; *q++ = '0' + b / 60 % 10;
                 *q++ = '"'; *q++ = '0' + (b % 60 * 100 / 60) / 10; *q++ = '0' + (b % 60 * 100 / 60) % 10; *q = 0; }
        else put(buf, "--");
        hud_text_right(228, 128, buf, 0, PAL_WHITE);
        hud_text(10, 150, "< >", 0, (*t >> 3) & 1 ? PAL_YELLOW : PAL_WHITE);
        hud_text_right(234, 150, "A RACE  B BACK", 0, PAL_WHITE);
        present();
        (*t)++;
    }
}

// ---------------------------------------------------------------- HUD

static void draw_hud(const Car *car, const Camera *cam, s32 view, s32 label_timer, s32 countdown)
{
    static const char *const view_names[VIEW_COUNT] = { "CHASE", "FAR CHASE", "OVERHEAD", "COCKPIT" };
    const Game *g = &g_game;
    char buf[24], *p;

    if (g_track) {
        if (countdown > 0) hud_text_huge(60, countdown > 120 ? "3" : countdown > 60 ? "2" : "1", PAL_YELLOW);
        else if (g->msg_timer > 0) {
            hud_text_centered(56, g->msg1, 1, g->msg_pal);
            if (g->msg2[0]) hud_text_centered(74, g->msg2, 0, PAL_WHITE);
        }
        if (label_timer > 0) hud_text_centered(120, view_names[view], 0, PAL_WHITE);
        (void)cam;
        return;
    }

    // Speed, revs and gear.
    s32 mph = (car_speed(car) * 67) / 2560;
    put_num(buf, g_opt[OPT_UNITS] ? mph * 16 / 10 : mph);
    hud_text_right(40, 4, buf, 1, PAL_YELLOW);
    hud_text(44, 10, g_opt[OPT_UNITS] ? "KMH" : "MPH", 0, PAL_YELLOW);
    s32 full = !g_opt[OPT_HUD];
    if (full) hud_tach(4, 21, car->rpm);
    buf[0] = car->gear == 0 ? 'R' : '0' + car->gear;
    buf[1] = 0;
    if (full) {
        hud_text(78, 21, buf, 0, PAL_WHITE);
        hud_text(4, 31, "N", 0, PAL_CYAN);
        hud_bar(12, 31, 10, (g->nitro * 10 + NITRO_MAX - 1) / NITRO_MAX, PAL_CYAN);
        p = put(buf, "*");
        p = put_num(p, game_stars());
        p = put(p, "/");
        put_num(p, STAR_COUNT);
        hud_text(4, 41, buf, 0, PAL_YELLOW);
        if (sb_valid(car)) {
            // How bent the soft-body car is.
            s32 d = sb_damage();
            p = put(buf, "DMG ");
            p = put_num(p, d);
            put(p, "%");
            hud_text(4, 51, buf, 0, d < 25 ? PAL_WHITE : d < 60 ? PAL_YELLOW : PAL_RED);
        }
    }

    // Score, combo and lap.
    if (full) {
    put_num(buf, g->score);
    hud_text_right(236, 4, buf, 1, PAL_WHITE);
    if (g->combo > 1 && g->combo_timer > 0) {
        p = put(buf, "COMBO X");
        put_num(p, g->combo);
        hud_text_right(236, 21, buf, 0, (g->combo_timer & 8) || g->combo_timer > 60 ? PAL_YELLOW : PAL_RED);
    }
    if (g->lap_active) {
        p = put(buf, "LAP ");
        game_time_text(p, g->lap_steps);
        hud_text_right(236, 31, buf, 0, PAL_WHITE);
        if (g->best_steps) {
            p = put(buf, "BEST ");
            game_time_text(p, g->best_steps);
            hud_text_right(236, 41, buf, 0, PAL_CYAN);
        }
        s32 a = game_arrow(car->x >> 8, car->z >> 8);
        if (a >= 0) hud_arrow(120, 14, a - cam->yaw, g->cp_flash ? PAL_GREEN : PAL_YELLOW);
        if (g->cp_flash > 60) {
            p = put(buf, "CHECKPOINT ");
            p = put_num(p, g->next_cp - 1);
            put(p, "/3");
            hud_text_centered(30, buf, 0, PAL_GREEN);
        }
    } else if (g->best_steps) {
        p = put(buf, "BEST ");
        game_time_text(p, g->best_steps);
        hud_text_right(236, 31, buf, 0, PAL_CYAN);
    }
    }

    // Countdown, stunt messages, live drift and airtime.
    if (countdown > 0) {
        hud_text_huge(52, countdown > 120 ? "3" : countdown > 60 ? "2" : "1", PAL_YELLOW);
    } else if (g->msg_timer > 0) {
        hud_text_centered(44, g->msg1, 1, g->msg_pal);
        if (g->msg2[0]) hud_text_centered(62, g->msg2, 0, PAL_WHITE);
    }
    if (car->mode == CAR_AIR && car->air_steps > 12) {
        p = put(buf, "AIR ");
        p = put_num(p, car->air_steps / 60);
        *p++ = '.';
        *p++ = '0' + (car->air_steps / 6) % 10;
        *p = 0;
        hud_text_centered(118, buf, 1, PAL_WHITE);
    } else if (g->drift_steps > 20) {
        p = put(buf, "DRIFT ");
        put_num(p, g->drift_points);
        hud_text_centered(118, buf, 1, PAL_YELLOW);
    }
    if (label_timer > 0) hud_text_centered(148, view_names[view], 0, PAL_WHITE);
}

// ---------------------------------------------------------------- main loop

// Pause menu; returns 1 to quit to the title.
static s32 pause_menu(Car *car, CamState *cs)
{
    static const char *const items[8] = {
        "RESUME", "RESET CAR", "OPTIONS", "SAVE GAME", "LOAD GAME", "RECORDS", "CHEATS", "QUIT TO TITLE",
    };
    sound_silence();
    menu_freeze();
    for (;;) {
        s32 i = menu_list("PAUSED", items, 8);
        if (i <= 0) return 0;
        if (i == 1) { car_respawn(car); props_reset(); cs->ready = 0; return 0; }
        if (i == 2) menu_options();
        if (i == 3) {
            s32 slot = menu_slot("SAVE GAME", 1);
            if (slot >= 0) {
                save_slot(slot, car, g_game.score);
                game_message("GAME SAVED", "", PAL_GREEN, 90);
                return 0;
            }
        }
        if (i == 4) {
            s32 slot = menu_slot("LOAD GAME", 0), score;
            if (slot >= 0 && load_slot(slot, car, &score)) {
                game_reset();
                fx_reset();
                g_game.score = score;
                cs->ready = 0;
                game_message("GAME LOADED", "", PAL_GREEN, 90);
                return 0;
            }
        }
        if (i == 5) menu_records();
        if (i == 6) menu_cheats();
        if (i == 7) {
            if (!g_opt[OPT_AUTOSAVE]) save_system();
            return 1;
        }
    }
}

// Pause menu on a circuit: 0 resume, 1 quit, 2 restart.
static s32 race_pause(void)
{
    static const char *const items[4] = { "RESUME", "RESTART RACE", "OPTIONS", "QUIT TO MENU" };
    sound_silence();
    menu_freeze();
    for (;;) {
        s32 i = menu_list("PAUSED", items, 4);
        if (i <= 0) return 0;
        if (i == 1) return 2;
        if (i == 2) menu_options();
        if (i == 3) {
            if (!g_opt[OPT_AUTOSAVE]) save_system();
            return 1;
        }
    }
}

// Drives Freedom City (track < 0) or a race on a circuit. Returns 1 when
// the race should start again.
static s32 play(s32 load_from, s32 track, s32 race_mode)
{
    Car *car = &g_car;
    track_load(track);
    options_apply();
    if (track >= 0) r_set_far(FAR_MIN_TRACK, FAR_MAX_TRACK, FOG1_TRACK, FOG2_TRACK);
    else r_set_far(FAR_MIN_CITY, FAR_MAX_CITY, FOG1_CITY, FOG2_CITY);
    game_reset();
    fx_reset();
    if (track >= 0) race_begin(track, race_mode, car);
    else car_reset(car, the_loop.x, START_Z - 81, 0);     // the whole car behind the start line
    props_reset();
    s32 countdown = 180;
    if (track < 0 && load_from >= 0) {
        s32 score;
        if (load_slot(load_from, car, &score)) {
            g_game.score = score;
            countdown = 0;
            game_message("GAME LOADED", "", PAL_GREEN, 90);
        }
    }
    Camera cam;
    CamState cs = { 0 };
    s32 view = VIEW_CHASE, label_timer = 0, frame = 0;
    u16 prev = ~REG_KEYINPUT;
    u32 last = cycles(), acc = 0;
    static const u32 step_len[3] = { STEP_CYCLES, STEP_CYCLES * 10 / 13, STEP_CYCLES * 4 / 3 };

    for (;;) {
        u32 frame_start = cycles();
        u16 keys = ~REG_KEYINPUT;
        u16 pressed = keys & ~prev;
        prev = keys;
        if (pressed & KEY_SELECT) {
            view = (view + 1) % VIEW_COUNT;
            label_timer = 90;
            cs.ready = 0;
        }
        if ((pressed & KEY_START) && countdown <= 0) {
            if (g_track) {
                s32 r = race_pause();
                if (r) return r == 2;
            } else if (pause_menu(car, &cs)) return 0;
            prev = ~REG_KEYINPUT;
            last = cycles();
            acc = 0;
            continue;
        }

        // Fixed 60 Hz physics, however long the last frame took to draw.
        // Game speed and air slow-mo stretch or shrink the step.
        if (g_track && g_race.state != RS_RACING) keys = g_race.state == RS_GOAL ? KEY_A : KEY_B;   // coast home
        car->boost = (keys & KEY_R) && g_game.nitro > 0 && countdown <= 0 && car->mode != CAR_CRASH;
        u32 now = cycles();
        acc += now - last;
        last = now;
        s32 steps = 0;
        for (;;) {
            u32 len = step_len[g_opt[OPT_SPEED]];
            if (g_opt[OPT_SLOWMO] && car->mode == CAR_AIR) len *= 3;
            if (acc < len || steps >= 4) break;
            if (countdown > 0) {
                // Hold the car on the line; the gas still revs the engine.
                if (countdown % 60 == 0) sound_play(SFX_BEEP);
                car_step(car, 0);
                if (keys & KEY_A) {
                    car->rpm += (6800 - car->rpm) / 5;
                    car->throttle = 1;
                }
                if (--countdown == 0) {
                    sound_play(SFX_GO);
                    game_message("GO!", g_track ? "" : "CROSS THE LINE TO START A LAP", PAL_GREEN, 120);
                }
            } else {
                car_step(car, keys);
            }
            props_step(car);
            fx_step(car);
            sound_step(car);
            game_step(car);
            if (g_track) race_step(car, countdown <= 0);
            camera_step(&cs, car, view);
            acc -= len;
            steps++;
        }
        if (steps == 4) acc = 0;
        label_timer -= steps;
        if (g_new_best) {
            g_new_best = 0;
            if (!g_opt[OPT_AUTOSAVE]) save_system();
        }
        if (g_opt[OPT_PAINT] == PAINT_RAINBOW) r_set_paint(paint_color(frame));
        frame++;

        camera_frame(&cam, &cs, car, view);

        r_begin(back_page(), &cam);
        if (g_track) {
            race_draw_rivals();
            world_draw(car->x >> 8, car->z >> 8);
            fx_draw_ground();
        } else {
            world_draw(car->x >> 8, car->z >> 8);
            fx_draw_ground();
            game_draw_world();
            props_draw();
            Car ghost;
            if (!g_opt[OPT_GHOST] && game_ghost(&ghost)) car_draw(&ghost, 1);
        }
        if (view != VIEW_COCKPIT || car->mode == CAR_CRASH) car_draw(car, 0);
        r_flush();

        hud_begin();
        if (g_track) race_hud(car, frame);
        draw_hud(car, &cam, view, label_timer, countdown);
        if (!g_track) game_draw_stars(frame);
        fx_draw_sprites();

        // Pull the draw distance in when a frame comes close to two vblanks,
        // and let it back out while there is room. The haze hides the edge.
        u32 work = cycles() - frame_start;
        if (work > 520000) r_far = r_far - 50 < r_far_min ? r_far_min : r_far - 50;
        else if (work < 460000 && r_far < r_far_max) r_far += 10;
        present();
        if (g_track && race_done()) {
            race_results();
            return 0;
        }
    }
}

int main(void)
{
    REG_WAITCNT = 0x4317;          // faster ROM access with prefetch
    REG_TM1CNT = 0x00840000;       // timers 0+1: free-running cycle counter
    REG_TM0CNT = 0x00800000;
    r_init();
    world_init();
    hud_init();
    sound_init();
    save_init();
    options_apply();
    REG_DISPCNT = MODE4 | BG2_ON | OBJ_ON | OBJ_1D;

    race_records_load();

    s32 t = 0, mode = 0, course = 0;
    for (;;) {
        title_screen(&t);
        for (;;) {
            s32 m = mode_select(&t, &mode);
            if (m < 0) break;
            if (m == MODE_OPTIONS) { menu_freeze(); menu_options(); continue; }
            if (m == MODE_RECORDS) { menu_freeze(); menu_records(); continue; }
            if (m == MODE_CITY) { play(-1, -1, 0); show_track(course); continue; }
            if (m == MODE_LOAD) {
                menu_freeze();
                s32 slot = menu_slot("LOAD GAME", 0);
                if (slot >= 0) { sound_play(SFX_START); play(slot, -1, 0); show_track(course); }
                continue;
            }
            for (;;) {
                s32 c = course_select(&t, &course, m);
                if (c < 0) break;
                while (play(-1, c, m == MODE_ARCADE ? RACE_ARCADE : RACE_FREE)) {}
                show_track(course);
                t = 0;
            }
        }
    }
}
