// Sound on the GBA's four DMG channels: engine on the wave channel (with a
// raspy overtone on square 1 when it is free), tyre squeal on square 2,
// impacts on noise, and jingles on square 1.
#ifndef SOUND_H
#define SOUND_H

#include "car.h"

enum {
    SFX_NONE,
    SFX_CRASH, SFX_LAND, SFX_BIG_LAND, SFX_SPLASH, SFX_SCRAPE,   // noise
    SFX_STUNT, SFX_LOOP, SFX_CHECKPOINT, SFX_LAP, SFX_BEST,       // jingles
    SFX_COMBO, SFX_START, SFX_BEEP, SFX_GO, SFX_FAIL,
};

void sound_init(void);
void sound_step(const Car *c);    // once per physics step
void sound_play(s32 sfx);
void sound_silence(void);
void sound_mode(s32 mode);         // 0 all on, 1 no engine, 2 off         // engine and squeal off (title screen)

#endif
