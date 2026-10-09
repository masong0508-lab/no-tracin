// The map editor. A cursor on the city seen from above: the d-pad moves it,
// A places the item shown at the top (or picks up the one under the
// cursor), B deletes, L/R change the item, SELECT turns it, START opens the
// menu (test drive, size, zoom, save, load). Everything placed is kept as a
// short list of packed words; the world features and props are rebuilt
// from the list whenever it changes.
#include "edit.h"
#include "world.h"
#include "props.h"
#include "game.h"
#include "menu.h"
#include "hud.h"
#include "sound.h"
#include "track.h"

#define EDIT_MAX 100        // items in a map
#define GRID     20         // items sit on a 1 m grid

// ---------------------------------------------------------------- items

enum {
    EI_KICKER, EI_BIG_KICKER, EI_LAUNCH, EI_LANDING, EI_WALL, EI_PAD,
    EI_CONE, EI_DRUM, EI_CRATES, EI_CONCRETE, EI_STAR, EI_COUNT
};
enum { IK_RAMP, IK_WALL, IK_PAD, IK_PROP, IK_STAR };

// Ramps: hw across, hl along, from h0 at the back to h1 at the front, at
// MEDIUM. Walls: h0 tall. Props: a row of them across the heading, hw
// apart, hl their radius.
typedef struct {
    const char *name;
    u8 kind, prop, def_size;
    s16 hw, hl, h0, h1;
} ItemDef;

static const ItemDef defs[EI_COUNT] = {
    [EI_KICKER]     = { "KICKER",       IK_RAMP, 0, 1, 50, 70, 0, 40 },
    [EI_BIG_KICKER] = { "BIG KICKER",   IK_RAMP, 0, 1, 70, 90, 0, 72 },
    [EI_LAUNCH]     = { "LAUNCH RAMP",  IK_RAMP, 0, 1, 60, 180, 0, 120 },
    [EI_LANDING]    = { "LANDING RAMP", IK_RAMP, 0, 1, 80, 140, 70, 0 },
    [EI_WALL]       = { "WALL",         IK_WALL, 0, 1, 8, 60, 30, 0 },
    [EI_PAD]        = { "BOOST PAD",    IK_PAD,  0, 0, 40, 50, 0, 0 },
    [EI_CONE]       = { "CONES",        IK_PROP, PROP_CONE, 0, 24, 5, 0, 0 },
    [EI_DRUM]       = { "OIL DRUMS",    IK_PROP, PROP_DRUM, 0, 15, 6, 0, 0 },
    [EI_CRATES]     = { "CRATE STACK",  IK_PROP, PROP_CRATE, 2, 16, 8, 0, 0 },
    [EI_CONCRETE]   = { "CONCRETE",     IK_PROP, PROP_BLOCK, 0, 22, 10, 0, 0 },
    [EI_STAR]       = { "BONUS STAR",   IK_STAR, 0, 0, 16, 16, 0, 0 },
};

static const u16 ramp_scale[4] = { 154, 256, 358, 461 };    // Q8, length and height
static const u16 wall_scale[4] = { 128, 256, 512, 1024 };   // Q8, length
static const u8 prop_count[4][4] = {
    { 1, 3, 5, 8 }, { 1, 2, 4, 6 }, { 1, 2, 3, 4 }, { 1, 2, 3, 5 },   // crates: rows of a pyramid
};
static const char *const size_names[4] = { "SMALL", "MEDIUM", "LARGE", "HUGE" };
static const u8 star_high[4] = { 16, 60, 110, 170 };   // a star's size is how high it floats
static const char *const star_names[4] = { "LOW", "MID", "HIGH", "SKY" };
static const char *const dir_names[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};

// An item packed in a word: x and z in grid steps (10 bits each), type,
// heading in sixteenths of a turn clockwise from north (+z), and size.
#define IT_X(v)    ((s32)((v) & 1023) * GRID)
#define IT_Z(v)    ((s32)(((v) >> 10) & 1023) * GRID)
#define IT_TYPE(v) ((s32)((v) >> 20) & 15)
#define IT_ROT(v)  ((s32)((v) >> 24) & 15)
#define IT_SIZE(v) ((s32)((v) >> 28) & 3)

static u32 pack(s32 type, s32 x, s32 z, s32 rot, s32 size)
{
    return (u32)(x / GRID) | (u32)(z / GRID) << 10 | (u32)type << 20 | (u32)rot << 24 | (u32)size << 28;
}

