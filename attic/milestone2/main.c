// No Tracin': Race Drivin' x Payback x Virtua Racing for the GBA.
// Milestone 2: stunt physics. A gas, B brake/reverse, L handbrake,
// d-pad steer, SELECT or R change camera, START reset the car.
#include "gba.h"
#include "world.h"
#include "car.h"
#include "font.h"

#define STEP_CYCLES 280896   // one 60 Hz physics step in CPU cycles

volatile u32 g_frames;       // read by the test harness
volatile u32 g_prof[4];      // cycles: clear, scene, flush; physics steps this frame
Car g_car;

static inline u32 cycles(void) { return REG_TM0D | (REG_TM1D << 16); }

// ---------------------------------------------------------------- text

static void draw_text(s32 x, s32 y, const char *text, s32 scale, u8 color)
{
    for (; *text; text++, x += 6 * scale) {
        s32 ch = *text;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) continue;
        const u8 *rows = font[ch - 32];
        for (s32 r = 0; r < 7; r++)
            for (s32 b = 0; b < 5; b++)
                if (rows[r] & (16 >> b))
                    r_rect(x + b * scale, y + r * scale, scale, scale, color);
    }
}

static s32 text_width(const char *text, s32 scale)
{
    s32 n = 0;
    while (text[n]) n++;
    return n * 6 * scale - scale;
}

static void draw_centered(s32 y, const char *text, s32 scale, u8 color, s32 backing)
{
    s32 w = text_width(text, scale), x = ((SCREEN_W - w) / 2) & ~1;
    if (backing) r_rect(x - 4, y - 3, w + 8, 7 * scale + 6, COLOR(M_HUD_BG, 0));
    draw_text(x, y, text, scale, color);
}

// Appends text or a number to a small string buffer.
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
static char *put_tenths(char *p, s32 v)
{
    p = put_num(p, v / 10);
    *p++ = '.';
    *p++ = '0' + v % 10;
    *p = 0;
    return p;
}

// ---------------------------------------------------------------- camera

enum { VIEW_CHASE, VIEW_OVERHEAD, VIEW_COCKPIT, VIEW_COUNT };

typedef struct {
    s32 x, y, z;        // Q8
    s32 heading;        // smoothed car heading, 65536 units
    s32 ready;
} CamState;

static void look_at(Camera *cam, s32 ex, s32 ey, s32 ez, s32 tx, s32 ty, s32 tz)
{
    s32 dx = (tx - ex) >> 8, dy = (ty - ey) >> 8, dz = (tz - ez) >> 8;
    cam->x = ex >> 8; cam->y = ey >> 8; cam->z = ez >> 8;
    cam->yaw = iatan2(dx, dz);
    cam->pitch = iatan2(-dy, isqrt(dx * dx + dz * dz));
}

