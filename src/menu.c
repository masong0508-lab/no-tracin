// Options, menus and saves. Menus are drawn straight into the Mode 4
// bitmap over a frozen copy of the last frame, so they can hold any
// amount of text (sprites run out at 128).
#include "menu.h"
#include "game.h"
#include "sound.h"
#include "hud.h"
#include "world.h"
#include "font.h"

u8 g_opt[OPT_COUNT];

// ---------------------------------------------------------------- options

typedef struct { const char *name; u8 count; const char *values[8]; } OptDef;
static const OptDef defs[OPT_COUNT] = {
    [OPT_GRAVITY]  = { "GRAVITY",      3, { "EARTH", "MOON", "JUPITER" } },
    [OPT_ENGINE]   = { "ENGINE",       3, { "STOCK", "TUNED", "ROCKET" } },
    [OPT_TYRES]    = { "TYRES",        3, { "STREET", "RACING", "ICE" } },
    [OPT_CRASHES]  = { "CRASHES",      2, { "ON", "OFF" } },
    [OPT_SPEED]    = { "GAME SPEED",   3, { "NORMAL", "TURBO", "CHILL" } },
    [OPT_SLOWMO]   = { "AIR SLOW-MO",  2, { "OFF", "ON" } },
    [OPT_TIME]     = { "TIME OF DAY",  5, { "DAY", "SUNSET", "NIGHT", "SYNTHWAVE", "GAME BOY" } },
    [OPT_PAINT]    = { "PAINT",        8, { "RED", "BLUE", "GREEN", "GOLD", "BLACK", "WHITE", "PINK", "RAINBOW" } },
    [OPT_SHAKE]    = { "CAMERA SHAKE", 2, { "ON", "OFF" } },
    [OPT_UNITS]    = { "SPEED UNITS",  2, { "MPH", "KMH" } },
    [OPT_GHOST]    = { "GHOST CAR",    2, { "ON", "OFF" } },
    [OPT_HUD]      = { "HUD",          2, { "FULL", "CLEAN" } },
    [OPT_SOUND]    = { "SOUND",        3, { "ON", "NO ENGINE", "OFF" } },
    [OPT_AUTOSAVE] = { "AUTOSAVE",     2, { "ON", "OFF" } },
};

static const u16 paints[7] = {
    RGB15(31, 5, 3), RGB15(4, 10, 31), RGB15(4, 26, 6), RGB15(31, 24, 4),
    RGB15(6, 6, 7), RGB15(30, 30, 30), RGB15(31, 10, 22),
};

u16 paint_color(s32 frame)
{
    if (g_opt[OPT_PAINT] != PAINT_RAINBOW) return paints[g_opt[OPT_PAINT]];
    // Hue wheel: six ramps between the primaries.
    s32 h = (frame * 3) % 192, seg = h / 32, t = h % 32, d = 31 - t;
    s32 r = 0, g = 0, b = 0;
    switch (seg) {
    case 0: r = 31; g = t; break;
    case 1: r = d; g = 31; break;
    case 2: g = 31; b = t; break;
    case 3: g = d; b = 31; break;
    case 4: b = 31; r = t; break;
    default: b = d; r = 31; break;
    }
    return RGB15(r, g, b);
}

void options_apply(void)
{
    static const s16 grav[3] = { 35, 13, 54 }, power[3] = { 28000, 40000, 64000 }, grip[3] = { 38, 56, 13 };
    car_g = grav[g_opt[OPT_GRAVITY]];
    car_power = power[g_opt[OPT_ENGINE]];
    car_grip = grip[g_opt[OPT_TYRES]];
    car_crashes = g_opt[OPT_CRASHES] == 0;
    g_units_kmh = g_opt[OPT_UNITS];
    r_init_palette(g_opt[OPT_TIME], paint_color(0));
    sound_mode(g_opt[OPT_SOUND]);
}

// ---------------------------------------------------------------- bitmap drawing

#define C_YELLOW COLOR(M_HUD, 0)
#define C_WHITE  COLOR(M_HUD, 1)
#define C_GREY   COLOR(M_HUD, 2)
#define C_CYAN   COLOR(M_HUD, 3)
#define C_PANEL  COLOR(M_HUD_BG, 0)
#define C_HILITE COLOR(M_HUD_BG, 1)
#define C_INK    COLOR(M_HUD_BG, 2)
#define C_RED    COLOR(M_HUD_BG, 3)

static u16 frozen[SCREEN_W * SCREEN_H / 2] EWRAM_BSS;
static volatile u16 *page;