static u32 items[EDIT_MAX] EWRAM_BSS;
static s32 item_count EWRAM_BSS;
static u32 got_star[(EDIT_MAX + 31) / 32] EWRAM_BSS;   // bonus stars picked up this run
static s32 boost_timer EWRAM_BSS;
s32 g_edit_testing EWRAM_BSS;

static s32 sizes(s32 type) { return defs[type].kind == IK_PAD ? 1 : 4; }

// Height of a bonus star's centre above the ground.
static s32 star_y(u32 v)
{
    s32 g = world_height(IT_X(v), IT_Z(v)) >> 8;
    return (g < 0 ? 0 : g) + star_high[IT_SIZE(v)];
}

// The item's footprint (half width across, half length along) and, for
// ramps and walls, its heights.
static void shape(s32 type, s32 size, s32 *hw, s32 *hl, s32 *h0, s32 *h1)
{
    const ItemDef *d = &defs[type];
    *hw = d->hw; *hl = d->hl; *h0 = d->h0; *h1 = d->h1;
    if (d->kind == IK_RAMP) {
        s32 s = ramp_scale[size];
        *hl = (d->hl * s) >> 8; *h0 = (d->h0 * s) >> 8; *h1 = (d->h1 * s) >> 8;
    } else if (d->kind == IK_WALL) {
        *hl = (d->hl * wall_scale[size]) >> 8;
    } else if (d->kind == IK_PROP) {
        s32 n = prop_count[d->prop][size];
        *hw = d->prop == PROP_CRATE ? n * 8 : ((n - 1) * d->hw) / 2 + d->hl;
        *hl = d->hl;
    }
}

static void size_text(char *buf, s32 type, s32 size);

// The features of one item (ramps, walls, pads). 0 when the table is full.
static s32 spawn_feature(u32 v)
{
    s32 t = IT_TYPE(v), k = defs[t].kind, hw, hl, h0, h1;
    if (k != IK_RAMP && k != IK_WALL && k != IK_PAD) return 1;
    shape(t, IT_SIZE(v), &hw, &hl, &h0, &h1);
    return world_dyn_add(k == IK_RAMP ? DYN_RAMP : k == IK_WALL ? DYN_WALL : DYN_PAD,
                         IT_X(v), IT_Z(v), IT_ROT(v) << 6, hw, hl, h0, h1);
}

// The props of one item: a row across its heading, or a pyramid of crates.
static s32 spawn_props(u32 v)
{
    s32 t = IT_TYPE(v);
    const ItemDef *d = &defs[t];
    if (d->kind != IK_PROP) return 1;
    s32 x = IT_X(v), z = IT_Z(v), a = IT_ROT(v) << 6, n = prop_count[d->prop][IT_SIZE(v)];
    s32 rx = icos(a), rz = -isin(a), ok = 1;          // across, to the right
    if (d->prop == PROP_CRATE) {
        for (s32 row = 0; row < n; row++)
            for (s32 i = 0; i < n - row; i++) {
                s32 off = (i * 2 - (n - row - 1)) * 8;
                ok &= props_add(PROP_CRATE, x + ((rx * off) >> 14), z + ((rz * off) >> 14), row * 16, a << 6);
            }
        return ok;
    }
    for (s32 i = 0; i < n; i++) {
        s32 off = ((2 * i - (n - 1)) * d->hw) / 2;
        ok &= props_add(d->prop, x + ((rx * off) >> 14), z + ((rz * off) >> 14), 0, a << 6);
    }
    return ok;
}

s32 edit_reset_props(void)
{
    props_reset();
    boost_timer = 0;
    for (u32 i = 0; i < sizeof(got_star) / 4; i++) got_star[i] = 0;
    if (g_track) return 1;
    s32 ok = 1;
    for (s32 i = 0; i < item_count; i++) ok &= spawn_props(items[i]);
    return ok;
}

// Rebuilds the world from the list. 0 when something had no room.
static s32 apply(void)
{
    s32 ok = 1;
    world_dyn_clear();
    for (s32 i = 0; i < item_count; i++) ok &= spawn_feature(items[i]);
    world_dyn_done();
    return edit_reset_props() && ok;
}

// ---------------------------------------------------------------- saving

