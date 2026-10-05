// Sprite layer drawn over the 3D view: HUD text, gauges and particles.
// Everything is built in a shadow copy of OAM and copied in during vblank.
#ifndef HUD_H
#define HUD_H

#include "gba.h"

// Sprite palettes.
enum {
    PAL_YELLOW, PAL_WHITE, PAL_RED, PAL_GREEN, PAL_CYAN,
    PAL_SMOKE, PAL_DARK_SMOKE, PAL_FIRE, PAL_SPLASH, PAL_DUST,
    PAL_TACH_OFF, PAL_TACH_GREEN, PAL_TACH_YELLOW, PAL_TACH_RED, PAL_SPARK,
};

// Particle sprite shapes.
enum { SPR_PUFF, SPR_SPARK, SPR_DROP };

void hud_init(void);
void hud_begin(void);
void hud_end(void);
void hud_commit(void);          // call during vblank

// Text: small glyphs are 6 px apart, big ones 12 px.
s32  hud_text(s32 x, s32 y, const char *s, s32 big, s32 pal);   // returns the end x
s32  hud_text_width(const char *s, s32 big);
void hud_text_centered(s32 y, const char *s, s32 big, s32 pal);
void hud_text_right(s32 right, s32 y, const char *s, s32 big, s32 pal);
// Big text scaled up 1.75x (title screen).
void hud_text_huge(s32 y, const char *s, s32 pal);

void hud_tach(s32 x, s32 y, s32 rpm);              // segmented rev counter
void hud_bar(s32 x, s32 y, s32 segs, s32 lit, s32 pal);   // a row of gauge segments
void hud_arrow(s32 x, s32 y, s32 angle, s32 pal);  // angle in 1024 units, 0 = up
// A particle centred on (x, y) with the given radius in pixels.
void hud_particle(s32 x, s32 y, s32 radius, s32 shape, s32 pal, s32 translucent);

#endif
