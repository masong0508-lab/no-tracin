// Race Drivin' GBA port: starter driving demo.
// Mode 4 (240x160, 256 colors, double buffered) with a pseudo-3D road.
// Controls: A = gas, B = brake, Left/Right = steer.

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

#define REG_DISPCNT   (*(volatile u16 *)0x04000000)
#define REG_VCOUNT    (*(volatile u16 *)0x04000006)
#define REG_KEYINPUT  (*(volatile u16 *)0x04000130)
#define PAL_BG        ((volatile u16 *)0x05000000)
#define VRAM_PAGE0    ((volatile u16 *)0x06000000)
#define VRAM_PAGE1    ((volatile u16 *)0x0600A000)

#define MODE4     0x0004
#define BG2_ON    0x0400
#define PAGE_BIT  0x0010

#define KEY_A     0x0001
#define KEY_B     0x0002
#define KEY_RIGHT 0x0010
#define KEY_LEFT  0x0020

#define RGB15(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

#define SCREEN_W  240
#define SCREEN_H  160
#define HORIZON   60
#define ROWS      (SCREEN_H - HORIZON)

enum {
    C_SKY_TOP, C_SKY_LOW, C_GRASS_A, C_GRASS_B, C_ROAD_A, C_ROAD_B,
    C_RUMBLE_A, C_RUMBLE_B, C_LANE, C_CAR, C_CAR_DARK, C_GLASS, C_TIRE,
    C_HUD, C_HUD_BG, C_HILL
};

// Track layout: curvature per segment (negative = left, positive = right).
static const s32 track[] = {
    0, 0, 0, 12, 24, 24, 12, 0, 0, -16, -32, -32, -16, 0, 0, 0,
    20, 36, 36, 20, 0, 0, -24, -24, -24, 0, 0, 0, 8, 16, 8, 0,
};
#define TRACK_LEN (sizeof(track) / sizeof(track[0]))

static s32 ztab[ROWS];    // distance for each road row
static s32 halfw[ROWS];   // road half-width in pixels for each row

static void set_palette(void)
{
    static const u16 pal[] = {
        RGB15(8, 14, 28),  RGB15(20, 24, 31), RGB15(4, 20, 4),  RGB15(3, 16, 3),
        RGB15(13, 13, 14), RGB15(11, 11, 12), RGB15(28, 2, 2),  RGB15(31, 31, 31),
        RGB15(31, 31, 31), RGB15(28, 4, 4),   RGB15(16, 2, 2),  RGB15(10, 18, 26),
        RGB15(2, 2, 3),    RGB15(31, 28, 4),  RGB15(4, 4, 6),   RGB15(10, 14, 10),
    };
    for (u32 i = 0; i < sizeof(pal) / sizeof(pal[0]); i++)
        PAL_BG[i] = pal[i];
}

static void build_tables(void)
{
    for (s32 r = 0; r < ROWS; r++) {
        ztab[r]  = 6000 / (r + 1);
        halfw[r] = 8 + (r * 112) / ROWS;
    }
}

#define IWRAM_CODE __attribute__((section(".iwram"), target("arm"), noinline))

// Fill pixel pairs [x0, x1) of a row with one color. Mode 4 needs 16-bit writes,
// so spans work in 2-pixel steps.
static inline void span(volatile u16 *row, s32 x0, s32 x1, u16 pair)
{
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    for (s32 i = x0 >> 1; i < (x1 >> 1); i++)
        row[i] = pair;
}

#define PAIR(c) ((u16)((c) | ((c) << 8)))

static void fill_rect(volatile u16 *page, s32 x, s32 y, s32 w, s32 h, u8 c)
{
    for (s32 j = y; j < y + h; j++)
        span(page + j * (SCREEN_W / 2), x, x + w, PAIR(c));
}

IWRAM_CODE static void draw_sky(volatile u16 *page, s32 scroll)
{
    // Column by column: sky down to the hill line, hill color below it.
    for (s32 x = 0; x < SCREEN_W; x += 2) {
        s32 hx  = (x + scroll) & 63;
        s32 top = HORIZON - ((hx < 32 ? hx : 63 - hx) / 2 + 2);
        volatile u16 *p = page + (x >> 1);
        for (s32 y = 0; y < HORIZON; y++, p += SCREEN_W / 2) {
            u8 c = y >= top ? C_HILL : (y < HORIZON / 2 ? C_SKY_TOP : C_SKY_LOW);
            *p = PAIR(c);
        }
    }
}