static void dma_copy32(volatile void *dst, const void *src, u32 words)
{
    *(volatile u32 *)0x040000D4 = (u32)src;
    *(volatile u32 *)0x040000D8 = (u32)dst;
    *(volatile u32 *)0x040000DC = 0x84000000 | words;
}

void menu_freeze(void)
{
    // The page just presented is the front one.
    volatile u16 *front = back_page() == VRAM_PAGE0 ? VRAM_PAGE1 : VRAM_PAGE0;
    dma_copy32(frozen, (const void *)front, SCREEN_W * SCREEN_H / 4);
}

static void rect(s32 x0, s32 y0, s32 x1, s32 y1, u32 c)
{
    u16 pair = c | (c << 8);
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    for (s32 y = y0; y < y1; y++) {
        volatile u16 *row = page + y * (SCREEN_W / 2);
        for (s32 x = x0 >> 1; x < (x1 >> 1); x++) row[x] = pair;
    }
}

static void pixel(s32 x, s32 y, u32 c)
{
    if ((u32)x >= SCREEN_W || (u32)y >= SCREEN_H) return;
    volatile u16 *p = page + (y * SCREEN_W + x) / 2;
    *p = (x & 1) ? ((*p & 0x00FF) | (c << 8)) : ((*p & 0xFF00) | c);
}

// 5x7 text with a drop shadow; scale 1 or 2. Returns the end x.
static s32 text(s32 x, s32 y, const char *s, u32 c, s32 scale)
{
    for (; *s; s++, x += 6 * scale) {
        s32 ch = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
        if (ch < 32 || ch > 95) continue;
        const u8 *g = font[ch - 32];
        for (s32 gy = 0; gy < 7; gy++)
            for (s32 gx = 0; gx < 5; gx++) {
                if (!((g[gy] >> (4 - gx)) & 1)) continue;
                for (s32 k = 0; k < scale * scale; k++) {
                    s32 px = x + gx * scale + k % scale, py = y + gy * scale + k / scale;
                    pixel(px + 1, py + 1, C_INK);
                    pixel(px, py, c);
                }
            }
    }
    return x;
}

static s32 width(const char *s, s32 scale) { s32 n = 0; while (s[n]) n++; return n * 6 * scale; }
static void text_c(s32 y, const char *s, u32 c, s32 scale) { text((SCREEN_W - width(s, scale)) / 2, y, s, c, scale); }
static void text_r(s32 r, s32 y, const char *s, u32 c) { text(r - width(s, 1), y, s, c, 1); }

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

// Starts a menu frame: the frozen picture, a panel and a title.
static void frame_begin(const char *title, s32 rows)
{
    page = back_page();
    dma_copy32(page, frozen, SCREEN_W * SCREEN_H / 4);
    s32 h = 30 + rows * 10;
    s32 y0 = (SCREEN_H - h) / 2 - 4;
    if (y0 < 2) y0 = 2;
    rect(14, y0, 226, y0 + h + 8, C_PANEL);
    rect(14, y0, 226, y0 + 1, C_YELLOW);
    rect(14, y0 + h + 7, 226, y0 + h + 8, C_YELLOW);
    text_c(y0 + 5, title, C_YELLOW, 2);
}

static s32 menu_top(s32 rows)
{
    s32 h = 30 + rows * 10, y0 = (SCREEN_H - h) / 2 - 4;
    return (y0 < 2 ? 2 : y0) + 26;
}

static void frame_end(void)
{
    hud_begin();
    present();
}

// Keys pressed this frame, with auto-repeat on the d-pad.
static u16 menu_keys(void)
{
    static u16 prev = 0xFFFF;
    static s32 held;
    u16 keys = ~REG_KEYINPUT & 0x3FF, pressed = keys & ~prev;
    if (keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
        if (++held > 12 && (held & 3) == 0) pressed |= keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    } else held = 0;
    prev = keys;
    return pressed;
}

static void flush_keys(void) { menu_keys(); }

// Redraw only when something changed; otherwise just wait for the next frame.
static s32 idle(u16 k, s32 *first)
{
    if (k || *first) { *first = 0; return 0; }
    while (REG_VCOUNT >= SCREEN_H) {}
    while (REG_VCOUNT < SCREEN_H) {}
    return 1;
}

// ---------------------------------------------------------------- menus

