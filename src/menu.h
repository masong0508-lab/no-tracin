// Options, menus drawn into the bitmap, and battery-backed saves.
#ifndef MENU_H
#define MENU_H

#include "gba.h"
#include "car.h"

enum {
    OPT_GRAVITY, OPT_ENGINE, OPT_TYRES, OPT_CRASHES, OPT_SPEED, OPT_SLOWMO,
    OPT_TIME, OPT_PAINT, OPT_SHAKE, OPT_UNITS, OPT_GHOST, OPT_HUD, OPT_SOUND,
    OPT_AUTOSAVE, OPT_PHYSICS, OPT_COUNT
};
enum { PAINT_RAINBOW = 7 };
extern u8 g_opt[OPT_COUNT];

void options_apply(void);          // push g_opt into physics, palette and sound
u16  paint_color(s32 frame);       // current car colour (cycles for rainbow)

// Provided by main.c.
volatile u16 *back_page(void);
void present(void);

// Menus over a frozen picture of the screen. menu_freeze() grabs the page
// that is about to be shown; the menus draw over a copy of it.
void menu_freeze(void);
s32  menu_list(const char *title, const char *const *items, s32 n);   // index, or -1 on B
void menu_options(void);
void menu_records(void);
void menu_cheats(void);           // type button codes
s32  menu_slot(const char *title, s32 saving);                        // slot 0..2, or -1
// Two columns of text in a panel; highlighted rows in yellow. Returns on A, B or START.
void menu_table(const char *title, const char *const *left, const char *const *right,
                const u8 *hilite, s32 n, const char *footer);

// Saves: options, records and the best ghost load at boot; three slots
// hold a car and score.
void save_init(void);              // load settings and records if a save exists
void save_system(void);
s32  save_slot(s32 slot, const Car *c, s32 score);
s32  load_slot(s32 slot, Car *c, s32 *score);
void save_erase(void);
// A small checksummed block in the save, outside the areas above.
#define RACE_SAVE_AT 0x7C00
void save_blob(u32 magic, u32 at, const void *p, u32 n);
s32  load_blob(u32 magic, u32 at, void *p, u32 n);

#endif