// Two maps in the save at MAP_AT, each a checksummed blob. The one saved or
// loaded last (the higher sequence number) is loaded on entering the city.
#define MAP_MAGIC 0x5045544E   // "NTEP"
#define MAP_AT    0x7880       // up to 0x7C00, where the race records start
#define MAP_SLOT  0x01C0
typedef struct { u16 seq; u8 count, version; u32 item[EDIT_MAX]; } MapSave;
static MapSave map_buf EWRAM_BSS;
static s32 slot_ok[2] EWRAM_BSS, slot_seq[2] EWRAM_BSS, slot_items[2] EWRAM_BSS;

static s32 read_map(s32 slot)
{
    return load_blob(MAP_MAGIC, MAP_AT + slot * MAP_SLOT, &map_buf, sizeof(map_buf)) &&
           map_buf.count <= EDIT_MAX;
}

static void scan_maps(void)
{
    for (s32 s = 0; s < 2; s++) {
        slot_ok[s] = read_map(s);
        slot_seq[s] = slot_ok[s] ? map_buf.seq : 0;
        slot_items[s] = slot_ok[s] ? map_buf.count : 0;
    }
}

static s32 newest_seq(void)
{
    s32 s = slot_ok[0] ? slot_seq[0] : 0;
    if (slot_ok[1] && (!slot_ok[0] || (s16)(slot_seq[1] - s) > 0)) s = slot_seq[1];
    return s;
}

static void write_map(s32 slot)
{
    scan_maps();
    map_buf.seq = newest_seq() + 1;
    map_buf.count = item_count;
    map_buf.version = 1;
    for (s32 i = 0; i < EDIT_MAX; i++) map_buf.item[i] = i < item_count ? items[i] : 0;
    save_blob(MAP_MAGIC, MAP_AT + slot * MAP_SLOT, &map_buf, sizeof(map_buf));
    scan_maps();
}

static s32 load_map(s32 slot)
{
    if (!read_map(slot)) return 0;
    item_count = 0;
    for (s32 i = 0; i < map_buf.count; i++)
        if (IT_TYPE(map_buf.item[i]) < EI_COUNT) items[item_count++] = map_buf.item[i];
    return 1;
}

// ---------------------------------------------------------------- the editor's state