s32 menu_list(const char *title, const char *const *items, s32 n)
{
    s32 sel = 0, first = 1;
    flush_keys();
    for (;;) {
        u16 k = menu_keys();
        if (k & KEY_UP)   { sel = (sel + n - 1) % n; sound_play(SFX_BEEP); }
        if (k & KEY_DOWN) { sel = (sel + 1) % n; sound_play(SFX_BEEP); }
        if (k & (KEY_A | KEY_START)) { sound_play(SFX_CHECKPOINT); return sel; }
        if (k & KEY_B) return -1;
        if (idle(k, &first)) continue;
        frame_begin(title, n);
        s32 y = menu_top(n);
        for (s32 i = 0; i < n; i++, y += 10) {
            if (i == sel) rect(24, y - 2, 216, y + 8, C_HILITE);
            text_c(y, items[i], i == sel ? C_YELLOW : C_WHITE, 1);
        }
        frame_end();
    }
}

void menu_options(void)
{
    s32 sel = 0, n = OPT_COUNT + 1, frame = 0, first = 1;
    flush_keys();
    for (;;) {
        u16 k = menu_keys();
        if (k & KEY_UP)   sel = (sel + n - 1) % n;
        if (k & KEY_DOWN) sel = (sel + 1) % n;
        if (sel < OPT_COUNT && (k & (KEY_LEFT | KEY_RIGHT | KEY_A))) {
            s32 c = defs[sel].count;
            g_opt[sel] = (g_opt[sel] + ((k & KEY_LEFT) ? c - 1 : 1)) % c;
            options_apply();
            sound_play(SFX_BEEP);
        }
        if ((k & KEY_B) || (sel == OPT_COUNT && (k & (KEY_A | KEY_START)))) {
            if (!g_opt[OPT_AUTOSAVE]) save_system();
            return;
        }
        if (g_opt[OPT_PAINT] == PAINT_RAINBOW) r_set_paint(paint_color(frame));
        frame++;
        if (idle(k, &first)) continue;

        frame_begin("OPTIONS", n - 2);
        s32 y = menu_top(n - 2) - 4;
        for (s32 i = 0; i < n; i++, y += 9) {
            if (i == sel) rect(20, y - 1, 220, y + 8, C_HILITE);
            if (i == OPT_COUNT) { text_c(y, "DONE", i == sel ? C_YELLOW : C_WHITE, 1); break; }
            text(26, y, defs[i].name, i == sel ? C_YELLOW : C_WHITE, 1);
            const char *v = defs[i].values[g_opt[i]];
            if (i == sel) {
                text(118, y, "<", C_CYAN, 1);
                text_r(216, y, ">", C_CYAN);
                text_r(208, y, v, C_CYAN);
            } else text_r(208, y, v, C_GREY);
        }
        frame_end();
    }
}

static void time_text(char *p, s32 steps)
{
    if (!steps) { put(p, "--"); return; }
    game_time_text(p, steps);
}

void menu_records(void)
{
    s32 first = 1;
    flush_keys();
    for (;;) {
        u16 k = menu_keys();
        if (k & (KEY_A | KEY_B | KEY_START)) return;
        if (idle(k, &first)) continue;
        const Records *r = &g_rec;
        char buf[24];
        frame_begin("RECORDS", 11);
        s32 y = menu_top(11);
        const char *names[11] = { "BEST LAP", "HIGH SCORE", "STUNTS", "LOOPS", "LONGEST JUMP",
                                  "MOST AIRTIME", "TOP SPEED", "CRASHES", "TIME PLAYED",
                                  "STARS", "TRICKS" };
        for (s32 i = 0; i < 11; i++, y += 10) {
            char *p = buf;
            switch (i) {
            case 0: time_text(buf, r->best_steps); break;
            case 1: put_num(p, r->high_score); break;
            case 2: put_num(p, r->stunts); break;
            case 3: put_num(p, r->loops); break;
            case 4: p = put_num(p, r->best_jump); put(p, " M"); break;
            case 5: p = put_num(p, r->best_air / 10); *p++ = '.'; p = put_num(p, r->best_air % 10); put(p, " S"); break;
            case 6:
                p = put_num(p, g_units_kmh ? r->top_mph * 16 / 10 : r->top_mph);
                put(p, g_units_kmh ? " KMH" : " MPH");
                break;
            case 7: put_num(p, r->crashes); break;
            case 9: p = put_num(p, game_stars()); p = put(p, "/"); put_num(p, STAR_COUNT); break;
            case 10: put_num(p, r->flips); break;
            case 8:
                p = put_num(p, r->play_secs / 3600); *p++ = 'H'; *p++ = ' ';
                p = put_num(p, (r->play_secs / 60) % 60); put(p, "M");
                break;
            }
            text(30, y, names[i], C_WHITE, 1);
            text_r(210, y, buf, C_CYAN);
        }
        frame_end();
    }
}

