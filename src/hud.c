// Sprite layer: the font, gauge and particle tiles are generated at start-up
// into the OBJ tile memory left free in bitmap mode (tiles 512-1023).
#include "hud.h"
#include "font.h"

#define OAM        ((volatile u32 *)0x07000000)
#define OBJ_TILES  ((volatile u16 *)0x06014000)
#define PAL_OBJ    ((volatile u16 *)0x05000200)
#define REG_DMA3SAD (*(volatile u32 *)0x040000D4)
#define REG_DMA3DAD (*(volatile u32 *)0x040000D8)
#define REG_DMA3CNT (*(volatile u32 *)0x040000DC)

#define T_SMALL     0      // 64 glyphs, 8x8
#define T_BIG       64     // 64 glyphs, 16x16
#define T_PUFF      320    // 32x32 smoke puff
#define T_PUFF_TINY 336
#define T_SPARK     337
#define T_DROP      338
#define T_SEG       339
#define T_ARROW     340    // 16x16
#define TILE(t)     (512 + (t))

#define A0_AFFINE   0x0100
#define A0_DOUBLE   0x0200
#define A0_HIDE     0x0200
#define A0_BLEND    0x0400
#define A1_SIZE16   0x4000
#define A1_SIZE32   0x8000
#define A1_MATRIX(i) ((i) << 9)
#define A2_PAL(p)   ((p) << 12)

#define SCALES      22     // puff scale steps, matrices 0..21
#define M_TITLE     22
#define M_ARROW     23

static u16 shadow[128 * 4] EWRAM_BSS;
static s32 used;
static u8  scale_radius[SCALES];   // puff radius in pixels at each scale step

// ---------------------------------------------------------------- tiles

// Pixel (x, y) of an 8x8 4bpp tile held as eight 32-bit rows.
static void put(u32 *tile, s32 x, s32 y, u32 c)
{
    tile[y] = (tile[y] & ~(15u << (x * 4))) | (c << (x * 4));
}
static u32 get(const u32 *tile, s32 x, s32 y) { return (tile[y] >> (x * 4)) & 15; }

static void upload(s32 index, const u32 *rows)
{
    volatile u16 *dst = OBJ_TILES + index * 16;
    for (s32 r = 0; r < 8; r++) {
        dst[r * 2]     = rows[r] & 0xFFFF;
        dst[r * 2 + 1] = rows[r] >> 16;
    }
}

// Uploads a w x h pixel image (one nibble per byte) as consecutive tiles,
// row-major, the way 1D-mapped sprites read them.
static void upload_image(s32 index, const u8 *img, s32 w, s32 h)
{
    for (s32 ty = 0; ty < h / 8; ty++)
        for (s32 tx = 0; tx < w / 8; tx++) {
            u32 rows[8] = { 0 };
            for (s32 y = 0; y < 8; y++)
                for (s32 x = 0; x < 8; x++)
                    put(rows, x, y, img[(ty * 8 + y) * w + tx * 8 + x]);
            upload(index + ty * (w / 8) + tx, rows);
        }
}

static s32 glyph_bit(s32 ch, s32 gx, s32 gy)
{
    if (gx < 0 || gx > 4 || gy < 0 || gy > 6) return 0;
    return (font[ch][gy] >> (4 - gx)) & 1;
}

