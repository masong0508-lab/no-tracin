// No Tracin': Race Drivin' x Payback x Virtua Racing for the GBA.
// Milestone 1: drivable flat-shaded 3D city with three camera views.
// Controls: A gas, B brake/reverse, d-pad steer, SELECT change camera.
#include "gba.h"

#define BLOCK      512                // city block pitch in world units
#define ROAD_HALF  64                 // half the road width
#define BLOCKS     8                  // city is BLOCKS x BLOCKS
#define WORLD      (BLOCK * BLOCKS)
#define MAX_SOLIDS 160
#define CAR_RADIUS 24

typedef struct { s32 x0, z0, x1, z1, h; u8 material; } Solid;

static Solid solids[MAX_SOLIDS] EWRAM_BSS;
static s32   solid_count;
static u8    block_kind[BLOCKS][BLOCKS];
static u8    block_first[BLOCKS][BLOCKS];   // first solid index in each block
static u8    block_count[BLOCKS][BLOCKS];

volatile u32 g_frames;      // read by the test harness to measure speed
volatile u32 g_prof[4];     // cycles: clear, city, flush, (scratch)

static inline u32 cycles(void) { return REG_TM0D | (REG_TM1D << 16); }

// ---------------------------------------------------------------- city

static u32 hash(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352d;
    x ^= x >> 15; x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static void add_solid(s32 x0, s32 z0, s32 x1, s32 z1, s32 h, s32 material)
{
    if (solid_count >= MAX_SOLIDS) return;
    Solid *s = &solids[solid_count++];
    s->x0 = x0; s->z0 = z0; s->x1 = x1; s->z1 = z1; s->h = h; s->material = material;
}

static void build_city(void)
{
    for (s32 bz = 0; bz < BLOCKS; bz++)
        for (s32 bx = 0; bx < BLOCKS; bx++) {
            u32 r = hash(bx * 131 + bz * 7919 + 17);
            s32 kind = r % 6;
            block_kind[bz][bx] = kind;
            s32 x0 = bx * BLOCK + ROAD_HALF + 24, x1 = (bx + 1) * BLOCK - ROAD_HALF - 24;
            s32 z0 = bz * BLOCK + ROAD_HALF + 24, z1 = (bz + 1) * BLOCK - ROAD_HALF - 24;
            s32 mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
            s32 mat = M_BLD0 + (r >> 8) % 6;
            s32 h = 120 + (r >> 12) % 280;
            block_first[bz][bx] = solid_count;
            switch (kind) {
            case 0:   // park with trees
                add_solid(mx - 90, mz - 90, mx - 50, mz - 50, 70, M_TREE);
                add_solid(mx + 40, mz + 30, mx + 84, mz + 74, 90, M_TREE);
                break;
            case 1: case 2:   // one big building
                add_solid(x0, z0, x1, z1, h, mat);
                break;
            case 3:   // two buildings
                add_solid(x0, z0, mx - 12, z1, h, mat);
                add_solid(mx + 12, z0, x1, z1, h / 2 + 60, M_BLD0 + (r >> 20) % 6);
                break;
            default:  // four small buildings
                add_solid(x0, z0, mx - 12, mz - 12, h / 2 + 40, mat);
                add_solid(mx + 12, z0, x1, mz - 12, h, M_BLD0 + (r >> 18) % 6);
                add_solid(x0, mz + 12, mx - 12, z1, h / 3 + 60, M_BLD0 + (r >> 22) % 6);
                add_solid(mx + 12, mz + 12, x1, z1, h / 2 + 80, mat);
                break;
            }
            block_count[bz][bx] = solid_count - block_first[bz][bx];
        }
}

static s32 visible(s32 x, s32 z, s32 radius)
{
    s32 side, depth = r_depth(x, z, &side);
    if (depth < -radius || depth > 1700 + radius) return 0;
    if (side < 0) side = -side;
    return side < depth * 2 + radius * 2;
}

static void draw_city(s32 car_x, s32 car_z)
{
    u8 shown[BLOCKS * BLOCKS];
    s32 shown_count = 0;
    // Ground layer: sidewalks and grass, then lane dashes near the car.
    for (s32 bz = 0; bz < BLOCKS; bz++)
        for (s32 bx = 0; bx < BLOCKS; bx++) {
            s32 cx = bx * BLOCK + BLOCK / 2, cz = bz * BLOCK + BLOCK / 2;
            if (!visible(cx, cz, BLOCK)) continue;
            shown[shown_count++] = bz * BLOCKS + bx;
            s32 x0 = bx * BLOCK + ROAD_HALF, x1 = (bx + 1) * BLOCK - ROAD_HALF;
            s32 z0 = bz * BLOCK + ROAD_HALF, z1 = (bz + 1) * BLOCK - ROAD_HALF;
            Vec3 q[4] = { { x0, 0, z1 }, { x1, 0, z1 }, { x1, 0, z0 }, { x0, 0, z0 } };
            r_ground(q, 4, COLOR(M_SIDEWALK, 0));
            if (block_kind[bz][bx] == 0) {
                Vec3 g[4] = { { x0 + 24, 0, z1 - 24 }, { x1 - 24, 0, z1 - 24 },
                              { x1 - 24, 0, z0 + 24 }, { x0 + 24, 0, z0 + 24 } };
                r_ground(g, 4, COLOR(M_GRASS, 0));
            }
        }

    const s32 reach = 560, dash = 40, gap = 88, half_w = 3;
    for (s32 k = 0; k <= BLOCKS; k++) {
        s32 line = k * BLOCK;
        // Roads running along z (constant x).
        if (car_x - line < reach && line - car_x < reach) {
            s32 start = ((car_z - reach) / (dash + gap)) * (dash + gap);
            for (s32 z = start; z < car_z + reach; z += dash + gap) {
                if (z < 0 || z > WORLD || ((z + dash / 2) % BLOCK) < ROAD_HALF + dash) continue;
                Vec3 q[4] = { { line - half_w, 0, z + dash }, { line + half_w, 0, z + dash },
                              { line + half_w, 0, z }, { line - half_w, 0, z } };
                r_ground(q, 4, COLOR(M_LINE, 0));
            }
        }
        // Roads running along x (constant z).
        if (car_z - line < reach && line - car_z < reach) {
            s32 start = ((car_x - reach) / (dash + gap)) * (dash + gap);
            for (s32 x = start; x < car_x + reach; x += dash + gap) {
                if (x < 0 || x > WORLD || ((x + dash / 2) % BLOCK) < ROAD_HALF + dash) continue;
                Vec3 q[4] = { { x, 0, line + half_w }, { x + dash, 0, line + half_w },
                              { x + dash, 0, line - half_w }, { x, 0, line - half_w } };
                r_ground(q, 4, COLOR(M_LINE, 0));
            }
        }
    }

    g_prof[3] = cycles();
    for (s32 b = 0; b < shown_count; b++) {
        s32 bz = shown[b] / BLOCKS, bx = shown[b] % BLOCKS;
        s32 first = block_first[bz][bx], end = first + block_count[bz][bx];
        for (s32 i = first; i < end; i++) {
            const Solid *s = &solids[i];
            if (!visible((s->x0 + s->x1) / 2, (s->z0 + s->z1) / 2, 240)) continue;
            r_box(s->x0, 0, s->z0, s->x1, s->h, s->z1, s->material);
        }
    }
}

// ---------------------------------------------------------------- car

typedef struct {
    s32 x, z;        // position in 1/16 world units
    s32 heading;     // 1024-unit angle, 0 = +z
    s32 speed;       // 1/16 world units per frame
} Car;

static s32 hits_solid(s32 x, s32 z)
{
    if (x < CAR_RADIUS || z < CAR_RADIUS || x > WORLD - CAR_RADIUS || z > WORLD - CAR_RADIUS)
        return 1;
    g_prof[3] = cycles();
    for (s32 i = 0; i < solid_count; i++) {
        const Solid *s = &solids[i];
        if (x > s->x0 - CAR_RADIUS && x < s->x1 + CAR_RADIUS &&
            z > s->z0 - CAR_RADIUS && z < s->z1 + CAR_RADIUS)
            return 1;
    }
    return 0;
}

static void update_car(Car *car, u16 keys)
{
    const s32 max_speed = 400, max_reverse = -120;

    if (keys & KEY_A)       car->speed += 5;
    else if (keys & KEY_B)  car->speed -= car->speed > 0 ? 10 : 3;
    else if (car->speed > 0) car->speed -= 2;
    else if (car->speed < 0) car->speed += 2;
    if (car->speed > max_speed) car->speed = max_speed;
    if (car->speed < max_reverse) car->speed = max_reverse;

    // Steering grip grows with speed and flips in reverse.
    s32 grip = car->speed < 0 ? -car->speed : car->speed;
    if (grip > 160) grip = 160;
    s32 turn = (grip * 9) / 160;
    if (car->speed < 0) turn = -turn;
    if (keys & KEY_LEFT)  car->heading -= turn;
    if (keys & KEY_RIGHT) car->heading += turn;

    s32 nx = car->x + ((car->speed * isin(car->heading)) >> 14);
    s32 nz = car->z + ((car->speed * icos(car->heading)) >> 14);
    if (!hits_solid(nx >> 4, nz >> 4)) {
        car->x = nx;
        car->z = nz;
    } else if (!hits_solid(nx >> 4, car->z >> 4)) {
        car->x = nx;                       // scrape along a wall facing z
        car->speed -= car->speed / 8;
    } else if (!hits_solid(car->x >> 4, nz >> 4)) {
        car->z = nz;                       // scrape along a wall facing x
        car->speed -= car->speed / 8;
    } else {
        car->speed = -car->speed / 3;      // head-on: bounce back
    }
}

static void draw_car(const Car *car)
{
    s32 x = car->x >> 4, z = car->z >> 4, h = car->heading;
    r_box_rot(x, 0, z, h, -22, 4, -24, -12, 18, -8, M_TIRE);
    r_box_rot(x, 0, z, h, 12, 4, -24, 22, 18, -8, M_TIRE);
    r_box_rot(x, 0, z, h, -22, 4, 20, -12, 18, 36, M_TIRE);
    r_box_rot(x, 0, z, h, 12, 4, 20, 22, 18, 36, M_TIRE);
    r_box_rot(x, 0, z, h, -20, 8, -42, 20, 24, 44, M_CAR);
    r_box_rot(x, 0, z, h, -16, 24, -22, 16, 38, 14, M_GLASS);
    r_box_rot(x, 0, z, h, -18, 24, -42, 18, 30, -34, M_CAR_TRIM);   // spoiler
}

// ---------------------------------------------------------------- camera

enum { VIEW_CHASE, VIEW_OVERHEAD, VIEW_COCKPIT, VIEW_COUNT };

static void place_camera(Camera *cam, const Car *car, s32 view, s32 *smooth_yaw)
{
    s32 diff = ((car->heading - *smooth_yaw + 512) & 1023) - 512;
    *smooth_yaw += view == VIEW_COCKPIT ? diff : diff / 5;

    s32 yaw = *smooth_yaw;
    s32 fx = isin(yaw), fz = icos(yaw);
    s32 back, height;
    switch (view) {
    case VIEW_OVERHEAD: back = 200; height = 440; cam->pitch = 190; break;
    case VIEW_COCKPIT:  back = -6;  height = 32;  cam->pitch = 8;   break;
    default:            back = 190; height = 72;  cam->pitch = 36;  break;
    }
    cam->x = (car->x >> 4) - ((fx * back) >> 14);
    cam->z = (car->z >> 4) - ((fz * back) >> 14);
    cam->y = height;
    cam->yaw = yaw;
}

// ---------------------------------------------------------------- text

static const char glyph_chars[] = "NOTRACI'PESHVDKM0123456789";
static const char *const glyphs[] = {
    "X...XXX..XX.X.XX..XXX...XX...XX...X",   // N
    ".XXX.X...XX...XX...XX...XX...X.XXX.",   // O
    "XXXXX..X....X....X....X....X....X..",   // T
    "XXXX.X...XX...XXXXX.X.X..X..X.X...X",   // R
    ".XXX.X...XX...XXXXXXX...XX...XX...X",   // A
    ".XXX.X...XX....X....X....X...X.XXX.",   // C
    ".XXX...X....X....X....X....X...XXX.",   // I
    "..X....X...X.......................",   // '
    "XXXX.X...XX...XXXXX.X....X....X....",   // P
    "XXXXXX....X....XXXX.X....X....XXXXX",   // E
    ".XXXXX....X.....XXX.....X....XXXXX.",   // S
    "X...XX...XX...XXXXXXX...XX...XX...X",   // H
    "X...XX...XX...XX...XX...X.X.X...X..",   // V
    "XXXX.X...XX...XX...XX...XX...XXXXX.",   // D
    "X...XX..X.X.X..XX...X.X..X..X.X...X",   // K
    "X...XXX.XXX.X.XX.X.XX...XX...XX...X",   // M
    ".XXX.X...XX..XXX.X.XXX..XX...X.XXX.",   // 0
    "..X...XX....X....X....X....X...XXX.",   // 1
    ".XXX.X...X....X...X...X...X...XXXXX",   // 2
    "XXXXX...X...X.....X.....XX...X.XXX.",   // 3
    "...X...XX..X.X.X..X.XXXXX...X....X.",   // 4
    "XXXXXX....XXXX.....X....XX...X.XXX.",   // 5
    "..XX..X...X....XXXX.X...XX...X.XXX.",   // 6
    "XXXXX....X...X...X...X....X....X...",   // 7
    ".XXX.X...XX...X.XXX.X...XX...X.XXX.",   // 8
    ".XXX.X...XX...X.XXXX....X...X..XX..",   // 9
};

// Scale must be even so every block lines up with Mode 4's 2-pixel writes.
static void draw_text(s32 x, s32 y, const char *text, s32 scale, u8 color)
{
    for (; *text; text++, x += 6 * scale) {
        s32 g = -1;
        for (s32 i = 0; glyph_chars[i]; i++)
            if (glyph_chars[i] == *text) { g = i; break; }
        if (g < 0) continue;
        const char *rows = glyphs[g];
        for (s32 i = 0; i < 35 && rows[i]; i++)
            if (rows[i] == 'X')
                r_rect(x + (i % 5) * scale, y + (i / 5) * scale, scale, scale, color);
    }
}

static s32 text_width(const char *text, s32 scale)
{
    s32 n = 0;
    while (text[n]) n++;
    return n * 6 * scale - scale;
}

static void draw_number(s32 x, s32 y, s32 value, s32 scale, u8 color)
{
    char buf[8];
    s32 i = 7;
    buf[i] = 0;
    do { buf[--i] = '0' + value % 10; value /= 10; } while (value && i > 0);
    draw_text(x, y, buf + i, scale, color);
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
    u16 prev = 0;
    for (;;) {
        u16 keys = ~REG_KEYINPUT;
        if ((keys & KEY_START) && !(prev & KEY_START)) break;
        prev = keys;

        // Slow orbit over the city while the title shows.
        s32 a = t * 2;
        cam.x = WORLD / 2 - ((isin(a) * 1500) >> 14);
        cam.z = WORLD / 2 - ((icos(a) * 1500) >> 14);
        cam.y = 520;
        cam.yaw = a;
        cam.pitch = 90;
        volatile u16 *page = back_page();
        r_begin(page, &cam);
        draw_city(WORLD / 2, WORLD / 2);
        r_flush();

        const char *title = "NO TRACIN'";
        draw_text((SCREEN_W - text_width(title, 4)) / 2 + 2, 28, title, 4, COLOR(M_HUD_BG, 0));
        draw_text((SCREEN_W - text_width(title, 4)) / 2, 26, title, 4, COLOR(M_HUD, 0));
        if ((t >> 4) & 1) {
            const char *press = "PRESS START";
            draw_text((SCREEN_W - text_width(press, 2)) / 2, 120, press, 2, COLOR(M_LINE, 0));
        }
        wait_vblank();
        REG_DISPCNT ^= PAGE_BIT;
        t++;
        g_frames++;
    }
}

int main(void)
{
    static const char *const view_names[VIEW_COUNT] = { "CHASE", "OVERHEAD", "COCKPIT" };

    REG_WAITCNT = 0x4317;   // faster ROM access with prefetch
    // Timers 0+1 cascade into a free-running cycle counter for profiling.
    REG_TM1CNT = 0x00840000;
    REG_TM0CNT = 0x00800000;
    r_init();
    r_init_palette();
    build_city();
    REG_DISPCNT = MODE4 | BG2_ON;

    title_screen();

    Car car = { (BLOCK * 4) << 4, (BLOCK * 3 + 160) << 4, 0, 0 };
    Camera cam;
    s32 view = VIEW_CHASE, smooth_yaw = 0, label_timer = 90;
    u16 prev = 0;

    for (;;) {
        u16 keys = ~REG_KEYINPUT;
        u16 pressed = keys & ~prev;
        prev = keys;
        if (pressed & (KEY_SELECT | KEY_R)) {
            view = (view + 1) % VIEW_COUNT;
            label_timer = 90;
        }

        update_car(&car, keys);
        place_camera(&cam, &car, view, &smooth_yaw);

        volatile u16 *page = back_page();
        u32 t0 = cycles();
        r_begin(page, &cam);
        u32 t1 = cycles();
        draw_city(car.x >> 4, car.z >> 4);
        if (view != VIEW_COCKPIT) draw_car(&car);
        u32 t2 = cycles();
        r_flush();
        u32 t3 = cycles();
        g_prof[3] -= t1; g_prof[0] = t1 - t0; g_prof[1] = t2 - t1; g_prof[2] = t3 - t2;

        // HUD: speed readout and the current camera name.
        s32 mph = (car.speed < 0 ? -car.speed : car.speed) * 140 / 400;
        r_rect(4, 4, 76, 18, COLOR(M_HUD_BG, 0));
        draw_number(8, 6, mph, 2, COLOR(M_HUD, 0));
        draw_text(46, 6, "MPH", 2, COLOR(M_HUD, 1));
        if (label_timer > 0) {
            label_timer--;
            const char *name = view_names[view];
            s32 w = text_width(name, 2);
            r_rect((SCREEN_W - w) / 2 - 4, 136, w + 8, 22, COLOR(M_HUD_BG, 0));
            draw_text((SCREEN_W - w) / 2, 140, name, 2, COLOR(M_LINE, 0));
        }

        wait_vblank();
        REG_DISPCNT ^= PAGE_BIT;
        g_frames++;
    }
}