// ---------------------------------------------------------------- cheats

// Button codes, typed on the cheat screen. Not saved: they last until power off.
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_L, B_R };
static const u8 codes[4][8] = {
    { B_UP, B_UP, B_DOWN, B_DOWN, B_LEFT, B_RIGHT, B_LEFT, B_RIGHT },   // fly
    { B_B, B_A, B_B, B_A, B_UP, B_DOWN, B_B, B_A },                     // glitch launch
    { B_L, B_R, B_L, B_R, B_L, B_R, B_UP, B_UP },                       // super hop
    { B_R, B_R, B_R, B_R, B_LEFT, B_LEFT, B_A, B_A },                   // infinite nitro
};
static const char *const cheat_names[4] = { "FLY MODE", "GLITCH LAUNCH", "SUPER HOP", "INFINITE NITRO" };

static s32 *cheat_flag(s32 i)
{
    return i == 0 ? &cheat_fly : i == 1 ? &cheat_glitch : i == 2 ? &cheat_hop : &cheat_nitro;
}

void menu_cheats(void)
{
    static const u16 bits[8] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_L, KEY_R };
    static const char sym[8] = { '^', 'V', '<', '>', 'A', 'B', 'L', 'R' };
    u8 typed[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
    s32 first = 1, flash = -1;
    flush_keys();
    for (;;) {
        u16 k = menu_keys();
        if (k & KEY_START) return;
        for (s32 b = 0; b < 8; b++) {
            if (!(k & bits[b])) continue;
            for (s32 i = 0; i < 7; i++) typed[i] = typed[i + 1];
            typed[7] = b;
            for (s32 c = 0; c < 4; c++) {
                s32 match = 1;
                for (s32 i = 0; i < 8; i++) match &= typed[i] == codes[c][i];
                if (!match) continue;
                *cheat_flag(c) = !*cheat_flag(c);
                flash = c;
                sound_play(*cheat_flag(c) ? SFX_BEST : SFX_FAIL);
                for (s32 i = 0; i < 8; i++) typed[i] = 255;
            }
        }
        if (idle(k, &first)) continue;
        frame_begin("CHEATS", 9);
        s32 y = menu_top(9);
        text_c(y, "TYPE A BUTTON CODE", C_WHITE, 1);
        char buf[20];
        for (s32 i = 0; i < 8; i++) { buf[i * 2] = typed[i] == 255 ? '-' : sym[typed[i]]; buf[i * 2 + 1] = ' '; }
        buf[15] = 0;
        text_c(y + 12, buf, C_CYAN, 2);
        y += 36;
        for (s32 c = 0; c < 4; c++, y += 10) {
            text(40, y, cheat_names[c], c == flash ? C_YELLOW : C_WHITE, 1);
            text_r(200, y, *cheat_flag(c) ? "ON" : "OFF", *cheat_flag(c) ? C_YELLOW : C_GREY);
        }
        text_c(y + 4, "START TO LEAVE", C_GREY, 1);
        frame_end();
    }
}

// ---------------------------------------------------------------- saves

// Battery SRAM, 32 KB, byte access only. The tag tells emulators the save type.
#define SRAM ((volatile u8 *)0x0E000000)
static const char sram_tag[] __attribute__((used, aligned(4))) = "SRAM_V113";

#define SYS_MAGIC  0x3152544E   // "NTR1"
#define SLOT_MAGIC 0x3153544E   // "NTS1"
#define SLOT_BASE  0x0100
#define SLOT_SIZE  0x0200
#define GHOST_BASE 0x0800

typedef struct {
    u32 magic;
    u8 opt[16];
    Records rec;
    s32 ghost_count;
    u32 sum;
} SysSave;

typedef struct {
    u32 magic;
    Car car;
    s32 score, play_secs;
    u32 sum;
} SlotSave;

static void sram_write(u32 at, const void *src, u32 n)
{
    const u8 *s = src;
    for (u32 i = 0; i < n; i++) SRAM[at + i] = s[i];
}

static void sram_read(u32 at, void *dst, u32 n)
{
    u8 *d = dst;
    for (u32 i = 0; i < n; i++) d[i] = SRAM[at + i];
}

static u32 checksum(const void *p, u32 n)
{
    const u8 *b = p;
    u32 s = 0x1234;
    for (u32 i = 0; i < n; i++) s = (s << 5) + s + b[i];
    return s;
}