static void update_camera(Camera *cam, CamState *cs, const Car *c, s32 view)
{
    s32 diff = (s16)(c->heading - cs->heading);
    cs->heading += diff / 5;

    if (view == VIEW_COCKPIT && c->mode != CAR_CRASH) {
        // Driver's eye, turning and pitching with the car (even round the loop).
        s32 h = c->heading >> 6, p = c->pitch >> 8;
        s32 up = 30, fwd = 4;
        s32 y1 = (up * icos(p) + fwd * isin(p)) >> 14;
        s32 z1 = (fwd * icos(p) - up * isin(p)) >> 14;
        cam->x = (c->x >> 8) + ((z1 * isin(h)) >> 14);
        cam->z = (c->z >> 8) + ((z1 * icos(h)) >> 14);
        cam->y = ((c->y + c->bob) >> 8) + y1;
        cam->yaw = h;
        cam->pitch = -p;
        cs->ready = 0;
        return;
    }

    s32 tx = c->x, ty = c->y + (16 << 8), tz = c->z;
    s32 ex, ey, ez;
    if (c->mode == CAR_LOOP && view == VIEW_CHASE) {
        // Side-on view of the loop so you can watch the car go round.
        ex = (the_loop.x - 460) << 8;
        ey = 150 << 8;
        ez = (the_loop.z - 40) << 8;
    } else if (c->mode == CAR_CRASH && cs->ready) {
        ex = cs->x; ey = cs->y; ez = cs->z;      // hold still and watch the wreck
    } else {
        s32 h = cs->heading >> 6;
        s32 back = view == VIEW_OVERHEAD ? 200 : 190;
        s32 height = view == VIEW_OVERHEAD ? 440 : 72;
        ex = c->x - ((isin(h) * back) >> 6);
        ez = c->z - ((icos(h) * back) >> 6);
        ey = (c->y > 0 ? c->y : 0) + (height << 8);
    }

    s32 far = (ex - cs->x) >> 8, farz = (ez - cs->z) >> 8;
    if (far < 0) far = -far;
    if (farz < 0) farz = -farz;
    if (!cs->ready || far + farz > 800) {
        cs->x = ex; cs->y = ey; cs->z = ez;
        cs->ready = 1;
    } else {
        cs->x += (ex - cs->x) / 4;
        cs->y += (ey - cs->y) / 4;
        cs->z += (ez - cs->z) / 4;
    }
    // Keep the camera out of buildings.
    s32 nx, nz, pen = world_collide(cs->x >> 8, cs->z >> 8, 20, &nx, &nz);
    if (pen > 0) {
        cs->x += (pen * nx) >> 6;
        cs->z += (pen * nz) >> 6;
    }
    s32 floor = world_height(cs->x >> 8, cs->z >> 8);
    if (floor < 0) floor = 0;
    if (cs->y < floor + (24 << 8)) cs->y = floor + (24 << 8);
    look_at(cam, cs->x, cs->y, cs->z, tx, ty, tz);
}

// ---------------------------------------------------------------- main loop

static inline void wait_vblank(void)
{
    while (REG_VCOUNT >= SCREEN_H) {}
    while (REG_VCOUNT < SCREEN_H) {}
}

static volatile u16 *back_page(void)
{
    return (REG_DISPCNT & PAGE_BIT) ? VRAM_PAGE0 : VRAM_PAGE1;
}

static void title_screen(void)
{
    Camera cam;
    s32 t = 0;
    u16 prev = ~REG_KEYINPUT;
    const s32 cx = (PARK_X0 + PARK_X1) / 2, cz = (PARK_Z0 + PARK_Z1) / 2;
    for (;;) {
        u16 keys = ~REG_KEYINPUT;
        if ((keys & KEY_START) && !(prev & KEY_START)) break;
        prev = keys;

        // Slow orbit around the Stunt Park.
        s32 a = t * 2;
        look_at(&cam, (cx - ((isin(a) * 1100) >> 14)) << 8, 420 << 8,
                (cz - ((icos(a) * 1100) >> 14)) << 8, cx << 8, 0, cz << 8);
        volatile u16 *page = back_page();
        r_begin(page, &cam);
        world_draw(cx, cz);
        r_flush();

        const char *title = "NO TRACIN'";
        s32 x = (SCREEN_W - text_width(title, 4)) / 2;
        draw_text(x + 2, 22, title, 4, COLOR(M_HUD_BG, 0));
        draw_text(x, 20, title, 4, COLOR(M_HUD, 0));
        draw_centered(54, "STUNT CITY", 2, COLOR(M_STUNT_WHITE, 0), 0);
        if ((t >> 3) & 1)
            draw_centered(128, "PRESS START", 2, COLOR(M_LINE, 0), 1);
        wait_vblank();
        REG_DISPCNT ^= PAGE_BIT;
        t++;
        g_frames++;
    }
}

