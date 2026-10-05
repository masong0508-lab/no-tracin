// Effects: tyre smoke, dust, sparks, fire, splashes and skid marks.
#ifndef FX_H
#define FX_H

#include "car.h"

void fx_reset(void);
void fx_step(const Car *c);       // once per physics step, after car_step
void fx_draw_ground(void);        // skid marks, before anything stands on them
void fx_draw_sprites(void);       // particles, into the sprite layer

#endif