static void build_font(void)
{
    for (s32 ch = 0; ch < 64; ch++) {
        // Small: glyph in colour 1 with a drop shadow in colour 2.
        u32 rows[8] = { 0 };
        for (s32 y = 0; y < 8; y++)
            for (s32 x = 0; x < 8; x++) {
                if (glyph_bit(ch, x - 1, y)) put(rows, x, y, 1);
                else if (glyph_bit(ch, x - 2, y - 1)) put(rows, x, y, 2);
            }
        upload(T_SMALL + ch, rows);

        // Big: doubled glyph, lower half in the darker colour 3, outlined in 2.
        u8 img[16 * 16];
        for (s32 y = 0; y < 16; y++)
            for (s32 x = 0; x < 16; x++) {
                s32 gx = (x - 2) >> 1, gy = (y - 1) >> 1;
                u8 c = 0;
                if (x >= 2 && y >= 1 && glyph_bit(ch, gx, gy)) c = y >= 9 ? 3 : 1;
                img[y * 16 + x] = c;
            }
        u8 out[16 * 16];
        for (s32 y = 0; y < 16; y++)
            for (s32 x = 0; x < 16; x++) {
                u8 c = img[y * 16 + x];
                if (!c) {
                    for (s32 dy = -1; dy <= 1 && !c; dy++)
                        for (s32 dx = -1; dx <= 1; dx++) {
                            s32 xx = x + dx, yy = y + dy;
                            if (xx >= 0 && xx < 16 && yy >= 0 && yy < 16 && img[yy * 16 + xx] &&
                                img[yy * 16 + xx] != 2) { c = 2; break; }
                        }
                }
                out[y * 16 + x] = c;
            }
        upload_image(T_BIG + ch * 4, out, 16, 16);
    }
}

static void build_shapes(void)
{
    static u8 img[32 * 32] EWRAM_BSS;
    // Smoke puff: a lumpy ball lit from the upper left.
    for (s32 y = 0; y < 32; y++)
        for (s32 x = 0; x < 32; x++) {
            s32 dx = x * 2 - 31, dy = y * 2 - 31;
            s32 a = iatan2(dy, dx);
            s32 r = 28 + ((isin(a * 3) + isin(a * 5 + 300)) >> 13);   // gently lumpy edge
            s32 d2 = dx * dx + dy * dy;
            u8 c = 0;
            if (d2 < r * r) {
                // Lit from the upper left, with a few billows inside.
                s32 light = dx + dy + (isin(dx * 9 + dy * 4) >> 11) + (isin(dy * 11 - dx * 3) >> 11);
                c = light < -16 ? 1 : light < 16 ? 2 : 3;
            }
            img[y * 32 + x] = c;
        }
    upload_image(T_PUFF, img, 32, 32);

    static const char *tiny[8] = {
        "........", "..1122..", ".111222.", ".112223.",
        ".122233.", "..2233..", "........", "........" };
    static const char *spark[8] = {
        "........", "...2....", "...1....", ".21112..",
        "...1....", "...2....", "........", "........" };
    static const char *drop[8] = {
        "........", "...1....", "..112...", "..122...",
        "..223...", "...3....", "........", "........" };
    static const char *seg[8] = {
        "........", "11111...", "11111...", "11111...",
        "11111...", "22222...", "........", "........" };
    const char **shapes[4] = { tiny, spark, drop, seg };
    for (s32 s = 0; s < 4; s++) {
        u32 rows[8] = { 0 };
        for (s32 y = 0; y < 8; y++)
            for (s32 x = 0; x < 8; x++) {
                char c = shapes[s][y][x];
                if (c != '.') put(rows, x, y, c - '0');
            }
        upload(T_PUFF_TINY + s, rows);
    }

    // Arrow pointing up, outlined.
    u8 arrow[16 * 16];
    for (s32 y = 0; y < 16; y++)
        for (s32 x = 0; x < 16; x++) {
            s32 cx = x * 2 - 15;
            s32 in = 0;
            if (y >= 2 && y < 9) in = (cx < 0 ? -cx : cx) <= (y - 2) * 2;     // head
            else if (y >= 9 && y < 14) in = (cx < 0 ? -cx : cx) <= 5;          // shaft
            arrow[y * 16 + x] = in ? (y < 8 ? 1 : 3) : 0;
        }
    for (s32 y = 0; y < 16; y++)
        for (s32 x = 0; x < 16; x++) {
            if (arrow[y * 16 + x]) continue;
            for (s32 d = 0; d < 4; d++) {
                s32 xx = x + (d == 0) - (d == 1), yy = y + (d == 2) - (d == 3);
                if (xx >= 0 && xx < 16 && yy >= 0 && yy < 16 &&
                    (arrow[yy * 16 + xx] == 1 || arrow[yy * 16 + xx] == 3)) {
                    arrow[y * 16 + x] = 2;
                    break;
                }
            }
        }
    upload_image(T_ARROW, arrow, 16, 16);
    (void)get;
}