void save_system(void)
{
    SysSave sv;
    s32 *count, max;
    void *ghost = game_ghost_data(&count, &max);
    sv.magic = SYS_MAGIC;
    for (s32 i = 0; i < 16; i++) sv.opt[i] = i < OPT_COUNT ? g_opt[i] : 0;
    sv.rec = g_rec;
    sv.ghost_count = *count;
    sv.sum = checksum(&sv, sizeof(sv) - 4);
    sram_write(GHOST_BASE, ghost, *count * 12);
    sram_write(0, &sv, sizeof(sv));
}

void save_init(void)
{
    (void)*(volatile const char *)sram_tag;
    *(volatile u16 *)0x04000204 |= 0x0003;     // SRAM wait states (8 cycles)
    SysSave sv;
    sram_read(0, &sv, sizeof(sv));
    if (sv.magic != SYS_MAGIC || sv.sum != checksum(&sv, sizeof(sv) - 4)) {
        for (s32 i = 0; i < OPT_COUNT; i++) g_opt[i] = 0;
        return;
    }
    for (s32 i = 0; i < OPT_COUNT; i++)
        g_opt[i] = sv.opt[i] < defs[i].count ? sv.opt[i] : 0;
    g_rec = sv.rec;
    s32 *count, max;
    void *ghost = game_ghost_data(&count, &max);
    if (sv.ghost_count > 0 && sv.ghost_count * 12 <= max) {
        sram_read(GHOST_BASE, ghost, sv.ghost_count * 12);
        *count = sv.ghost_count;
    }
}

s32 save_slot(s32 slot, const Car *c, s32 score)
{
    SlotSave s;
    s.magic = SLOT_MAGIC;
    s.car = *c;
    s.score = score;
    s.play_secs = g_rec.play_secs;
    s.sum = checksum(&s, sizeof(s) - 4);
    sram_write(SLOT_BASE + slot * SLOT_SIZE, &s, sizeof(s));
    save_system();
    return 1;
}

static s32 read_slot(s32 slot, SlotSave *s)
{
    sram_read(SLOT_BASE + slot * SLOT_SIZE, s, sizeof(*s));
    return s->magic == SLOT_MAGIC && s->sum == checksum(s, sizeof(*s) - 4);
}

s32 load_slot(s32 slot, Car *c, s32 *score)
{
    SlotSave s;
    if (!read_slot(slot, &s)) return 0;
    *c = s.car;
    *score = s.score;
    return 1;
}

void save_erase(void)
{
    for (u32 i = 0; i < SLOT_BASE + 3 * SLOT_SIZE; i++) SRAM[i] = 0;
}

s32 menu_slot(const char *title, s32 saving)
{
    s32 sel = 0, first = 1;
    flush_keys();
    for (;;) {
        u16 k = menu_keys();
        if (k & KEY_UP)   sel = (sel + 2) % 3;
        if (k & KEY_DOWN) sel = (sel + 1) % 3;
        if (k & KEY_B) return -1;
        if (!(k & (KEY_A | KEY_START)) && idle(k, &first)) continue;
        SlotSave s[3];
        s32 ok[3];
        for (s32 i = 0; i < 3; i++) ok[i] = read_slot(i, &s[i]);
        if (k & (KEY_A | KEY_START)) {
            if (saving || ok[sel]) return sel;
            sound_play(SFX_FAIL);
        }
        frame_begin(title, 7);
        s32 y = menu_top(7);
        for (s32 i = 0; i < 3; i++, y += 22) {
            char buf[32], *p;
            if (i == sel) rect(22, y - 3, 218, y + 18, C_HILITE);
            p = put(buf, "SLOT ");
            put_num(p, i + 1);
            text(30, y, buf, i == sel ? C_YELLOW : C_WHITE, 1);
            if (!ok[i]) { text(30, y + 9, "EMPTY", C_GREY, 1); continue; }
            s32 x = s[i].car.x >> 8, z = s[i].car.z >> 8;
            text_r(210, y, x > PARK_X0 && x < PARK_X1 && z > PARK_Z0 && z < PARK_Z1 ? "STUNT PARK" : "CITY", C_CYAN);
            p = put(buf, "SCORE ");
            put_num(p, s[i].score);
            text(30, y + 9, buf, C_GREY, 1);
            p = put_num(buf, s[i].play_secs / 60);
            put(p, " MIN PLAYED");
            text_r(210, y + 9, buf, C_GREY);
        }
        text_c(y + 2, saving ? "A SAVE   B BACK" : "A LOAD   B BACK", C_GREY, 1);
        frame_end();
    }
}
