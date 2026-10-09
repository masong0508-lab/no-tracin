// Loose things in Freedom City that the car can knock flying: traffic cones,
// oil drums, stacks of crates and concrete blocks. Each one is a little rigid
// body with its own weight; light ones scatter, heavy ones dent the car.
#ifndef PROPS_H
#define PROPS_H

#include "car.h"

void props_reset(void);          // lays them out (city only; circuits have none)
void props_step(Car *c);         // one physics step
void props_draw(void);
s32  props_smashed(void);        // knocked over since the last reset

#endif