static void set_palette(s32 bank, u16 c1, u16 c2, u16 c3)
{
    PAL_OBJ[bank * 16 + 1] = c1;
    PAL_OBJ[bank * 16 + 2] = c2;
    PAL_OBJ[bank * 16 + 3] = c3;
}

static void set_matrix(s32 i, s32 pa, s32 pb, s32 pc, s32 pd)
{
    shadow[i * 16 + 3] = pa;
    shadow[i * 16 + 7] = pb;
    shadow[i * 16 + 11] = pc;
    shadow[i * 16 + 15] = pd;
}

void hud_init(void)
{
    build_font();
    build_shapes();
    u16 ink = RGB15(2, 2, 5);
    set_palette(PAL_YELLOW, RGB15(31, 29, 8), ink, RGB15(30, 20, 2));
    set_palette(PAL_WHITE, RGB15(31, 31, 31), ink, RGB15(21, 24, 29));
    set_palette(PAL_RED, RGB15(31, 9, 6), RGB15(5, 1, 1), RGB15(25, 3, 2));
    set_palette(PAL_GREEN, RGB15(12, 31, 10), RGB15(1, 5, 1), RGB15(4, 23, 6));
    set_palette(PAL_CYAN, RGB15(12, 28, 31), RGB15(1, 4, 9), RGB15(4, 19, 28));
    set_palette(PAL_SMOKE, RGB15(29, 29, 29), RGB15(23, 23, 24), RGB15(16, 16, 18));
    set_palette(PAL_DARK_SMOKE, RGB15(13, 13, 13), RGB15(9, 9, 9), RGB15(5, 5, 6));
    set_palette(PAL_FIRE, RGB15(31, 30, 12), RGB15(31, 19, 3), RGB15(27, 8, 2));
    set_palette(PAL_SPLASH, RGB15(31, 31, 31), RGB15(22, 27, 31), RGB15(13, 20, 30));
    set_palette(PAL_DUST, RGB15(27, 23, 16), RGB15(22, 18, 12), RGB15(17, 14, 9));
    set_palette(PAL_TACH_OFF, RGB15(6, 7, 9), RGB15(3, 3, 5), 0);
    set_palette(PAL_TACH_GREEN, RGB15(9, 29, 8), RGB15(3, 14, 3), 0);
    set_palette(PAL_TACH_YELLOW, RGB15(31, 28, 6), RGB15(18, 14, 2), 0);
    set_palette(PAL_TACH_RED, RGB15(31, 7, 4), RGB15(16, 2, 2), 0);
    set_palette(PAL_SPARK, RGB15(31, 31, 22), RGB15(31, 22, 6), RGB15(31, 12, 2));

    // Puff scales from 1/4 to 2x in 10% steps; the puff is ~15 px in radius.
    s32 s = 64;                                // Q8
    for (s32 i = 0; i < SCALES; i++) {
        s32 inv = 65536 / s;
        set_matrix(i, inv, 0, 0, inv);
        scale_radius[i] = (15 * s) >> 8;
        s = (s * 282) >> 8;
    }
    set_matrix(M_TITLE, 146, 0, 0, 146);       // 1.75x
    for (s32 i = 0; i < 128; i++) shadow[i * 4] = A0_HIDE;
    hud_commit();
    // Smoke blends with the 3D view behind it.
    *(volatile u16 *)0x04000050 = 0x2400;      // BLDCNT: 2nd target BG2 + backdrop
    *(volatile u16 *)0x04000052 = 0x0A08;      // BLDALPHA: 8/16 sprite, 10/16 scene
}

// ---------------------------------------------------------------- sprites

void hud_begin(void) { used = 0; }

void hud_end(void)
{
    for (s32 i = used; i < 128; i++) shadow[i * 4] = A0_HIDE;
}

void hud_commit(void)
{
    REG_DMA3SAD = (u32)shadow;
    REG_DMA3DAD = (u32)OAM;
    REG_DMA3CNT = 0x84000000 | 256;
}

static void add(u32 a0, u32 a1, u32 a2)
{
    if (used >= 128) return;
    u16 *e = &shadow[used++ * 4];
    e[0] = a0;
    e[1] = a1;
    e[2] = a2;
}