typedef struct {
    s32 x, z;                   // cursor, world units (items snap to the grid)
    s32 rot, type, zoom;
    u8 size[EI_COUNT];
    s32 ready;                  // sizes set up
    s32 held;                   // carrying a picked-up item
    u32 held_item;              // where it was, for B to put it back
    s32 dirty, slot;            // unsaved changes; the map slot in use
    s32 cam_x, cam_z;           // camera focus, following the cursor
    char msg[24];
    s32 msg_timer, msg_pal;
} Editor;
static Editor ed EWRAM_BSS;

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_num(char *p, s32 v)
{
    char tmp[12];
    s32 i = 0;
    do { tmp[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = tmp[--i];
    *p = 0;
    return p;
}

static void message(const char *s, s32 pal)
{
    put(ed.msg, s);
    ed.msg_pal = pal;
    ed.msg_timer = 100;
}

void edit_enter_city(void)
{
    g_edit_testing = 0;
    ed.held = 0;
    ed.dirty = 0;
    item_count = 0;
    scan_maps();
    s32 slot = -1;
    if (slot_ok[0]) slot = 0;
    if (slot_ok[1] && (slot < 0 || (s16)(slot_seq[1] - slot_seq[0]) > 0)) slot = 1;
    ed.slot = slot < 0 ? 0 : slot;
    if (slot >= 0) load_map(slot);
    apply();
}

// ---------------------------------------------------------------- driving on it

void edit_step(Car *c)
{
    if (boost_timer > 0) boost_timer--;
    if (c->mode == CAR_CRASH) { boost_timer = 0; return; }
    s32 x = c->x >> 8, z = c->z >> 8, y = c->y >> 8;
    if (c->mode != CAR_LOOP && world_dyn_pad(x, z) && y - (world_height(x, z) >> 8) < 30) {
        if (!boost_timer) sound_play(SFX_CHECKPOINT);
        boost_timer = 45;
        g_game.nitro = NITRO_MAX;
    }
    for (s32 i = 0; i < item_count; i++) {
        u32 v = items[i];
        if (IT_TYPE(v) != EI_STAR || ((got_star[i >> 5] >> (i & 31)) & 1)) continue;
        s32 dx = x - IT_X(v), dz = z - IT_Z(v);
        if (dx < -40 || dx > 40 || dz < -40 || dz > 40) continue;
        s32 dy = y + 12 - star_y(v);
        if (dy < -50 || dy > 50) continue;
        got_star[i >> 5] |= 1u << (i & 31);
        g_game.score += 500;
        g_game.nitro = NITRO_MAX;
        game_message("BONUS STAR", "+500", PAL_CYAN, 120);
        sound_play(SFX_COMBO);
    }
}

s32 edit_boosting(void) { return boost_timer > 0; }

void edit_draw_sprites(s32 frame)
{
    for (s32 i = 0; i < item_count; i++) {
        u32 v = items[i];
        if (IT_TYPE(v) != EI_STAR || ((got_star[i >> 5] >> (i & 31)) & 1)) continue;
        s32 x = IT_X(v), z = IT_Z(v), side, d = r_depth(x, z, &side);
        if (d < 30 || d > r_far) continue;
        s32 sx, sy, bob = (isin(frame * 12 + i * 90) * 4) >> 14;
        if (!r_project(x, star_y(v) + bob, z, &sx, &sy)) continue;
        if (sx < -8 || sx > SCREEN_W + 8 || sy < -8 || sy > SCREEN_H + 8) continue;
        if (d < 450) hud_text(sx - 6, sy - 7, "*", 1, PAL_CYAN);   // steady: nothing here blinks
        else         hud_text(sx - 3, sy - 3, "*", 0, PAL_CYAN);
    }
}

// ---------------------------------------------------------------- editing

static s32 snap(s32 v) { return (v + GRID / 2) / GRID * GRID; }

// The item under (x, z): the last placed whose footprint holds it, or -1.
static s32 under(s32 x, s32 z)
{
    for (s32 i = item_count - 1; i >= 0; i--) {
        u32 v = items[i];
        s32 hw, hl, h0, h1, a = IT_ROT(v) << 6;
        shape(IT_TYPE(v), IT_SIZE(v), &hw, &hl, &h0, &h1);
        s32 dx = x - IT_X(v), dz = z - IT_Z(v);
        s32 along = (dx * isin(a) + dz * icos(a)) >> 14, across = (dx * icos(a) - dz * isin(a)) >> 14;
        if (along < 0) along = -along;
        if (across < 0) across = -across;
        if (along <= hl + 6 && across <= hw + 6) return i;
    }
    return -1;
}

// Would it stand in a building, a tree, the canal or off the edge of town?
// Checks the corners, the middles of the sides and the centre.
static s32 blocked(u32 v)
{
    s32 hw, hl, h0, h1, t = IT_TYPE(v), a = IT_ROT(v) << 6;
    shape(t, IT_SIZE(v), &hw, &hl, &h0, &h1);
    s32 fx = isin(a), fz = icos(a), x = IT_X(v), z = IT_Z(v);
    if (defs[t].kind == IK_STAR) return world_blocked(x, z, 4) && !world_in_water(x, z);
    for (s32 i = -1; i <= 1; i++)
        for (s32 j = -1; j <= 1; j++) {
            s32 l = i * hl, w = j * hw;
            if (world_blocked(x + ((fx * l + fz * w) >> 14), z + ((fz * l - fx * w) >> 14), 2)) return 1;
        }
    return 0;
}

static void remove_item(s32 i)
{
    for (item_count--; i < item_count; i++) items[i] = items[i + 1];
}

static s32 place(void)
{
    u32 v = pack(ed.type, snap(ed.x), snap(ed.z), ed.rot, ed.size[ed.type]);
    if (item_count >= EDIT_MAX) { message("MAP FULL", PAL_RED); sound_play(SFX_FAIL); return 0; }
    if (blocked(v)) { message("CAN'T BUILD THERE", PAL_RED); sound_play(SFX_FAIL); return 0; }
    items[item_count++] = v;
    if (!apply()) {
        item_count--;
        apply();
        message("NO ROOM FOR MORE", PAL_RED);
        sound_play(SFX_FAIL);
        return 0;
    }
    ed.dirty = 1;
    sound_play(SFX_CHECKPOINT);
    return 1;
}

static void name_message(const char *verb, u32 v)
{
    char buf[24];
    put(put(buf, verb), defs[IT_TYPE(v)].name);
    message(buf, PAL_WHITE);
}

// ---------------------------------------------------------------- drawing

// Zoom levels: camera height and how far back it sits, the draw distance
// and the haze, and the cursor's top speed (units per 60th of a second).
static const s16 zoom_h[3] = { 300, 520, 900 }, zoom_back[3] = { 120, 210, 360 };
static const s16 zoom_far[3] = { 700, 1000, 1500 }, zoom_speed[3] = { 10, 20, 40 };
static const char *const zoom_names[3] = { "NEAR", "MID", "FAR" };

static void set_far(void)
{
    s32 f = zoom_far[ed.zoom];
    r_set_far(f, f, f * 3 / 4, f + 300);
}

// A preview of an item at the cursor: ramps, walls and pads as they will
// look; props as plain boxes.
static void draw_preview(s32 type, s32 x, s32 z, s32 rot, s32 size)
{
    const ItemDef *d = &defs[type];
    s32 hw, hl, h0, h1, a = rot << 6;
    shape(type, size, &hw, &hl, &h0, &h1);
    if (d->kind == IK_RAMP) { world_draw_dyn(DYN_RAMP, x, z, a, hw, hl, h0, h1); return; }
    if (d->kind == IK_WALL) { world_draw_dyn(DYN_WALL, x, z, a, hw, hl, h0, h1); return; }
    if (d->kind == IK_PAD)  { world_draw_dyn(DYN_PAD, x, z, a, hw, hl, h0, h1); return; }
    if (d->kind != IK_PROP) return;
    static const u8 mats[4] = { M_FIRE, M_STUNT_RED, M_BLD4, M_SIDEWALK };
    static const u8 tall[4] = { 12, 16, 16, 16 };
    s32 fx = isin(a), fz = icos(a), r = d->hl;
    s32 m[9] = { fz, 0, -fx, 0, 16384, 0, fx, 0, fz };
    s32 n = prop_count[d->prop][size];
    if (d->prop == PROP_CRATE) {
        // The pyramid as one stepped block per row.
        for (s32 row = 0; row < n; row++) {
            s32 w = (n - row) * 8;
            r_box_mat(x, (world_height(x, z) >> 8) + row * 16, z, m, -w, 0, -8, w, 16, 8, M_BLD4);
        }
        return;
    }
    for (s32 i = 0; i < n; i++) {
        s32 off = ((2 * i - (n - 1)) * d->hw) / 2;
        s32 px = x + ((fz * off) >> 14), pz = z - ((fx * off) >> 14);
        r_box_mat(px, world_height(px, pz) >> 8, pz, m, -r, 0, -r, r, tall[d->prop], r, mats[d->prop]);
    }
}

// A yellow arrow on the ground under the cursor, pointing its heading.
static void draw_arrow(s32 x, s32 z, s32 rot)
{
    s32 a = rot << 6, fx = isin(a), fz = icos(a), s = 14 + ed.zoom * 10;
    Vec3 t[3] = {
        { x + ((fx * s * 2) >> 14), 0, z + ((fz * s * 2) >> 14) },
        { x + ((fz * s - fx * s) >> 14), 0, z + ((-fx * s - fz * s) >> 14) },
        { x + ((-fz * s - fx * s) >> 14), 0, z + ((fx * s - fz * s) >> 14) },
    };
    r_ground(t, 3, COLOR(M_LINE, 0));
}

static void draw_frame(const Car *car, s32 frame, s32 hover)
{
    s32 sx = snap(ed.x), sz = snap(ed.z);
    s32 h = zoom_h[ed.zoom], back = zoom_back[ed.zoom];
    s32 g = world_height(sx, sz) >> 8;
    if (g < 0) g = 0;
    Camera cam;
    cam.x = ed.cam_x; cam.z = ed.cam_z - back; cam.y = g + h;
    cam.yaw = 0;
    cam.pitch = iatan2(h, back);

    r_begin(back_page(), &cam);
    world_draw(ed.cam_x, ed.cam_z);
    draw_arrow(sx, sz, ed.rot);
    game_draw_world();
    props_draw();
    car_draw(car, 0);
    if (hover < 0) draw_preview(ed.type, sx, sz, ed.rot, ed.size[ed.type]);
    r_flush();

    hud_begin();
    edit_draw_sprites(frame);
    s32 px, py;
    if (r_project(sx, g + 2, sz, &px, &py))
        hud_text(px - 6, py - 7, "+", 1, hover >= 0 ? PAL_GREEN : PAL_YELLOW);   // steady colours, no blinking
    if (defs[ed.type].kind == IK_STAR && hover < 0 && r_project(sx, g + star_high[ed.size[ed.type]], sz, &px, &py))
        hud_text(px - 6, py - 7, "*", 1, PAL_CYAN);

    char buf[40], *p;
    hud_text_centered(4, defs[ed.type].name, 1, ed.held ? PAL_GREEN : PAL_YELLOW);
    hud_text(4, 8, "L<", 0, PAL_CYAN);
    hud_text_right(236, 8, ">R", 0, PAL_CYAN);
    p = buf;
    if (ed.held) p = put(p, "MOVING  ");
    if (sizes(ed.type) > 1) { size_text(p, ed.type, ed.size[ed.type]); while (*p) p++; p = put(p, "  "); }
    p = put(p, dir_names[ed.rot]);
    p = put(p, "  ");
    p = put_num(p, item_count);
    p = put(p, "/");
    put_num(p, EDIT_MAX);
    hud_text_centered(22, buf, 0, PAL_WHITE);
    if (ed.msg_timer > 0) hud_text_centered(40, ed.msg, 1, ed.msg_pal);

    const char *l1 = ed.held ? "A:DROP  B:PUT BACK  SEL:TURN"
                   : hover >= 0 ? "A:PICK UP  B:DELETE  L/R:ITEM"
                   : "A:PLACE  B:UNDO  L/R:ITEM  SEL:TURN";
    hud_text_centered(140, l1, 0, PAL_WHITE);
    hud_text_centered(150, "SEL+^V:ZOOM   START:MENU", 0, PAL_CYAN);
    present();
}

// ---------------------------------------------------------------- menus

static void size_text(char *buf, s32 type, s32 size)
{
    const ItemDef *d = &defs[type];
    if (d->kind == IK_STAR) { put(buf, star_names[size]); return; }
    if (d->kind != IK_PROP) { put(buf, size_names[size]); return; }
    s32 n = prop_count[d->prop][size];
    if (d->prop == PROP_CRATE) n = n * (n + 1) / 2;
    put_num(put(buf, "X"), n);
}

// Picks a map slot; -1 for back.
static s32 pick_slot(const char *title)
{
    static char lines[2][24] EWRAM_BSS;
    const char *list[2] = { lines[0], lines[1] };
    scan_maps();
    for (s32 s = 0; s < 2; s++) {
        char *p = put(lines[s], s ? "MAP B  " : "MAP A  ");
        if (!slot_ok[s]) put(p, "EMPTY");
        else { p = put_num(p, slot_items[s]); put(p, " ITEMS"); }
    }
    return menu_list(title, list, 2);
}

static void help(void)
{
    static const char *const left[8] = {
        "D-PAD", "A", "B", "L / R", "SELECT", "SELECT+^/V", "SELECT+</>", "START",
    };
    static const char *const right[8] = {
        "MOVE CURSOR", "PLACE / PICK UP", "DELETE / UNDO", "CHANGE ITEM",
        "TURN ITEM", "ZOOM IN / OUT", "TURN 90 DEG", "TEST, SIZE, SAVE",
    };
    menu_table("HOW TO USE", left, right, 0, 8, "SAVED MAP LOADS IN STUNT CITY");
}

enum { MR_EDIT, MR_TEST, MR_EXIT };

static s32 save_to(s32 slot)
{
    write_map(slot);
    ed.slot = slot;
    ed.dirty = 0;
    message(slot ? "SAVED TO MAP B" : "SAVED TO MAP A", PAL_GREEN);
    sound_play(SFX_BEST);
    return MR_EDIT;
}

static s32 editor_menu(void)
{
    static char size_line[24] EWRAM_BSS, zoom_line[16] EWRAM_BSS;
    menu_freeze();
    for (;;) {
        char *p = put(size_line, defs[ed.type].kind == IK_STAR ? "HEIGHT: " : "SIZE: ");
        if (sizes(ed.type) > 1) size_text(p, ed.type, ed.size[ed.type]); else put(p, "-");
        put(put(zoom_line, "ZOOM: "), zoom_names[ed.zoom]);
        const char *list[9] = {
            "KEEP EDITING", "TEST DRIVE", size_line, zoom_line, "SAVE MAP", "LOAD MAP",
            "CLEAR MAP", "HOW TO USE", "EXIT EDITOR",
        };
        s32 i = menu_list("MAP EDITOR", list, 9);
        if (i <= 0) return MR_EDIT;
        if (i == 1) return MR_TEST;
        if (i == 2) {
            if (sizes(ed.type) == 1) { sound_play(SFX_FAIL); continue; }
            static char lines[4][16] EWRAM_BSS;
            const char *l[4] = { lines[0], lines[1], lines[2], lines[3] };
            for (s32 s = 0; s < 4; s++) size_text(lines[s], ed.type, s);
            s32 s = menu_list(defs[ed.type].name, l, 4);
            if (s >= 0) { ed.size[ed.type] = s; return MR_EDIT; }
        }
        if (i == 3) {
            s32 z = menu_list("ZOOM", zoom_names, 3);
            if (z >= 0) { ed.zoom = z; return MR_EDIT; }
        }
        if (i == 4) {
            s32 s = pick_slot("SAVE MAP");
            if (s >= 0) return save_to(s);
        }
        if (i == 5) {
            s32 s = pick_slot("LOAD MAP");
            if (s >= 0) {
                if (!load_map(s)) { sound_play(SFX_FAIL); continue; }
                write_map(s);           // now the newest: this is the one the city loads
                ed.slot = s;
                ed.dirty = 0;
                ed.held = 0;
                apply();
                message(s ? "LOADED MAP B" : "LOADED MAP A", PAL_GREEN);
                return MR_EDIT;
            }
        }
        if (i == 6) {
            static const char *const yn[2] = { "NO", "YES, CLEAR IT" };
            if (menu_list("CLEAR MAP?", yn, 2) == 1) {
                item_count = 0;
                ed.held = 0;
                ed.dirty = 1;
                apply();
                message("MAP CLEARED", PAL_WHITE);
                return MR_EDIT;
            }
        }
        if (i == 7) help();
        if (i == 8) {
            if (!ed.dirty) return MR_EXIT;
            static char save_line[24] EWRAM_BSS;
            put(save_line, ed.slot ? "SAVE TO MAP B" : "SAVE TO MAP A");
            const char *l[3] = { save_line, "DON'T SAVE", "KEEP EDITING" };
            s32 s = menu_list("SAVE CHANGES?", l, 3);
            if (s == 0) { save_to(ed.slot); return MR_EXIT; }
            if (s == 1) return MR_EXIT;
        }
    }
}

// ---------------------------------------------------------------- the editor

#define TICK 280896   // CPU cycles in a 60th of a second
static inline u32 cycles(void) { return REG_TM0D | (REG_TM1D << 16); }

s32 edit_run(Car *car)
{
    if (!ed.ready) {
        for (s32 t = 0; t < EI_COUNT; t++) ed.size[t] = defs[t].def_size;
        ed.zoom = 1;
        ed.ready = 1;
    }
    if (!g_edit_testing) {
        // Start where the car is, facing its way.
        ed.x = snap(car->x >> 8);
        ed.z = snap(car->z >> 8);
        ed.rot = ((car->heading + 2048) >> 12) & 15;
    }
    g_edit_testing = 0;
    ed.cam_x = ed.x;
    ed.cam_z = ed.z;
    ed.msg_timer = 0;
    sound_silence();
    apply();                    // everything back where it was placed
    set_far();

    u16 prev = ~REG_KEYINPUT & 0x3FF;
    u32 last = cycles(), acc = 0;
    s32 frame = 0, hold = 0, sel_used = 0, result = 0;
    for (;;) {
        u32 now = cycles();
        acc += now - last;
        last = now;
        s32 ticks = acc / TICK;
        acc -= ticks * TICK;
        if (ticks > 4) ticks = 4;

        u16 keys = ~REG_KEYINPUT & 0x3FF, pressed = keys & ~prev, released = prev & ~keys;
        prev = keys;
        const u16 pad = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;

        if (keys & KEY_SELECT) {
            // SELECT held: up/down zoom, left/right turn a quarter.
            if (pressed & KEY_SELECT) sel_used = 0;
            if (pressed & pad) sel_used = 1;
            if ((pressed & KEY_UP) && ed.zoom > 0) ed.zoom--;
            if ((pressed & KEY_DOWN) && ed.zoom < 2) ed.zoom++;
            if (pressed & KEY_LEFT)  ed.rot = (ed.rot + 12) & 15;
            if (pressed & KEY_RIGHT) ed.rot = (ed.rot + 4) & 15;
            if (pressed & (KEY_UP | KEY_DOWN)) set_far();
            if (pressed & pad) sound_play(SFX_BEEP);
            hold = 0;
        } else {
            if ((released & KEY_SELECT) && !sel_used) { ed.rot = (ed.rot + 1) & 15; sound_play(SFX_BEEP); }
            // The d-pad: a tap moves one grid step, holding it speeds up.
            s32 dx = !!(keys & KEY_RIGHT) - !!(keys & KEY_LEFT), dz = !!(keys & KEY_UP) - !!(keys & KEY_DOWN);
            if (pressed & pad) {
                s32 px = !!(pressed & KEY_RIGHT) - !!(pressed & KEY_LEFT), pz = !!(pressed & KEY_UP) - !!(pressed & KEY_DOWN);
                ed.x = snap(ed.x) + px * GRID;
                ed.z = snap(ed.z) + pz * GRID;
                hold = 0;
            } else if (keys & pad) {
                hold += ticks;
                if (hold > 12) {
                    s32 v = 1 + (hold - 12) / 3, top = zoom_speed[ed.zoom];
                    if (v > top) v = top;
                    ed.x += dx * v * ticks;
                    ed.z += dz * v * ticks;
                }
            } else {
                hold = 0;
                ed.x = snap(ed.x);
                ed.z = snap(ed.z);
            }
            if (ed.x < 2 * GRID) ed.x = 2 * GRID;
            if (ed.z < 2 * GRID) ed.z = 2 * GRID;
            if (ed.x > WORLD - 2 * GRID) ed.x = WORLD - 2 * GRID;
            if (ed.z > WORLD - 2 * GRID) ed.z = WORLD - 2 * GRID;
        }

        s32 hover = ed.held ? -1 : under(snap(ed.x), snap(ed.z));
        if (pressed & KEY_L) { ed.type = (ed.type + EI_COUNT - 1) % EI_COUNT; sound_play(SFX_BEEP); }
        if (pressed & KEY_R) { ed.type = (ed.type + 1) % EI_COUNT; sound_play(SFX_BEEP); }
        if (pressed & KEY_A) {
            if (ed.held) {
                if (place()) { ed.held = 0; name_message("MOVED ", items[item_count - 1]); }
            } else if (hover >= 0) {
                // Pick it up: the cursor takes its place, type, heading and size.
                u32 v = items[hover];
                ed.held_item = v;
                ed.held = 1;
                ed.type = IT_TYPE(v);
                ed.rot = IT_ROT(v);
                ed.size[ed.type] = IT_SIZE(v);
                ed.x = IT_X(v);
                ed.z = IT_Z(v);
                remove_item(hover);
                apply();
                ed.dirty = 1;
                message("A TO DROP IT", PAL_GREEN);
                sound_play(SFX_BEEP);
            } else if (place()) {
                name_message("PLACED ", items[item_count - 1]);
            }
        }
        if (pressed & KEY_B) {
            if (ed.held) {
                items[item_count++] = ed.held_item;
                ed.held = 0;
                apply();
                message("PUT BACK", PAL_WHITE);
                sound_play(SFX_BEEP);
            } else {
                s32 i = hover;
                const char *verb = "DELETED ";
                if (i < 0 && item_count) { i = item_count - 1; verb = "UNDO "; }
                if (i < 0) { message("NOTHING TO DELETE", PAL_RED); sound_play(SFX_FAIL); }
                else {
                    name_message(verb, items[i]);
                    remove_item(i);
                    apply();
                    ed.dirty = 1;
                    sound_play(SFX_SCRAPE);
                }
            }
            hover = under(snap(ed.x), snap(ed.z));
        }
        if (pressed & KEY_START) {
            if (ed.held) {                // drop it back before leaving the cursor
                items[item_count++] = ed.held_item;
                ed.held = 0;
                apply();
            }
            s32 r = editor_menu();
            if (r == MR_TEST) {
                s32 x = snap(ed.x), z = snap(ed.z);
                if (world_blocked(x, z, 24)) {
                    message("MOVE TO A ROAD FIRST", PAL_RED);
                    sound_play(SFX_FAIL);
                } else {
                    edit_reset_props();
                    car_reset(car, x, z, ed.rot << 12);
                    g_edit_testing = 1;
                    result = 1;
                    break;
                }
            }
            if (r == MR_EXIT) break;
            set_far();
            prev = ~REG_KEYINPUT & 0x3FF;
            last = cycles();
            acc = 0;
            continue;
        }

        if (ed.msg_timer > 0) ed.msg_timer -= ticks;
        // The camera glides after the cursor.
        ed.cam_x += (snap(ed.x) - ed.cam_x + (ed.cam_x < snap(ed.x) ? 1 : 0)) / 2;
        ed.cam_z += (snap(ed.z) - ed.cam_z + (ed.cam_z < snap(ed.z) ? 1 : 0)) / 2;
        draw_frame(car, frame++, hover);
    }
    r_set_far(FAR_MIN_CITY, FAR_MAX_CITY, FOG1_CITY, FOG2_CITY);
    return result;
}