int main(void)
{
    static const char *const view_names[VIEW_COUNT] = { "CHASE", "OVERHEAD", "COCKPIT" };

    REG_WAITCNT = 0x4317;          // faster ROM access with prefetch
    REG_TM1CNT = 0x00840000;       // timers 0+1: free-running cycle counter
    REG_TM0CNT = 0x00800000;
    r_init();
    r_init_palette();
    world_init();
    REG_DISPCNT = MODE4 | BG2_ON;

    title_screen();

    Car *car = &g_car;
    car_reset(car, the_loop.x, PARK_Z0 + 90, 0);
    Camera cam;
    CamState cs = { 0 };
    s32 view = VIEW_CHASE, label_timer = 90, msg_timer = 0;
    char msg1[24] = "", msg2[24] = "";
    u16 prev = ~REG_KEYINPUT;
    u32 last = cycles(), acc = 0;

    for (;;) {
        u16 keys = ~REG_KEYINPUT;
        u16 pressed = keys & ~prev;
        prev = keys;
        if (pressed & (KEY_SELECT | KEY_R)) {
            view = (view + 1) % VIEW_COUNT;
            label_timer = 90;
            cs.ready = 0;
        }
        if (pressed & KEY_START) {
            car_respawn(car);
            cs.ready = 0;
        }

        // Fixed 60 Hz physics, however long the last frame took to draw.
        u32 now = cycles();
        acc += now - last;
        last = now;
        s32 steps = 0;
        while (acc >= STEP_CYCLES && steps < 4) {
            car_step(car, keys);
            acc -= STEP_CYCLES;
            steps++;
        }
        if (steps == 4) acc = 0;
        g_prof[3] = steps;
        msg_timer -= steps;
        label_timer -= steps;

        // Turn stunt events into on-screen messages.
        if (car->event) {
            char *p;
            msg2[0] = 0;
            switch (car->event) {
            case EV_JUMP:
            case EV_BIG_LANDING:
                p = put(msg1, car->event == EV_JUMP ? "JUMP " : "HARD LANDING ");
                p = put_num(p, car->event_a);
                put(p, " M");
                p = put(msg2, "AIR ");
                p = put_tenths(p, car->event_b);
                put(p, " S");
                break;
            case EV_LOOP:
                put(msg1, "LOOP!");
                p = put(msg2, "EXIT ");
                p = put_num(p, car->event_a);
                put(p, " MPH");
                break;
            case EV_SPLASH: put(msg1, "SPLASH!"); break;
            case EV_FELL:   put(msg1, "TOO SLOW!"); put(msg2, "FELL OFF THE LOOP"); break;
            default:        put(msg1, "CRASH!"); break;
            }
            msg_timer = 150;
            car->event = 0;
        }

        update_camera(&cam, &cs, car, view);

        volatile u16 *page = back_page();
        u32 t0 = cycles();
        r_begin(page, &cam);
        u32 t1 = cycles();
        world_draw(car->x >> 8, car->z >> 8);
        if (view != VIEW_COCKPIT || car->mode == CAR_CRASH) car_draw(car);
        u32 t2 = cycles();
        r_flush();
        u32 t3 = cycles();
        g_prof[0] = t1 - t0; g_prof[1] = t2 - t1; g_prof[2] = t3 - t2;

        // HUD: speed, airtime, camera name and stunt messages.
        s32 mph = (car_speed(car) * 67) / 2560;
        r_rect(4, 4, 76, 18, COLOR(M_HUD_BG, 0));
        char buf[16];
        put_num(buf, mph);
        draw_text(8, 6, buf, 2, COLOR(M_HUD, 0));
        draw_text(46, 6, "MPH", 2, COLOR(M_HUD, 1));
        if (car->mode == CAR_AIR && car->air_steps > 12) {
            char *p = put(buf, "AIR ");
            put_tenths(p, car->air_steps / 6);
            r_rect(150, 4, 86, 18, COLOR(M_HUD_BG, 0));
            draw_text(154, 6, buf, 2, COLOR(M_STUNT_WHITE, 0));
        }
        if (car->skid && car->mode == CAR_GROUND && mph > 25)
            draw_centered(118, "DRIFT", 2, COLOR(M_STUNT_WHITE, 0), 1);
        if (msg_timer > 0) {
            u8 color = (car->mode == CAR_CRASH) ? COLOR(M_STUNT_RED, 0) : COLOR(M_HUD, 0);
            draw_centered(36, msg1, 2, color, 1);
            if (msg2[0]) draw_centered(56, msg2, 2, COLOR(M_STUNT_WHITE, 0), 1);
        }
        if (label_timer > 0) {
            draw_centered(140, view_names[view], 2, COLOR(M_LINE, 0), 1);
        }

        wait_vblank();
        REG_DISPCNT ^= PAGE_BIT;
        g_frames++;
    }
}