static void sprite(s32 x, s32 y, s32 size, u32 a0, u32 a1, u32 a2)
{
    if (x >= SCREEN_W || y >= SCREEN_H || x + size <= 0 || y + size <= 0) return;
    add(a0 | (y & 255), a1 | (x & 511), a2);
}

static s32 glyph(char c)
{
    s32 ch = c;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) return 0;
    return ch - 32;
}

s32 hud_text_width(const char *s, s32 big)
{
    s32 n = 0;
    while (s[n]) n++;
    return n ? n * (big ? 12 : 6) - (big ? 2 : 1) : 0;
}

s32 hud_text(s32 x, s32 y, const char *s, s32 big, s32 pal)
{
    for (; *s; s++, x += big ? 12 : 6) {
        s32 g = glyph(*s);
        if (!g) continue;
        if (big) sprite(x - 2, y - 1, 16, 0, A1_SIZE16, TILE(T_BIG + g * 4) | A2_PAL(pal));
        else     sprite(x - 1, y, 8, 0, 0, TILE(T_SMALL + g) | A2_PAL(pal));
    }
    return x;
}

void hud_text_centered(s32 y, const char *s, s32 big, s32 pal)
{
    hud_text((SCREEN_W - hud_text_width(s, big)) / 2, y, s, big, pal);
}

void hud_text_right(s32 right, s32 y, const char *s, s32 big, s32 pal)
{
    hud_text(right - hud_text_width(s, big), y, s, big, pal);
}

void hud_text_huge(s32 y, const char *s, s32 pal)
{
    s32 n = 0;
    while (s[n]) n++;
    s32 x = (SCREEN_W - n * 21) / 2;
    for (; *s; s++, x += 21) {
        s32 g = glyph(*s);
        if (!g) continue;
        // 16x16 glyph in a 32x32 double-size box, scaled about its centre.
        sprite(x - 6, y - 8, 32, A0_AFFINE | A0_DOUBLE, A1_SIZE16 | A1_MATRIX(M_TITLE),
               TILE(T_BIG + g * 4) | A2_PAL(pal));
    }
}

void hud_tach(s32 x, s32 y, s32 rpm)
{
    for (s32 i = 0; i < 12; i++) {
        s32 lit = rpm > 1000 + i * 520;
        s32 pal = !lit ? PAL_TACH_OFF : i < 7 ? PAL_TACH_GREEN : i < 10 ? PAL_TACH_YELLOW : PAL_TACH_RED;
        sprite(x + i * 6, y, 8, 0, 0, TILE(T_SEG) | A2_PAL(pal));
    }
}

void hud_bar(s32 x, s32 y, s32 segs, s32 lit, s32 pal)
{
    for (s32 i = 0; i < segs; i++)
        sprite(x + i * 6, y, 8, 0, 0, TILE(T_SEG) | A2_PAL(i < lit ? pal : PAL_TACH_OFF));
}

void hud_arrow(s32 x, s32 y, s32 angle, s32 pal)
{
    s32 c = icos(angle) >> 6, s = isin(angle) >> 6;
    set_matrix(M_ARROW, c, s, -s, c);
    sprite(x - 16, y - 16, 32, A0_AFFINE | A0_DOUBLE, A1_SIZE16 | A1_MATRIX(M_ARROW),
           TILE(T_ARROW) | A2_PAL(pal));
}

void hud_particle(s32 x, s32 y, s32 radius, s32 shape, s32 pal, s32 translucent)
{
    u32 blend = translucent ? A0_BLEND : 0;
    if (shape == SPR_PUFF && radius >= scale_radius[0]) {
        s32 k = 0;
        while (k < SCALES - 1 && scale_radius[k + 1] <= radius) k++;
        sprite(x - 32, y - 32, 64, A0_AFFINE | A0_DOUBLE | blend, A1_SIZE32 | A1_MATRIX(k),
               TILE(T_PUFF) | A2_PAL(pal));
        return;
    }
    s32 t = shape == SPR_PUFF ? T_PUFF_TINY : shape == SPR_SPARK ? T_SPARK : T_DROP;
    sprite(x - 4, y - 4, 8, blend, 0, TILE(t) | A2_PAL(pal));
}
