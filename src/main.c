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
        if (view == VIEW_OVERHEAD)  { back = 210; height = 440; }
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

// ---------------------------------------------------------------- title

// Returns the save slot to continue from, or -1 for a fresh start.
static s32 title_screen(void)
{
    static const char *const items[4] = { "DRIVE", "LOAD GAME", "OPTIONS", "RECORDS" };
    Camera cam;
    s32 t = 0, sel = 0;
    u16 prev = ~REG_KEYINPUT;
    const s32 cx = (PARK_X0 + PARK_X1) / 2, cz = (PARK_Z0 + PARK_Z1) / 2;
    sound_silence();
    for (;;) {
        u16 keys = ~REG_KEYINPUT, pressed = keys & ~prev;
        prev = keys;
        if (pressed & KEY_UP)   { sel = (sel + 3) % 4; sound_play(SFX_BEEP); }
        if (pressed & KEY_DOWN) { sel = (sel + 1) % 4; sound_play(SFX_BEEP); }
        if (pressed & (KEY_A | KEY_START)) {
            if (sel == 0) break;
            menu_freeze();
            if (sel == 1) {
                s32 slot = menu_slot("LOAD GAME", 0);
                if (slot >= 0) { sound_play(SFX_START); return slot; }
            } else if (sel == 2) menu_options();
            else menu_records();
            prev = ~REG_KEYINPUT;
        }
        if (g_opt[OPT_PAINT] == PAINT_RAINBOW) r_set_paint(paint_color(t));

        // Slow orbit around the Stunt Park.
        s32 a = t * 2;
        look_at(&cam, (cx - ((isin(a) * 1100) >> 14)) << 8, 420 << 8,
                (cz - ((icos(a) * 1100) >> 14)) << 8, cx << 8, 0, cz << 8);
        r_begin(back_page(), &cam);
        world_draw(cx, cz);
        r_flush();

        hud_begin();
        hud_text_huge(14, "NO TRACIN'", PAL_YELLOW);
        hud_text_centered(44, "STUNT CITY", 1, PAL_WHITE);
        for (s32 i = 0; i < 4; i++) {
            s32 on = i == sel;
            hud_text_centered(72 + i * 14, items[i], 0, on ? PAL_YELLOW : PAL_WHITE);
            if (on) {
                s32 w = hud_text_width(items[i], 0) / 2;
                hud_text(120 - w - 12, 72 + i * 14, ">", 0, PAL_YELLOW);
                hud_text(120 + w + 6, 72 + i * 14, "<", 0, PAL_YELLOW);
            }
        }
        hud_text_centered(148, "A GAS B BRAKE L DRIFT R NITRO", 0, PAL_WHITE);
        present();
        t++;
    }
    sound_play(SFX_START);
    return -1;
}

// ---------------------------------------------------------------- HUD

static void draw_hud(const Car *car, const Camera *cam, s32 view, s32 label_timer, s32 countdown)
{
    static const char *const view_names[VIEW_COUNT] = { "CHASE", "FAR CHASE", "OVERHEAD", "COCKPIT" };
    const Game *g = &g_game;
    char buf[24], *p;

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
        if (i == 1) { car_respawn(car); cs->ready = 0; return 0; }
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

static void play(s32 load_from)
{
    Car *car = &g_car;
    car_reset(car, the_loop.x, PARK_Z0 + 36, 0);
    game_reset();
    fx_reset();
    s32 countdown = 180;
    if (load_from >= 0) {
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
            if (pause_menu(car, &cs)) return;
            prev = ~REG_KEYINPUT;
            last = cycles();
            acc = 0;
            continue;
        }

        // Fixed 60 Hz physics, however long the last frame took to draw.
        // Game speed and air slow-mo stretch or shrink the step.
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
                    game_message("GO!", "CROSS THE LINE TO START A LAP", PAL_GREEN, 120);
                }
            } else {
                car_step(car, keys);
            }
            fx_step(car);
            sound_step(car);
            game_step(car);
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
        world_draw(car->x >> 8, car->z >> 8);
        fx_draw_ground();
        game_draw_world();
        Car ghost;
        if (!g_opt[OPT_GHOST] && game_ghost(&ghost)) car_draw(&ghost, 1);
        if (view != VIEW_COCKPIT || car->mode == CAR_CRASH) car_draw(car, 0);
        r_flush();

        hud_begin();
        draw_hud(car, &cam, view, label_timer, countdown);
        game_draw_stars(frame);
        fx_draw_sprites();

        // Pull the draw distance in when a frame comes close to two vblanks,
        // and let it back out while there is room. The haze hides the edge.
        u32 work = cycles() - frame_start;
        if (work > 520000) r_far = r_far - 50 < R_FAR_MIN ? R_FAR_MIN : r_far - 50;
        else if (work < 460000 && r_far < R_FAR_MAX) r_far += 10;
        present();
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

    for (;;) play(title_screen());
}