IWRAM_CODE static void draw_road(volatile u16 *page, s32 pos, s32 curve, s32 player_x)
{
    for (s32 r = 0; r < ROWS; r++) {
        volatile u16 *row = page + (HORIZON + r) * (SCREEN_W / 2);
        s32 depth  = ROWS - r;
        s32 hw     = halfw[r];
        s32 bend   = (curve * depth * depth) >> 12;      // road bends toward the horizon
        s32 center = SCREEN_W / 2 + bend - (((player_x >> 7) * hw) >> 7);
        s32 rumble = hw / 6 + 2;
        s32 lane   = hw / 24 + 1;
        s32 stripe = ((ztab[r] + pos) >> 7) & 1;

        u16 grass = stripe ? PAIR(C_GRASS_A) : PAIR(C_GRASS_B);
        u16 road  = stripe ? PAIR(C_ROAD_A) : PAIR(C_ROAD_B);
        u16 rumb  = stripe ? PAIR(C_RUMBLE_A) : PAIR(C_RUMBLE_B);

        span(row, 0, center - hw - rumble, grass);
        span(row, center - hw - rumble, center - hw, rumb);
        span(row, center - hw, center + hw, road);
        if (stripe)
            span(row, center - lane, center + lane, PAIR(C_LANE));
        span(row, center + hw, center + hw + rumble, rumb);
        span(row, center + hw + rumble, SCREEN_W, grass);
    }
}

static void draw_car(volatile u16 *page, s32 lean)
{
    s32 x = SCREEN_W / 2 - 24 + lean;
    s32 y = SCREEN_H - 30;
    fill_rect(page, x + 2,  y + 18, 10, 10, C_TIRE);
    fill_rect(page, x + 36, y + 18, 10, 10, C_TIRE);
    fill_rect(page, x,      y + 8,  48, 14, C_CAR);
    fill_rect(page, x + 8,  y,      32, 10, C_CAR_DARK);
    fill_rect(page, x + 12, y + 2,  24, 6,  C_GLASS);
    fill_rect(page, x + 4,  y + 22, 40, 2,  C_CAR_DARK);
}

static void draw_hud(volatile u16 *page, s32 speed, s32 max_speed)
{
    fill_rect(page, 4, 4, 84, 8, C_HUD_BG);
    fill_rect(page, 6, 6, (speed * 80) / max_speed, 4, C_HUD);
}

static inline void wait_vblank(void)
{
    while (REG_VCOUNT >= SCREEN_H) {}
    while (REG_VCOUNT < SCREEN_H) {}
}

int main(void)
{
    const s32 max_speed = 96;
    s32 pos = 0, speed = 0, player_x = 0, curve = 0, scroll = 0;

    set_palette();
    build_tables();
    REG_DISPCNT = MODE4 | BG2_ON;

    for (;;) {
        u16 keys = ~REG_KEYINPUT;

        // Throttle, brake, coast.
        if (keys & KEY_A)       speed += 2;
        else if (keys & KEY_B)  speed -= 4;
        else                    speed -= 1;

        // Off-road slows the car down.
        if ((player_x < 0 ? -player_x : player_x) > 14000 && speed > 32)
            speed -= 4;

        if (speed < 0) speed = 0;
        if (speed > max_speed) speed = max_speed;

        // Steering scales with speed so the car can't turn while parked.
        s32 steer = (speed * 3) >> 1;
        if (keys & KEY_LEFT)  player_x -= steer * 2;
        if (keys & KEY_RIGHT) player_x += steer * 2;

        // Ease toward the current segment's curvature, then push the car outward.
        s32 target = track[(pos >> 13) % TRACK_LEN];
        curve += (target - curve) / 8;
        player_x -= (curve * speed) >> 5;
        if (player_x < -24000) player_x = -24000;
        if (player_x >  24000) player_x =  24000;

        pos    += speed;
        scroll += (curve * speed) >> 10;

        // Draw into the hidden page, then flip.
        volatile u16 *back = (REG_DISPCNT & PAGE_BIT) ? VRAM_PAGE0 : VRAM_PAGE1;
        draw_sky(back, scroll);
        draw_road(back, pos, curve, player_x);

        s32 lean = 0;
        if (keys & KEY_LEFT)  lean = -4;
        if (keys & KEY_RIGHT) lean = 4;
        draw_car(back, lean);
        draw_hud(back, speed, max_speed);

        wait_vblank();
        REG_DISPCNT ^= PAGE_BIT;
    }
}
